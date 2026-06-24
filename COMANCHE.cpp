#include "COMANCHE.h"
#include "IPlug_include_in_plug_src.h"
#include "IControls.h"

#include <filesystem>
#include <fstream>
#include <cmath>
namespace fs = std::filesystem;

// LFO frequency (Hz) per param — 0 = no modulation (enum/toggle/volume/feedback/macro)
static constexpr float kLfoFreq[kNumParams] = {
    // kReverbAmount, kReverbDecay, kDistAmount, kDistMode
    0.19f, 0.23f, 0.17f, 0.0f,
    // kDelaySyncMode, kDelayTimeMs, kDelayFeedback(off), kDelayMix
    0.0f,  0.29f, 0.0f,  0.27f,
    // kDelayLowcut, kDelayHighcut, kChorusAmount, kHpFreq
    0.13f, 0.37f, 0.21f, 0.41f,
    // kLpFreq, kOutputVol(off), kMacro
    0.33f, 0.0f,  0.0f
};

// ─── Constructor ─────────────────────────────────────────────────────────────

COMANCHE::COMANCHE(const InstanceInfo& info)
  : Plugin(info, MakeConfig(kNumParams, 1))
{
    GetParam(kReverbAmount) ->InitDouble("Reverb",      0.0,     0.0,   1.0,    0.001);
    GetParam(kReverbDecay)  ->InitDouble("ReverbDecay", 2.0,     0.3,   8.0,    0.01, "s");
    GetParam(kDistAmount)   ->InitDouble("Dist",        0.0,     0.0,   1.0,    0.001);
    GetParam(kDistMode)     ->InitEnum  ("DistMode",    0,       3,     "", 0, "", "CLIP","TAPE","TUBE");
    GetParam(kDelaySyncMode)->InitDouble ("DelaySync",   0,       0,     1,  1);
    GetParam(kDelayTimeMs)  ->InitDouble("DelayTime",   500.0,   1.0,   2000.0, 1.0,  "ms");
    GetParam(kDelayFeedback)->InitDouble("DelayFb",     0.35,    0.0,   0.97,   0.001);
    GetParam(kDelayMix)     ->InitDouble("DelayMix",    0.0,     0.0,   1.0,    0.001);
    GetParam(kDelayLowcut)  ->InitDouble("DelayLo",     20.0,    20.0,  500.0,  1.0,  "Hz");
    GetParam(kDelayHighcut) ->InitDouble("DelayHi",     18000.0, 1000.0,18000.0,1.0,  "Hz");
    GetParam(kChorusAmount) ->InitDouble("Chorus",      0.0,     0.0,   1.0,    0.001);
    GetParam(kHpFreq)       ->InitDouble("HP",          20.0,    20.0,  400.0,  1.0,  "Hz");
    GetParam(kLpFreq)       ->InitDouble("LP",          20000.0, 500.0, 20000.0,1.0,  "Hz");
    GetParam(kOutputVol)    ->InitDouble("OutVol",      0.0,   -60.0,   6.0,    0.1, "dB");
    GetParam(kMacro)        ->InitDouble("Macro",       0.0,     0.0,   1.0,    0.001);

    loadPersistentState();

#if IPLUG_EDITOR
    mMakeGraphicsFunc = [&]() {
        return MakeGraphics(*this, PLUG_WIDTH, PLUG_HEIGHT, PLUG_FPS,
                            GetScaleForScreen(PLUG_WIDTH, PLUG_HEIGHT));
    };

    mLayoutFunc = [&](IGraphics* g) {
        g->LoadFont("RobotoMono", ROBOTO_MONO_FN);
        g->EnableMouseOver(true);
        g->AttachPanelBackground(CT::bgPrimary);

        const IRECT full = g->GetBounds();

        // ── Header row (32px) ────────────────────────────────────────────────
        const IRECT hdr(full.L, full.T, full.R, full.T + 32.0f);
        g->AttachControl(new ITextControl(
            IRECT(hdr.L+12, hdr.T, hdr.L+240, hdr.B),
            "comanche v0.1",
            IText(12.0f, CT::fgPrimary, "RobotoMono", EAlign::Near, EVAlign::Middle)));

        // ── Left panel (260px): folder button + save icon + sample list ───────
        const float panelT = full.T + 34.0f;
        const float panelB = full.B;
        const IRECT lp(full.L, panelT, full.L+260.0f, panelB);

        g->AttachControl(new IPanelControl(lp, CT::bgPanel));

        const float btnRowT = lp.T + 4.0f;
        const float btnRowB = btnRowT + 26.0f;
        g->AttachControl(new ButtonControl(
            IRECT(lp.L+4, btnRowT, lp.L+186, btnRowB),
            "Buscar librerias",
            [this](IGraphics* gr) {
                WDL_String dir;
                gr->PromptForDirectory(dir, [this](const WDL_String& /*name*/, const WDL_String& path) {
                    if (path.GetLength() > 0) {
                        mFolderPath = path.Get();
                        mLibrary.setFolder(mFolderPath);
                        savePersistentState();
                        if (GetUI()) GetUI()->SetAllControlsDirty();
                    }
                });
            }));

        g->AttachControl(new SaveIconButton(
            IRECT(lp.L+192, btnRowT, lp.L+218, btnRowB),  // 26×26 (1:1)
            [this]() { savePreset(); }));

        const IRECT listR(lp.L+2, btnRowB+4, lp.R-2, panelB-2);
        g->AttachControl(new SampleListControl(listR, mLibrary.getSampleNames(), mSelectedIdx,
            // OnDrop
            [this](const std::string& filePath) {
                size_t slash = filePath.find_last_of("/\\");
                if (slash == std::string::npos) return;
                mFolderPath = filePath.substr(0, slash);
                mLibrary.setFolder(mFolderPath);
                savePersistentState();
                for (int i = 0; i < (int)mLibrary.getSampleNames().size(); i++) {
                    if (mLibrary.getSamplePath(i) == filePath) { loadSample(i); break; }
                }
                if (GetUI()) GetUI()->SetAllControlsDirty();
            },
            // OnRemove (right-click → "Eliminar Sonido")
            [this](int idx) { removeFromLibrary(idx); },
            // OnResetPreset (right-click → "Eliminar Preset")
            [this](int idx) { resetPresetForSample(idx); }
        ));

        // ── Right panel ────────────────────────────────────────────────────────
        const IRECT rp(full.L+260.0f, panelT, full.R, panelB);

        // ── MACRO knob (centred, top) ─────────────────────────────────────────
        const float mKW = 108, mKH = 130;
        const float mKX = rp.MW() - mKW*0.5f;
        const float mKY = rp.T + 8.0f;
        g->AttachControl(new ComanacheKnob(
            IRECT(mKX, mKY, mKX+mKW, mKY+mKH),
            kMacro, "MACRO", CT::knobGold, true));

        // ── Effect group boxes layout ─────────────────────────────────────────
        const float r1T  = rp.T + 148.0f;
        const float boxH = 168.0f;
        const float r2T  = r1T + boxH + 6.0f;
        const float bxL  = rp.L + 6.0f;

        // Row 1: REVERB(138) | DIST(178) | DELAY(rest)
        const float rvW = 138.0f;
        const float dtW = 178.0f;
        const float dlW = rp.R - bxL - rvW - 6 - dtW - 6 - 6;

        const IRECT boxReverb(bxL,             r1T, bxL+rvW,           r1T+boxH);
        const IRECT boxDist  (bxL+rvW+6,       r1T, bxL+rvW+6+dtW,     r1T+boxH);
        const IRECT boxDelay (bxL+rvW+6+dtW+6, r1T, rp.R-6,            r1T+boxH);

        // Row 2: CHORUS(138) | OUTPUT(rest)
        const IRECT boxChorus(bxL,        r2T, bxL+rvW,  r2T+boxH);
        const IRECT boxOutput(bxL+rvW+6,  r2T, rp.R-6,   r2T+boxH);

        g->AttachControl(new IPanelControl(boxReverb, CT::boxReverb));
        g->AttachControl(new IPanelControl(boxDist,   CT::boxDist));
        g->AttachControl(new IPanelControl(boxDelay,  CT::boxDelay));
        g->AttachControl(new IPanelControl(boxChorus, CT::boxChorus));
        g->AttachControl(new IPanelControl(boxOutput, CT::boxOutput));

        auto addTitle = [&](const IRECT& box, const char* label) {
            g->AttachControl(new ITextControl(
                IRECT(box.L+6, box.T+3, box.R-6, box.T+16),
                label,
                IText(9.0f, CT::fgPrimary, "RobotoMono", EAlign::Near, EVAlign::Middle)));
        };
        addTitle(boxReverb, "REVERB");
        addTitle(boxDist,   "DIST");
        addTitle(boxDelay,  "DELAY");
        addTitle(boxChorus, "CHORUS");
        addTitle(boxOutput, "OUTPUT");

        auto addKnob = [&](const IRECT& knobR, int param, const char* lbl, IColor fill) {
            g->AttachControl(new ComanacheKnob(knobR, param, lbl, fill, false, &mMacroLink[param], mParamMod));
            g->AttachControl(new MacroLinkButton(
                IRECT(knobR.R-10, knobR.T, knobR.R, knobR.T+10), mMacroLink[param]));
        };

        // ── REVERB box ────────────────────────────────────────────────────────
        {
            const float kW=70, kH=90;
            const float kX = boxReverb.MW() - kW*0.5f;
            const float kY = boxReverb.T + 18.0f;
            addKnob(IRECT(kX,kY,kX+kW,kY+kH), kReverbAmount, "REVERB", CT::knobBlue);

            // Reverb decay drag-value + macro link
            const float dvW=60, dvH=18;
            const float dvX = boxReverb.MW() - dvW*0.5f - 8.0f;
            const float dvY = kY + kH + 4.0f;
            g->AttachControl(new TinyDragValue(
                IRECT(dvX, dvY, dvX+dvW, dvY+dvH), kReverbDecay));
            g->AttachControl(new ITextControl(
                IRECT(dvX, dvY+dvH+1, dvX+dvW, dvY+dvH+11),
                "decay",
                IText(8.0f, CT::fgPrimary, "RobotoMono", EAlign::Center, EVAlign::Middle)));
            g->AttachControl(new MacroLinkButton(
                IRECT(dvX+dvW+3, dvY+4, dvX+dvW+3+10, dvY+4+10),
                mMacroLink[kReverbDecay]));
        }

        // ── DIST box ─────────────────────────────────────────────────────────
        {
            const float kW=66, kH=90;
            const float kX = boxDist.L + 8.0f;
            const float kY = boxDist.T + 18.0f;
            addKnob(IRECT(kX,kY,kX+kW,kY+kH), kDistAmount, "Distorsion", CT::knobGold);

            const float bX=kX+kW+8, bW=56, bH=22, bGap=4;
            const float totalBH=3*bH+2*bGap;
            const float bY=kY+(kH-totalBH)*0.5f;
            g->AttachControl(new ModeButton(IRECT(bX,bY,               bX+bW,bY+bH),           kDistMode,0,"CLIP"));
            g->AttachControl(new ModeButton(IRECT(bX,bY+bH+bGap,       bX+bW,bY+2*bH+bGap),   kDistMode,1,"TAPE"));
            g->AttachControl(new ModeButton(IRECT(bX,bY+2*(bH+bGap),   bX+bW,bY+2*(bH+bGap)+bH), kDistMode,2,"TUBE"));
        }

        // ── DELAY box ────────────────────────────────────────────────────────
        {
            const float kW=60, kH=84;
            const float kY = boxDelay.T + 18.0f;
            const float kXa = boxDelay.L + 8.0f;   // TIME knob
            const float kXb = kXa + kW + 8.0f;     // FEEDBACK knob
            const float kXc = kXb + kW + 16.0f;    // MIX / Cantidad knob

            g->AttachControl(new DelayTimeKnob(IRECT(kXa,kY,kXa+kW,kY+kH), CT::knobGrey));
            addKnob(IRECT(kXb,kY,kXb+kW,kY+kH), kDelayFeedback, "FEEDBK",  CT::knobGrey);
            addKnob(IRECT(kXc,kY,kXc+kW,kY+kH), kDelayMix,      "Cantidad",CT::knobBlue);

            // TEMPO toggle button below TIME knob
            const float sbT = kY + kH + 4.0f;
            g->AttachControl(new TempoToggleButton(
                IRECT(kXa, sbT, kXa+kW, sbT+18.0f), kDelaySyncMode));

            // BandFilterSlider for delay filter (right portion)
            const float bfsW=36, bfsH=boxH-28.0f;
            const float bfsX=boxDelay.R-8.0f-bfsW;
            const float bfsY=boxDelay.T+18.0f;
            g->AttachControl(new BandFilterSlider(
                IRECT(bfsX, bfsY, bfsX+bfsW, bfsY+bfsH),
                kDelayLowcut, kDelayHighcut));
            g->AttachControl(new ITextControl(
                IRECT(bfsX-2, boxDelay.T+3, bfsX+bfsW+2, boxDelay.T+16),
                "FILTER",
                IText(9.0f, CT::fgPrimary, "RobotoMono", EAlign::Center, EVAlign::Middle)));
        }

        // ── CHORUS box ───────────────────────────────────────────────────────
        {
            const float kW=70, kH=90;
            const float kX = boxChorus.MW() - kW*0.5f;
            const float kY = boxChorus.T + 18.0f;
            addKnob(IRECT(kX,kY,kX+kW,kY+kH), kChorusAmount, "CHORUS", CT::knobBlue);
        }

        // ── OUTPUT box ───────────────────────────────────────────────────────
        {
            const float innerL = boxOutput.L + 8.0f;
            const float innerT = boxOutput.T + 18.0f;
            const float innerB = boxOutput.B - 14.0f;

            // VolumeSlider (VU L+R + fader) with 0dB mark
            const float vsW=46.0f;
            g->AttachControl(new VolumeSlider(
                IRECT(innerL, innerT, innerL+vsW, innerB),
                kOutputVol, &mVuPeakL, &mVuPeakR));
            g->AttachControl(new ITextControl(
                IRECT(innerL, innerB+1, innerL+vsW, innerB+13),
                "VOL",
                IText(9.0f, CT::fgPrimary, "RobotoMono", EAlign::Center, EVAlign::Middle)));

            // BandFilterSlider for output HP/LP (vertical, beside vol)
            const float bfsW=36.0f;
            const float bfsX=innerL+vsW+10.0f;
            g->AttachControl(new BandFilterSlider(
                IRECT(bfsX, innerT, bfsX+bfsW, innerB),
                kHpFreq, kLpFreq, 20.0f, 20000.0f));
            g->AttachControl(new ITextControl(
                IRECT(bfsX-2, boxOutput.T+3, bfsX+bfsW+2, boxOutput.T+16),
                "FILTER",
                IText(9.0f, CT::fgPrimary, "RobotoMono", EAlign::Center, EVAlign::Middle)));
        }
    };
#endif
}

