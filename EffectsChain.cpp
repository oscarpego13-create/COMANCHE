#include "EffectsChain.h"

static constexpr float kTwoPiF = 6.28318530718f;
static constexpr double kTwoPi = 6.28318530717958647;

// ─── Biquad ──────────────────────────────────────────────────────────────────

Biquad Biquad::makeHP(double sr, float freq)
{
    freq = std::clamp(freq, 10.0f, (float)(sr * 0.49));
    double w0    = kTwoPi * freq / sr;
    double cos0  = std::cos(w0), sin0 = std::sin(w0);
    double alpha = sin0 / std::sqrt(2.0);
    double inv   = 1.0 / (1.0 + alpha);
    Biquad f;
    f.b0 = (float)(( (1.0+cos0)*0.5) * inv);
    f.b1 = (float)(( -(1.0+cos0))    * inv);
    f.b2 = f.b0;
    f.a1 = (float)((-2.0*cos0)       * inv);
    f.a2 = (float)((1.0-alpha)       * inv);
    return f;
}

Biquad Biquad::makeLP(double sr, float freq)
{
    freq = std::clamp(freq, 10.0f, (float)(sr * 0.49));
    double w0    = kTwoPi * freq / sr;
    double cos0  = std::cos(w0), sin0 = std::sin(w0);
    double alpha = sin0 / std::sqrt(2.0);
    double inv   = 1.0 / (1.0 + alpha);
    Biquad f;
    f.b0 = (float)(((1.0-cos0)*0.5) * inv);
    f.b1 = (float)(( (1.0-cos0))    * inv);
    f.b2 = f.b0;
    f.a1 = (float)((-2.0*cos0)      * inv);
    f.a2 = (float)((1.0-alpha)      * inv);
    return f;
}

// ─── ComanacheReverb (Freeverb) ──────────────────────────────────────────────

void ComanacheReverb::prepare(double sampleRate)
{
    const float sc = (float)(sampleRate / 44100.0);
    const int cL[kN] = {1116,1188,1277,1356,1422,1491,1557,1617};
    const int cR[kN] = {1139,1211,1300,1379,1445,1514,1580,1640};
    const int aL[kA] = {556,441,341,225};
    const int aR[kA] = {579,464,364,248};
    for (int i=0;i<kN;i++){combL[i].resize((int)(cL[i]*sc));combR[i].resize((int)(cR[i]*sc));}
    for (int i=0;i<kA;i++){apL[i].resize((int)(aL[i]*sc));  apR[i].resize((int)(aR[i]*sc));}
}

void ComanacheReverb::process(float* L, float* R, int n, float amount, float decaySec)
{
    if (amount <= 0.0f) return;
    // RT60 from requested decay (longest comb = 1617/44100 s at any SR)
    static constexpr float kLongCombSec = 1617.0f / 44100.0f;
    float logFb = -3.0f * kLongCombSec / std::max(0.05f, decaySec);
    const float fb   = std::pow(10.0f, logFb);
    const float damp = 0.30f;
    const float wet  = amount;
    const float dry  = 1.0f - amount;

    for (int i = 0; i < n; i++) {
        float inL = L[i], inR = R[i];
        float input = (inL + inR) * 0.015f;
        float oL=0, oR=0;
        for (int j=0;j<kN;j++) { oL+=combL[j].process(input,fb,damp); oR+=combR[j].process(input,fb,damp); }
        for (int j=0;j<kA;j++) { oL=apL[j].process(oL); oR=apR[j].process(oR); }
        L[i] = inL*dry + oL*wet;
        R[i] = inR*dry + oR*wet;
    }
}

// ─── EffectsChain ────────────────────────────────────────────────────────────

