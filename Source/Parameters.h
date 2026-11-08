#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace dm::plugin::params {

/// The parameter ids. A host saves automation and a session saves the state
/// under these, so they never change.
inline constexpr const char *driveId = "drive";
inline constexpr const char *outputId = "output";
inline constexpr const char *lookaheadId = "lookahead";
inline constexpr const char *autoGainId = "autoGain";
inline constexpr const char *deltaId = "delta";

/// How far the input can be pushed into the machine, and pulled back from it.
inline constexpr float kDriveMinDb = -12.0f;
inline constexpr float kDriveMaxDb = 24.0f;

/// The level after the machine.
inline constexpr float kOutputMinDb = -24.0f;
inline constexpr float kOutputMaxDb = 12.0f;

/// All five, in the order a host lists them.
juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

/// A level as the readouts and the host show it: one decimal, no plus sign.
juce::String decibelText(float db);

} // namespace dm::plugin::params
