#pragma once
#include <JuceHeader.h>
#include "SamplerVoice.h"

// Wraps juce::Synthesiser with 8 voices and convenience methods for loading sounds
class ComanacheSamplerInstrument : public juce::Synthesiser
{
public:
    ComanacheSamplerInstrument() = default;

    // Replace the active sound with a new one decoded from the given buffer
    void loadSound(const juce::AudioBuffer<float>& buffer, double sourceSampleRate);

    void clearSound();
};
