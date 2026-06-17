#pragma once
#include <JuceHeader.h>

// Design system colours
namespace CTheme
{
    inline const juce::Colour bgPrimary  { 0xFFEDEAE4 };
    inline const juce::Colour bgPanel    { 0xFFE2DED7 };
    inline const juce::Colour fgPrimary  { 0xFF1A1A1A };
    inline const juce::Colour knobGrey   { 0xFF9B9B8E };
    inline const juce::Colour knobBlue   { 0xFF8BA3B8 };
    inline const juce::Colour knobGold   { 0xFFB8A882 };
    inline const juce::Colour btnActive  { 0xFF1A1A1A };
    inline const juce::Colour btnInactive{ 0xFFB8A882 };
    inline const juce::Colour textLight  { 0xFFEDEAE4 };
}

class CustomLookAndFeel : public juce::LookAndFeel_V4
{
public:
    CustomLookAndFeel();

    // Flat knob: solid circle + 2 px line from centre to edge indicating value
    void drawRotarySlider(juce::Graphics&, int x, int y, int width, int height,
                          float sliderPosProportional, float rotaryStartAngle,
                          float rotaryEndAngle, juce::Slider&) override;

    // Rounded-rect button, active = dark bg / white text, inactive = gold
    void drawButtonBackground(juce::Graphics&, juce::Button&,
                              const juce::Colour& backgroundColour,
                              bool shouldDrawButtonAsHighlighted,
                              bool shouldDrawButtonAsDown) override;

    void drawButtonText(juce::Graphics&, juce::TextButton&,
                        bool isMouseOverButton, bool isButtonDown) override;

    // List box: flat, bg-panel rows
    void drawListBoxItem(int rowNumber, juce::Graphics&, int width, int height,
                         bool rowIsSelected) override;

    juce::Font getTextButtonFont(juce::TextButton&, int buttonHeight) override;
    juce::Font getLabelFont(juce::Label&) override;

    // Load Roboto Mono from embedded binary data
    void loadFont();

private:
    juce::Font robotoMono;
};