// ─── DSP ─────────────────────────────────────────────────────────────────────

#if IPLUG_DSP

void COMANCHE::OnReset()
{
    mFX.prepare(GetSampleRate(), GetBlockSize());
    for (auto& v : mVoices) v = Voice{};
    mVoiceTimer = 0;
}

void COMANCHE::ProcessMidiMsg(const IMidiMsg& msg)
{
    mMidiQueue.Add(msg);
}

void COMANCHE::ProcessBlock(sample** /*inputs*/, sample** outputs, int nFrames)
{
    const int n = std::min(nFrames, kMaxBlock);

    std::fill(mTmpL, mTmpL+n, 0.0f);
    std::fill(mTmpR, mTmpR+n, 0.0f);

    for (int s = 0; s < n; s++) {
        while (!mMidiQueue.Empty()) {
            auto msg = mMidiQueue.Peek();
            if (msg.mOffset > s) break;
            mMidiQueue.Remove();
            auto status = msg.StatusMsg();
            if (status == IMidiMsg::kNoteOn && msg.Velocity() > 0)
                noteOn(msg.NoteNumber(), msg.Velocity() / 127.0f);
            else if (status == IMidiMsg::kNoteOff ||
                    (status == IMidiMsg::kNoteOn && msg.Velocity() == 0))
                noteOff(msg.NoteNumber());
        }

        if (mSampleLen > 0) {
            for (auto& v : mVoices) {
                if (!v.active) continue;
                if (v.srcPos >= mSampleLen) { v.active=false; continue; }
                float env = v.level;
                if (v.inRelease) {
                    if (v.relLeft <= 0.0f) { v.active=false; continue; }
                    env *= v.relLeft / v.relTotal;
                    v.relLeft -= 1.0f;
                }
                mTmpL[s] += lerp(mSampleL, v.srcPos) * env;
                mTmpR[s] += lerp(mSampleR, v.srcPos) * env;
                v.srcPos  += v.pitchRatio;
            }
        }
        mVoiceTimer++;
    }

    const float macro = (float)GetParam(kMacro)->GetNormalized();

    // Update slow per-param LFO modulation (amplitude = macro * 0.08 normalized range)
    static constexpr float kTwoPi = 6.28318530718f;
    const float blockDur = (float)n / (float)GetSampleRate();
    for (int i = 0; i < kNumParams; i++) {
        if (kLfoFreq[i] <= 0.0f) { mParamMod[i] = 0.0f; continue; }
        mModPhase[i] += kLfoFreq[i] * kTwoPi * blockDur;
        if (mModPhase[i] > kTwoPi) mModPhase[i] -= kTwoPi;
        // Sum of two incommensurate sinusoids → irregular, non-repeating shape
        float v = std::sin(mModPhase[i])
                + 0.5f * std::sin(mModPhase[i] * 2.7183f + (float)i * 1.618f);
        mParamMod[i] = macro * 0.07f * (v / 1.5f);
    }

    auto applyMacro = [&](int idx) -> float {
        auto* p = GetParam(idx);
        float baseN = (float)p->GetNormalized();
        float effN  = baseN;
        // Macro link offset (only when linked)
        if (mMacroLink[idx] != 0)
            effN = std::clamp(effN + macro * (float)mMacroLink[idx], 0.0f, 1.0f);
        // Slow LFO modulation (all continuous params, scales with macro)
        if (kLfoFreq[idx] > 0.0f)
            effN = std::clamp(effN + mParamMod[idx], 0.0f, 1.0f);
        return (float)p->FromNormalized(effN);
    };

    EffectsParameters ep;
    ep.reverbAmount  = applyMacro(kReverbAmount);
    ep.reverbDecay   = applyMacro(kReverbDecay);
    ep.distAmount    = applyMacro(kDistAmount);
    ep.distMode      = (int)GetParam(kDistMode)->Value();
    ep.delaySyncMode = (int)GetParam(kDelaySyncMode)->Value();
    ep.delayTimeMs   = applyMacro(kDelayTimeMs);
    ep.delayFeedback = applyMacro(kDelayFeedback);
    ep.delayMix      = applyMacro(kDelayMix);
    ep.delayLowcut   = applyMacro(kDelayLowcut);
    ep.delayHighcut  = applyMacro(kDelayHighcut);
    ep.chorusAmount  = applyMacro(kChorusAmount);
    ep.hpFreq        = applyMacro(kHpFreq);
    ep.lpFreq        = applyMacro(kLpFreq);
    float dBVol      = applyMacro(kOutputVol);  // dB, -60..+6
    ep.outputVol     = (dBVol <= -59.0f) ? 0.0f : std::pow(10.0f, dBVol / 20.0f);
    ep.macroModAmount = macro;
    ep.bpm           = GetTempo() > 0.0 ? GetTempo() : 120.0;

    mFX.setParameters(ep);
    mFX.process(mTmpL, mTmpR, n);

    float peakL=0, peakR=0;
    for (int i = 0; i < n; i++) {
        outputs[0][i] = (sample)mTmpL[i];
        outputs[1][i] = (sample)mTmpR[i];
        peakL = std::max(peakL, std::abs(mTmpL[i]));
        peakR = std::max(peakR, std::abs(mTmpR[i]));
    }
    // Fast peak decay (~0.4s half-life per ProcessBlock call at typical block sizes)
    mVuPeakL.store(std::max(mVuPeakL.load() * 0.982f, peakL));
    mVuPeakR.store(std::max(mVuPeakR.load() * 0.982f, peakR));
}

