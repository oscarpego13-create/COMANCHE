#pragma once
#include <JuceHeader.h>

struct EffectsParameters
{
    float  reverbAmount  { 0.0f };
    float  distAmount    { 0.0f };
    int    distMode      { 0 };        // 0=CLIP 1=TAPE 2=TUBE
    int    delaySyncMode { 0 };        // 0=FREE 1=1/4 2=1/8 3=1/16 4=1/4T 5=1/8T
    float  delayTimeMs   { 500.0f };
    float  delayLowcut   { 20.0f };
    float  delayHighcut  { 18000.0f };
    float  chorusAmount  { 0.0f };
    float  hpFreq        { 20.0f };
    float  lpFreq        { 20000.0f };
    double bpm           { 120.0 };
};

// Simple 2nd-order biquad usable on the audio thread with no allocations
struct Biquad
{
    float b0{1}, b1{0}, b2{0}, a1{0}, a2{0};
    float x1{0}, x2{0}, y1{0}, y2{0};

    float process(float x) noexcept
    {
        float y = b0*x + b1*x1 + b2*x2 - a1*y1 - a2*y2;
        x2=x1; x1=x; y2=y1; y1=y;
        return y;
    }
    void reset() noexcept { x1=x2=y1=y2=0.0f; }

    // 2nd-order Butterworth high-pass (12 dB/oct)
    static Biquad makeHP(double sr, float freq);
    // 2nd-order Butterworth low-pass (12 dB/oct)
    static Biquad makeLP(double sr, float freq);
};

class EffectsChain
{
public:
    void prepare(double sampleRate, int samplesPerBlock);
    void process(juce::AudioBuffer<float>& buffer);
    void setParameters(const EffectsParameters& p);

private:
    EffectsParameters params;
    double sr { 44100.0 };

    //-- Reverb
    juce::Reverb reverb;

    //-- Delay
    static constexpr int kMaxDelaySeconds = 3;
    static constexpr float kFeedback      = 0.35f;
    static constexpr float kPingPongMs    = 15.0f;

    std::vector<float> delayBufL, delayBufR;
    int delayWritePos  { 0 };
    int delayBufSize   { 0 };

    // Feedback filters (one per channel)
    Biquad dlcL, dlcR;   // lowcut
    Biquad dhcL, dhcR;   // highcut

    float lastDlcFreq { -1 }, lastDhcFreq { -1 };

    //-- Chorus (Juno-60 style)
    static constexpr int kChorusBufSeconds = 1;
    std::vector<float> chorusBufL, chorusBufR;
    int  chorusWritePos { 0 };
    int  chorusBufSize  { 0 };
    float chorusPhase   { 0.0f };

    //-- Output filters
    Biquad hpL, hpR, lpL, lpR;
    float lastHpFreq { -1 }, lastLpFreq { -1 };

    //-- Helpers
    float getDelayTimeSamples() const;
    float readBuf(const std::vector<float>& buf, int writePos, float delaySamples) const;

    void processReverb(juce::AudioBuffer<float>& buffer);
    void processDistortion(juce::AudioBuffer<float>& buffer);
    void processDelay(juce::AudioBuffer<float>& buffer);
    void processChorus(juce::AudioBuffer<float>& buffer);
    void processOutputFilters(juce::AudioBuffer<float>& buffer);

    static float distClip(float x, float amount) noexcept;
    static float distTape(float x, float amount) noexcept;
    static float distTube(float x, float amount) noexcept;
};
