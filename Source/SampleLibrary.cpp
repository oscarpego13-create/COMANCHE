#include "SampleLibrary.h"

SampleLibrary::SampleLibrary()
{
    formatManager.registerBasicFormats();
}

void SampleLibrary::setFolder(const juce::File& folder)
{
    currentFolder = folder;
    scan();
}

juce::File SampleLibrary::getSampleFile(int index) const
{
    if (juce::isPositiveAndBelow(index, sampleFiles.size()))
        return sampleFiles[index];
    return {};
}

bool SampleLibrary::loadSample(const juce::File& file,
                               juce::AudioBuffer<float>& buffer,
                               double& sampleRateOut)
{
    std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(file));
    if (reader == nullptr) return false;

    sampleRateOut = reader->sampleRate;
    buffer.setSize((int)reader->numChannels, (int)reader->lengthInSamples);
    reader->read(&buffer, 0, (int)reader->lengthInSamples, 0, true, true);
    return true;
}

juce::String SampleLibrary::getFolderPath() const
{
    return currentFolder.getFullPathName();
}

void SampleLibrary::setFolderFromPath(const juce::String& path)
{
    if (path.isNotEmpty())
    {
        juce::File f(path);
        if (f.isDirectory())
            setFolder(f);
    }
}

void SampleLibrary::scan()
{
    sampleFiles.clear();
    sampleNames.clear();

    if (!currentFolder.isDirectory()) return;

    currentFolder.findChildFiles(sampleFiles, juce::File::findFiles, false, "*.wav");
    sampleFiles.sort();

    for (auto& f : sampleFiles)
        sampleNames.add(f.getFileNameWithoutExtension());
}
