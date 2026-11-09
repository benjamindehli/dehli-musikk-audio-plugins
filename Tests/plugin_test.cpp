// Console and Cassette as plugins, run with no host and no display: the
// parameters, the latency, what drive, auto gain, delta and output do to a
// tone, the ceiling, the bus layouts, the saved state, bypass, a host that
// sends more than it promised, and the window's controls.

#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>

#include "Parameters.h"
#include "PluginEditor.h"
#include "PluginProcessor.h"

namespace {

int failures = 0;
int checks = 0;

/// Counts a check, and prints it when it fails.
void check(bool ok, const std::string &what) {
  ++checks;
  if (!ok) {
    ++failures;
    std::printf("  FAIL  %s\n", what.c_str());
  }
}

void section(const std::string &name) { std::printf("%s\n", name.c_str()); }

using dm::plugin::MachineProcessor;
namespace params = dm::plugin::params;

constexpr double kRate = 48000.0;
constexpr int kBlock = 256;
constexpr double kTwoPi = 6.283185307179586;

/// Sets a parameter by its plain value, as the host would.
void set(MachineProcessor &p, const char *id, float value) {
  auto *param = p.apvts.getParameter(id);
  param->setValueNotifyingHost(param->convertTo0to1(value));
}

/// A processor of one machine, prepared at 48 kHz in stereo, with the
/// parameters set before it is prepared so nothing is still ramping.
std::unique_ptr<MachineProcessor>
make(dm::ClipType machine,
     const std::vector<std::pair<const char *, float>> &settings = {},
     int block = kBlock) {
  auto p = std::make_unique<MachineProcessor>(machine);
  for (const auto &[id, value] : settings)
    set(*p, id, value);

  p->setPlayConfigDetails(2, 2, kRate, block);
  p->prepareToPlay(kRate, block);
  return p;
}

/// A sine on both channels.
juce::AudioBuffer<float> tone(double hz, double db, int length) {
  juce::AudioBuffer<float> buffer(2, length);
  const auto amp = std::pow(10.0, db / 20.0);
  for (int i = 0; i < length; ++i) {
    const auto v = (float)(amp * std::sin(kTwoPi * hz * i / kRate));
    buffer.setSample(0, i, v);
    buffer.setSample(1, i, v);
  }
  return buffer;
}

/// Runs a buffer through in blocks of the given size.
void render(MachineProcessor &p, juce::AudioBuffer<float> &buffer,
            int block = kBlock, bool bypassed = false) {
  juce::MidiBuffer midi;
  for (int at = 0; at < buffer.getNumSamples(); at += block) {
    const auto n = std::min(block, buffer.getNumSamples() - at);
    juce::AudioBuffer<float> view(buffer.getArrayOfWritePointers(),
                                  buffer.getNumChannels(), at, n);
    if (bypassed)
      p.processBlockBypassed(view, midi);
    else
      p.processBlock(view, midi);
  }
}

/// The RMS level of the left channel from `from` on, in dB.
double levelDb(const juce::AudioBuffer<float> &b, int from) {
  double sum = 0.0;
  for (int i = from; i < b.getNumSamples(); ++i)
    sum += (double)b.getSample(0, i) * b.getSample(0, i);
  const auto rms = std::sqrt(sum / std::max(1, b.getNumSamples() - from));
  return 20.0 * std::log10(std::max(rms, 1.0e-12));
}

/// What a tone comes out at, against what it went in at, once things have
/// settled.
double gainDb(dm::ClipType machine,
              const std::vector<std::pair<const char *, float>> &settings,
              double hz = 1000.0, double db = -40.0) {
  auto p = make(machine, settings);
  const auto length = (int)kRate;
  auto in = tone(hz, db, length);
  auto out = in;
  render(*p, out);

  const auto settle = length / 4;
  return levelDb(out, settle) - levelDb(in, settle);
}

std::string named(dm::ClipType machine, const std::string &what) {
  return std::string(dm::clipTypeName(machine)) + ": " + what;
}

void testParameters(dm::ClipType machine) {
  auto p = make(machine);

  check(p->getName() == dm::clipTypeName(machine),
        named(machine, "the plugin is named after its machine"));

  const struct {
    const char *id;
    float wanted;
  } defaults[] = {{params::driveId, 0.0f},
                  {params::outputId, 0.0f},
                  {params::autoGainId, 0.0f},
                  {params::deltaId, 0.0f}};

  for (const auto &d : defaults)
    check(std::abs(p->apvts.getRawParameterValue(d.id)->load() - d.wanted) <
              1.0e-6f,
          named(machine, std::string(d.id) + " starts where it should"));

  check(p->getParameters().size() == 4,
        named(machine, "there are four parameters"));

  for (const auto *id : {params::driveId, params::outputId}) {
    const auto range = p->apvts.getParameterRange(id);
    check(std::abs(range.start + 24.0f) < 1.0e-6f &&
              std::abs(range.end - 24.0f) < 1.0e-6f,
          named(machine, std::string(id) + " runs from -24 dB to 24 dB"));
  }

  check(p->apvts.getParameter(params::driveId)->getName(32) == "Input",
        named(machine, "the host calls the drive Input, as the window does"));
  check(
      p->apvts.getParameter(params::driveId)
              ->getText(
                  p->apvts.getParameter(params::driveId)->convertTo0to1(-0.04f),
                  8) == "0.0",
      named(machine, "a level just below zero reads 0.0, not -0.0"));
}

void testLatency(dm::ClipType machine) {
  auto p = make(machine);
  check(p->getLatencySamples() == 96,
        named(machine, "the lookahead reports 96 samples at 48 kHz"));

  p->setPlayConfigDetails(2, 2, 96000.0, kBlock);
  p->prepareToPlay(96000.0, kBlock);
  check(p->getLatencySamples() == 192,
        named(machine, "and 192 at 96 kHz, the same 2 ms"));
}

void testLevels(dm::ClipType machine) {
  const auto unity = gainDb(machine, {});
  check(std::abs(unity) < 1.0,
        named(machine, "a quiet tone comes out at the level it went in (" +
                           std::to_string(unity) + " dB)"));

  const auto driven = gainDb(machine, {{params::driveId, 12.0f}});
  check(std::abs(driven - 12.0) < 1.5,
        named(machine, "12 dB of drive raises a quiet tone by about 12 dB (" +
                           std::to_string(driven) + " dB)"));

  // Exactly the drive, whatever the machine does with the louder signal. The
  // deck's Dolby hears 12 dB more and answers differently, as the real one
  // does, so the result is near unity rather than at it.
  const auto compensated =
      gainDb(machine, {{params::driveId, 12.0f}, {params::autoGainId, 1.0f}});
  check(std::abs(driven - compensated - 12.0) < 0.05,
        named(machine, "auto gain takes exactly the drive back off (" +
                           std::to_string(driven - compensated) + " dB)"));
  check(std::abs(compensated - unity) < 1.0,
        named(machine, "which leaves a quiet tone near where it was (" +
                           std::to_string(compensated - unity) + " dB)"));

  const auto quieter = gainDb(machine, {{params::outputId, -6.0f}});
  check(std::abs(quieter - unity + 6.0) < 0.2,
        named(machine, "the output turns it down by 6 dB (" +
                           std::to_string(quieter - unity) + " dB)"));
}

void testDelta(dm::ClipType machine) {
  // A quiet low tone, which the machine barely changes, so its delta is well
  // below the tone. Low, so the deck's wow and flutter, which the delta keeps,
  // moves it by only a small part of a cycle. Not silent, since the machines'
  // filters turn the phase a little: the console's 15 Hz coupling turns a
  // 100 Hz tone by about 8 degrees, which leaves a delta 16 dB down. Out of
  // line by the 96 samples of the lookahead, the delta would be about 1 dB up.
  const auto clean = gainDb(machine, {{params::deltaId, 1.0f}}, 100.0, -30.0);
  check(clean < -12.0,
        named(machine, "the delta of a quiet tone is well below it (" +
                           std::to_string(clean) + " dB)"));

  // Driven hard, the machine changes the tone, and the delta is what changed.
  const auto driven = gainDb(machine,
                             {{params::deltaId, 1.0f},
                              {params::driveId, 18.0f},
                              {params::autoGainId, 1.0f}},
                             100.0, -6.0);
  check(driven > clean + 10.0,
        named(machine, "driven hard, the delta rises (" +
                           std::to_string(driven) + " dB)"));

  // The output level comes before the input is taken off, so turning the
  // output down 6 dB leaves half the tone less all of it: half the tone, out
  // of phase. Taken off before the output, it would be 6 dB under the clean
  // delta instead, as if the output level had no part in what is heard.
  const auto quieter =
      gainDb(machine, {{params::deltaId, 1.0f}, {params::outputId, -6.0f}},
             100.0, -30.0);
  check(std::abs(quieter + 6.0) < 1.0,
        named(machine, "with the output 6 dB down, the delta is half the "
                       "tone (" +
                           std::to_string(quieter) + " dB)"));
}

void testCeiling(dm::ClipType machine) {
  auto p = make(machine, {{params::driveId, params::kDriveMaxDb}});
  auto buffer = tone(100.0, 0.0, (int)kRate);
  render(*p, buffer);

  bool finite = true;
  float peak = 0.0f;
  for (int c = 0; c < 2; ++c)
    for (int i = 0; i < buffer.getNumSamples(); ++i) {
      const auto v = buffer.getSample(c, i);
      finite = finite && std::isfinite(v);
      peak = std::max(peak, std::abs(v));
    }

  check(finite && peak <= 1.0f,
        named(machine, "full drive into a full-scale tone stays within full "
                       "scale (" +
                           std::to_string(peak) + ")"));
}

void testLayouts(dm::ClipType machine) {
  auto p = std::make_unique<MachineProcessor>(machine);

  juce::AudioProcessor::BusesLayout mono, stereo, mixed, surround;
  mono.inputBuses.add(juce::AudioChannelSet::mono());
  mono.outputBuses.add(juce::AudioChannelSet::mono());
  stereo.inputBuses.add(juce::AudioChannelSet::stereo());
  stereo.outputBuses.add(juce::AudioChannelSet::stereo());
  mixed.inputBuses.add(juce::AudioChannelSet::mono());
  mixed.outputBuses.add(juce::AudioChannelSet::stereo());
  surround.inputBuses.add(juce::AudioChannelSet::create5point1());
  surround.outputBuses.add(juce::AudioChannelSet::create5point1());

  check(p->checkBusesLayoutSupported(mono) &&
            p->checkBusesLayoutSupported(stereo),
        named(machine, "mono and stereo are supported"));
  check(!p->checkBusesLayoutSupported(mixed) &&
            !p->checkBusesLayoutSupported(surround),
        named(machine, "mono into stereo and surround are not"));

  // A mono bus runs, and comes out at the level a stereo one does.
  p->setBusesLayout(mono);
  p->prepareToPlay(kRate, kBlock);

  juce::AudioBuffer<float> buffer(1, (int)kRate);
  auto source = tone(1000.0, -40.0, (int)kRate);
  buffer.copyFrom(0, 0, source, 0, 0, (int)kRate);
  render(*p, buffer);

  const auto settle = (int)kRate / 4;
  check(std::abs(levelDb(buffer, settle) - levelDb(source, settle)) < 1.0,
        named(machine, "a mono bus passes a quiet tone at its level"));
}

void testState(dm::ClipType machine) {
  auto p = make(machine, {{params::driveId, 7.5f},
                          {params::outputId, -3.0f},
                          {params::autoGainId, 1.0f},
                          {params::deltaId, 1.0f}});

  juce::MemoryBlock saved;
  p->getStateInformation(saved);

  auto q = std::make_unique<MachineProcessor>(machine);
  q->setStateInformation(saved.getData(), (int)saved.getSize());

  bool same = true;
  for (const auto *id :
       {params::driveId, params::outputId, params::autoGainId, params::deltaId})
    same =
        same && std::abs(p->apvts.getRawParameterValue(id)->load() -
                         q->apvts.getRawParameterValue(id)->load()) < 1.0e-4f;

  check(same, named(machine, "every parameter comes back from a saved state"));

  // Something that is not a state of this plugin is ignored, not half read.
  const char junk[] = "not a state";
  q->setStateInformation(junk, (int)sizeof junk);
  check(std::abs(q->apvts.getRawParameterValue(params::driveId)->load() -
                 7.5f) < 1.0e-4f,
        named(machine, "a state it cannot read leaves the parameters alone"));
}

void testBypass(dm::ClipType machine) {
  auto p = make(machine);

  juce::AudioBuffer<float> buffer(2, 1024);
  buffer.clear();
  buffer.setSample(0, 0, 1.0f);
  buffer.setSample(1, 0, 1.0f);
  render(*p, buffer, kBlock, true);

  int at = -1;
  for (int i = 0; i < buffer.getNumSamples(); ++i)
    if (buffer.getSample(0, i) > 0.5f) {
      at = i;
      break;
    }

  check(at == p->getLatencySamples(),
        named(machine, "bypassed, the audio is delayed by the latency (" +
                           std::to_string(at) + ")"));
}

void testBigBlocks(dm::ClipType machine) {
  // The same tone in the blocks the host promised, and in blocks four times
  // that, which are cut into pieces. The two come out the same.
  const auto settings =
      std::vector<std::pair<const char *, float>>{{params::driveId, 9.0f}};

  auto promised = make(machine, settings, kBlock);
  auto overfull = make(machine, settings, kBlock);

  auto a = tone(220.0, -12.0, 8192);
  auto b = a;
  render(*promised, a, kBlock);
  render(*overfull, b, kBlock * 4);

  bool same = true;
  for (int c = 0; c < 2; ++c)
    for (int i = 0; i < a.getNumSamples(); ++i)
      same = same && !(a.getSample(c, i) < b.getSample(c, i)) &&
             !(b.getSample(c, i) < a.getSample(c, i));

  check(same, named(machine, "a block bigger than promised comes out the "
                             "same as the promised ones"));
}

void testEditor(dm::ClipType machine) {
  auto p = make(machine);
  std::unique_ptr<juce::AudioProcessorEditor> base(p->createEditor());
  auto *editor = dynamic_cast<dm::plugin::MachineEditor *>(base.get());

  check(editor != nullptr, named(machine, "the window opens"));
  if (editor == nullptr)
    return;

  check(editor->getWidth() == dm::plugin::MachineEditor::kWidth &&
            editor->getHeight() == dm::plugin::MachineEditor::kHeight,
        named(machine, "at the size it is drawn at"));

  set(*p, params::driveId, 6.0f);
  check(std::abs(editor->getInputKnob().getValue() - 6.0) < 1.0e-3,
        named(machine, "the input knob follows its parameter"));

  editor->getOutputKnob().setValue(-4.5, juce::sendNotificationSync);
  check(std::abs(p->apvts.getRawParameterValue(params::outputId)->load() +
                 4.5f) < 1.0e-3f,
        named(machine, "the output knob moves its parameter"));

  editor->refresh();
  check(editor->getInputReadout().getReading() == "6.0" &&
            editor->getOutputReadout().getReading() == "-4.5",
        named(machine, "the readouts show the levels"));

  check((bool)editor->getInputKnob().getProperties()["ringless"] &&
            (bool)editor->getOutputKnob().getProperties()["ringless"],
        named(machine, "the knobs have no ring of lights"));
  check(editor->getInputReadout().getLampColour() ==
                dm::plugin::looks::readout &&
            editor->getOutputReadout().getLampColour() ==
                dm::plugin::looks::readout,
        named(machine, "the readouts are red"));
  check(editor->getInputMeter().isVertical() &&
            editor->getOutputMeter().isVertical() &&
            editor->getInputMeter().getHeight() >
                editor->getInputMeter().getWidth() * 4,
        named(machine, "the meters stand"));
  check(editor->getAutoGainButton().getWidth() ==
                editor->getAutoGainButton().getHeight() &&
            editor->getDeltaButton().getBounds() ==
                editor->getAutoGainButton().getBounds().withX(
                    editor->getDeltaButton().getX()),
        named(machine, "the switches are square and side by side"));

  // As a click does, but at once, since no message loop runs here.
  editor->getDeltaButton().setToggleState(true, juce::sendNotificationSync);
  check(p->apvts.getRawParameterValue(params::deltaId)->load() > 0.5f,
        named(machine, "the delta switch turns delta on"));

  editor->getAutoGainButton().setToggleState(true, juce::sendNotificationSync);
  check(p->apvts.getRawParameterValue(params::autoGainId)->load() > 0.5f,
        named(machine, "the auto gain switch turns auto gain on"));

  // Twice the size is allowed and keeps the shape, more is not.
  editor->setBoundsConstrained({0, 0, dm::plugin::MachineEditor::kWidth * 3,
                                dm::plugin::MachineEditor::kHeight * 3});
  check(editor->getWidth() == dm::plugin::MachineEditor::kWidth * 2,
        named(machine, "the window grows to twice its size and no more"));
}

/// Saves each machine's window as a PNG at 1x and 2x into a folder, for
/// looking at and for comparing one build with another.
int renderWindows(const juce::File &folder) {
  if (!folder.createDirectory()) {
    std::printf("cannot make %s\n", folder.getFullPathName().toRawUTF8());
    return 1;
  }

  for (const auto machine : {dm::ClipType::Console, dm::ClipType::Cassette}) {
    // Auto gain on, so a lit switch shows beside an unlit one.
    auto p =
        make(machine, {{params::driveId, 6.0f}, {params::autoGainId, 1.0f}});

    // Some signal, so the meters show something.
    auto buffer = tone(220.0, -9.0, 4096);
    render(*p, buffer);

    std::unique_ptr<juce::AudioProcessorEditor> editor(p->createEditor());
    if (auto *machineEditor =
            dynamic_cast<dm::plugin::MachineEditor *>(editor.get()))
      machineEditor->refresh();

    for (const auto scale : {1, 2}) {
      const auto image = editor->createComponentSnapshot(
          editor->getLocalBounds(), true, (float)scale);
      const auto file = folder.getChildFile(
          juce::String(dm::clipTypeName(machine)).toLowerCase() + "-" +
          juce::String(scale) + "x.png");

      // A stream appends to a file that is already there.
      file.deleteFile();

      juce::FileOutputStream stream(file);
      juce::PNGImageFormat png;
      if (!stream.openedOk() || !png.writeImageToStream(image, stream)) {
        std::printf("cannot write %s\n", file.getFullPathName().toRawUTF8());
        return 1;
      }

      std::printf("wrote %s\n", file.getFullPathName().toRawUTF8());
    }
  }

  return 0;
}

} // namespace

int main(int argc, char **argv) {
  const juce::ScopedJuceInitialiser_GUI juce;

  // --render <folder> saves the windows instead of running the tests.
  if (argc == 3 && std::string(argv[1]) == "--render")
    return renderWindows(juce::File::getCurrentWorkingDirectory().getChildFile(
        juce::String(argv[2])));

  for (const auto machine : {dm::ClipType::Console, dm::ClipType::Cassette}) {
    section(dm::clipTypeName(machine));
    testParameters(machine);
    testLatency(machine);
    testLevels(machine);
    testDelta(machine);
    testCeiling(machine);
    testLayouts(machine);
    testState(machine);
    testBypass(machine);
    testBigBlocks(machine);
    testEditor(machine);
  }

  std::printf("\n%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
