#include "PresetManager.h"

PresetManager::PresetManager(juce::AudioProcessorValueTreeState&) {}

juce::File PresetManager::jsonFileFor(const juce::File& sampleFile)
{
    return sampleFile.withFileExtension("json");
}

void PresetManager::loadPreset(const juce::File& sampleFile,
                               juce::AudioProcessorValueTreeState& apvts)
{
    currentJsonFile = jsonFileFor(sampleFile);
    if (!currentJsonFile.existsAsFile()) return;

    juce::String jsonText = currentJsonFile.loadFileAsString();
    auto parsed = juce::JSON::parse(jsonText);
    if (auto* obj = parsed.getDynamicObject())
    {
        for (auto& kv : obj->getProperties())
        {
            if (auto* param = apvts.getParameter(kv.name.toString()))
                param->setValueNotifyingHost(param->convertTo0to1((float)kv.value));
        }
    }
}

void PresetManager::saveCurrentPreset(juce::AudioProcessorValueTreeState& apvts)
{
    if (currentJsonFile == juce::File{}) return;

    auto obj = std::make_unique<juce::DynamicObject>();
    for (auto* param : apvts.processor.getParameters())
    {
        if (auto* arp = dynamic_cast<juce::RangedAudioParameter*>(param))
        {
            const float denormalized = arp->convertFrom0to1(arp->getValue());
            obj->setProperty(arp->paramID, denormalized);
        }
    }

    juce::String jsonText = juce::JSON::toString(obj.get(), true);
    currentJsonFile.replaceWithText(jsonText);
}

void PresetManager::deleteCurrentPreset()
{
    if (currentJsonFile.existsAsFile())
        currentJsonFile.deleteFile();
}
