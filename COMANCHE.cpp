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
    GetParam(kDelayLowcut)  ->InitDouble("DelayLo",    20.0,   20.0,  500.0,  1.0, "Hz");
    GetParam(kDelayHighcut) ->InitDouble("DelayHi",    18000.0,2000.0,18000.0,1.0, "Hz");
    GetParam(kChorusAmount) ->InitDouble("Chorus",     0.0,    0.0,   1.0,   0.001);
    GetParam(kHpFreq)       ->InitDouble("HP",         20.0,   20.0,  2000.0, 1.0, "Hz");
    GetParam(kLpFreq)       ->InitDouble("LP",         20000.0,500.0, 20000.0,1.0, "Hz");
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
            "comanche  \xc2\xb7  v1.0",
            IText(11.0f, CT::fgPrimary, "RobotoMono", EAlign::Near, EVAlign::Middle)));

        // CHANGE FOLDER button
        g->AttachControl(new ButtonControl(
            IRECT(hdr.R-150, hdr.T+6, hdr.R-8, hdr.B-6),
            "CHANGE FOLDER",
            [this](IGraphics* gr) {
                WDL_String dir;
                gr->PromptForDirectory(dir, [this](const WDL_String& /*name*/, const WDL_String& path) {
                    if (path.GetLength() > 0) {
                        mFolderPath = std::string(path.Get());
                        mLibrary.setFolder(mFolderPath);
                        if (GetUI()) GetUI()->SetAllControlsDirty();
                    }
                });
            }));

        // ── Left panel (280px): sample list + buttons ─────────────────────────
        const float panelT = full.T + 34.0f;
        const float panelB = full.B - 22.0f;
        const IRECT leftPanel(full.L, panelT, full.L+280, panelB);

        // Background
        g->AttachControl(new IPanelControl(leftPanel, CT::bgPanel));

        // Sample list fills everything except bottom 36px
        const IRECT listR(leftPanel.L+4, leftPanel.T+4,
                          leftPanel.R-4, leftPanel.B-40);
        g->AttachControl(new SampleListControl(listR, mLibrary.getSampleNames(), mSelectedIdx));

        // SAVE PRESET / DEL PRESET buttons
        const float btnT = leftPanel.B - 36;
        const float btnB = leftPanel.B - 8;
        const float btnMid = leftPanel.MW();

        g->AttachControl(new ButtonControl(
            IRECT(leftPanel.L+8, btnT, btnMid-4, btnB),
            "SAVE PRESET",
            [this](IGraphics*) { savePreset(); }));

        g->AttachControl(new ButtonControl(
            IRECT(btnMid+4, btnT, leftPanel.R-8, btnB),
            "DEL PRESET",
            [this](IGraphics*) { deletePreset(); }));

        // ── Right panel: MACRO + effects grid ────────────────────────────────
        const IRECT rp(full.L+280, panelT, full.R, panelB);

        // MACRO knob (90×90, centred top)
        const float mKW = 108, mKH = 110;
        g->AttachControl(new ComanacheKnob(
            IRECT(rp.MW()-mKW*0.5f, rp.T+8, rp.MW()+mKW*0.5f, rp.T+8+mKH),
            kMacro, "MACRO", CT::knobGold, true));

        // Effects row 1: REVERB | DIST | DELAY
        const float efxT = rp.T + 128;
        const float kW = 74, kH = 90;
        const float btnH = 22, btnW = 40;
        float x = rp.L + 16;

        // REVERB
        g->AttachControl(new ComanacheKnob(IRECT(x, efxT, x+kW, efxT+kH), kReverbAmount, "REVERB", CT::knobBlue));
        x += kW + 12;

        // DIST knob + CLIP/TAPE/TUBE buttons
        g->AttachControl(new ComanacheKnob(IRECT(x, efxT, x+kW, efxT+kH), kDistAmount, "DIST", CT::knobGold));
        x += kW + 4;
        float bx = x;
        float bGap = 4;
        int totalBH = 3*btnH + 2*bGap;
        float bTop = efxT + (kH - totalBH) * 0.5f;
        g->AttachControl(new ModeButton(IRECT(bx, bTop,             bx+btnW, bTop+btnH),            kDistMode, 0, "CLIP"));
        g->AttachControl(new ModeButton(IRECT(bx, bTop+btnH+bGap,   bx+btnW, bTop+2*btnH+bGap),     kDistMode, 1, "TAPE"));
        g->AttachControl(new ModeButton(IRECT(bx, bTop+2*(btnH+bGap),bx+btnW,bTop+2*(btnH+bGap)+btnH),kDistMode,2,"TUBE"));
        x += btnW + 14;

        // DELAY: TIME knob + lowcut/highcut knobs + sync buttons
        g->AttachControl(new ComanacheKnob(IRECT(x, efxT, x+kW, efxT+kH), kDelayTimeMs,   "TIME",   CT::knobGrey));
        x += kW + 4;
        g->AttachControl(new ComanacheKnob(IRECT(x, efxT, x+kW, efxT+kH), kDelayLowcut,   "LOWCUT", CT::knobGrey));
        x += kW + 4;
        g->AttachControl(new ComanacheKnob(IRECT(x, efxT, x+kW, efxT+kH), kDelayHighcut,  "HIOUT",  CT::knobGrey));
        x += kW + 4;

        // Sync mode buttons (stacked 2 cols × 3 rows)
        const char* syncLabels[] = {"FREE","1/4","1/8","1/16","1/4T","1/8T"};
        const float sbW=44, sbH=19, sbGap=3;
        for (int i = 0; i < 6; i++) {
            int col = i%2, row = i/2;
            float sx = x + col*(sbW+3);
            float sy = efxT + 6 + row*(sbH+sbGap);
            g->AttachControl(new ModeButton(IRECT(sx,sy,sx+sbW,sy+sbH), kDelaySyncMode, i, syncLabels[i]));
        }

        // Effects row 2: CHORUS | HP | LP
        const float efxT2 = efxT + kH + 14;
        float x2 = rp.L + 16;
        g->AttachControl(new ComanacheKnob(IRECT(x2, efxT2, x2+kW, efxT2+kH), kChorusAmount, "CHORUS", CT::knobBlue));
        x2 += kW + 20;
        g->AttachControl(new ComanacheKnob(IRECT(x2, efxT2, x2+kW, efxT2+kH), kHpFreq, "HP", CT::knobGrey));
        x2 += kW + 12;
        g->AttachControl(new ComanacheKnob(IRECT(x2, efxT2, x2+kW, efxT2+kH), kLpFreq, "LP", CT::knobGrey));

        // ── Footer: folder path ───────────────────────────────────────────────
        g->AttachControl(new ITextControl(
            IRECT(full.L+12, full.B-20, full.R-12, full.B),
            mLibrary.getFolder().empty() ? "no folder selected" : mLibrary.getFolder().c_str(),
            IText(9.0f, IColor(120,26,26,26), nullptr, EAlign::Near, EVAlign::Middle)));
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
        // MIDI at this sample position
        while (!mMidiQueue.Empty()) {
            auto msg = mMidiQueue.Peek();
            if (msg.mOffset > s) break;
            mMidiQueue.Remove();
            auto status = msg.StatusMsg();
            if (status == IMidiMsg::kNoteOn  && msg.Velocity() > 0)
                noteOn(msg.NoteNumber(), msg.Velocity() / 127.0f);
            else if (status == IMidiMsg::kNoteOff ||
                    (status == IMidiMsg::kNoteOn && msg.Velocity() == 0))
                noteOff(msg.NoteNumber());
        }

        // Render voices
        if (mSampleLen > 0) {
            for (auto& v : mVoices) {
                if (!v.active) continue;
                if (v.srcPos >= mSampleLen) { v.active = false; continue; }

                float env = v.level;
                if (v.inRelease) {
                    if (v.relLeft <= 0.0f) { v.active = false; continue; }
                    env *= v.relLeft / v.relTotal;
                    v.relLeft -= 1.0f;
                }
                mTmpL[s] += lerp(mSampleL, v.srcPos) * env;
                mTmpR[s] += lerp(mSampleR, v.srcPos) * env;
                v.srcPos += v.pitchRatio;
            }
        }
        mVoiceTimer++;
    }

    // Effects
    const float macro = (float)GetParam(kMacro)->Value();
    EffectsParameters ep;
    ep.reverbAmount  = mMacro.apply(macro, (float)GetParam(kReverbAmount)->Value(), MacroController::Target::Reverb);
    ep.distAmount    = mMacro.apply(macro, (float)GetParam(kDistAmount)->Value(),   MacroController::Target::Dist);
    ep.distMode      = (int)GetParam(kDistMode)->Value();
    ep.delaySyncMode = (int)GetParam(kDelaySyncMode)->Value();
    ep.delayTimeMs   = (float)GetParam(kDelayTimeMs)->Value();
    ep.delayLowcut   = (float)GetParam(kDelayLowcut)->Value();
    ep.delayHighcut  = (float)GetParam(kDelayHighcut)->Value();
    ep.chorusAmount  = mMacro.apply(macro, (float)GetParam(kChorusAmount)->Value(), MacroController::Target::Chorus);
    ep.hpFreq        = (float)GetParam(kHpFreq)->Value();
    ep.lpFreq        = (float)GetParam(kLpFreq)->Value();
    ep.bpm           = GetTempo() > 0.0 ? GetTempo() : 120.0;
    mFX.setParameters(ep);
    mFX.process(mTmpL, mTmpR, n);

    for (int i = 0; i < n; i++) {
        outputs[0][i] = (sample)mTmpL[i];
        outputs[1][i] = (sample)mTmpR[i];
    }
}

