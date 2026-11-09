#pragma once

#include <memory>

#include <dm_gui/dm_gui.h>
#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"

namespace dm::plugin {

/// The window: the machine's name along the top, then a column for the input
/// and one for the output, each with its knob, its readout and its meter, and
/// the two switches in a row under them. Drawn at one size and scaled to the
/// window.
class MachineEditor final : public juce::AudioProcessorEditor,
                            private juce::Timer {
public:
  explicit MachineEditor(MachineProcessor &);
  ~MachineEditor() override;

  /// The size everything is laid out at. The window keeps this shape and
  /// scales it.
  static constexpr int kWidth = 320;
  static constexpr int kHeight = 460;

  void resized() override;

  /// The controls, for the tests.
  dm::ui::Slider &getInputKnob() noexcept { return inputKnob; }
  dm::ui::Slider &getOutputKnob() noexcept { return outputKnob; }
  dm::ui::GlowButton &getAutoGainButton() noexcept { return autoGainButton; }
  dm::ui::GlowButton &getDeltaButton() noexcept { return deltaButton; }
  dm::ui::SegmentDisplay &getInputReadout() noexcept { return inputReadout; }
  dm::ui::SegmentDisplay &getOutputReadout() noexcept { return outputReadout; }
  dm::ui::StereoOutputMeter &getInputMeter() noexcept { return inputMeter; }
  dm::ui::StereoOutputMeter &getOutputMeter() noexcept { return outputMeter; }

  /// Moves the meters and the readouts on, as the timer does. Public so the
  /// tests can call it with no message loop running.
  void refresh();

private:
  /// The shared look, with the switches' legends allowed a second line, so a
  /// square switch can say AUTO GAIN.
  class Look final : public dm::ui::LookAndFeel {
  public:
    void drawButtonText(juce::Graphics &, juce::TextButton &, bool,
                        bool) override;
  };

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
  static juce::Rectangle<int> inputPocket();
  static juce::Rectangle<int> outputPocket();
  static juce::Rectangle<int> switchesPocket();

  /// Where a column's name sits, at the top of its pocket.
  static juce::Rectangle<int> captionIn(juce::Rectangle<int> pocket);

  MachineProcessor &machineProcessor;

  /// First, so it is destroyed after every control that draws with it.
  Look lookAndFeel;

  Panel panel{*this};

  dm::ui::Slider inputKnob, outputKnob;
  dm::ui::SegmentDisplay inputReadout{"dB"};
  dm::ui::SegmentDisplay outputReadout{"dB"};

  dm::ui::GlowButton autoGainButton, deltaButton;

  dm::ui::StereoOutputMeter inputMeter, outputMeter;

  std::unique_ptr<juce::Drawable> makersMark;

  using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
  using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

  SliderAttachment inputAttachment, outputAttachment;
  ButtonAttachment autoGainAttachment, deltaAttachment;

  juce::TooltipWindow tooltips{this};

  /// Keeps the window to the panel's shape.
  juce::ComponentBoundsConstrainer shape;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MachineEditor)
};

/// The colour a machine lights in, the same as on Overtonium's bar.
juce::Colour machineColour(dm::ClipType);

namespace looks {
/// The readouts, in the red of an LED display.
inline const juce::Colour readout{0xffff3b2a};

/// A lit switch, in the warm yellow of a filament bulb.
inline const juce::Colour lamp{0xffffd98a};

/// The meters, amber rising to orange and then red at full scale.
inline const juce::Colour meterLow{0xffe9a23b};
inline const juce::Colour meterHigh{0xfff07a26};
inline const juce::Colour meterTop{0xffee3f2c};
} // namespace looks

} // namespace dm::plugin
