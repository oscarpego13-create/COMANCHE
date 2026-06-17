#include "EffectsChain.h"

//==============================================================================
// Biquad coefficient factories (bilinear-transform Butterworth)
//==============================================================================

Biquad Biquad::makeHP(double sr, float freq)
{
    freq = juce::jlimit(10.0f, (float)(sr * 0.49), freq);
    const double w0  = juce::MathConstants<double>::twoPi * freq / sr;
    const double cos0 = std::cos(w0);
    const double sin0 = std::sin(w0);
    const double alpha = sin0 / std::sqrt(2.0); // Q = 1/sqrt(2) for Butterworth

    const double a0inv = 1.0 / (1.0 + alpha);
    Biquad f;
    f.b0 = (float)(( (1.0 + cos0) / 2.0) * a0inv);
    f.b1 = (float)(( -(1.0 + cos0))      * a0inv);
    f.b2 = f.b0;
    f.a1 = (float)((-2.0 * cos0)          * a0inv);
    f.a2 = (float)((1.0 - alpha)          * a0inv);
    return f;
}

Biquad Biquad::makeLP(double sr, float freq)
{
    freq = juce::jlimit(10.0f, (float)(sr * 0.49), freq);
    const double w0   = juce::MathConstants<double>::twoPi * freq / sr;
    const double cos0 = std::cos(w0);
    const double sin0 = std::sin(w0);
    const double alpha = sin0 / std::sqrt(2.0);

    const double a0inv = 1.0 / (1.0 + alpha);
    Biquad f;
    f.b0 = (float)(((1.0 - cos0) / 2.0) * a0inv);
    f.b1 = (float)( (1.0 - cos0)        * a0inv);
    f.b2 = f.b0;
    f.a1 = (float)((-2.0 * cos0)        * a0inv);
    f.a2 = (float)((1.0 - alpha)        * a0inv);
    return f;
}

//==============================================================================
void EffectsChain::prepare(double sampleRate, int)
{
    sr = sampleRate;

    // Delay buffer: enough for 3 seconds + ping-pong offset
    delayBufSize = (int)(sr * kMaxDelaySeconds) + 4096;
    delayBufL.assign(delayBufSize, 0.0f);
    delayBufR.assign(delayBufSize, 0.0f);
    delayWritePos = 0;

    // Chorus buffer: 1 second
    chorusBufSize = (int)(sr * kChorusBufSeconds);
    chorusBufL.assign(chorusBufSize, 0.0f);
    chorusBufR.assign(chorusBufSize, 0.0f);
    chorusWritePos = 0;
    chorusPhase    = 0.0f;

    // Reset filters
    dlcL.reset(); dlcR.reset();
    dhcL.reset(); dhcR.reset();
    hpL.reset();  hpR.reset();
    lpL.reset();  lpR.reset();

    lastDlcFreq = -1; lastDhcFreq = -1;
    lastHpFreq  = -1; lastLpFreq  = -1;

    reverb.reset();
}

void EffectsChain::setParameters(const EffectsParameters& p)
{
    params = p;
}

void EffectsChain::process(juce::AudioBuffer<float>& buffer)
{
    if (buffer.getNumSamples() == 0) return;

    processReverb(buffer);
    processDistortion(buffer);
    processDelay(buffer);
    processChorus(buffer);
    processOutputFilters(buffer);
}

//==============================================================================
void EffectsChain::processReverb(juce::AudioBuffer<float>& buffer)
{
    if (params.reverbAmount <= 0.0f) return;

    juce::Reverb::Parameters rp;
    rp.roomSize   = 0.5f;
    rp.damping    = 0.8f;
    rp.width      = 1.0f;
    rp.wetLevel   = params.reverbAmount;
    rp.dryLevel   = 1.0f - params.reverbAmount;
    rp.freezeMode = 0.0f;
    reverb.setParameters(rp);

    if (buffer.getNumChannels() >= 2)
        reverb.processStereo(buffer.getWritePointer(0),
                             buffer.getWritePointer(1),
                             buffer.getNumSamples());
    else
        reverb.processMono(buffer.getWritePointer(0), buffer.getNumSamples());
}

//==============================================================================
float EffectsChain::distClip(float x, float amount) noexcept
{
    const float drive = 1.0f + amount * 9.0f;
    return std::tanh(drive * x);
}

float EffectsChain::distTape(float x, float amount) noexcept
{
    const float drive = 1.0f + amount * 3.0f;
    const float d = drive * x;
    return d / (1.0f + std::abs(d));
}

float EffectsChain::distTube(float x, float amount) noexcept
{
    const float drive = 1.0f + amount * 5.0f;
    const float d = drive * x;
    if (d >= 0.0f)
        return std::tanh(d);
    else
        return d / (1.0f + 0.5f * std::abs(d));
}

void EffectsChain::processDistortion(juce::AudioBuffer<float>& buffer)
{
    if (params.distAmount <= 0.0f) return;

    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        float* data = buffer.getWritePointer(ch);
        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            switch (params.distMode)
            {
                case 1:  data[i] = distTape(data[i], params.distAmount); break;
                case 2:  data[i] = distTube(data[i], params.distAmount); break;
                default: data[i] = distClip(data[i], params.distAmount); break;
            }
        }
    }
}