// ─── Voice management ────────────────────────────────────────────────────────

float COMANCHE::lerp(const std::vector<float>& b, double pos)
{
    int i = (int)pos;
    float f = (float)(pos - i);
    float s0 = (i   < (int)b.size()) ? b[i]   : 0.0f;
    float s1 = (i+1 < (int)b.size()) ? b[i+1] : 0.0f;
    return s0 + f*(s1-s0);
}

int COMANCHE::stealVoice()
{
    int oldest = 0;
    for (int i = 1; i < kNumVoices; i++)
        if (mVoices[i].startSample < mVoices[oldest].startSample)
            oldest = i;
    return oldest;
}

void COMANCHE::noteOn(int note, float vel)
{
    int vi = -1;
    for (int i = 0; i < kNumVoices; i++)
        if (!mVoices[i].active) { vi=i; break; }
    if (vi == -1) vi = stealVoice();

    auto& v = mVoices[vi];
    v.active     = true;
    v.inRelease  = false;
    v.noteNum    = note;
    v.srcPos     = 0.0;
    v.pitchRatio = std::pow(2.0, (note-60)/12.0) * mSampleSR / GetSampleRate();
    v.level      = vel;
    v.startSample= mVoiceTimer;
    v.relTotal   = 0.08f * (float)GetSampleRate();
    v.relLeft    = 0.0f;
}

