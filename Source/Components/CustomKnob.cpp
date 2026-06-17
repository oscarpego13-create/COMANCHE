#include "CustomKnob.h"

CustomKnob::CustomKnob(const juce::String& labelText,
                       juce::Colour knobColour,
                       bool isMacro)
    : macro(isMacro), colour(knobColour)
{
    const int knobPx = macro ? 90 : 52;

    slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    slider.setRotaryParameters(juce::MathConstants<float>::pi * 1.25f,
                                juce::MathConstants<float>::pi * 2.75f,
                                true);
    slider.setColour(juce::Slider::rotarySliderFillColourId, knobColour);
    addAndMakeVisible(slider);

    nameLabel.setText(labelText.toUpperCase(), juce::dontSendNotification);
    nameLabel.setJustificationType(juce::Justification::centred);
    nameLabel.setColour(juce::Label::textColourId, CTheme::fgPrimary);
    addAndMakeVisible(nameLabel);

    valueLabel.setJustificationType(juce::Justification::centred);
    valueLabel.setColour(juce::Label::textColourId, CTheme::fgPrimary);
    addAndMakeVisible(valueLabel);

    slider.onValueChange = [this] { updateValueLabel(); };
    updateValueLabel();

    // Component size hint
    setSize(knobPx + 8, knobPx + 30);
}

void CustomKnob::resized()
{
    auto bounds = getLocalBounds();

    // Name label at top (12px)
    nameLabel.setBounds(bounds.removeFromTop(14));

    // Value label at bottom (12px)
    auto valueBounds = bounds.removeFromBottom(14);

    // Slider fills the middle
    slider.setBounds(bounds);

    valueLabel.setBounds(valueBounds);
}

void CustomKnob::updateValueLabel()
{
    valueLabel.setText(juce::String(slider.getValue(), 2), juce::dontSendNotification);
}
