#include "SampleListPanel.h"
#include "../PluginProcessor.h"
#include "../CustomLookAndFeel.h"

SampleListPanel::SampleListPanel(ComanacheAudioProcessor& processor)
    : proc(processor)
{
    listBox.setModel(this);
    listBox.setRowHeight(22);
    listBox.setColour(juce::ListBox::backgroundColourId, CTheme::bgPanel);
    listBox.setColour(juce::ListBox::outlineColourId, juce::Colours::transparentBlack);
    addAndMakeVisible(listBox);

    saveBtn.setClickingTogglesState(false);
    saveBtn.onClick = [this] { proc.savePreset(); };
    addAndMakeVisible(saveBtn);

    delBtn.setClickingTogglesState(false);
    delBtn.onClick = [this] { proc.deletePreset(); };
    addAndMakeVisible(delBtn);
}

void SampleListPanel::resized()
{
    auto bounds = getLocalBounds().reduced(8);

    // Button row at bottom
    auto btnRow = bounds.removeFromBottom(28);
    const int btnW = btnRow.getWidth() / 2 - 4;
    saveBtn.setBounds(btnRow.removeFromLeft(btnW));
    btnRow.removeFromLeft(8);
    delBtn.setBounds(btnRow);

    bounds.removeFromBottom(6);
    listBox.setBounds(bounds);
}

void SampleListPanel::paint(juce::Graphics& g)
{
    g.fillAll(CTheme::bgPanel);
}

void SampleListPanel::refreshList()
{
    listBox.updateContent();
    listBox.repaint();
}

int SampleListPanel::getNumRows()
{
    return proc.sampleLibrary.getSampleNames().size();
}

void SampleListPanel::paintListBoxItem(int row, juce::Graphics& g,
                                       int width, int height, bool selected)
{
    if (selected)
    {
        g.setColour(CTheme::fgPrimary);
        g.fillRect(0, 0, width, height);
    }

    const auto& names = proc.sampleLibrary.getSampleNames();
    if (juce::isPositiveAndBelow(row, names.size()))
    {
        g.setColour(selected ? CTheme::textLight : CTheme::fgPrimary);
        juce::Font f(juce::FontOptions(10.0f));
        g.setFont(f);
        g.drawText(names[row], 8, 0, width - 8, height, juce::Justification::centredLeft);
    }
}

void SampleListPanel::listBoxItemClicked(int row, const juce::MouseEvent&)
{
    selectedRow = row;
    juce::File f = proc.sampleLibrary.getSampleFile(row);
    if (f.existsAsFile())
        proc.loadSample(f);
}
