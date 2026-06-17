#pragma once
#include <JuceHeader.h>
#include "../SampleLibrary.h"

class ComanacheAudioProcessor;

// Left panel (280 px): scrollable WAV list + SAVE PRESET / DEL PRESET buttons
class SampleListPanel : public juce::Component,
                        public juce::ListBoxModel
{
public:
    explicit SampleListPanel(ComanacheAudioProcessor& processor);

    void resized() override;
    void paint(juce::Graphics& g) override;

    // Refresh list after folder change
    void refreshList();

    //-- ListBoxModel
    int  getNumRows() override;
    void paintListBoxItem(int rowNumber, juce::Graphics&,
                          int width, int height, bool selected) override;
    void listBoxItemClicked(int row, const juce::MouseEvent&) override;

private:
    ComanacheAudioProcessor& proc;

    juce::ListBox    listBox;
    juce::TextButton saveBtn { "SAVE PRESET" };
    juce::TextButton delBtn  { "DEL PRESET"  };

    int selectedRow { -1 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SampleListPanel)
};
