#pragma once
#include <JuceHeader.h>
#include "../CustomLookAndFeel.h"

// Self-contained knob component: Slider (rotary) + name label + value label.
// The knob colour is set via Slider::rotarySliderFillColourId on the slider.
class CustomKnob : public juce::Component
{
public:
    // isMacro = true → 90×90 px knob; false → 52×52 px knob
    explicit CustomKnob(const juce::String& labelText,
                        juce::Colour knobColour,
                        bool isMacro = false);

    void resized() override;

    // Public slider so the parent can attach a SliderAttachment
    juce::Slider slider;

private:
    juce::Label  nameLabel;
    juce::Label  valueLabel;
    bool         macro;
    juce::Colour colour;

    void updateValueLabel();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CustomKnob)
};
