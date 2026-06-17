#include "SamplerInstrument.h"

void ComanacheSamplerInstrument::loadSound(const juce::AudioBuffer<float>& buffer,
                                           double sourceSampleRate)
{
    clearSounds();
    addSound(new ComanacheSamplerSound(buffer, sourceSampleRate));
}

void ComanacheSamplerInstrument::clearSound()
{
    clearSounds();
}
