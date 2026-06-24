#pragma once
#include <vector>
#include <cmath>
#include <algorithm>
#include <cstring>

// ─── Biquad (Butterworth 2nd order, bilinear transform) ──────────────────────
struct Biquad
{
    float b0{1}, b1{0}, b2{0}, a1{0}, a2{0};
    float x1{0}, x2{0}, y1{0}, y2{0};

    float process(float x) noexcept {
        float y = b0*x + b1*x1 + b2*x2 - a1*y1 - a2*y2;
        x2=x1; x1=x; y2=y1; y1=y;
        return y;
    }
    void reset() noexcept { x1=x2=y1=y2=0.0f; }
    static Biquad makeHP(double sr, float freq);
    static Biquad makeLP(double sr, float freq);
};

// ─── Freeverb-style plate reverb ─────────────────────────────────────────────
class ComanacheReverb
{
public:
    void prepare(double sampleRate);
    void process(float* L, float* R, int numSamples, float amount, float decaySec);

private:
    struct CombFilter {
        std::vector<float> buf;
        int pos{0};
        float lastOut{0.0f};
        void resize(int n) { buf.assign(n, 0.0f); pos=0; lastOut=0.0f; }
        float process(float in, float fb, float damp) noexcept {
            float out = buf[pos];
            lastOut = out*(1.0f-damp) + lastOut*damp;
            buf[pos] = in + lastOut*fb;
            if (++pos >= (int)buf.size()) pos = 0;
            return out;
        }
    };
    struct AllpassFilter {
        std::vector<float> buf;
        int pos{0};
        void resize(int n) { buf.assign(n, 0.0f); pos=0; }
        float process(float in) noexcept {
            float out = buf[pos];
            buf[pos] = in + out*0.5f;
            if (++pos >= (int)buf.size()) pos = 0;
            return out - in;
        }
    };

    static constexpr int kN = 8, kA = 4;
    CombFilter   combL[kN], combR[kN];
    AllpassFilter apL[kA], apR[kA];
};

// ─── Effects parameter snapshot ──────────────────────────────────────────────
struct EffectsParameters {
    float  reverbAmount  { 0.0f };
    float  reverbDecay   { 2.0f };
    float  distAmount    { 0.0f };
    int    distMode      { 0 };
    int    delaySyncMode { 0 };
    float  delayTimeMs   { 500.0f };
    float  delayFeedback { 0.35f };
    float  delayMix      { 0.5f };
    float  delayLowcut   { 20.0f };
    float  delayHighcut  { 18000.0f };
    float  chorusAmount  { 0.0f };
    float  hpFreq        { 20.0f };
    float  lpFreq        { 20000.0f };
    float  outputVol     { 1.0f };
    float  macroModAmount{ 0.0f };
    double bpm           { 120.0 };
};

// ─── Full chain: Reverb → Dist → Delay → Chorus → HP/LP ─────────────────────
class EffectsChain
{
public:
    void prepare(double sampleRate, int maxBlockSize);
    void setParameters(const EffectsParameters& p);
    void process(float* outL, float* outR, int numSamples);

private:
    EffectsParameters params;
    double sr { 44100.0 };

    ComanacheReverb reverb;

    // Ping-pong delay
    static constexpr int   kMaxDelaySec = 3;
    static constexpr float kPingPongMs  = 15.0f;
    std::vector<float> delBufL, delBufR;
    int delPos{0}, delSize{0};

    Biquad dlcL, dlcR, dhcL, dhcR;
    float lastDlcFreq{-1}, lastDhcFreq{-1};

    // Bitcrush on delay return (SR reduction at 2x delayHighcut)
    Biquad bcLpL, bcLpR;          // LP to clean up bitcrush aliases (same freq as dhc)
    float bcHoldL{0}, bcHoldR{0};
    float bcCountL{0}, bcCountR{0};
    // Delay return pitch vibrato (macro-driven)
    float vibratoPhase{0};

    // Chorus
    std::vector<float> choBufL, choBufR;
    int choPos{0}, choSize{0};
    float choPhase{0.0f};

    // Tape HF rolloff (progressive LP as dist increases)
    Biquad tapeHfL, tapeHfR;
    float lastTapeHfFreq{-1};

    // Output filters
    Biquad hpL, hpR, lpL, lpR;
    float lastHpFreq{-1}, lastLpFreq{-1};

    float delayTimeSamples() const;
    float readBuf(const std::vector<float>& b, int wp, float delay) const;

    static float distClip(float x, float a) noexcept;
    static float distTape(float x, float a) noexcept;
    static float distTube(float x, float a) noexcept;
};