void EffectsChain::prepare(double sampleRate, int)
{
    sr = sampleRate;
    reverb.prepare(sr);

    delSize = (int)(sr * kMaxDelaySec) + 4096;
    delBufL.assign(delSize, 0.0f);
    delBufR.assign(delSize, 0.0f);
    delPos = 0;

    choSize = (int)(sr * 1);
    choBufL.assign(choSize, 0.0f);
    choBufR.assign(choSize, 0.0f);
    choPos = 0; choPhase = 0.0f;

    dlcL.reset(); dlcR.reset(); dhcL.reset(); dhcR.reset();
    hpL.reset();  hpR.reset();  lpL.reset();  lpR.reset();
    bcLpL.reset(); bcLpR.reset();
    bcHoldL=bcHoldR=bcCountL=bcCountR=0.0f;
    vibratoPhase=0.0f;
    lastDlcFreq=lastDhcFreq=lastHpFreq=lastLpFreq=-1;
}

void EffectsChain::setParameters(const EffectsParameters& p) { params = p; }

float EffectsChain::readBuf(const std::vector<float>& b, int wp, float delay) const
{
    const int sz = (int)b.size();
    float rp = (float)wp - delay;
    while (rp < 0) rp += sz;
    int i0 = (int)rp % sz, i1 = (i0+1) % sz;
    float f = rp - (int)rp;
    return b[i0] + f*(b[i1]-b[i0]);
}

float EffectsChain::delayTimeSamples() const
{
    if (params.delaySyncMode == 0) return params.delayTimeMs * 0.001f * (float)sr;
    // Sync on: delayTimeMs (1..2000) is divided into 5 equal bands → note divisions
    double bps = params.bpm / 60.0;
    int step = std::clamp((int)(params.delayTimeMs / 400.0f), 0, 4);
    double sec;
    switch (step) {
        case 0: sec = 1.0/bps;         break; // 1/4
        case 1: sec = 0.5/bps;         break; // 1/8
        case 2: sec = 0.25/bps;        break; // 1/16
        case 3: sec = (2.0/3.0)/bps;   break; // 1/4T
        case 4: sec = (1.0/3.0)/bps;   break; // 1/8T
        default: sec = 0.5/bps;        break;
    }
    return (float)(sec * sr);
}

float EffectsChain::distClip(float x, float a) noexcept { return std::tanh((1.0f+a*22.0f)*x); }
float EffectsChain::distTape(float x, float a) noexcept { float d=(1.0f+a*12.0f)*x; return d/(1.0f+std::abs(d)); }
float EffectsChain::distTube(float x, float a) noexcept {
    float d=(1.0f+a*16.0f)*x;
    return d>=0.0f ? std::tanh(d) : d/(1.0f+0.5f*std::abs(d));
}