void COMANCHE::noteOff(int note)
{
    for (auto& v : mVoices)
        if (v.active && !v.inRelease && v.noteNum == note) {
            v.inRelease = true;
            v.relLeft   = v.relTotal;
        }
}

// ─── State serialization ─────────────────────────────────────────────────────

bool COMANCHE::SerializeState(IByteChunk& chunk) const
{
    WDL_String fs(mFolderPath.c_str());
    chunk.PutStr(fs.Get());
    return SerializeParams(chunk);
}

int COMANCHE::UnserializeState(const IByteChunk& chunk, int pos)
{
    WDL_String fs;
    pos = chunk.GetStr(fs, pos);
    mFolderPath = fs.Get();
    if (!mFolderPath.empty()) mLibrary.setFolder(mFolderPath);
    return UnserializeParams(chunk, pos);
}

#endif // IPLUG_DSP

// ─── Editor callbacks ─────────────────────────────────────────────────────────

#if IPLUG_EDITOR

void COMANCHE::OnUIOpen()
{
    if (GetUI()) GetUI()->SetAllControlsDirty();
}

bool COMANCHE::OnMessage(int msgTag, int, int /*dataSize*/, const void* pData)
{
    if (msgTag == kMsgLoadSample) {
        int idx = *(const int*)pData;
        loadSample(idx);
        return true;
    }
    return false;
}

