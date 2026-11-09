#include "PluginEditor.h"

#include <tuple>

#include "Parameters.h"

namespace dm::plugin {

namespace colours = dm::ui::colours;

namespace {
/// The band along the top with the name and the maker's mark.
constexpr int kHeaderHeight = 40;

/// The margin round the pockets and between them.
constexpr int kMargin = 10;

/// How far the window can be made larger than the panel is drawn.
constexpr double kLargest = 2.0;

/// The two columns, input and output, side by side under the header.
constexpr int kColumnWidth = 145;
constexpr int kColumnHeight = 328;

/// The knobs, the meters and the square switches.
constexpr int kKnobSize = 100;
constexpr int kMeterWidth = 22;
constexpr int kSwitchSize = 46;
} // namespace

juce::Colour machineColour(dm::ClipType machine) {
  switch (machine) {
  case dm::ClipType::Console:
    return juce::Colour(0xffe48e26);
  case dm::ClipType::Cassette:
    return juce::Colour(0xffabac1d);
  case dm::ClipType::Soft:
  case dm::ClipType::Hard:
  case dm::ClipType::Asymmetric:
  case dm::ClipType::Limiter:
  case dm::ClipType::Fold:
  case dm::ClipType::Print:
  case dm::ClipType::NumTypes:
    break;
  }

  return colours::accent;
}

juce::Rectangle<int> MachineEditor::inputPocket() {
  return {kMargin, kHeaderHeight + 8, kColumnWidth, kColumnHeight};
}

juce::Rectangle<int> MachineEditor::outputPocket() {
  return inputPocket().withX(kWidth - kMargin - kColumnWidth);
}

juce::Rectangle<int> MachineEditor::switchesPocket() {
  const auto top = inputPocket().getBottom() + kMargin;
  return {kMargin, top, kWidth - 2 * kMargin, kHeight - kMargin - top};
}

juce::Rectangle<int> MachineEditor::captionIn(juce::Rectangle<int> pocket) {
  return pocket.withTrimmedTop(8).withHeight(14);
}

void MachineEditor::Look::drawButtonText(juce::Graphics &g,
                                         juce::TextButton &button, bool, bool) {
  // As the shared look draws a legend, in the same ink, but sized for a
  // square switch and free to break onto a second line.
  g.setFont(dm::ui::makeFont(8.5f, true));

  const auto cap =
      lampCapBounds(button.getLocalBounds().toFloat(), lampGangOf(button));

  g.setGradientFill(legendInk(lampLegend(), cap, button.getToggleState()));

  g.drawFittedText(button.getButtonText(), cap.toNearestInt().reduced(2),
                   juce::Justification::centred, 2, 1.0f);
}

MachineEditor::MachineEditor(MachineProcessor &p)
    : juce::AudioProcessorEditor(p), machineProcessor(p),
      makersMark(dm::ui::logoMakersMark()),
      inputAttachment(p.apvts, params::driveId, inputKnob),
      outputAttachment(p.apvts, params::outputId, outputKnob),
      autoGainAttachment(p.apvts, params::autoGainId, autoGainButton),
      deltaAttachment(p.apvts, params::deltaId, deltaButton) {
  setLookAndFeel(&lookAndFeel);

  for (auto *knob : {&inputKnob, &outputKnob}) {
    knob->setSliderStyle(juce::Slider::RotaryVerticalDrag);
    knob->setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);

    // All cap and a grey stripe, with no ring of lights round it.
    knob->setColour(juce::Slider::rotarySliderFillColourId, colours::textDim);
    knob->getProperties().set("ringless", true);

    // Back to 0 dB, which is where both start.
    knob->setDoubleClickReturnValue(true, 0.0);
    knob->setPopupDisplayEnabled(true, true, this);
    panel.addAndMakeVisible(*knob);
  }

  inputKnob.setTitle("Input");
  inputKnob.setTooltip("How hard the input is driven into the machine");
  outputKnob.setTitle("Output");
  outputKnob.setTooltip("The level after the machine");

  for (auto *readout : {&inputReadout, &outputReadout}) {
    readout->setLampColour(looks::readout);
    panel.addAndMakeVisible(*readout);
  }

  const struct {
    dm::ui::GlowButton &button;
    const char *text;
    const char *tip;
  } switches[] = {
      {autoGainButton, "AUTO\nGAIN",
       "Turns the output down by as much as the input turns it up"},
      {deltaButton, "DELTA",
       "Plays only what the plugin adds or takes away: the output less the "
       "input"},
  };

  for (const auto &s : switches) {
    s.button.setButtonText(s.text);
    s.button.setTooltip(s.tip);
    s.button.setClickingTogglesState(true);
    s.button.setColour(juce::TextButton::textColourOnId, looks::lamp);
    panel.addAndMakeVisible(s.button);
  }

  autoGainButton.setTitle("Auto gain");
  deltaButton.setTitle("Delta");

