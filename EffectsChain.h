#pragma once
#include <vector>
#include <cmath>
#include <algorithm>
#include <cstring>

// ─── State-variable filter (Simper TPT, 2nd order Butterworth) ───────────────
// Safe to modulate: setCutoff() only updates coefficients and keeps the
// integrator state, so moving the cutoff never resets the filter (no clicks).
struct SvfFilter
{
    enum class Mode { LP, HP };
    Mode  mode{Mode::LP};
    float a1{1}, a2{0}, a3{0};
    float ic1{0}, ic2{0};
    static constexpr float k = 1.41421356f;   // 1/Q, Q = 0.707

    void setCutoff(double sr, float freq) noexcept {
        freq = std::clamp(freq, 10.0f, (float)(sr * 0.49));
        const float g = (float)std::tan(3.14159265358979 * freq / sr);
        a1 = 1.0f / (1.0f + g*(g + k));
        a2 = g * a1;
        a3 = g * a2;
    }
    float process(float v0) noexcept {
        const float v3 = v0 - ic2;
        const float v1 = a1*ic1 + a2*v3;
        const float v2 = ic2 + a2*ic1 + a3*v3;
        ic1 = 2.0f*v1 - ic1;
        ic2 = 2.0f*v2 - ic2;
        return mode == Mode::LP ? v2 : v0 - k*v1 - v2;
    }
    void reset() noexcept { ic1 = ic2 = 0.0f; }
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

    SvfFilter dlcL, dlcR, dhcL, dhcR;

    // Bitcrush on delay return (SR reduction at 2x delayHighcut)
    SvfFilter bcLpL, bcLpR;       // LP to clean up bitcrush aliases (same freq as dhc)
    float bcHoldL{0}, bcHoldR{0};
    float bcCountL{0}, bcCountR{0};
    // Delay return pitch vibrato (macro-driven)
    float vibratoPhase{0};

    // Chorus
    std::vector<float> choBufL, choBufR;
    int choPos{0}, choSize{0};
    float choPhase{0.0f};

    // Tape HF rolloff (progressive LP as dist increases)
    SvfFilter tapeHfL, tapeHfR;

    // Output filters
    SvfFilter hpL, hpR, lpL, lpR;

    // Cutoffs are smoothed in log-frequency every kFilterSub samples (not once per
    // host block), so sweeps are continuous whatever the buffer size.
    static constexpr int kFilterSub = 16;
    float filterSmoothCoeff{0.0f};
    static float smoothFreq(float cur, float target, float a) noexcept;

    // Parameter smoothers — prevent zipper noise on rapid knob changes
    float mSmoothDistAmount{0.0f};
    float mSmoothHpFreq{20.0f};
    float mSmoothLpFreq{20000.0f};
    float mSmoothDlcFreq{20.0f};
    float mSmoothDhcFreq{18000.0f};
    float mSmoothMacroMod{0.0f};

    float delayTimeSamples() const;
    float readBuf(const std::vector<float>& b, int wp, float delay) const;

    static float distClip(float x, float a) noexcept;
    static float distTape(float x, float a) noexcept;
    static float distTube(float x, float a) noexcept;
};
