#pragma once
#include <JuceHeader.h>

// Reads and writes per-sample JSON preset files stored alongside the .wav
class PresetManager
{
public:
    explicit PresetManager(juce::AudioProcessorValueTreeState& apvts);

    // Called when a sample is selected: auto-loads JSON if present
    void loadPreset(const juce::File& sampleFile,
                    juce::AudioProcessorValueTreeState& apvts);

    // Write current APVTS values to the JSON file for the active sample
    void saveCurrentPreset(juce::AudioProcessorValueTreeState& apvts);

    // Delete the JSON file for the active sample
    void deleteCurrentPreset();

    bool hasActivePresetFile() const { return currentJsonFile.existsAsFile(); }

private:
    juce::File currentJsonFile;

    static juce::File jsonFileFor(const juce::File& sampleFile);
};
