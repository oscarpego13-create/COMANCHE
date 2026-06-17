#include "EffectsPanel.h"
#include "../PluginProcessor.h"

EffectsPanel::EffectsPanel(ComanacheAudioProcessor& processor)
    : proc(processor)
{
    auto& apvts = proc.apvts;

    // Macro
    addAndMakeVisible(macroKnob);
    macroAtt = std::make_unique<SliderAtt>(apvts, "macro", macroKnob.slider);

    // Reverb
    addAndMakeVisible(reverbKnob);
    reverbAtt = std::make_unique<SliderAtt>(apvts, "reverb_amount", reverbKnob.slider);

    // Distortion
    addAndMakeVisible(distKnob);
    distAtt = std::make_unique<SliderAtt>(apvts, "dist_amount", distKnob.slider);
    for (auto* b : { &clipBtn, &tapeBtn, &tubeBtn })
    {
        b->setClickingTogglesState(false);
        addAndMakeVisible(b);
    }
    setupDistButtons();

    // Delay
    addAndMakeVisible(delayTimeKnob);
    addAndMakeVisible(delayLowcutKnob);
    addAndMakeVisible(delayHighcutKnob);
    delayTimeAtt = std::make_unique<SliderAtt>(apvts, "delay_time_ms",  delayTimeKnob.slider);
    delayLcAtt   = std::make_unique<SliderAtt>(apvts, "delay_lowcut",   delayLowcutKnob.slider);
    delayHcAtt   = std::make_unique<SliderAtt>(apvts, "delay_highcut",  delayHighcutKnob.slider);
    for (auto* b : { &freeBtn, &d14Btn, &d18Btn, &d116Btn, &d14TBtn, &d18TBtn })
    {
        b->setClickingTogglesState(false);
        addAndMakeVisible(b);
    }
    setupDelayButtons();

    // Chorus
    addAndMakeVisible(chorusKnob);
    chorusAtt = std::make_unique<SliderAtt>(apvts, "chorus_amount", chorusKnob.slider);

    // Output filter
    addAndMakeVisible(hpKnob);
    addAndMakeVisible(lpKnob);
    hpAtt = std::make_unique<SliderAtt>(apvts, "hp_freq", hpKnob.slider);
    lpAtt = std::make_unique<SliderAtt>(apvts, "lp_freq", lpKnob.slider);
}

EffectsPanel::~EffectsPanel() = default;

//==============================================================================
void EffectsPanel::setupDistButtons()
{
    auto setMode = [this](int m) { updateDistMode(m); };
    clipBtn.onClick = [setMode] { setMode(0); };
    tapeBtn.onClick = [setMode] { setMode(1); };
    tubeBtn.onClick = [setMode] { setMode(2); };
    updateDistMode((int)*proc.apvts.getRawParameterValue("dist_mode"));
}

void EffectsPanel::setupDelayButtons()
{
    auto setMode = [this](int m) { updateDelayMode(m); };
    freeBtn.onClick  = [setMode] { setMode(0); };
    d14Btn.onClick   = [setMode] { setMode(1); };
    d18Btn.onClick   = [setMode] { setMode(2); };
    d116Btn.onClick  = [setMode] { setMode(3); };
    d14TBtn.onClick  = [setMode] { setMode(4); };
    d18TBtn.onClick  = [setMode] { setMode(5); };
    updateDelayMode((int)*proc.apvts.getRawParameterValue("delay_sync_mode"));
}

void EffectsPanel::updateDistMode(int mode)
{
    if (auto* p = proc.apvts.getParameter("dist_mode"))
        p->setValueNotifyingHost(p->convertTo0to1((float)mode));

    clipBtn.setToggleState(mode == 0, juce::dontSendNotification);
    tapeBtn.setToggleState(mode == 1, juce::dontSendNotification);
    tubeBtn.setToggleState(mode == 2, juce::dontSendNotification);

    clipBtn.repaint(); tapeBtn.repaint(); tubeBtn.repaint();
}

