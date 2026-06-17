#include "PluginEditor.h"

ComanacheAudioProcessorEditor::ComanacheAudioProcessorEditor(ComanacheAudioProcessor& p)
    : AudioProcessorEditor(&p), proc(p),
      samplePanel(p), effectsPanel(p)
{
    setLookAndFeel(&laf);
    setSize(900, 550);
    setResizable(false, false);

    // Title label
    titleLabel.setText("comanche  \xc2\xb7  v1.0", juce::dontSendNotification);
    titleLabel.setColour(juce::Label::textColourId, CTheme::fgPrimary);
    titleLabel.setFont(juce::Font(juce::FontOptions(11.0f)).boldened());
    addAndMakeVisible(titleLabel);

    // Change folder button
    changeFolderBtn.setClickingTogglesState(false);
    changeFolderBtn.onClick = [this] { showFolderChooser(); };
    addAndMakeVisible(changeFolderBtn);

    addAndMakeVisible(samplePanel);
    addAndMakeVisible(effectsPanel);

    // Folder path footer
    folderPathLabel.setColour(juce::Label::textColourId, CTheme::fgPrimary.withAlpha(0.5f));
    folderPathLabel.setFont(juce::Font(juce::FontOptions(9.0f)));
    addAndMakeVisible(folderPathLabel);
    updateFolderLabel();

    // On first launch, show folder chooser if no folder is set
    if (!proc.sampleLibrary.getFolder().isDirectory())
    {
        juce::MessageManager::callAsync([this] { showFolderChooser(); });
    }
}

ComanacheAudioProcessorEditor::~ComanacheAudioProcessorEditor()
{
    setLookAndFeel(nullptr);
}

//==============================================================================
void ComanacheAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(CTheme::bgPrimary);

    // Thin separator between header and body
    g.setColour(CTheme::fgPrimary.withAlpha(0.15f));
    g.drawHorizontalLine(32, 0.0f, (float)getWidth());
}

void ComanacheAudioProcessorEditor::resized()
{
    auto bounds = getLocalBounds();

    // Header row (32 px)
    auto header = bounds.removeFromTop(32);
    titleLabel.setBounds     (header.removeFromLeft(200).reduced(8, 0));
    changeFolderBtn.setBounds(header.removeFromRight(130).reduced(6, 6));

    // Footer row (20 px)
    auto footer = bounds.removeFromBottom(20);
    folderPathLabel.setBounds(footer.reduced(12, 0));

    // Left panel 280px, right panel fills the rest
    samplePanel .setBounds(bounds.removeFromLeft(280));
    effectsPanel.setBounds(bounds);
}

//==============================================================================
void ComanacheAudioProcessorEditor::showFolderChooser()
{
    fileChooser = std::make_shared<juce::FileChooser>(
        "Choose samples folder",
        proc.sampleLibrary.getFolder().isDirectory()
            ? proc.sampleLibrary.getFolder()
            : juce::File::getSpecialLocation(juce::File::userMusicDirectory));

    const int flags = juce::FileBrowserComponent::openMode
                    | juce::FileBrowserComponent::canSelectDirectories;

    fileChooser->launchAsync(flags, [this](const juce::FileChooser& fc)
    {
        auto result = fc.getResult();
        if (result.isDirectory())
        {
            proc.sampleLibrary.setFolder(result);
            samplePanel.refreshList();
            updateFolderLabel();
        }
    });
}

void ComanacheAudioProcessorEditor::updateFolderLabel()
{
    juce::String path = proc.sampleLibrary.getFolder().getFullPathName();
    folderPathLabel.setText(path.isEmpty() ? "no folder selected" : path,
                            juce::dontSendNotification);
}
