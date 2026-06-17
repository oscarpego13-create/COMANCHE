#pragma once
#include <JuceHeader.h>

// Scans a folder for WAV files and decodes them on demand
class SampleLibrary
{
public:
    SampleLibrary();

    void setFolder(const juce::File& folder);
    juce::File getFolder() const { return currentFolder; }

    const juce::StringArray& getSampleNames() const { return sampleNames; }
    juce::File getSampleFile(int index) const;

    // Decode a WAV file into buffer; returns false if the file can't be read
    bool loadSample(const juce::File& file,
                    juce::AudioBuffer<float>& buffer,
                    double& sampleRateOut);

    // Serialize / restore folder path as a ValueTree property
    juce::String getFolderPath() const;
    void setFolderFromPath(const juce::String& path);

private:
    juce::File              currentFolder;
    juce::Array<juce::File> sampleFiles;
    juce::StringArray       sampleNames;
    juce::AudioFormatManager formatManager;

    void scan();
};
