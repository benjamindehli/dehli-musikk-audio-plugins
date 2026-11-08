#pragma once

#include <atomic>
#include <utility>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>

#include "dm/dsp/OutputStage.h"

namespace dm::plugin {

/// One machine from the output stage as a plugin of its own: drive into it,
/// the machine, then the level out, with the input taken back off for delta.
///
/// Console and Cassette are both this class. Which one is given at
/// construction, so one test program can run both.
class MachineProcessor final : public juce::AudioProcessor,
                               private juce::Timer {
public:
  explicit MachineProcessor(dm::ClipType machine);
  ~MachineProcessor() override;

  /// Which machine this is.
  dm::ClipType getMachine() const noexcept { return machine; }

  void prepareToPlay(double sampleRate, int maximumBlockSize) override;
  void releaseResources() override {}

  bool isBusesLayoutSupported(const BusesLayout &) const override;

  // Single precision only. The double versions stay JUCE's, which a host is
  // never asked to use since this does not claim to support them.
  using juce::AudioProcessor::processBlock;
  using juce::AudioProcessor::processBlockBypassed;

  void processBlock(juce::AudioBuffer<float> &, juce::MidiBuffer &) override;

  /// Bypassed, the audio is still delayed by the reported latency, so it
  /// stays in time with everything else in the session.
  void processBlockBypassed(juce::AudioBuffer<float> &,
                            juce::MidiBuffer &) override;

  juce::AudioProcessorEditor *createEditor() override;
  bool hasEditor() const override { return true; }

  const juce::String getName() const override;
  bool acceptsMidi() const override { return false; }
  bool producesMidi() const override { return false; }
  bool isMidiEffect() const override { return false; }
  double getTailLengthSeconds() const override;

  int getNumPrograms() override { return 1; }
  int getCurrentProgram() override { return 0; }
  void setCurrentProgram(int) override {}
  const juce::String getProgramName(int) override { return {}; }
  void changeProgramName(int, const juce::String &) override {}

  void getStateInformation(juce::MemoryBlock &) override;
  void setStateInformation(const void *, int) override;

  /// Tells the host the latency the lookahead switch asks for, if that has
  /// changed. On the message thread, from a timer, since a host automating the
  /// switch would move it from the audio thread, which is the wrong thread for
  /// telling a host its graph has changed. Public for the tests.
  void reportLatency();

  /// The latency for the switch as it is now, at the prepared rate.
  int wantedLatency() const;

  /// The loudest sample in and out since the editor last asked, per channel.
  /// Written by the audio thread, taken by the editor.
  struct Peaks {
    std::atomic<float> left{0.0f}, right{0.0f};

    void raise(float l, float r) noexcept;
    std::pair<float, float> take() noexcept;
  };

  Peaks inputPeaks, outputPeaks;

  juce::AudioProcessorValueTreeState apvts;

private:
  void timerCallback() override { reportLatency(); }

  /// Processes up to one scratch buffer's worth, which is what prepare sized
  /// everything for. processBlock cuts a bigger block into these.
  void processChunk(float *left, float *right, int numSamples) noexcept;

  /// Writes the input into the dry line and reads it back delayed by the
  /// stage's latency, into dryLeft and dryRight.
  void delayDry(const float *left, const float *right, int numSamples,
                int latency) noexcept;

  const dm::ClipType machine;
  dm::OutputStage stage;

  std::atomic<float> *drive = nullptr;
  std::atomic<float> *output = nullptr;
  std::atomic<float> *lookahead = nullptr;
  std::atomic<float> *autoGain = nullptr;
  std::atomic<float> *delta = nullptr;

  juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative>
      driveGain, makeUp, outputGain;
  juce::SmoothedValue<float> deltaMix;

  /// How late the machine itself is, beyond the reported latency: the
  /// deck's transport, which plays through a delay centred on a millisecond
  /// as the tape's speed wanders. Part of the sound, so not reported, but
  /// delta lines the input up with it, or delta would be mostly that shift.
  int machineDelay = 0;

  /// The input before anything is done to it, delayed to line up with what
  /// leaves the machine. A ring as long as the longest delay.
  std::vector<float> lineLeft, lineRight;
  int lineAt = 0;

  /// Scratch for one chunk: the delayed input, and a right channel for a mono
  /// bus, since the stage always works on two.
  std::vector<float> dryLeft, dryRight, spareRight;
  int chunkSize = 0;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MachineProcessor)
};

} // namespace dm::plugin
