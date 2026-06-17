#pragma once

#include <JuceHeader.h>
#include "SampleLibrary.h"
#include "SamplerInstrument.h"
#include "EffectsChain.h"
#include "MacroController.h"
#include "PresetManager.h"

// Parameter IDs — single source of truth used by all components
namespace ParamID
{
    inline constexpr const char* reverbAmount  = "reverb_amount";
    inline constexpr const char* distAmount    = "dist_amount";
    inline constexpr const char* distMode      = "dist_mode";
    inline constexpr const char* delaySyncMode = "delay_sync_mode";
    inline constexpr const char* delayTimeMs   = "delay_time_ms";
    inline constexpr const char* delayLowcut   = "delay_lowcut";
    inline constexpr const char* delayHighcut  = "delay_highcut";
    inline constexpr const char* chorusAmount  = "chorus_amount";
    inline constexpr const char* hpFreq        = "hp_freq";
    inline constexpr const char* lpFreq        = "lp_freq";
    inline constexpr const char* macro         = "macro";
}

class ComanacheAudioProcessor : public juce::AudioProcessor,
                                public juce::AudioProcessorValueTreeState::Listener
{
public:
    ComanacheAudioProcessor();
    ~ComanacheAudioProcessor() override;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    //==============================================================================
    const juce::String getName() const override { return "COMANCHE"; }
    bool acceptsMidi() const override  { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 2.0; }

    //==============================================================================
    int getNumPrograms() override    { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    //==============================================================================
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==============================================================================
    // Called when an APVTS parameter changes
    void parameterChanged (const juce::String& parameterID, float newValue) override;

    //==============================================================================
    juce::AudioProcessorValueTreeState apvts;
    SampleLibrary    sampleLibrary;
    SamplerInstrument samplerInstrument;
    EffectsChain     effectsChain;
    MacroController  macroController;
    PresetManager    presetManager;

    // Called by the editor when the user selects a sample from the list
    void loadSample (const juce::File& file);

    // Called by the editor preset buttons
    void savePreset();
    void deletePreset();

    // Returns the BPM from the host playhead (0 if unavailable)
    double getCurrentBpm() const;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // Persist the samples folder path inside the plugin state
    juce::ValueTree folderState { "FolderState" };

    double currentBpm { 120.0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ComanacheAudioProcessor)
};
