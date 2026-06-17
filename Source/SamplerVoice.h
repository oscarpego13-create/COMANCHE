#pragma once
#include <JuceHeader.h>

// Holds the decoded audio data and source sample rate for one loaded sample
class ComanacheSamplerSound : public juce::SynthesiserSound
{
public:
    ComanacheSamplerSound(const juce::AudioBuffer<float>& buffer, double sourceSampleRate);

    bool appliesToNote(int) override    { return true; }
    bool appliesToChannel(int) override { return true; }

    const juce::AudioBuffer<float>& getBuffer() const  { return data; }
    double getSourceSampleRate() const                  { return sourceSampleRate; }

private:
    juce::AudioBuffer<float> data;
    double sourceSampleRate;
};

// One polyphonic voice: resamples the loaded AudioBuffer to produce pitched output
class ComanacheSamplerVoice : public juce::SynthesiserVoice
{
public:
    ComanacheSamplerVoice() = default;

    bool canPlaySound(juce::SynthesiserSound* sound) override;

    void startNote(int midiNoteNumber, float velocity,
                   juce::SynthesiserSound* sound, int pitchWheelPosition) override;

    void stopNote(float velocity, bool allowTailOff) override;

    void pitchWheelMoved(int) override {}
    void controllerMoved(int, int) override {}

    void renderNextBlock(juce::AudioBuffer<float>& outputBuffer,
                         int startSample, int numSamples) override;

private:
    double sourceSamplePosition { 0.0 };
    double pitchRatio           { 1.0 };
    float  level                { 0.0f };

    // Release envelope
    bool  inRelease            { false };
    float releaseSamplesTotal  { 0.0f };
    float releaseSamplesLeft   { 0.0f };

    static constexpr float kReleaseSeconds { 0.08f };
};
