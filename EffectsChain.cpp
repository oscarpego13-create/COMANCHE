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

    for (SvfFilter* f : {&dlcL, &dlcR, &hpL, &hpR}) { f->mode = SvfFilter::Mode::HP; f->reset(); }
    for (SvfFilter* f : {&dhcL, &dhcR, &bcLpL, &bcLpR, &tapeHfL, &tapeHfR, &lpL, &lpR}) { f->mode = SvfFilter::Mode::LP; f->reset(); }
    bcHoldL=bcHoldR=bcCountL=bcCountR=0.0f;
    vibratoPhase=0.0f;
    filterSmoothCoeff = 1.0f - std::exp(-(float)kFilterSub / (float)(sr * 0.020)); // 20ms glide
    // Start at the current values so a reset doesn't sweep from the defaults
    mSmoothDistAmount=params.distAmount; mSmoothMacroMod=params.macroModAmount;
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
        mSmoothDistAmount += alpha * (params.distAmount    - mSmoothDistAmount);
        mSmoothMacroMod   += alpha * (params.macroModAmount - mSmoothMacroMod);
    }

    // 1. Reverb
    reverb.process(outL, outR, n, params.reverbAmount, params.reverbDecay);

    // 2. Distortion + gain compensation + tape HF rolloff
    if (mSmoothDistAmount > 0.0001f) {
        const float comp = 1.0f / (1.0f + mSmoothDistAmount * 4.0f);
        for (int i=0;i<n;i++) {
            float dL, dR;
            switch (params.distMode) {
                case 1: dL=distTape(outL[i],mSmoothDistAmount); dR=distTape(outR[i],mSmoothDistAmount); break;
                case 2: dL=distTube(outL[i],mSmoothDistAmount); dR=distTube(outR[i],mSmoothDistAmount); break;
                default:dL=distClip(outL[i],mSmoothDistAmount); dR=distClip(outR[i],mSmoothDistAmount); break;
            }
            outL[i] = dL * comp;
            outR[i] = dR * comp;
        }
        // TAPE: progressive HF loss (cutoff falls from 20kHz→3kHz as dist increases)
        if (params.distMode == 1) {
            float fc = 20000.0f * std::pow(0.15f, mSmoothDistAmount);
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

        const float fb = std::clamp(params.delayFeedback, 0.0f, 0.97f);
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

    // 5. Output HP + LP filters (cutoffs glide per sub-block)
    const float vol = params.outputVol;
    for (int c0 = 0; c0 < n; c0 += kFilterSub) {
        const int c1 = std::min(n, c0 + kFilterSub);
        mSmoothHpFreq = smoothFreq(mSmoothHpFreq, params.hpFreq, filterSmoothCoeff);
        mSmoothLpFreq = smoothFreq(mSmoothLpFreq, params.lpFreq, filterSmoothCoeff);
        hpL.setCutoff(sr, mSmoothHpFreq); hpR.setCutoff(sr, mSmoothHpFreq);
        lpL.setCutoff(sr, mSmoothLpFreq); lpR.setCutoff(sr, mSmoothLpFreq);
        for (int i=c0;i<c1;i++) {
            outL[i] = lpL.process(hpL.process(outL[i])) * vol;
            outR[i] = lpR.process(hpR.process(outR[i])) * vol;
        }
    }
}