  for (auto *meter : {&inputMeter, &outputMeter}) {
    meter->setVertical(true);
    meter->setLampColours(looks::meterLow, looks::meterHigh, looks::meterTop);
    panel.addAndMakeVisible(*meter);
  }

  addAndMakeVisible(panel);

  // The panel's shape at any size, from the size it is drawn at to twice it.
  shape.setFixedAspectRatio((double)kWidth / (double)kHeight);
  shape.setSizeLimits(kWidth, kHeight, juce::roundToInt(kWidth * kLargest),
                      juce::roundToInt(kHeight * kLargest));
  setConstrainer(&shape);
  setResizable(true, true);

  layOut();
  refresh();
  setSize(kWidth, kHeight);

  startTimerHz(30);
}

MachineEditor::~MachineEditor() {
  stopTimer();
  setLookAndFeel(nullptr);
}

void MachineEditor::layOut() {
  panel.setSize(kWidth, kHeight);

  for (auto [pocket, knob, readout, meter] :
       {std::tuple<juce::Rectangle<int>, juce::Component *, juce::Component *,
                   juce::Component *>{inputPocket(), &inputKnob, &inputReadout,
                                      &inputMeter},
        {outputPocket(), &outputKnob, &outputReadout, &outputMeter}}) {
    auto column =
        pocket.withTrimmedTop(captionIn(pocket).getBottom() - pocket.getY());

    column.removeFromTop(4);
    knob->setBounds(column.removeFromTop(kKnobSize).withSizeKeepingCentre(
        kKnobSize, kKnobSize));

    column.removeFromTop(6);
    readout->setBounds(column.removeFromTop(24).withSizeKeepingCentre(76, 24));

    column.removeFromTop(12);
    meter->setBounds(column.withTrimmedBottom(12).withSizeKeepingCentre(
        kMeterWidth, column.getHeight() - 12));
  }

  // Each switch square, under its own column.
  const auto row = switchesPocket();
  for (auto [button, pocket] :
       {std::pair<juce::Component *, juce::Rectangle<int>>{&autoGainButton,
                                                           inputPocket()},
        {&deltaButton, outputPocket()}})
    button->setBounds(juce::Rectangle<int>(kSwitchSize, kSwitchSize)
                          .withCentre({pocket.getCentreX(), row.getCentreY()}));
}

void MachineEditor::resized() {
  // The panel is laid out once at its own size and drawn scaled, so every
  // size is the same picture.
  const auto scale = (float)getWidth() / (float)kWidth;
  panel.setTransform(juce::AffineTransform::scale(scale));
}

void MachineEditor::refresh() {
  const auto [inL, inR] = machineProcessor.inputPeaks.take();
  const auto [outL, outR] = machineProcessor.outputPeaks.take();
  inputMeter.push(inL, inR);
  outputMeter.push(outL, outR);

  const auto &state = machineProcessor.apvts;
  inputReadout.setReading(
      params::decibelText(state.getRawParameterValue(params::driveId)->load()),
      true);
  outputReadout.setReading(
      params::decibelText(state.getRawParameterValue(params::outputId)->load()),
      true);
}

void MachineEditor::Panel::paint(juce::Graphics &g) {
  const auto bounds = getLocalBounds();

  g.fillAll(colours::background);

  // The header: a lighter band with the machine's name and the maker's mark.
  const auto header = bounds.withHeight(kHeaderHeight);
  g.setGradientFill(juce::ColourGradient(colours::panel.brighter(0.08f), 0.0f,
                                         0.0f, colours::panel, 0.0f,
                                         (float)kHeaderHeight, false));
  g.fillRect(header);
  g.setColour(juce::Colours::black.withAlpha(0.5f));
  g.fillRect(header.withTop(header.getBottom() - 1));

  dm::ui::paintGrain(g, bounds);

  const auto name =
      juce::String(dm::clipTypeName(editor.machineProcessor.getMachine()));
  g.setColour(machineColour(editor.machineProcessor.getMachine()));
  g.setFont(dm::ui::makeFont(20.0f, true).withExtraKerningFactor(0.12f));
  g.drawText(name.toUpperCase(), header.reduced(kMargin + 4, 0),
             juce::Justification::centredLeft, false);

  if (editor.makersMark != nullptr)
    editor.makersMark->drawWithin(g, header.reduced(kMargin + 2, 10).toFloat(),
                                  juce::RectanglePlacement::xRight |
                                      juce::RectanglePlacement::yMid,
                                  0.85f);

  for (const auto &pocket : {inputPocket(), outputPocket(), switchesPocket()})
    dm::ui::paintGroupPocket(g, pocket);

  // Each column's name at its top.
  g.setColour(colours::textDim);
  g.setFont(dm::ui::makeFont(10.0f, true).withExtraKerningFactor(0.08f));

  for (auto [pocket, caption] :
       {std::pair<juce::Rectangle<int>, const char *>{inputPocket(), "INPUT"},
        {outputPocket(), "OUTPUT"}})
    g.drawText(caption, captionIn(pocket), juce::Justification::centred, false);
}

} // namespace dm::plugin
