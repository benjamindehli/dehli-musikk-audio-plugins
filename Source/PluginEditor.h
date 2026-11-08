#pragma once

#include <memory>

#include <dm_gui/dm_gui.h>
#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"

namespace dm::plugin {

/// The window: the machine's name, two knobs with their readouts, the three
/// switches and the meters in and out, drawn at one size and scaled to the
/// window.
class MachineEditor final : public juce::AudioProcessorEditor,
                            private juce::Timer {
public:
  explicit MachineEditor(MachineProcessor &);
  ~MachineEditor() override;

  /// The size everything is laid out at. The window keeps this shape and
  /// scales it.
  static constexpr int kWidth = 460;
  static constexpr int kHeight = 220;

  void resized() override;

  /// The controls, for the tests.
  dm::ui::LabelledKnob &getDriveKnob() noexcept { return driveKnob; }
  dm::ui::LabelledKnob &getOutputKnob() noexcept { return outputKnob; }
  dm::ui::GlowButton &getLookaheadButton() noexcept { return lookaheadButton; }
  dm::ui::GlowButton &getAutoGainButton() noexcept { return autoGainButton; }
  dm::ui::GlowButton &getDeltaButton() noexcept { return deltaButton; }
  dm::ui::SegmentDisplay &getDriveReadout() noexcept { return driveReadout; }
  dm::ui::SegmentDisplay &getOutputReadout() noexcept { return outputReadout; }

  /// Moves the meters and the readouts on, as the timer does. Public so the
  /// tests can call it with no message loop running.
  void refresh();

private:
  /// Everything in the window, laid out at kWidth by kHeight and scaled.
  class Panel final : public juce::Component {
  public:
    explicit Panel(MachineEditor &owner) : editor(owner) {}
    void paint(juce::Graphics &) override;

  private:
    MachineEditor &editor;
  };

  void timerCallback() override { refresh(); }

  void layOut();

  /// Where each group sits in the panel, shared by paint and layOut.
  static juce::Rectangle<int> knobsPocket();
  static juce::Rectangle<int> switchesPocket();
  static juce::Rectangle<int> metersPocket();

  MachineProcessor &machineProcessor;

  /// First, so it is destroyed after every control that draws with it.
  dm::ui::LookAndFeel lookAndFeel;

  Panel panel{*this};

  dm::ui::LabelledKnob driveKnob{"DRIVE"};
  dm::ui::LabelledKnob outputKnob{"OUTPUT"};
  dm::ui::SegmentDisplay driveReadout{"dB"};
  dm::ui::SegmentDisplay outputReadout{"dB"};

  dm::ui::GlowButton lookaheadButton, autoGainButton, deltaButton;

  dm::ui::StereoOutputMeter inputMeter, outputMeter;

  std::unique_ptr<juce::Drawable> makersMark;

  using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
  using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

  SliderAttachment driveAttachment, outputAttachment;
  ButtonAttachment lookaheadAttachment, autoGainAttachment, deltaAttachment;

  juce::TooltipWindow tooltips{this};

  /// Keeps the window to the panel's shape.
  juce::ComponentBoundsConstrainer shape;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MachineEditor)
};

/// The colour a machine lights in, the same as on Overtonium's bar.
juce::Colour machineColour(dm::ClipType);

} // namespace dm::plugin
