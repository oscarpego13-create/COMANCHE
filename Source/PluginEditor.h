#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "CustomLookAndFeel.h"
#include "Components/SampleListPanel.h"
#include "Components/EffectsPanel.h"

class ComanacheAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    explicit ComanacheAudioProcessorEditor(ComanacheAudioProcessor&);
    ~ComanacheAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    ComanacheAudioProcessor& proc;
    CustomLookAndFeel         laf;

    // Header row
    juce::Label       titleLabel;
    juce::TextButton  changeFolderBtn { "CHANGE FOLDER" };

    // Main panels
    SampleListPanel samplePanel;
    EffectsPanel    effectsPanel;

    // Footer
    juce::Label folderPathLabel;

    void showFolderChooser();
    void updateFolderLabel();

    // FileChooser kept alive across async callback
    std::shared_ptr<juce::FileChooser> fileChooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ComanacheAudioProcessorEditor)
};