// ─── Voice management ─────────────────────────────────────────────────────────

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
    // Find oldest active voice
    int oldest = 0;
    for (int i = 1; i < kNumVoices; i++)
        if (mVoices[i].startSample < mVoices[oldest].startSample)
            oldest = i;
    return oldest;
}

void COMANCHE::noteOn(int note, float vel)
{
    // Find free voice or steal oldest
    int vi = -1;
    for (int i = 0; i < kNumVoices; i++)
        if (!mVoices[i].active) { vi = i; break; }
    if (vi == -1) vi = stealVoice();

    auto& v = mVoices[vi];
    v.active      = true;
    v.inRelease   = false;
    v.noteNum     = note;
    v.srcPos      = 0.0;
    v.pitchRatio  = std::pow(2.0, (note-60)/12.0) * mSampleSR / GetSampleRate();
    v.level       = vel;
    v.startSample = mVoiceTimer;
    v.relTotal    = 0.08f * (float)GetSampleRate();
    v.relLeft     = 0.0f;
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

// ─── Editor callbacks ────────────────────────────────────────────────────────

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

#endif // IPLUG_EDITOR

// ─── Sample + preset helpers ─────────────────────────────────────────────────

void COMANCHE::loadSample(int idx)
{
    std::string path = mLibrary.getSamplePath(idx);
    if (path.empty()) return;

    std::vector<float> left, right;
    int numCh; double sr;
    if (!mLibrary.loadSample(path, left, right, numCh, sr)) return;

    // Swap sample data atomically-ish (audio thread will notice on next block)
    mSampleL   = std::move(left);
    mSampleR   = std::move(right);
    mSampleLen = (int)mSampleL.size();
    mSampleSR  = sr;
    mSelectedIdx = idx;

    // All voices off
    for (auto& v : mVoices) v.active = false;

    // Auto-load preset JSON if present
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

void COMANCHE::deletePreset() { mPresets.deletePreset(); }
