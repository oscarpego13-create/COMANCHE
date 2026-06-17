#include "SamplerVoice.h"

//==============================================================================
ComanacheSamplerSound::ComanacheSamplerSound(const juce::AudioBuffer<float>& buffer,
                                             double sr)
    : data(buffer), sourceSampleRate(sr) {}

//==============================================================================
bool ComanacheSamplerVoice::canPlaySound(juce::SynthesiserSound* sound)
{
    return dynamic_cast<ComanacheSamplerSound*>(sound) != nullptr;
}

void ComanacheSamplerVoice::startNote(int midiNoteNumber, float velocity,
                                      juce::SynthesiserSound* sound, int)
{
    auto* s = dynamic_cast<ComanacheSamplerSound*>(sound);
    if (s == nullptr) return;

    // Pitch ratio: distance in semitones from C3 (MIDI 60), converted to playback speed
    const int semitones = midiNoteNumber - 60;
    pitchRatio = std::pow(2.0, semitones / 12.0)
               * s->getSourceSampleRate() / getSampleRate();

    sourceSamplePosition = 0.0;
    level  = velocity;
    inRelease = false;
    releaseSamplesTotal = kReleaseSeconds * (float)getSampleRate();
    releaseSamplesLeft  = 0.0f;
}

void ComanacheSamplerVoice::stopNote(float, bool allowTailOff)
{
    if (allowTailOff && !inRelease)
    {
        inRelease          = true;
        releaseSamplesLeft = releaseSamplesTotal;
    }
    else
    {
        clearCurrentNote();
        sourceSamplePosition = 0.0;
        inRelease = false;
    }
}

void ComanacheSamplerVoice::renderNextBlock(juce::AudioBuffer<float>& output,
                                            int startSample, int numSamples)
{
    auto* sound = dynamic_cast<ComanacheSamplerSound*>(getCurrentlyPlayingSound().get());
    if (sound == nullptr) return;

    const auto& src      = sound->getBuffer();
    const int   srcLen   = src.getNumSamples();
    const int   srcCh    = src.getNumChannels();
    const int   outCh    = output.getNumChannels();

    for (int i = 0; i < numSamples; ++i)
    {
        if (sourceSamplePosition >= srcLen)
        {
            clearCurrentNote();
            break;
        }

        // Release envelope multiplier
        float env = level;
        if (inRelease)
        {
            if (releaseSamplesLeft <= 0.0f)
            {
                clearCurrentNote();
                break;
            }
            env *= releaseSamplesLeft / releaseSamplesTotal;
            releaseSamplesLeft -= 1.0f;
        }

        // Linear interpolation for pitch-shifted playback
        const int   pos  = (int)sourceSamplePosition;
        const float frac = (float)(sourceSamplePosition - pos);

        for (int ch = 0; ch < outCh; ++ch)
        {
            const int sCh = ch % srcCh;
            const float s0 = src.getSample(sCh, pos);
            const float s1 = (pos + 1 < srcLen) ? src.getSample(sCh, pos + 1) : 0.0f;
            output.addSample(ch, startSample + i, (s0 + frac * (s1 - s0)) * env);
        }

        sourceSamplePosition += pitchRatio;
    }
}