//==============================================================================
float EffectsChain::getDelayTimeSamples() const
{
    if (params.delaySyncMode == 0)
        return params.delayTimeMs * 0.001f * (float)sr;

    // Sync modes: calculate from BPM
    const double bps = params.bpm / 60.0;
    double seconds = 0.5 / bps; // default: 1/8
    switch (params.delaySyncMode)
    {
        case 1: seconds = 1.0  / bps;         break; // 1/4
        case 2: seconds = 0.5  / bps;         break; // 1/8
        case 3: seconds = 0.25 / bps;         break; // 1/16
        case 4: seconds = (2.0 / 3.0) / bps;  break; // 1/4T
        case 5: seconds = (1.0 / 3.0) / bps;  break; // 1/8T
        default: break;
    }
    return (float)(seconds * sr);
}

float EffectsChain::readBuf(const std::vector<float>& buf,
                            int writePos, float delaySamples) const
{
    const int bufSize = (int)buf.size();
    float readPosF = (float)writePos - delaySamples;
    while (readPosF < 0) readPosF += bufSize;
    const int   rp0  = (int)readPosF % bufSize;
    const int   rp1  = (rp0 + 1) % bufSize;
    const float frac = readPosF - (float)(int)readPosF;
    return buf[rp0] + frac * (buf[rp1] - buf[rp0]);
}

void EffectsChain::processDelay(juce::AudioBuffer<float>& buffer)
{
    const float delaySamples    = getDelayTimeSamples();
    const float pingPongSamples = kPingPongMs * 0.001f * (float)sr;

    // Rebuild feedback filters only when frequencies change
    if (params.delayLowcut != lastDlcFreq)
    {
        dlcL = dhcL = Biquad::makeHP(sr, params.delayLowcut);
        dlcR = dhcR = dlcL;
        lastDlcFreq = params.delayLowcut;
    }
    if (params.delayHighcut != lastDhcFreq)
    {
        dhcL = Biquad::makeLP(sr, params.delayHighcut);
        dhcR = dhcL;
        lastDhcFreq = params.delayHighcut;
    }

    const float delaySamplesL = delaySamples;
    const float delaySamplesR = delaySamples + pingPongSamples;

    const int numSamples = buffer.getNumSamples();
    float* outL = buffer.getWritePointer(0);
    float* outR = buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) : buffer.getWritePointer(0);

    for (int i = 0; i < numSamples; ++i)
    {
        // Read wet from delay lines
        float wetL = readBuf(delayBufL, delayWritePos, delaySamplesL);
        float wetR = readBuf(delayBufR, delayWritePos, delaySamplesR);

        // Filter feedback
        float fbL = dlcL.process(dhcL.process(wetR * kFeedback)); // crossed (ping-pong)
        float fbR = dlcR.process(dhcR.process(wetL * kFeedback));

        // Write to delay buffer: input + feedback (crossed)
        delayBufL[delayWritePos] = outL[i] + fbL;
        delayBufR[delayWritePos] = outR[i] + fbR;

        // Mix dry + wet
        outL[i] += wetL;
        outR[i] += wetR;

        delayWritePos = (delayWritePos + 1) % delayBufSize;
    }
}

//==============================================================================
void EffectsChain::processChorus(juce::AudioBuffer<float>& buffer)
{
    if (params.chorusAmount <= 0.0f) return;

    const float centerMs  = 5.0f;
    const float depthMs   = 2.0f;
    const float lfoRateHz = 0.5f;
    const float phaseInc  = juce::MathConstants<float>::twoPi * lfoRateHz / (float)sr;

    const float centerSamples = centerMs  * 0.001f * (float)sr;
    const float depthSamples  = depthMs   * 0.001f * (float)sr;

    float* outL = buffer.getWritePointer(0);
    float* outR = buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) : buffer.getWritePointer(0);

    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        // Write to chorus ring buffers
        chorusBufL[chorusWritePos] = outL[i];
        chorusBufR[chorusWritePos] = outR[i];

        // Opposite LFO phases for L and R
        const float modL = centerSamples + depthSamples * std::sin(chorusPhase);
        const float modR = centerSamples + depthSamples * std::sin(chorusPhase + juce::MathConstants<float>::pi);

        const float wetL = readBuf(chorusBufL, chorusWritePos, modL);
        const float wetR = readBuf(chorusBufR, chorusWritePos, modR);

        outL[i] = outL[i] * (1.0f - params.chorusAmount) + wetL * params.chorusAmount;
        outR[i] = outR[i] * (1.0f - params.chorusAmount) + wetR * params.chorusAmount;

        chorusWritePos = (chorusWritePos + 1) % chorusBufSize;
        chorusPhase += phaseInc;
        if (chorusPhase > juce::MathConstants<float>::twoPi)
            chorusPhase -= juce::MathConstants<float>::twoPi;
    }
}

//==============================================================================
void EffectsChain::processOutputFilters(juce::AudioBuffer<float>& buffer)
{
    if (params.hpFreq != lastHpFreq)
    {
        hpL = hpR = Biquad::makeHP(sr, params.hpFreq);
        lastHpFreq = params.hpFreq;
    }
    if (params.lpFreq != lastLpFreq)
    {
        lpL = lpR = Biquad::makeLP(sr, params.lpFreq);
        lastLpFreq = params.lpFreq;
    }

    float* dataL = buffer.getWritePointer(0);
    float* dataR = buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) : buffer.getWritePointer(0);

    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        dataL[i] = lpL.process(hpL.process(dataL[i]));
        dataR[i] = lpR.process(hpR.process(dataR[i]));
    }
}
