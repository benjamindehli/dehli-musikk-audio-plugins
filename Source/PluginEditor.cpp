#include "PluginEditor.h"

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

juce::Rectangle<int> MachineEditor::knobsPocket() {
  return {kMargin, kHeaderHeight + 8, 196, kHeight - kHeaderHeight - 18};
}

juce::Rectangle<int> MachineEditor::switchesPocket() {
  const auto knobs = knobsPocket();
  return knobs.withX(knobs.getRight() + 8).withWidth(112);
}

juce::Rectangle<int> MachineEditor::metersPocket() {
  const auto switches = switchesPocket();
  return switches.withX(switches.getRight() + 8).withRight(kWidth - kMargin);
}

MachineEditor::MachineEditor(MachineProcessor &p)
    : juce::AudioProcessorEditor(p), machineProcessor(p),
      makersMark(dm::ui::logoMakersMark()),
      driveAttachment(p.apvts, params::driveId, driveKnob.slider),
      outputAttachment(p.apvts, params::outputId, outputKnob.slider),
      lookaheadAttachment(p.apvts, params::lookaheadId, lookaheadButton),
      autoGainAttachment(p.apvts, params::autoGainId, autoGainButton),
      deltaAttachment(p.apvts, params::deltaId, deltaButton) {
  setLookAndFeel(&lookAndFeel);

  const auto lit = machineColour(p.getMachine());

  for (auto *knob : {&driveKnob, &outputKnob}) {
    knob->slider.setColour(juce::Slider::rotarySliderFillColourId, lit);

    // Back to 0 dB, which is where both start.
    knob->slider.setDoubleClickReturnValue(true, 0.0);
    knob->slider.setPopupDisplayEnabled(true, true, this);
    panel.addAndMakeVisible(*knob);
  }

  driveKnob.slider.setTitle("Drive");
  driveKnob.slider.setTooltip("How hard the input is driven into the machine");
  outputKnob.slider.setTitle("Output");
  outputKnob.slider.setTooltip("The level after the machine");

  for (auto *readout : {&driveReadout, &outputReadout})
    panel.addAndMakeVisible(*readout);

  const struct {
    dm::ui::GlowButton &button;
    const char *text;
    const char *tip;
    juce::Colour lamp;
  } switches[] = {
      {lookaheadButton, "LOOKAHEAD",
       "The limiter looks 2 ms ahead, which is smoother and adds that much "
       "latency",
       colours::accent},
      {autoGainButton, "AUTO GAIN",
       "Turns the output down by as much as the drive turns the input up",
       colours::accent},
      {deltaButton, "DELTA",
       "Plays only what the machine adds or takes away: the output less the "
       "input",
       colours::soloOn},
  };

  for (const auto &s : switches) {
    s.button.setButtonText(s.text);
    s.button.setTooltip(s.tip);
    s.button.setClickingTogglesState(true);
    s.button.setColour(juce::TextButton::textColourOnId, s.lamp);
    panel.addAndMakeVisible(s.button);
  }

  for (auto *meter : {&inputMeter, &outputMeter})
    panel.addAndMakeVisible(*meter);

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

  const auto knobs = knobsPocket();
  const auto knobWidth = (knobs.getWidth() - 12) / 2;

  auto column = knobs.reduced(6, 6);
  for (auto [knob, readout] : {std::pair<juce::Component *, juce::Component *>{
                                   &driveKnob, &driveReadout},
                               {&outputKnob, &outputReadout}}) {
    auto slot = column.removeFromLeft(knobWidth);
    knob->setBounds(slot.removeFromTop(104));
    readout->setBounds(slot.removeFromTop(30).withSizeKeepingCentre(72, 24));
  }

  auto switches = switchesPocket().reduced(10, 12);
  const auto step = switches.getHeight() / 3;
  for (auto *button : {&lookaheadButton, &autoGainButton, &deltaButton})
    button->setBounds(
        switches.removeFromTop(step).withSizeKeepingCentre(92, 26));

  auto meters = metersPocket().reduced(10, 12);
  const auto half = meters.getHeight() / 2;
  for (auto *meter : {&inputMeter, &outputMeter}) {
    auto slot = meters.removeFromTop(half);
    slot.removeFromTop(22); // the caption
    meter->setBounds(slot.removeFromTop(16));
  }
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
  driveReadout.setReading(
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

  for (const auto &pocket : {knobsPocket(), switchesPocket(), metersPocket()})
    dm::ui::paintGroupPocket(g, pocket);

  // The captions over the two meters.
  g.setColour(colours::textDim);
  g.setFont(dm::ui::makeFont(9.0f, true));

  const auto meters = metersPocket().reduced(10, 12);
  const auto half = meters.getHeight() / 2;
  int top = meters.getY();
  for (const auto *caption : {"IN", "OUT"}) {
    g.drawText(caption, meters.getX(), top + 4, meters.getWidth(), 14,
               juce::Justification::centredLeft, false);
    top += half;
  }
}

} // namespace dm::plugin
