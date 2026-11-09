#include "PluginProcessor.h"

#include <algorithm>
#include <cmath>

#include "Parameters.h"
#include "PluginEditor.h"
#include "dm/dsp/Cassette.h"

namespace dm::plugin {

namespace {
/// How long a move of a knob or a switch takes to arrive, so it does not
/// click.
constexpr double kRampSeconds = 0.02;

/// The largest block the scratch buffers are sized for when the host promises
/// nothing sensible. A bigger block is cut into pieces of whatever was sized.
constexpr int kFallbackBlock = 512;

float magnitude(const float *x, int n) noexcept {
  float peak = 0.0f;
  for (int i = 0; i < n; ++i)
    peak = std::max(peak, std::abs(x[i]));
  return peak;
}
} // namespace

void MachineProcessor::Peaks::raise(float l, float r) noexcept {
  // Only this thread raises, so a read and a write cannot lose a peak to
  // another raise. A take in between is at worst one block late.
  if (l > left.load(std::memory_order_relaxed))
    left.store(l, std::memory_order_relaxed);
  if (r > right.load(std::memory_order_relaxed))
    right.store(r, std::memory_order_relaxed);
}

std::pair<float, float> MachineProcessor::Peaks::take() noexcept {
  return {left.exchange(0.0f, std::memory_order_relaxed),
          right.exchange(0.0f, std::memory_order_relaxed)};
}

MachineProcessor::MachineProcessor(dm::ClipType machineToRun)
    : juce::AudioProcessor(
          BusesProperties()
              .withInput("Input", juce::AudioChannelSet::stereo(), true)
              .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "state", params::createLayout()),
      machine(machineToRun) {
  drive = apvts.getRawParameterValue(params::driveId);
  output = apvts.getRawParameterValue(params::outputId);
  autoGain = apvts.getRawParameterValue(params::autoGainId);
  delta = apvts.getRawParameterValue(params::deltaId);

  // The latency is reported before anything is prepared, at a guessed rate,
  // so a host that asks early gets a figure of the right size.
  setLatencySamples(wantedLatency());
}

const juce::String MachineProcessor::getName() const {
  return dm::clipTypeName(machine);
}

double MachineProcessor::getTailLengthSeconds() const {
  // The deck's hiss goes on for a while once nothing plays. The console has
  // nothing of its own to let ring.
  return machine == dm::ClipType::Cassette ? 0.5 : 0.0;
}

bool MachineProcessor::isBusesLayoutSupported(const BusesLayout &layout) const {
  const auto in = layout.getMainInputChannelSet();
  const auto out = layout.getMainOutputChannelSet();

  return in == out && (out == juce::AudioChannelSet::mono() ||
                       out == juce::AudioChannelSet::stereo());
}

int MachineProcessor::wantedLatency() const {
  const auto rate = getSampleRate() > 0.0 ? getSampleRate() : 48000.0;
  return dm::OutputStage::lookaheadSamples(rate);
}

void MachineProcessor::prepareToPlay(double sampleRate, int maximumBlockSize) {
  stage.prepare(sampleRate);

  chunkSize = maximumBlockSize > 0 ? maximumBlockSize : kFallbackBlock;
  dryLeft.assign((size_t)chunkSize, 0.0f);
  dryRight.assign((size_t)chunkSize, 0.0f);
  spareRight.assign((size_t)chunkSize, 0.0f);

  machineDelay =
      machine == dm::ClipType::Cassette
          ? (int)std::lround(dm::cassette::kFlutterCentre * sampleRate)
          : 0;

  // One more than the longest delay, so the newest sample and the one that
  // many samples older are never the same slot.
  const auto longest =
      dm::OutputStage::lookaheadSamples(sampleRate) + machineDelay;
  lineLeft.assign((size_t)longest + 1, 0.0f);
  lineRight.assign((size_t)longest + 1, 0.0f);
  lineAt = 0;

  const auto driveTarget = juce::Decibels::decibelsToGain(drive->load());

  driveGain.reset(sampleRate, kRampSeconds);
  driveGain.setCurrentAndTargetValue(driveTarget);
  makeUp.reset(sampleRate, kRampSeconds);
  makeUp.setCurrentAndTargetValue(autoGain->load() > 0.5f ? 1.0f / driveTarget
                                                          : 1.0f);
  outputGain.reset(sampleRate, kRampSeconds);
  outputGain.setCurrentAndTargetValue(
      juce::Decibels::decibelsToGain(output->load()));
  deltaMix.reset(sampleRate, kRampSeconds);
  deltaMix.setCurrentAndTargetValue(delta->load() > 0.5f ? 1.0f : 0.0f);

  // The lookahead is always on, so the latency only changes with the rate.
  // Prepare runs on the host's chosen thread with nothing playing, which is a
  // safe place to tell it, and setLatencySamples tells the host only on a
  // change.
  setLatencySamples(wantedLatency());
}

void MachineProcessor::delayDry(const float *left, const float *right,
                                int numSamples, int latency) noexcept {
  const auto size = (int)lineLeft.size();
  latency = std::clamp(latency, 0, size - 1);

  for (int i = 0; i < numSamples; ++i) {
    lineLeft[(size_t)lineAt] = left[i];
    lineRight[(size_t)lineAt] = right[i];

    const auto from = (lineAt - latency + size) % size;
    dryLeft[(size_t)i] = lineLeft[(size_t)from];
    dryRight[(size_t)i] = lineRight[(size_t)from];

    lineAt = (lineAt + 1) % size;
  }
}

void MachineProcessor::processChunk(float *left, float *right,
                                    int numSamples) noexcept {
  inputPeaks.raise(magnitude(left, numSamples), magnitude(right, numSamples));

  // The input as it arrived, kept before the drive touches it, for delta.
  std::copy(left, left + numSamples, dryLeft.begin());
  std::copy(right, right + numSamples, dryRight.begin());

  const auto driveTarget = juce::Decibels::decibelsToGain(drive->load());
  driveGain.setTargetValue(driveTarget);
  makeUp.setTargetValue(autoGain->load() > 0.5f ? 1.0f / driveTarget : 1.0f);
  outputGain.setTargetValue(juce::Decibels::decibelsToGain(output->load()));
  deltaMix.setTargetValue(delta->load() > 0.5f ? 1.0f : 0.0f);

  for (int i = 0; i < numSamples; ++i) {
    const auto g = driveGain.getNextValue();
    left[i] *= g;
    right[i] *= g;
  }

  // Always shaping, and always with the lookahead, which lets the limiter's
  // gain move smoothly.
  constexpr bool shaping = true;
  constexpr bool ahead = true;
  stage.process(left, right, numSamples, machine, shaping, ahead);

  // The dry signal has to match the stage's latency, with the deck's own
  // delay on top.
  delayDry(dryLeft.data(), dryRight.data(), numSamples,
           stage.latency() + machineDelay);

  for (int i = 0; i < numSamples; ++i) {
    const auto up = makeUp.getNextValue();
    const auto take = deltaMix.getNextValue();
    const auto out = outputGain.getNextValue();

    // The dry signal comes off what would otherwise be heard, after the
    // output level, so delta is exactly the output less the input.
    left[i] = left[i] * up * out - take * dryLeft[(size_t)i];
    right[i] = right[i] * up * out - take * dryRight[(size_t)i];
  }

  outputPeaks.raise(magnitude(left, numSamples), magnitude(right, numSamples));
}

void MachineProcessor::processBlock(juce::AudioBuffer<float> &buffer,
                                    juce::MidiBuffer &) {
  const juce::ScopedNoDenormals noDenormals;

  const auto total = buffer.getNumSamples();
  const auto channels = std::min(buffer.getNumChannels(), 2);

  if (channels == 0 || chunkSize == 0)
    return;

  // A host may hand over more than it promised. Growing the scratch here would
  // allocate on the audio thread, so the block is cut into what fits.
  for (int at = 0; at < total; at += chunkSize) {
    const auto n = std::min(chunkSize, total - at);
    auto *left = buffer.getWritePointer(0, at);

    float *right = spareRight.data();
    if (channels > 1)
      right = buffer.getWritePointer(1, at);
    else
      std::copy(left, left + n, spareRight.begin());

    processChunk(left, right, n);
  }
}

void MachineProcessor::processBlockBypassed(juce::AudioBuffer<float> &buffer,
                                            juce::MidiBuffer &) {
  const auto total = buffer.getNumSamples();
  const auto channels = std::min(buffer.getNumChannels(), 2);

  if (channels == 0 || chunkSize == 0)
    return;

  for (int at = 0; at < total; at += chunkSize) {
    const auto n = std::min(chunkSize, total - at);
    auto *left = buffer.getWritePointer(0, at);
    auto *right = channels > 1 ? buffer.getWritePointer(1, at) : left;

    delayDry(left, right, n, getLatencySamples());

    std::copy(dryLeft.begin(), dryLeft.begin() + n, left);
    if (channels > 1)
      std::copy(dryRight.begin(), dryRight.begin() + n, right);
  }
}

juce::AudioProcessorEditor *MachineProcessor::createEditor() {
  return new MachineEditor(*this);
}

void MachineProcessor::getStateInformation(juce::MemoryBlock &destination) {
  const auto state = apvts.copyState();

  if (const auto xml = state.createXml())
    copyXmlToBinary(*xml, destination);
}

void MachineProcessor::setStateInformation(const void *data, int size) {
  const auto xml = getXmlFromBinary(data, size);

  if (xml != nullptr && xml->hasTagName(apvts.state.getType()))
    apvts.replaceState(juce::ValueTree::fromXml(*xml));
}

} // namespace dm::plugin