#endif

// ─── Sample + preset helpers ──────────────────────────────────────────────────

void COMANCHE::loadSample(int idx)
{
    std::string path = mLibrary.getSamplePath(idx);
    if (path.empty()) return;

    std::vector<float> left, right;
    int numCh; double sr;
    if (!mLibrary.loadSample(path, left, right, numCh, sr)) return;

    mSampleL   = std::move(left);
    mSampleR   = std::move(right);
    mSampleLen = (int)mSampleL.size();
    mSampleSR  = sr;
    mSelectedIdx = idx;

    for (auto& v : mVoices) v.active = false;

    // Reset to defaults, then restore saved preset (if any)
    mPresets.onSampleLoaded(path);
    for (int pi = 0; pi < kNumParams; pi++)
        GetParam(pi)->SetNormalized(GetParam(pi)->GetDefault(true));
    for (auto& kv : mPresets.getValues()) {
        for (int pi = 0; pi < kNumParams; pi++) {
            auto* p = GetParam(pi);
            if (std::string(p->GetName()) == kv.first) {
                p->SetNormalized(p->ToNormalized(kv.second));
                break;
            }
        }
    }
    // Knobs/sliders read from GetParam()->GetNormalized() directly in Draw(), so dirty is enough
    if (GetUI()) GetUI()->SetAllControlsDirty();
}

