#include "EffectsChain.h"

static constexpr float kTwoPiF = 6.28318530718f;

// ─── Filter smoothing ────────────────────────────────────────────────────────

float EffectsChain::smoothFreq(float cur, float target, float a) noexcept
{
    // One-pole in log2 space: equal-sounding glide across the whole range
    const float lc = std::log2(std::max(cur, 1.0f)), lt = std::log2(std::max(target, 1.0f));
    return std::exp2(lc + a * (lt - lc));
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

void ComanacheReverb::process(float* L, float* R, int n, float& amount, float target, float glide, float decaySec, bool keepRunning)
{
    if (target <= 0.0f && amount < 0.0001f && !keepRunning) { amount = 0.0f; return; }
    // RT60 from requested decay (longest comb = 1617/44100 s at any SR)
    static constexpr float kLongCombSec = 1617.0f / 44100.0f;
    float logFb = -3.0f * kLongCombSec / std::max(0.05f, decaySec);
    const float fb   = std::pow(10.0f, logFb);
    const float damp = 0.30f;

    for (int i = 0; i < n; i++) {
        amount += glide * (target - amount);
        const float wet = amount, dry = 1.0f - amount;
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

    for (SvfFilter* f : {&dlcL, &dlcR, &hpL, &hpR}) { f->mode = SvfFilter::Mode::HP; f->reset(); }
    for (SvfFilter* f : {&dhcL, &dhcR, &bcLpL, &bcLpR, &tapeHfL, &tapeHfR, &lpL, &lpR}) { f->mode = SvfFilter::Mode::LP; f->reset(); }
    bcHoldL=bcHoldR=bcCountL=bcCountR=0.0f;
    vibratoPhase=0.0f;
    filterSmoothCoeff = 1.0f - std::exp(-(float)kFilterSub / (float)(sr * 0.020)); // 20ms glide
    // Start at the current values so a reset doesn't sweep from the defaults
    mSmoothDistAmount=params.distAmount; mSmoothMacroMod=params.macroModAmount;
    mSmoothVol=params.outputVol;
    mSmoothReverb=params.reverbAmount; mSmoothDelayMix=params.delayMix;
    mSmoothFeedback=params.delayFeedback; mSmoothChorus=params.chorusAmount;
    mixGlide = 1.0f - std::exp(-1.0f / (float)(sr * 0.010));              // 10 ms per-sample glide
    distGlide = 1.0f - std::exp(-(float)kFilterSub / (float)(sr * 0.030));
    volSmoothCoeff = 1.0f - std::exp(-1.0f / (float)(sr * 0.015));   // 15 ms per-sample glide
    mSmoothHpFreq=params.hpFreq; mSmoothLpFreq=params.lpFreq;
    mSmoothDlcFreq=params.delayLowcut; mSmoothDhcFreq=params.delayHighcut;
    hpL.setCutoff(sr, mSmoothHpFreq);   hpR.setCutoff(sr, mSmoothHpFreq);
    lpL.setCutoff(sr, mSmoothLpFreq);   lpR.setCutoff(sr, mSmoothLpFreq);
    dlcL.setCutoff(sr, mSmoothDlcFreq); dlcR.setCutoff(sr, mSmoothDlcFreq);
    dhcL.setCutoff(sr, mSmoothDhcFreq); dhcR.setCutoff(sr, mSmoothDhcFreq);
    bcLpL.setCutoff(sr, mSmoothDhcFreq); bcLpR.setCutoff(sr, mSmoothDhcFreq);
    tapeHfL.setCutoff(sr, 20000.0f);    tapeHfR.setCutoff(sr, 20000.0f);
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

// Blending dry and delayed copies loses the most level half way, and none at either end of the knob.
// Smooth curve through 0, +2, +3, +2, 0 dB at 0, 25, 50, 75, 100 %, returned as a linear gain.
static inline float chorusMakeup(float amount) noexcept
{
    const float u = amount - 0.5f, u2 = u * u;
    const float dB = 3.0f - (52.0f / 3.0f) * u2 + (64.0f / 3.0f) * u2 * u2;
    return std::exp(dB * 0.115129255f);   // ln(10) / 20
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

    // Smooth params to prevent zipper noise / sampling artifacts on rapid changes (~30ms tau).
    // Filter cutoffs are smoothed per sub-block inside their loops instead.
    {
        const float blockDur = (float)n / (float)sr;
        const float alpha = 1.0f - std::exp(-blockDur / 0.030f);
        mSmoothMacroMod   += alpha * (params.macroModAmount - mSmoothMacroMod);
    }

    // 1. Reverb
    reverb.process(outL, outR, n, mSmoothReverb, params.reverbAmount, mixGlide, params.reverbDecay, params.keepRunning);

    // 2. Distortion + gain compensation + tape HF rolloff
    if (mSmoothDistAmount > 0.0001f || params.distAmount > 0.0001f) {
        // Knob response: the drive follows amount^2.2 and the saturated signal is blended in over the
        // first 35 % of the knob, so low settings stay clean and the grit shows up around 30-40 %.
        // At 100 % it is the same full drive as before. Updated per sub-block: the MACRO can move it fast.
        float amt = 0.0f;
        for (int c0 = 0; c0 < n; c0 += kFilterSub) {
            const int c1 = std::min(n, c0 + kFilterSub);
            mSmoothDistAmount += distGlide * (params.distAmount - mSmoothDistAmount);
            amt = std::pow(mSmoothDistAmount, 2.2f);
            const float s     = std::min(1.0f, mSmoothDistAmount / 0.35f);
            const float wet   = s * s * (3.0f - 2.0f * s);
            const float comp  = 1.0f / (1.0f + amt * 4.0f);
            for (int i=c0;i<c1;i++) {
                float dL, dR;
                switch (params.distMode) {
                    case 1: dL=distTape(outL[i],amt); dR=distTape(outR[i],amt); break;
                    case 2: dL=distTube(outL[i],amt); dR=distTube(outR[i],amt); break;
                    default:dL=distClip(outL[i],amt); dR=distClip(outR[i],amt); break;
                }
                outL[i] += wet * (dL * comp - outL[i]);
                outR[i] += wet * (dR * comp - outR[i]);
            }
        }
        // TAPE: progressive HF loss (cutoff falls from 20kHz→3kHz as dist increases)
        if (params.distMode == 1) {
            float fc = 20000.0f * std::pow(0.15f, amt);
            fc = std::max(fc, 3000.0f);
            tapeHfL.setCutoff(sr, fc);
            tapeHfR.setCutoff(sr, fc);
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

        // Pitch vibrato on delay return (macro-driven, ≈0.28Hz — smoothed to prevent jumps)
        const float vibDepth = mSmoothMacroMod * 0.0003f * (float)sr;
        const float vibInc   = kTwoPiF * 0.28f / (float)sr;

        const float fbTarget = std::clamp(params.delayFeedback, 0.0f, 0.97f);
        for (int c0 = 0; c0 < n; c0 += kFilterSub) {
          const int c1 = std::min(n, c0 + kFilterSub);
          mSmoothDlcFreq = smoothFreq(mSmoothDlcFreq, params.delayLowcut,  filterSmoothCoeff);
          mSmoothDhcFreq = smoothFreq(mSmoothDhcFreq, params.delayHighcut, filterSmoothCoeff);
          dlcL.setCutoff(sr, mSmoothDlcFreq);  dlcR.setCutoff(sr, mSmoothDlcFreq);
          dhcL.setCutoff(sr, mSmoothDhcFreq);  dhcR.setCutoff(sr, mSmoothDhcFreq);
          bcLpL.setCutoff(sr, mSmoothDhcFreq); bcLpR.setCutoff(sr, mSmoothDhcFreq);

          // Bitcrush: sample-rate reduction at 2x delayHighcut
          const float bcSR   = std::max(4000.0f, 2.0f * mSmoothDhcFreq);
          const float bcHold = std::max(1.0f, (float)sr / bcSR);

          for (int i=c0;i<c1;i++) {
            mSmoothFeedback += mixGlide * (fbTarget - mSmoothFeedback);
            mSmoothDelayMix += mixGlide * (params.delayMix - mSmoothDelayMix);
            const float fb = mSmoothFeedback;
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
            outL[i] += outWL * mSmoothDelayMix;
            outR[i] += outWR * mSmoothDelayMix;
            if (++delPos >= delSize) delPos = 0;
          }
        }
    }

    // 4. Chorus
    if (params.chorusAmount > 0.0f || mSmoothChorus > 0.0001f || params.keepRunning) {
        const float center = 5.0f*0.001f*(float)sr;
        const float depth  = 2.0f*0.001f*(float)sr;
        const float inc    = kTwoPiF * 0.5f / (float)sr;
        for (int i=0;i<n;i++) {
            mSmoothChorus += mixGlide * (params.chorusAmount - mSmoothChorus);
            choBufL[choPos]=outL[i]; choBufR[choPos]=outR[i];
            float mL = center + depth*std::sin(choPhase);
            float mR = center + depth*std::sin(choPhase + (float)M_PI);
            float wL = readBuf(choBufL, choPos, mL);
            float wR = readBuf(choBufR, choPos, mR);
            const float makeup = chorusMakeup(mSmoothChorus);
            outL[i] = (outL[i]*(1.0f-mSmoothChorus) + wL*mSmoothChorus) * makeup;
            outR[i] = (outR[i]*(1.0f-mSmoothChorus) + wR*mSmoothChorus) * makeup;
            if (++choPos >= choSize) choPos = 0;
            choPhase += inc;
            if (choPhase > kTwoPiF) choPhase -= kTwoPiF;
        }
    }

    // 5. Output HP + LP filters (cutoffs glide per sub-block)
    // The volume glides per sample: a value held per block steps at every block edge, which
    // clicks when the fader is moved quickly.
    const float volTarget = params.outputVol;
    for (int c0 = 0; c0 < n; c0 += kFilterSub) {
        const int c1 = std::min(n, c0 + kFilterSub);
        mSmoothHpFreq = smoothFreq(mSmoothHpFreq, params.hpFreq, filterSmoothCoeff);
        mSmoothLpFreq = smoothFreq(mSmoothLpFreq, params.lpFreq, filterSmoothCoeff);
        hpL.setCutoff(sr, mSmoothHpFreq); hpR.setCutoff(sr, mSmoothHpFreq);
        lpL.setCutoff(sr, mSmoothLpFreq); lpR.setCutoff(sr, mSmoothLpFreq);
        for (int i=c0;i<c1;i++) {
            mSmoothVol += volSmoothCoeff * (volTarget - mSmoothVol);
            outL[i] = lpL.process(hpL.process(outL[i])) * mSmoothVol;
            outR[i] = lpR.process(hpR.process(outR[i])) * mSmoothVol;
        }
    }
}
