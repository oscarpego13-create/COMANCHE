#include "COMANCHE.h"
#include "IPlug_include_in_plug_src.h"
#include "IControls.h"

// ─── Constructor ─────────────────────────────────────────────────────────────

COMANCHE::COMANCHE(const InstanceInfo& info)
  : Plugin(info, MakeConfig(kNumParams, 1))
{
    GetParam(kReverbAmount) ->InitDouble("Reverb",     0.0,    0.0,   1.0,   0.001);
    GetParam(kDistAmount)   ->InitDouble("Dist",       0.0,    0.0,   1.0,   0.001);
    GetParam(kDistMode)     ->InitEnum  ("DistMode",   0,      3,     "", 0, "", "CLIP","TAPE","TUBE");
    GetParam(kDelaySyncMode)->InitEnum  ("DelaySync",  0,      6,     "", 0, "", "FREE","1/4","1/8","1/16","1/4T","1/8T");
    GetParam(kDelayTimeMs)  ->InitDouble("DelayTime",  500.0,  1.0,   2000.0, 1.0, "ms");
    GetParam(kDelayFeedback)->InitDouble("DelayFb",    0.35,   0.0,   0.97,   0.001);
    GetParam(kDelayLowcut)  ->InitDouble("DelayLo",    20.0,   20.0,  500.0,  1.0, "Hz");
    GetParam(kDelayHighcut) ->InitDouble("DelayHi",    18000.0,2000.0,18000.0,1.0, "Hz");
    GetParam(kChorusAmount) ->InitDouble("Chorus",     0.0,    0.0,   1.0,   0.001);
    GetParam(kHpFreq)       ->InitDouble("HP",         20.0,   20.0,  2000.0, 1.0, "Hz");
    GetParam(kLpFreq)       ->InitDouble("LP",         20000.0,500.0, 20000.0,1.0, "Hz");
    GetParam(kOutputVol)    ->InitDouble("OutVol",     0.85,   0.0,   1.0,   0.001);
    GetParam(kMacro)        ->InitDouble("Macro",      0.0,    0.0,   1.0,   0.001);

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
            "comanche  \xc2\xb7  v1.1",
            IText(12.0f, CT::fgPrimary, "RobotoMono", EAlign::Near, EVAlign::Middle)));

        // ── Left panel (260px): folder button + save icon + sample list ───────
        const float panelT = full.T + 34.0f;
        const float panelB = full.B - 22.0f;
        const IRECT lp(full.L, panelT, full.L+260.0f, panelB);

        g->AttachControl(new IPanelControl(lp, CT::bgPanel));

        // "Buscar librerias" button + save icon at top of panel
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
                        if (GetUI()) GetUI()->SetAllControlsDirty();
                    }
                });
            }));

        g->AttachControl(new SaveIconButton(
            IRECT(lp.L+190, btnRowT, lp.L+256, btnRowB),
            [this]() { savePreset(); }));

        // Sample list with scrollbar
        const IRECT listR(lp.L+2, btnRowB+4, lp.R-2, panelB-2);
        g->AttachControl(new SampleListControl(listR, mLibrary.getSampleNames(), mSelectedIdx));

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
        // Row 1: y=rp.T+148 to rp.T+316 (168px), Row 2: y=rp.T+322 to rp.T+490 (168px)
        const float r1T  = rp.T + 148.0f;
        const float boxH = 168.0f;
        const float r2T  = r1T + boxH + 6.0f;
        const float bxL  = rp.L + 6.0f;

        // Row 1 box widths: REVERB=138, DIST=178, DELAY=rest
        const float rvW  = 138.0f;
        const float dtW  = 178.0f;
        const float dlW  = rp.R - bxL - rvW - 6 - dtW - 6 - 6;  // ~352px

        const IRECT boxReverb(bxL,             r1T, bxL+rvW,           r1T+boxH);
        const IRECT boxDist  (bxL+rvW+6,       r1T, bxL+rvW+6+dtW,     r1T+boxH);
        const IRECT boxDelay (bxL+rvW+6+dtW+6, r1T, rp.R-6,            r1T+boxH);

        // Row 2 box widths: CHORUS=138, OUTPUT=rest
        const IRECT boxChorus(bxL,        r2T, bxL+rvW,  r2T+boxH);
        const IRECT boxOutput(bxL+rvW+6,  r2T, rp.R-6,   r2T+boxH);

        // Draw pastel box backgrounds
        g->AttachControl(new IPanelControl(boxReverb, CT::boxReverb));
        g->AttachControl(new IPanelControl(boxDist,   CT::boxDist));
        g->AttachControl(new IPanelControl(boxDelay,  CT::boxDelay));
        g->AttachControl(new IPanelControl(boxChorus, CT::boxChorus));
        g->AttachControl(new IPanelControl(boxOutput, CT::boxOutput));

        // Box title helper
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

        // Helper: place knob + macro link button inside a box
        auto addKnob = [&](const IRECT& knobR, int param, const char* lbl, IColor fill) {
            auto* knob = new ComanacheKnob(knobR, param, lbl, fill,
                                           false, &mMacroLink[param]);
            g->AttachControl(knob);
            // MacroLinkButton at top-right corner of knob rect
            g->AttachControl(new MacroLinkButton(
                IRECT(knobR.R-10, knobR.T, knobR.R, knobR.T+10),
                mMacroLink[param]));
        };

        // ── REVERB box ────────────────────────────────────────────────────────
        {
            float kW=70, kH=90;
            float kX = boxReverb.MW()-kW*0.5f;
            float kY = boxReverb.T + 18.0f;
            addKnob(IRECT(kX,kY,kX+kW,kY+kH), kReverbAmount, "REVERB", CT::knobBlue);
        }

        // ── DIST box ─────────────────────────────────────────────────────────
        {
            float kW=66, kH=90;
            float kX = boxDist.L + 8.0f;
            float kY = boxDist.T + 18.0f;
            addKnob(IRECT(kX,kY,kX+kW,kY+kH), kDistAmount, "DRIVE", CT::knobGold);

            // Mode buttons CLIP/TAPE/TUBE
            float bX = kX+kW+8, bW=56, bH=22, bGap=4;
            float totalBH = 3*bH+2*bGap;
            float bY = kY + (kH-totalBH)*0.5f;
            g->AttachControl(new ModeButton(IRECT(bX,bY,           bX+bW,bY+bH),         kDistMode,0,"CLIP"));
            g->AttachControl(new ModeButton(IRECT(bX,bY+bH+bGap,   bX+bW,bY+2*bH+bGap),  kDistMode,1,"TAPE"));
            g->AttachControl(new ModeButton(IRECT(bX,bY+2*(bH+bGap),bX+bW,bY+2*(bH+bGap)+bH),kDistMode,2,"TUBE"));
        }

        // ── DELAY box ────────────────────────────────────────────────────────
        {
            float kW=60, kH=84;
            float kY = boxDelay.T + 18.0f;
            float kXa = boxDelay.L + 8.0f;  // TIME knob
            float kXb = kXa + kW + 8.0f;    // FEEDBACK knob

            addKnob(IRECT(kXa,kY,kXa+kW,kY+kH), kDelayTimeMs,   "TIME",  CT::knobGrey);
            addKnob(IRECT(kXb,kY,kXb+kW,kY+kH), kDelayFeedback, "FEEDBK",CT::knobGrey);

            // BPM sync button below TIME knob
            float sbT = kY + kH + 2.0f;
            float sbH = 16.0f;
            float sbW = kW * 2.0f + 8.0f;
            // Sync mode buttons (3 × 2 grid)
            const char* syncLabels[] = {"FREE","1/4","1/8","1/16","1/4T","1/8T"};
            float sbBtnW=42, sbBtnH=14, sbBtnGap=2;
            for (int i=0;i<6;i++) {
                int col=i%2, row=i/2;
                float sx = kXa + col*(sbBtnW+sbBtnGap);
                float sy = sbT + row*(sbBtnH+sbBtnGap);
                g->AttachControl(new ModeButton(IRECT(sx,sy,sx+sbBtnW,sy+sbBtnH),
                    kDelaySyncMode, i, syncLabels[i]));
            }

            // 2D delay filter (1.5:1 ratio) — fills right portion of delay box
            float dfL = kXb + kW + 10.0f;
            float dfR = boxDelay.R - 8.0f;
            float dfW = dfR - dfL;
            float dfH = dfW / 1.5f;
            float dfT = boxDelay.T + (boxH - dfH) * 0.5f;
            g->AttachControl(new DelayFilterControl(
                IRECT(dfL, dfT, dfR, dfT+dfH),
                kDelayLowcut, kDelayHighcut));

            // "FILTER" label above 2D control
            g->AttachControl(new ITextControl(
                IRECT(dfL, boxDelay.T+3, dfR, boxDelay.T+16),
                "FILTER",
                IText(9.0f, CT::fgPrimary, "RobotoMono", EAlign::Near, EVAlign::Middle)));
        }

        // ── CHORUS box ───────────────────────────────────────────────────────
        {
            float kW=70, kH=90;
            float kX = boxChorus.MW()-kW*0.5f;
            float kY = boxChorus.T + 18.0f;
            addKnob(IRECT(kX,kY,kX+kW,kY+kH), kChorusAmount, "CHORUS", CT::knobBlue);
        }

        // ── OUTPUT box ───────────────────────────────────────────────────────
        {
            float innerL = boxOutput.L + 8.0f;
            float innerT = boxOutput.T + 18.0f;
            float innerH = boxH - 26.0f;  // available height below title

            // Volume slider + VU meter
            float vsW = 46.0f;  // VU(9+2+9) + gap(4) + slider(14+2) = 40 → 46 total incl. handle overhang
            float vsT = innerT;
            float vsB = boxOutput.B - 16.0f;
            g->AttachControl(new VolumeSlider(
                IRECT(innerL, vsT, innerL+vsW, vsB),
                kOutputVol, &mVuPeakL, &mVuPeakR));
            // "VOL" label below
            g->AttachControl(new ITextControl(
                IRECT(innerL, vsB+1, innerL+vsW, vsB+13),
                "VOL",
                IText(9.0f, CT::fgPrimary, "RobotoMono", EAlign::Center, EVAlign::Middle)));

            float kGap = 18.0f;
            float kX   = innerL + vsW + kGap;
            float kW   = 68.0f, kH = 90.0f;
            float kY   = innerT;

            addKnob(IRECT(kX,kY,kX+kW,kY+kH), kHpFreq, "HP", CT::knobGrey);
            kX += kW + 12.0f;
            addKnob(IRECT(kX,kY,kX+kW,kY+kH), kLpFreq, "LP", CT::knobGrey);
        }

        // ── Footer: folder path ───────────────────────────────────────────────
        g->AttachControl(new ITextControl(
            IRECT(full.L+12, full.B-20, full.R-12, full.B),
            mLibrary.getFolder().empty() ? "no folder selected" : mLibrary.getFolder().c_str(),
            IText(10.0f, CT::fgPrimary, "RobotoMono", EAlign::Near, EVAlign::Middle)));
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

    // Apply macro links and build effects parameters
    const float macro = (float)GetParam(kMacro)->GetNormalized();
    auto applyMacro = [&](int idx) -> float {
        auto* p = GetParam(idx);
        if (mMacroLink[idx] == 0) return (float)p->Value();
        float baseN = (float)p->GetNormalized();
        float effN  = std::clamp(baseN + macro * (float)mMacroLink[idx], 0.0f, 1.0f);
        return (float)p->FromNormalized(effN);
    };

    EffectsParameters ep;
    ep.reverbAmount  = applyMacro(kReverbAmount);
    ep.distAmount    = applyMacro(kDistAmount);
    ep.distMode      = (int)GetParam(kDistMode)->Value();
    ep.delaySyncMode = (int)GetParam(kDelaySyncMode)->Value();
    ep.delayTimeMs   = applyMacro(kDelayTimeMs);
    ep.delayFeedback = applyMacro(kDelayFeedback);
    ep.delayLowcut   = applyMacro(kDelayLowcut);
    ep.delayHighcut  = applyMacro(kDelayHighcut);
    ep.chorusAmount  = applyMacro(kChorusAmount);
    ep.hpFreq        = applyMacro(kHpFreq);
    ep.lpFreq        = applyMacro(kLpFreq);
    ep.outputVol     = applyMacro(kOutputVol);
    ep.bpm           = GetTempo() > 0.0 ? GetTempo() : 120.0;

    mFX.setParameters(ep);
    mFX.process(mTmpL, mTmpR, n);

    // VU peak measurement (post-effects)
    float peakL=0, peakR=0;
    for (int i = 0; i < n; i++) {
        outputs[0][i] = (sample)mTmpL[i];
        outputs[1][i] = (sample)mTmpR[i];
        peakL = std::max(peakL, std::abs(mTmpL[i]));
        peakR = std::max(peakR, std::abs(mTmpR[i]));
    }
    mVuPeakL.store(std::max(mVuPeakL.load() * 0.9994f, peakL));
    mVuPeakR.store(std::max(mVuPeakR.load() * 0.9994f, peakR));
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

    // Auto-load preset if it exists for this sample
    mPresets.onSampleLoaded(path);
    for (auto& kv : mPresets.getValues()) {
        for (int pi = 0; pi < kNumParams; pi++) {
            auto* p = GetParam(pi);
            if (std::string(p->GetName()) == kv.first) {
                p->SetNormalized(p->ToNormalized(kv.second));
                break;
            }
        }
    }
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
