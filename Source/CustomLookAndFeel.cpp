#include "CustomLookAndFeel.h"
#include "BinaryData.h"

CustomLookAndFeel::CustomLookAndFeel()
{
    loadFont();

    setColour(juce::ResizableWindow::backgroundColourId, CTheme::bgPrimary);
    setColour(juce::Slider::rotarySliderFillColourId,   CTheme::knobGrey);
    setColour(juce::Label::textColourId,                CTheme::fgPrimary);
    setColour(juce::ListBox::backgroundColourId,        CTheme::bgPanel);
    setColour(juce::ListBox::outlineColourId,           juce::Colours::transparentBlack);
}

void CustomLookAndFeel::loadFont()
{
    robotoMono = juce::Font(juce::FontOptions(juce::Typeface::createSystemTypefaceFor(
        BinaryData::RobotoMonoRegular_ttf,
        (size_t)BinaryData::RobotoMonoRegular_ttfSize)));
    robotoMono.setHeight(11.0f);
}

//==============================================================================
void CustomLookAndFeel::drawRotarySlider(juce::Graphics& g,
                                         int x, int y, int width, int height,
                                         float sliderPos,
                                         float startAngle, float endAngle,
                                         juce::Slider& slider)
{
    const float radius   = (float)juce::jmin(width, height) * 0.5f - 4.0f;
    const float centreX  = (float)x + (float)width  * 0.5f;
    const float centreY  = (float)y + (float)height * 0.5f;
    const float angle    = startAngle + sliderPos * (endAngle - startAngle);

    // Knob body
    juce::Colour knobColour = slider.findColour(juce::Slider::rotarySliderFillColourId);
    g.setColour(knobColour);
    g.fillEllipse(centreX - radius, centreY - radius, radius * 2.0f, radius * 2.0f);

    // Knob outline
    g.setColour(CTheme::fgPrimary);
    g.drawEllipse(centreX - radius, centreY - radius, radius * 2.0f, radius * 2.0f, 1.5f);

    // Indicator line from centre to edge
    const float lineR = radius - 2.0f;
    g.drawLine(centreX,
               centreY,
               centreX + lineR * std::sin(angle),
               centreY - lineR * std::cos(angle),
               2.0f);
}

//==============================================================================
void CustomLookAndFeel::drawButtonBackground(juce::Graphics& g,
                                              juce::Button& button,
                                              const juce::Colour&,
                                              bool highlighted, bool down)
{
    const bool active = button.getToggleState();
    juce::Colour bg   = active ? CTheme::btnActive : CTheme::btnInactive;
    if (down || highlighted)
        bg = bg.brighter(0.1f);

    g.setColour(bg);
    g.fillRoundedRectangle(button.getLocalBounds().toFloat(), 2.0f);
}

void CustomLookAndFeel::drawButtonText(juce::Graphics& g,
                                        juce::TextButton& button,
                                        bool, bool)
{
    const bool active = button.getToggleState();
    g.setColour(active ? CTheme::textLight : CTheme::fgPrimary);

    juce::Font f = robotoMono.withHeight(10.0f).boldened();
    g.setFont(f);
    g.drawFittedText(button.getButtonText().toUpperCase(),
                     button.getLocalBounds(), juce::Justification::centred, 1);
}

//==============================================================================
void CustomLookAndFeel::drawListBoxItem(int, juce::Graphics& g,
                                        int width, int height, bool selected)
{
    if (selected)
    {
        g.setColour(CTheme::fgPrimary);
        g.fillRect(0, 0, width, height);
    }
}

//==============================================================================
juce::Font CustomLookAndFeel::getTextButtonFont(juce::TextButton&, int)
{
    return robotoMono.withHeight(10.0f).boldened();
}

juce::Font CustomLookAndFeel::getLabelFont(juce::Label&)
{
    return robotoMono.withHeight(10.0f);
}