void EffectsChain::process(float* outL, float* outR, int n)
{
    if (n == 0) return;

    // 1. Reverb
    reverb.process(outL, outR, n, params.reverbAmount, params.reverbDecay);

    // 2. Distortion + gain compensation + tape HF rolloff
    if (params.distAmount > 0.0f) {
        const float comp = 1.0f / (1.0f + params.distAmount * 4.0f);
        for (int i=0;i<n;i++) {
            float dL, dR;
            switch (params.distMode) {
                case 1: dL=distTape(outL[i],params.distAmount); dR=distTape(outR[i],params.distAmount); break;
                case 2: dL=distTube(outL[i],params.distAmount); dR=distTube(outR[i],params.distAmount); break;
                default:dL=distClip(outL[i],params.distAmount); dR=distClip(outR[i],params.distAmount); break;
            }
            outL[i] = dL * comp;
            outR[i] = dR * comp;
        }
        // TAPE: progressive HF loss (cutoff falls from 20kHz→3kHz as dist increases)
        if (params.distMode == 1) {
            float fc = 20000.0f * std::pow(0.15f, params.distAmount);
            fc = std::max(fc, 3000.0f);
            if (std::abs(fc - lastTapeHfFreq) > 20.0f) {
                tapeHfL = tapeHfR = Biquad::makeLP(sr, fc);
                lastTapeHfFreq = fc;
            }
            for (int i=0;i<n;i++) {
                outL[i] = tapeHfL.process(outL[i]);
                outR[i] = tapeHfR.process(outR[i]);
            }
        }
    }

    // 3. Ping-pong delay
    {
        const float dtL = delayTimeSamples();
        const float dtR = dtL + kPingPongMs * 0.001f * (float)sr;

        if (params.delayLowcut != lastDlcFreq) {
            dlcL=dlcR=Biquad::makeHP(sr, params.delayLowcut); lastDlcFreq=params.delayLowcut;
        }
        if (params.delayHighcut != lastDhcFreq) {
            dhcL=dhcR=Biquad::makeLP(sr, params.delayHighcut); lastDhcFreq=params.delayHighcut;
            bcLpL=bcLpR=Biquad::makeLP(sr, params.delayHighcut); // bitcrush output LP tracks same freq
        }

        // Bitcrush: sample-rate reduction at 2x delayHighcut
        const float bcSR   = std::max(4000.0f, 2.0f * params.delayHighcut);
        const float bcHold = std::max(1.0f, (float)sr / bcSR);
        // Pitch vibrato on delay return (macro-driven, ≈0.28Hz, max ±0.3ms — subtle)
        const float vibDepth = params.macroModAmount * 0.0003f * (float)sr;
        const float vibInc   = kTwoPiF * 0.28f / (float)sr;

        const float fb = std::clamp(params.delayFeedback, 0.0f, 0.97f);
        for (int i=0;i<n;i++) {
            // Vibrato: slowly wobble read position
            float vib = vibDepth * std::sin(vibratoPhase);
            vibratoPhase += vibInc;
            if (vibratoPhase > kTwoPiF) vibratoPhase -= kTwoPiF;

            float wL = readBuf(delBufL, delPos, std::max(1.0f, dtL + vib));
            float wR = readBuf(delBufR, delPos, std::max(1.0f, dtR + vib));

            // Bitcrush: hold sample for bcHold samples (sample-rate reduction)
            bcCountL += 1.0f;
            if (bcCountL >= bcHold) { bcHoldL = wL; bcCountL = 0.0f; }
            bcCountR += 1.0f;
            if (bcCountR >= bcHold) { bcHoldR = wR; bcCountR = 0.0f; }

            // Output: bitcrushed → LP filter cleans up aliasing
            float outWL = bcLpL.process(bcHoldL);
            float outWR = bcLpR.process(bcHoldR);

            // Feedback: bitcrushed raw through existing LP(dhc) + HP(dlc)
            float fbL = dlcL.process(dhcL.process(bcHoldR * fb));
            float fbR = dlcR.process(dhcR.process(bcHoldL * fb));
            delBufL[delPos] = outL[i] + fbL;
            delBufR[delPos] = outR[i] + fbR;
            outL[i] += outWL * params.delayMix;
            outR[i] += outWR * params.delayMix;
            if (++delPos >= delSize) delPos = 0;
        }
    }

    // 4. Chorus
    if (params.chorusAmount > 0.0f) {
        const float center = 5.0f*0.001f*(float)sr;
        const float depth  = 2.0f*0.001f*(float)sr;
        const float inc    = kTwoPiF * 0.5f / (float)sr;
        for (int i=0;i<n;i++) {
            choBufL[choPos]=outL[i]; choBufR[choPos]=outR[i];
            float mL = center + depth*std::sin(choPhase);
            float mR = center + depth*std::sin(choPhase + (float)M_PI);
            float wL = readBuf(choBufL, choPos, mL);
            float wR = readBuf(choBufR, choPos, mR);
            outL[i] = outL[i]*(1.0f-params.chorusAmount) + wL*params.chorusAmount;
            outR[i] = outR[i]*(1.0f-params.chorusAmount) + wR*params.chorusAmount;
            if (++choPos >= choSize) choPos = 0;
            choPhase += inc;
            if (choPhase > kTwoPiF) choPhase -= kTwoPiF;
        }
    }

    // 5. Output HP + LP filters
    if (params.hpFreq != lastHpFreq) { hpL=hpR=Biquad::makeHP(sr, params.hpFreq); lastHpFreq=params.hpFreq; }
    if (params.lpFreq != lastLpFreq) { lpL=lpR=Biquad::makeLP(sr, params.lpFreq); lastLpFreq=params.lpFreq; }
    const float vol = params.outputVol;
    for (int i=0;i<n;i++) {
        outL[i] = lpL.process(hpL.process(outL[i])) * vol;
        outR[i] = lpR.process(hpR.process(outR[i])) * vol;
    }
}