void COMANCHE::savePreset()
{
    std::map<std::string, float> vals;
    for (int i = 0; i < kNumParams; i++) {
        auto* p = GetParam(i);
        vals[p->GetName()] = (float)p->Value();
    }
    mPresets.save(vals);
}

void COMANCHE::removeFromLibrary(int idx)
{
    mLibrary.removeAt(idx);
    if (mSelectedIdx == idx) mSelectedIdx = -1;
    else if (mSelectedIdx > idx) mSelectedIdx--;
    if (GetUI()) GetUI()->SetAllControlsDirty();
}

void COMANCHE::resetPresetForSample(int idx)
{
    std::string path = mLibrary.getSamplePath(idx);
    if (path.empty()) return;
    // Point preset manager at this sample and delete its preset file
    mPresets.onSampleLoaded(path);
    mPresets.deletePreset();
    // If this is the currently loaded sample, reset params to defaults
    if (idx == mSelectedIdx) {
        for (int pi = 0; pi < kNumParams; pi++)
            GetParam(pi)->SetNormalized(GetParam(pi)->GetDefault(true));
        if (GetUI()) GetUI()->SetAllControlsDirty();
    }
}

// ─── Cross-session persistence (independent of DAW project) ──────────────────

static std::string persistStatePath()
{
    const char* home = getenv("HOME");
    if (!home) return {};
    return std::string(home) + "/Library/Application Support/COMANCHE/session.json";
}

void COMANCHE::savePersistentState() const
{
    std::string p = persistStatePath();
    if (p.empty()) return;
    fs::create_directories(fs::path(p).parent_path());
    std::ofstream f(p);
    if (!f.is_open()) return;
    // Escape backslashes in path (macOS paths won't have them, but just in case)
    std::string escaped;
    for (char c : mFolderPath) { if (c == '"' || c == '\\') escaped += '\\'; escaped += c; }
    f << "{\"folder\":\"" << escaped << "\"}";
}

void COMANCHE::loadPersistentState()
{
    std::string p = persistStatePath();
    if (p.empty()) return;
    std::ifstream f(p);
    if (!f.is_open()) return;
    std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    size_t a = text.find("\"folder\":\"");
    if (a == std::string::npos) return;
    a += 10;
    size_t b = text.find('"', a);
    if (b == std::string::npos) return;
    mFolderPath = text.substr(a, b - a);
    if (!mFolderPath.empty() && fs::is_directory(mFolderPath))
        mLibrary.setFolder(mFolderPath);
}
