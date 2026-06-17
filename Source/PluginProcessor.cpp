#include "PluginProcessor.h"
#include "PluginEditor.h"

ComanacheAudioProcessor::ComanacheAudioProcessor()
    : AudioProcessor(BusesProperties()
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "Parameters", createParameterLayout()),
      presetManager(apvts)
{
    for (int i = 0; i < 8; ++i)
        samplerInstrument.addVoice(new ComanacheSamplerVoice());

    samplerInstrument.setNoteStealingEnabled(true);
}

ComanacheAudioProcessor::~ComanacheAudioProcessor()
{
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout
ComanacheAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        ParamID::reverbAmount,  "Reverb Amount",  0.0f, 1.0f,     0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        ParamID::distAmount,    "Dist Amount",    0.0f, 1.0f,     0.0f));
    params.push_back(std::make_unique<juce::AudioParameterInt>(
        ParamID::distMode,      "Dist Mode",      0,    2,        0));
    params.push_back(std::make_unique<juce::AudioParameterInt>(
        ParamID::delaySyncMode, "Delay Sync Mode",0,    5,        0));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        ParamID::delayTimeMs,   "Delay Time ms",  1.0f, 2000.0f,  500.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        ParamID::delayLowcut,   "Delay Lowcut",   20.0f, 500.0f,  20.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        ParamID::delayHighcut,  "Delay Highcut",  2000.0f, 18000.0f, 18000.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        ParamID::chorusAmount,  "Chorus Amount",  0.0f, 1.0f,     0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        ParamID::hpFreq,        "HP Freq",        20.0f, 2000.0f, 20.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        ParamID::lpFreq,        "LP Freq",        500.0f, 20000.0f, 20000.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        ParamID::macro,         "Macro",          0.0f, 1.0f,     0.0f));

    return { params.begin(), params.end() };
}

//==============================================================================
void ComanacheAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    samplerInstrument.setCurrentPlaybackSampleRate(sampleRate);
    effectsChain.prepare(sampleRate, samplesPerBlock);
}

void ComanacheAudioProcessor::releaseResources()
{
}

void ComanacheAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                           juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();

    // Read BPM from host playhead
    if (auto* ph = getPlayHead())
    {
        juce::AudioPlayHead::CurrentPositionInfo info;
        if (ph->getCurrentPosition(info) && info.bpm > 0.0)
            currentBpm = info.bpm;
    }

    samplerInstrument.renderNextBlock(buffer, midiMessages, 0, buffer.getNumSamples());

    // Build effects parameter struct, applying macro offsets
    const float macroVal = *apvts.getRawParameterValue(ParamID::macro);
    EffectsParameters ep;
    ep.reverbAmount  = macroController.apply(macroVal, *apvts.getRawParameterValue(ParamID::reverbAmount), MacroController::Target::Reverb);
    ep.distAmount    = macroController.apply(macroVal, *apvts.getRawParameterValue(ParamID::distAmount),   MacroController::Target::Dist);
    ep.distMode      = (int)*apvts.getRawParameterValue(ParamID::distMode);
    ep.delaySyncMode = (int)*apvts.getRawParameterValue(ParamID::delaySyncMode);
    ep.delayTimeMs   = *apvts.getRawParameterValue(ParamID::delayTimeMs);
    ep.delayLowcut   = *apvts.getRawParameterValue(ParamID::delayLowcut);
    ep.delayHighcut  = *apvts.getRawParameterValue(ParamID::delayHighcut);
    ep.chorusAmount  = macroController.apply(macroVal, *apvts.getRawParameterValue(ParamID::chorusAmount), MacroController::Target::Chorus);
    ep.hpFreq        = *apvts.getRawParameterValue(ParamID::hpFreq);
    ep.lpFreq        = *apvts.getRawParameterValue(ParamID::lpFreq);
    ep.bpm           = currentBpm;

    effectsChain.setParameters(ep);
    effectsChain.process(buffer);
}

//==============================================================================
void ComanacheAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty("sampleFolder", sampleLibrary.getFolder().getFullPathName(), nullptr);

    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void ComanacheAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml(getXmlFromBinary(data, sizeInBytes));
    if (xml == nullptr) return;

    auto state = juce::ValueTree::fromXml(*xml);
    if (!state.isValid()) return;

    apvts.replaceState(state);

    juce::String folderPath = state.getProperty("sampleFolder", "").toString();
    if (folderPath.isNotEmpty())
    {
        juce::File folder(folderPath);
        if (folder.isDirectory())
            sampleLibrary.setFolder(folder);
    }
}

//==============================================================================
void ComanacheAudioProcessor::parameterChanged(const juce::String&, float)
{
    // Parameters are read directly from APVTS in processBlock
}

//==============================================================================
void ComanacheAudioProcessor::loadSample(const juce::File& file)
{
    juce::AudioBuffer<float> buffer;
    double fileSampleRate = 44100.0;

    if (sampleLibrary.loadSample(file, buffer, fileSampleRate))
    {
        samplerInstrument.loadSound(buffer, fileSampleRate);
        presetManager.loadPreset(file, apvts);
    }
}

void ComanacheAudioProcessor::savePreset()
{
    presetManager.saveCurrentPreset(apvts);
}

void ComanacheAudioProcessor::deletePreset()
{
    presetManager.deleteCurrentPreset();
}

double ComanacheAudioProcessor::getCurrentBpm() const
{
    return currentBpm;
}

//==============================================================================
juce::AudioProcessorEditor* ComanacheAudioProcessor::createEditor()
{
    return new ComanacheAudioProcessorEditor(*this);
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ComanacheAudioProcessor();
}