void EffectsPanel::updateDelayMode(int mode)
{
    if (auto* p = proc.apvts.getParameter("delay_sync_mode"))
        p->setValueNotifyingHost(p->convertTo0to1((float)mode));

    freeBtn.setToggleState (mode == 0, juce::dontSendNotification);
    d14Btn.setToggleState  (mode == 1, juce::dontSendNotification);
    d18Btn.setToggleState  (mode == 2, juce::dontSendNotification);
    d116Btn.setToggleState (mode == 3, juce::dontSendNotification);
    d14TBtn.setToggleState (mode == 4, juce::dontSendNotification);
    d18TBtn.setToggleState (mode == 5, juce::dontSendNotification);

    for (auto* b : { &freeBtn, &d14Btn, &d18Btn, &d116Btn, &d14TBtn, &d18TBtn })
        b->repaint();

    // Show/hide free-time knob based on sync mode
    delayTimeKnob.setVisible(mode == 0);
}

//==============================================================================
void EffectsPanel::paint(juce::Graphics& g)
{
    g.fillAll(CTheme::bgPrimary);

    // Inner panel rect
    auto panelBounds = getLocalBounds().reduced(10);
    g.setColour(CTheme::bgPanel);
    g.fillRoundedRectangle(panelBounds.toFloat(), 6.0f);
}

void EffectsPanel::resized()
{
    const int pad   = 16;
    const int btnH  = 22;
    const int btnW  = 38;
    const int knobW = 70;   // width allocated per knob slot (52px knob + margins)

    auto area = getLocalBounds().reduced(pad);

    // ---- MACRO (centred, top) ----
    const int macroW = 120;
    macroKnob.setBounds(area.getX() + (area.getWidth() - macroW) / 2,
                        area.getY(), macroW, 120);
    area.removeFromTop(128);

    // ---- Two-row effects grid ----
    // Row 1: REVERB | DIST | DELAY
    // Row 2: CHORUS | OUTPUT FILTER

    const int rowH = 120;
    auto row1 = area.removeFromTop(rowH);

    // --- REVERB (1 knob) ---
    const int revW = knobW;
    reverbKnob.setBounds(row1.removeFromLeft(revW));

    // spacer
    row1.removeFromLeft(12);

    // --- DIST (knob + 3 buttons stacked) ---
    const int distW = knobW + 8 + btnW;
    auto distArea = row1.removeFromLeft(distW);
    distKnob.setBounds(distArea.removeFromLeft(knobW));
    distArea.removeFromLeft(8);
    // Stack buttons vertically
    const int bGap = 4;
    const int totalBtnH = 3 * btnH + 2 * bGap;
    auto btnCol = distArea.withSizeKeepingCentre(btnW, totalBtnH);
    clipBtn.setBounds(btnCol.removeFromTop(btnH)); btnCol.removeFromTop(bGap);
    tapeBtn.setBounds(btnCol.removeFromTop(btnH)); btnCol.removeFromTop(bGap);
    tubeBtn.setBounds(btnCol.removeFromTop(btnH));

    // spacer
    row1.removeFromLeft(12);

    // --- DELAY (3 knobs + 6 sync buttons) ---
    auto delayArea = row1;
    // Knobs row
    auto delayKnobRow = delayArea.removeFromTop(80);
    delayTimeKnob.setBounds   (delayKnobRow.removeFromLeft(knobW));
    delayLowcutKnob.setBounds (delayKnobRow.removeFromLeft(knobW));
    delayHighcutKnob.setBounds(delayKnobRow.removeFromLeft(knobW));

    // Sync button row
    auto syncBtnRow = delayArea;
    const int syncBtnW = 40;
    for (auto* b : { &freeBtn, &d14Btn, &d18Btn, &d116Btn, &d14TBtn, &d18TBtn })
    {
        b->setBounds(syncBtnRow.removeFromLeft(syncBtnW));
        syncBtnRow.removeFromLeft(3);
    }

    // Row 2
    area.removeFromTop(8);
    auto row2 = area.removeFromTop(rowH);

    // --- CHORUS ---
    chorusKnob.setBounds(row2.removeFromLeft(knobW));
    row2.removeFromLeft(20);

    // --- OUTPUT FILTER ---
    hpKnob.setBounds(row2.removeFromLeft(knobW));
    row2.removeFromLeft(8);
    lpKnob.setBounds(row2.removeFromLeft(knobW));
}
