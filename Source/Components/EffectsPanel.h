#pragma once
#include <JuceHeader.h>
#include "CustomKnob.h"
#include "../CustomLookAndFeel.h"

class ComanacheAudioProcessor;

// Right panel: MACRO knob + full effects grid
class EffectsPanel : public juce::Component
{
public:
    explicit EffectsPanel(ComanacheAudioProcessor& processor);
    ~EffectsPanel() override;

    void resized() override;
    void paint(juce::Graphics& g) override;

private:
    ComanacheAudioProcessor& proc;

    //-- MACRO
    CustomKnob macroKnob  { "MACRO",  CTheme::knobGold, true };

    //-- REVERB
    CustomKnob reverbKnob { "REVERB", CTheme::knobBlue };

    //-- DISTORTION
    CustomKnob distKnob   { "DIST",   CTheme::knobGold };
    juce::TextButton clipBtn  { "CLIP" };
    juce::TextButton tapeBtn  { "TAPE" };
    juce::TextButton tubeBtn  { "TUBE" };

    //-- DELAY
    CustomKnob delayTimeKnob   { "TIME",   CTheme::knobGrey };
    CustomKnob delayLowcutKnob { "LOWCUT", CTheme::knobGrey };
    CustomKnob delayHighcutKnob{ "HIOUT",  CTheme::knobGrey };
    juce::TextButton freeBtn  { "FREE" };
    juce::TextButton d14Btn   { "1/4"  };
    juce::TextButton d18Btn   { "1/8"  };
    juce::TextButton d116Btn  { "1/16" };
    juce::TextButton d14TBtn  { "1/4T" };
    juce::TextButton d18TBtn  { "1/8T" };

    //-- CHORUS
    CustomKnob chorusKnob { "CHORUS", CTheme::knobBlue };

    //-- OUTPUT FILTER
    CustomKnob hpKnob { "HP", CTheme::knobGrey };
    CustomKnob lpKnob { "LP", CTheme::knobGrey };

    // APVTS slider & button attachments
    using SliderAtt = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAtt = juce::AudioProcessorValueTreeState::ButtonAttachment;

    std::unique_ptr<SliderAtt> macroAtt, reverbAtt, distAtt;
    std::unique_ptr<SliderAtt> delayTimeAtt, delayLcAtt, delayHcAtt;
    std::unique_ptr<SliderAtt> chorusAtt, hpAtt, lpAtt;

    // dist_mode and delay_sync_mode are int params → managed via buttons + ComboBoxAttachment workaround
    void setupDistButtons();
    void setupDelayButtons();
    void updateDistMode(int mode);
    void updateDelayMode(int mode);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EffectsPanel)
};
