#include "Parameters.h"

namespace dm::plugin::params {

juce::String decibelText(float db) {
  // Never "-0.0", which a readout would show as a stray minus.
  const auto tenths = juce::roundToInt(db * 10.0f);
  return juce::String((float)tenths / 10.0f, 1);
}

juce::AudioProcessorValueTreeState::ParameterLayout createLayout() {
  juce::AudioProcessorValueTreeState::ParameterLayout layout;

  const auto decibels = juce::AudioParameterFloatAttributes()
                            .withLabel("dB")
                            .withStringFromValueFunction(
                                [](float v, int) { return decibelText(v); });

  layout.add(std::make_unique<juce::AudioParameterFloat>(
      juce::ParameterID{driveId, 1}, "Drive",
      juce::NormalisableRange<float>(kDriveMinDb, kDriveMaxDb, 0.1f), 0.0f,
      decibels));

  layout.add(std::make_unique<juce::AudioParameterFloat>(
      juce::ParameterID{outputId, 1}, "Output",
      juce::NormalisableRange<float>(kOutputMinDb, kOutputMaxDb, 0.1f), 0.0f,
      decibels));

  layout.add(std::make_unique<juce::AudioParameterBool>(
      juce::ParameterID{lookaheadId, 1}, "Lookahead", true));

  layout.add(std::make_unique<juce::AudioParameterBool>(
      juce::ParameterID{autoGainId, 1}, "Auto gain", false));

  layout.add(std::make_unique<juce::AudioParameterBool>(
      juce::ParameterID{deltaId, 1}, "Delta", false));

  return layout;
}

} // namespace dm::plugin::params
