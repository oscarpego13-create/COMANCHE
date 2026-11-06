#include "COMANCHE.h"
#include "Trace.h"
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
    // kDelaySyncMode, kDelayTimeMs(off-pitch), kDelayFeedback(off), kDelayMix
    0.0f,  0.0f,  0.0f,  0.27f,
    // kDelayLowcut, kDelayHighcut(off), kChorusAmount, kHpFreq(off-filter)
    0.13f, 0.0f,  0.21f, 0.0f,
    // kLpFreq(off-filter), kOutputVol(off), kMacro
    0.0f,  0.0f,  0.0f
};

// ─── Constructor ─────────────────────────────────────────────────────────────

COMANCHE::COMANCHE(const InstanceInfo& info)
  : Plugin(info, MakeConfig(kNumParams, 1))
{
    ctrace("---- COMANCHE ctor begin (instance %p)", (void*)this);
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

    ctrace("ctor: params ready, loadPersistentState");
    loadPersistentState();
    ctrace("ctor: loadPersistentState done (%d samples)", (int)mLibrary.getSamplePaths().size());

#if IPLUG_EDITOR
    // Report the scaled window size from the start (same int maths as IGraphics::WindowWidth()).
    // Otherwise opening the editor asks the host to resize the view from inside attached(),
    // which crashes FL Studio.
    SetEditorSize(static_cast<int>(static_cast<float>(PLUG_WIDTH) * UI_SCALE),
                  static_cast<int>(static_cast<float>(PLUG_HEIGHT) * UI_SCALE));
    mMakeGraphicsFunc = [&]() {
        ctrace("MakeGraphics");
        return MakeGraphics(*this, PLUG_WIDTH, PLUG_HEIGHT, PLUG_FPS,
                            GetScaleForScreen(PLUG_WIDTH, PLUG_HEIGHT) * UI_SCALE);
    };

    // Layout = the "Comanche UI Lab" design (v0.1.3): effects in one row, OUTPUT top-right,
    // MACRO next to OUTPUT, horse band behind the MACRO, full-height sound panel.
    mLayoutFunc = [&](IGraphics* g) {
        ctrace("layout begin %dx%d scale %.2f", g->Width(), g->Height(), g->GetDrawScale());
        g->LoadFont("RobotoMono", ROBOTO_MONO_FN);
        g->EnableMouseOver(true);
        g->AttachPanelBackground(CT::bgPrimary);

        const IRECT full = g->GetBounds();
        const float W = full.W(), H = full.H();
        const float hdrB = 32.0f, gap = 4.0f;

        // ── Sound panel (full height): folder button + save + reset + sample list ──
        const float panelW = 190.0f;
        const IRECT lp(0, 0, panelW, H);
        g->AttachControl(new IPanelControl(lp, CT::bgPanel));

        const float bT = 4.0f, bB = 30.0f, fbR = 116.0f;
        g->AttachControl(new ButtonControl(
            IRECT(4, bT, fbR, bB),
            "Buscar librerias",
            [this](IGraphics* gr) {
                WDL_String dir;
                gr->PromptForDirectory(dir, [this](const WDL_String& /*name*/, const WDL_String& path) {
                    if (path.GetLength() > 0) {
                        mFolderPath = path.Get();
                        { mLibrary.setFolder(mFolderPath); startClassification(); }
                        savePersistentState();
                        markStateDirty();
                        if (GetUI()) GetUI()->SetAllControlsDirty();
                    }
                });
            }));
        g->AttachControl(new SaveIconButton(IRECT(fbR+6, bT, fbR+32, bB), [this]() { savePreset(); }));
        g->AttachControl(new ResetIconButton(IRECT(fbR+38, bT, fbR+64, bB), [this]() { resetPreset(); }));

        // Type filter: TODO · BASS · LEAD / PAD · KEYS
        g->AttachControl(new TypeFilterBar(IRECT(4, bB+4, panelW-4, bB+20), mTypeFilter,
            [this]() { if (GetUI()) GetUI()->SetAllControlsDirty(); }));

        auto* sampleList = new SampleListControl(IRECT(2, bB+24, panelW-2, H-2), mLibrary.getSampleNames(), mSelectedIdx,
            [this](int i) { return soundTypeOf(i); }, mTypeFilter, mTypesVersion,
            // OnDrop
            [this](const std::string& filePath) {
                size_t slash = filePath.find_last_of("/\\");
                if (slash == std::string::npos) return;
                mFolderPath = filePath.substr(0, slash);
                { mLibrary.setFolder(mFolderPath); startClassification(); }
                savePersistentState();
                markStateDirty();
                for (int i = 0; i < (int)mLibrary.getSampleNames().size(); i++) {
                    if (mLibrary.getSamplePath(i) == filePath) { loadSample(i); break; }
                }
                if (GetUI()) GetUI()->SetAllControlsDirty();
            },
            // OnRemove (right-click → "Eliminar Sonido")
            [this](int idx) { removeFromLibrary(idx); },
            // OnResetPreset (right-click → "Eliminar Preset")
            [this](int idx) { resetPresetForSample(idx); }
        );
        g->AttachControl(sampleList);

        // Up / down arrows step through the library. A press can reach us twice (from the view and
        // forwarded by the host), so one within 5 ms of the last is the same press.
        g->SetKeyHandlerFunc([this, sampleList](const IKeyPress& key, bool isUp) {
            if (key.VK != kVK_UP && key.VK != kVK_DOWN) return false;
            const auto now = std::chrono::steady_clock::now();
            if (!isUp && now - mLastArrowKey > std::chrono::milliseconds(5)) {
                mLastArrowKey = now;
                sampleList->moveSelection(key.VK == kVK_DOWN ? 1 : -1);
            }
            return true;
        });

        // ── Header: signature top right, settings gear just left of it ───────────
        SettingsPanel* settings = nullptr;
        {
            const IText sigText(8.0f, CT::textInk, "RobotoMono", EAlign::Far, EVAlign::Middle);
            const char* sig = "@laclave.pro v0.1.6";
            g->AttachControl(new ITextControl(IRECT(W-260, 0, W-12, hdrB), sig, sigText));
            IRECT tb;
            const float tw = g->MeasureText(sigText, sig, tb);
            const float gx = W - 12.0f - tw - 8.0f, gs = 12.0f;
            // Panel opens to the left of the gear (right edge just left of it) so it never leaves the window;
            // attached last, after the layout, so it draws on top
            settings = new SettingsPanel(IRECT(gx-gs-4-SettingsPanel::kW, 4, gx-gs-4, 4+SettingsPanel::kH), mMono, mGlideMs, mVelDynamics,
                SettingsPanel::LoopZone{
                    [this] { return mLoopStartN.load(); },
                    [this] { return mLoopEndN.load(); },
                    [this] { return mSampleSR > 0.0 ? (float)(mSampleLen / mSampleSR) : 0.0f; },
                    [this] { return mLoopManual.load(); },
                    [this](float s, float e, bool final) { setLoopRegion(s, e, final); },
                    [this] { resetLoopRegion(); } },
                [this] { return mLoopEnabled.load(); },
                [this](bool on) { setLoopEnabled(on); },
                [this] { markStateDirty(); });
            g->AttachControl(new SettingsGear(IRECT(gx-gs, hdrB*0.5f-gs*0.5f, gx, hdrB*0.5f+gs*0.5f),
                [settings] { settings->Toggle(); }));
        }

        // ── Right side geometry ────────────────────────────────────────────────
        const float rL = panelW, rR = W, rT = hdrB + 2.0f;
        const float r1T = rT + 173.0f, bH = 110.0f, boxW = 90.0f;
        const float kw = 58.0f, kh = kw * 90.0f / 70.0f;          // every knob but the MACRO
        const float dlW = 8 + kw + 8 + kw + 16 + kw + 4 + 36 + 8;  // DELAY box fits its content
        const float outW = 112.0f;
        const IRECT boxReverb(rL+gap,          r1T, rL+gap+boxW,          r1T+bH);
        const IRECT boxDist  (boxReverb.R+gap, r1T, boxReverb.R+gap+boxW, r1T+bH);
        const IRECT boxDelay (boxDist.R+gap,   r1T, boxDist.R+gap+dlW,    r1T+bH);
        const IRECT boxChorus(boxDelay.R+gap,  r1T, boxDelay.R+gap+boxW,  r1T+bH);
        const IRECT boxOutput(rR-gap-outW, rT+gap, rR-gap, r1T-gap);

        // MACRO sits just left of OUTPUT; the horse band runs from the sound panel to OUTPUT, behind it
        const float mW = 121.0f, mH = mW * 130.0f / 108.0f, mX = boxOutput.L - 4.0f - mW*0.5f;
        const IRECT band(rL+1, rT+gap, boxOutput.L-gap, r1T-gap);
        const float hR = mX - mW*0.5f - 4.0f, hL = std::max(band.L + 6.0f, hR - 208.0f);
        // Sprite cells were baked at 202x135 (horse scale 202/180), bottom-aligned in the band
        const float hx = hL + ((hR - hL) - HORSE_FRAME_W) * 0.5f;
        const IRECT horse(hx, band.B - 2.0f - HORSE_FRAME_H, hx + HORSE_FRAME_W, band.B - 2.0f);
        ctrace("layout: loading horse bitmap");
        g->AttachControl(new HorseScene(band, horse, g->LoadBitmap(HORSE_FN), mMacroShown));
        ctrace("layout: horse ok");

        // COMANCHE logo centred over the horse (artwork from the pack cover)
        {
            const float lw = 268.0f, lh = 21.0f;
            const float lx = std::clamp(horse.MW() - lw*0.5f, band.L + 4.0f, band.R - 4.0f - lw), ly = band.T - 26.0f;
            g->AttachControl(new LogoControl(IRECT(lx, ly, lx+lw, ly+lh), g->LoadBitmap(TITLE_FN)));
        }

        // Right click on the MACRO opens its curve window, beside it so the knob stays in view
        const float curveR = mX - mW*0.5f - 6.0f;
        auto* macroCurve = new MacroCurvePanel(IRECT(std::max(panelW + 4.0f, curveR - MacroCurvePanel::kW), 6.0f, curveR, 6.0f + MacroCurvePanel::kH),
            mMacroCurveOn, mMacroShape, mMacroRate, mMacroFreeMs, mMacroPhase, [this] { markStateDirty(); });
        auto* macroKnob = new ComanacheKnob(IRECT(mX - mW*0.5f, rT+8, mX + mW*0.5f, rT+8+mH),
                                            kMacro, "MACRO", CT::knobGold, true);
        macroKnob->setMacroShown(&mMacroShown);
        g->AttachControl(macroKnob);
        g->AttachControl(new MacroClickLayer(macroKnob->GetRECT(), macroKnob, [macroCurve] { macroCurve->Toggle(); }));

        // ── Effect boxes ──────────────────────────────────────────────────────
        g->AttachControl(new IPanelControl(boxReverb, CT::boxReverb));
        g->AttachControl(new IPanelControl(boxDist,   CT::boxDist));
        g->AttachControl(new IPanelControl(boxDelay,  CT::boxDelay));
        g->AttachControl(new IPanelControl(boxChorus, CT::boxChorus));
        g->AttachControl(new IPanelControl(boxOutput, CT::boxOutput));

        auto addTitle = [&](const IRECT& box, const char* label) {
            g->AttachControl(new ITextControl(
                IRECT(box.L+6, box.T+3, box.R-6, box.T+16), label,
                IText(9.0f, CT::textInk, "RobotoMono", EAlign::Near, EVAlign::Middle)));
        };
        addTitle(boxReverb, "REVERB");
        addTitle(boxDist,   "DIST");
        addTitle(boxDelay,  "DELAY");
        addTitle(boxChorus, "CHORUS");
        addTitle(boxOutput, "OUTPUT");

        auto addKnob = [&](float x, float y, int param, const char* lbl, IColor fill) {
            const IRECT r(x, y, x+kw, y+kh);
            // A label sits below the value, past the knob's own area: grow the control so it isn't clipped
            const float labelBottom = 4.0f + 0.7f*kw + UI::kLabelGap + 14.0f;
            const IRECT drawR = (lbl && lbl[0]) ? IRECT(r.L, r.T, r.R, r.T + std::max(kh, labelBottom)) : r;
            auto* knob = new ComanacheKnob(drawR, param, lbl, fill, false, &mMacroLink[param], mParamMod);
            knob->setMacroShown(&mMacroShown);
            g->AttachControl(knob);
            g->AttachControl(new MacroLinkButton(IRECT(r.R-10, r.T, r.R, r.T+10), mMacroLink[param],
                                                 [this]() { markStateDirty(); }));
        };
        // Selector row (decay, DIST mode, TEMPO): centred on the knob label line
        const float kY = r1T + 18.0f;
        const float selY = kY + 0.7f*kw + UI::kLabelGap + 2.0f + 1.0f;

        // REVERB: knob + decay value
        {
            addKnob(boxReverb.MW() - kw*0.5f, kY, kReverbAmount, "", CT::knobBlue);
            const float dvW = 60, dvH = 18, dvX = boxReverb.MW() - dvW*0.5f - 8.0f;
            g->AttachControl(new TinyDragValue(IRECT(dvX, selY, dvX+dvW, selY+dvH), kReverbDecay));
            g->AttachControl(new MacroLinkButton(IRECT(dvX+dvW+3, selY+4, dvX+dvW+13, selY+14),
                                                 mMacroLink[kReverbDecay], [this]() { markStateDirty(); }));
        }
        // DIST: knob + one button cycling CLIP → TAPE → TUBE
        {
            addKnob(boxDist.MW() - kw*0.5f, kY, kDistAmount, "", CT::knobGold);
            g->AttachControl(new CycleModeButton(IRECT(boxDist.MW()-28, selY, boxDist.MW()+28, selY+18),
                                                 kDistMode, "CLIP", "TAPE", "TUBE"));
        }
        // DELAY: Cantidad · TIME (+ TEMPO) · FEEDBK · FILTER
        {
            const float xMix = boxDelay.L + 8.0f, xTime = xMix + kw + 16.0f, xFb = xTime + kw + 8.0f;
            addKnob(xMix, kY, kDelayMix, "", CT::knobBlue);
            g->AttachControl(new DelayTimeKnob(IRECT(xTime, kY, xTime+kw, kY+kh), CT::knobGrey));
            addKnob(xFb, kY, kDelayFeedback, "FEEDBK", CT::knobGrey);
            g->AttachControl(new TempoToggleButton(IRECT(xTime, selY, xTime+kw, selY+18.0f), kDelaySyncMode));

            const float fX = xFb + kw + 4.0f, fW = 36.0f, fY = boxDelay.T + 18.0f, fH = bH - 28.0f;
            g->AttachControl(new BandFilterSlider(IRECT(fX, fY, fX+fW, fY+fH), kDelayLowcut, kDelayHighcut));
            g->AttachControl(new ITextControl(IRECT(fX-2, boxDelay.T+3, fX+fW+2, boxDelay.T+16), "FILTER",
                IText(9.0f, CT::textInk, "RobotoMono", EAlign::Center, EVAlign::Middle)));
        }
        // CHORUS
        addKnob(boxChorus.MW() - kw*0.5f, kY, kChorusAmount, "", CT::knobBlue);

        // OUTPUT: VU + volume fader, HP/LP band filter
        {
            const float iL = boxOutput.L + 8.0f, iT = boxOutput.T + 18.0f, iB = boxOutput.B - 14.0f;
            g->AttachControl(new VolumeSlider(IRECT(iL, iT, iL+46.0f, iB), kOutputVol, &mVuPeakL, &mVuPeakR));
            const float fX = iL + 56.0f;
            g->AttachControl(new BandFilterSlider(IRECT(fX, iT, fX+36.0f, iB), kHpFreq, kLpFreq, 20.0f, 20000.0f));
            g->AttachControl(new ITextControl(IRECT(fX-2, boxOutput.T+3, fX+38, boxOutput.T+16), "FILTER",
                IText(9.0f, CT::textInk, "RobotoMono", EAlign::Center, EVAlign::Middle)));
        }

        g->AttachControl(settings);
        g->AttachControl(macroCurve);
        // iPlug2's dirty-rect merging cuts the horse band down to one side of a panel crossing it, and
        // everything past the panel (the MACRO) stops redrawing. Strict drawing skips that merge; it
        // repaints more, so it is only on while a panel is open.
        auto panelsChanged = [g, settings, macroCurve] { g->SetStrictDrawing(!settings->IsHidden() || !macroCurve->IsHidden()); };
        settings->setOnVisibility(panelsChanged);
        macroCurve->setOnVisibility(panelsChanged);
        ctrace("layout end (%d controls)", g->NControls());
    };
#endif
}

// ─── DSP ─────────────────────────────────────────────────────────────────────

#if IPLUG_DSP

void COMANCHE::OnReset()
{
    mNumHeld = 0;
    ctrace("OnReset sr=%.0f block=%d", GetSampleRate(), GetBlockSize());
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
    if (mTraceBlocks < 3) { mTraceBlocks++; ctrace("ProcessBlock #%d nFrames=%d sr=%.0f", mTraceBlocks, nFrames, GetSampleRate()); }
    const int n = std::min(nFrames, kMaxBlock);

    std::fill(mTmpL, mTmpL+n, 0.0f);
    std::fill(mTmpR, mTmpR+n, 0.0f);

    // Never block the audio thread: if a sample is being swapped in, skip the voices this block
    std::unique_lock<std::mutex> sampleLock(mSampleMutex, std::try_to_lock);
    const bool haveSample = sampleLock.owns_lock() && mSampleLen > 0;

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

        if (haveSample) {
            const std::vector<float>& srcL = mLoopActive ? mLoopL : mSampleL;
            const std::vector<float>& srcR = mLoopActive ? mLoopR : mSampleR;
            for (auto& v : mVoices) {
                if (!v.active) continue;
                // Looping types (Bass, Lead / Pad) wrap inside the crossfaded loop until released
                if (mLoopActive && v.srcPos >= mLoopEnd) v.srcPos -= (double)(mLoopEnd - mLoopStart);
                if (v.srcPos >= mSampleLen) { v.active=false; continue; }
                if (v.glideStep != 0.0) {   // portamento: constant time, linear in semitones
                    v.semis += v.glideStep;
                    if ((v.glideStep > 0.0) == (v.semis >= v.targetSemis)) { v.semis = v.targetSemis; v.glideStep = 0.0; }
                    v.pitchRatio = ratioFor(v.semis);
                }
                float env = v.level;
                if (v.inRelease) {
                    if (v.relLeft <= 0.0f) { v.active=false; continue; }
                    env *= v.relLeft / v.relTotal;
                    v.relLeft -= 1.0f;
                }
                const int wrapAt = mLoopActive ? mLoopEnd : -1;
                float outL = lerp(srcL, v.srcPos, wrapAt, mLoopStart);
                float outR = lerp(srcR, v.srcPos, wrapAt, mLoopStart);
                if (mLoopFadeLeft > 0 && v.fadePos >= 0.0 && mLoopActive) {
                    // Loop moved under this voice: fade from what it was playing into the new position
                    const float w = 1.0f - (float)mLoopFadeLeft / (float)mLoopFadeTotal;
                    if (v.fadePos >= mPrevLoopEnd) v.fadePos -= (double)(mPrevLoopEnd - mPrevLoopStart);
                    outL = lerp(mPrevLoopL, v.fadePos, mPrevLoopEnd, mPrevLoopStart) * (1.0f - w) + outL * w;
                    outR = lerp(mPrevLoopR, v.fadePos, mPrevLoopEnd, mPrevLoopStart) * (1.0f - w) + outR * w;
                    v.fadePos += v.pitchRatio;
                }
                mTmpL[s] += outL * env;
                mTmpR[s] += outR * env;
                v.srcPos  += v.pitchRatio;
            }
            if (mLoopFadeLeft > 0) mLoopFadeLeft--;
        }
        mVoiceTimer++;
    }
    if (sampleLock.owns_lock()) sampleLock.unlock();

    const bool macroOnCurve = mMacroCurveOn.load();
    const float macroRaw = macroOnCurve ? macroCurveValue(n) : (float)GetParam(kMacro)->GetNormalized();
    // Smooth macro before applying to DSP — prevents zipper noise and pitch jumps
    static constexpr float kTwoPi = 6.28318530718f;
    const float blockDur = (float)n / (float)GetSampleRate();
    {
        // 50 ms tau by hand; a curve keeps its shape (the effects glide the rest)
        const float a = 1.0f - std::exp(-blockDur / (macroOnCurve ? 0.004f : 0.050f));
        mSmoothMacro += a * (macroRaw - mSmoothMacro);
    }
    const float macro = mSmoothMacro;
    mMacroShown = macroOnCurve ? macro : -1.0f;

    // Update slow per-param LFO modulation (amplitude = macro * 0.07 normalized range)
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
    ep.keepRunning   = macroOnCurve;
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

// Inside a loop the sample after the last one is the loop start, not what follows the loop end
// Where the curve is at the end of this block. In sync with the transport running, the position comes
// from the host so the MACRO stays on the grid through jumps and loops; otherwise it runs on its own,
// at the project tempo or the free time in ms.
float COMANCHE::macroCurveValue(int nFrames)
{
    const int rate = std::clamp(mMacroRate.load(), 0, MacroCurves::kNumRates - 1);
    const double sr = GetSampleRate(), blockSec = nFrames / sr;
    const double bpm = GetTempo() > 0.0 ? GetTempo() : 120.0;
    if (rate == MacroCurves::kFreeRate) {
        mMacroPhaseRun += blockSec * 1000.0 / std::max(1.0f, mMacroFreeMs.load());
    } else {
        const double beats = MacroCurves::rateBeats(rate), blockBeats = blockSec * bpm / 60.0;
        if (GetTransportIsRunning() && GetPPQPos() >= 0.0) mMacroPhaseRun = (GetPPQPos() + blockBeats) / beats;
        else mMacroPhaseRun += blockBeats / beats;
    }
    mMacroPhaseRun -= std::floor(mMacroPhaseRun);
    mMacroPhase = (float)mMacroPhaseRun;
    return MacroCurves::value(mMacroShape.load(), (float)mMacroPhaseRun);
}

float COMANCHE::lerp(const std::vector<float>& b, double pos, int loopEnd, int loopStart)
{
    int i = (int)pos;
    float f = (float)(pos - i);
    int j = i + 1;
    if (loopEnd > 0 && j >= loopEnd) j = loopStart + (j - loopEnd);
    float s0 = (i < (int)b.size()) ? b[i] : 0.0f;
    float s1 = (j < (int)b.size()) ? b[j] : 0.0f;
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

double COMANCHE::ratioFor(double semis) const
{
    return std::pow(2.0, semis / 12.0) * mSampleSR / GetSampleRate();
}

void COMANCHE::glideVoice(Voice& v, int note)
{
    v.noteNum     = note;
    v.targetSemis = note - 60;
    const double samples = std::max(1.0, mGlideMs.load() * 0.001 * GetSampleRate());
    v.glideStep   = (v.targetSemis - v.semis) / samples;
}

void COMANCHE::noteOn(int note, float vel)
{
    // Track held keys (newest last) for mono glide
    int k = 0;
    for (int i = 0; i < mNumHeld; i++) if (mHeld[i] != note) mHeld[k++] = mHeld[i];
    mNumHeld = k;
    if (mNumHeld < (int)mHeld.size()) mHeld[mNumHeld++] = note;

    // Mono + loop with glide: a new key while one is held slides the sounding voice to it (legato)
    if (mMono && mLoopActive && mGlideMs.load() > 0.5f) {
        for (auto& v : mVoices)
            if (v.active && !v.inRelease) { glideVoice(v, note); return; }
    }
    // Mono: a new note fades out whatever is playing (5 ms) before starting
    if (mMono) {
        for (auto& v : mVoices)
            if (v.active && !v.inRelease) {
                v.inRelease = true;
                v.relTotal = v.relLeft = 0.005f * (float)GetSampleRate();
            }
    }
    int vi = -1;
    for (int i = 0; i < kNumVoices; i++)
        if (!mVoices[i].active) { vi=i; break; }
    if (vi == -1) vi = stealVoice();

    auto& v = mVoices[vi];
    v.active     = true;
    v.inRelease  = false;
    v.noteNum    = note;
    v.srcPos     = 0.0;
    v.fadePos    = -1.0;
    v.semis      = v.targetSemis = note - 60;
    v.glideStep  = 0.0;
    v.pitchRatio = ratioFor(v.semis);
    // Velocity -> dynamics: at 100 % the key velocity sets the level, at 0 % every note plays at full level
    v.level      = 1.0f - mVelDynamics.load() * (1.0f - vel);
    v.startSample= mVoiceTimer;
    v.relTotal   = 0.08f * (float)GetSampleRate();
    v.relLeft    = 0.0f;
}

void COMANCHE::noteOff(int note)
{
    int k = 0;
    for (int i = 0; i < mNumHeld; i++) if (mHeld[i] != note) mHeld[k++] = mHeld[i];
    mNumHeld = k;

    // Mono glide: releasing the sounding key while others are held slides back to the newest of them
    if (mMono && mLoopActive && mGlideMs.load() > 0.5f && mNumHeld > 0) {
        for (auto& v : mVoices)
            if (v.active && !v.inRelease && v.noteNum == note) { glideVoice(v, mHeld[mNumHeld - 1]); return; }
    }
    for (auto& v : mVoices)
        if (v.active && !v.inRelease && v.noteNum == note) {
            v.inRelease = true;
            v.relLeft   = v.relTotal;
        }
}

// ─── State serialization ─────────────────────────────────────────────────────
// Format v7: [magic(int)] [folder(str)] [samplePath(str)] [macroLink(int*kNumParams)] [loop(int)] [mono(int)] [glideMs(float)] [velDynamics(float)]
//            [loopManual(int)] [loopStart(float)] [loopEnd(float)] [curveOn(int)] [curveShape(int)] [curveRate(int)] [curveFreeMs(float)] [params]
// Format v6: same without the MACRO curve
// Format v5: same without the manual loop zone
// Format v4: same without velDynamics
// Format v3: same without glide
// Format v2: same without loop/mono
// Format v1 (old, broken): [folder(str)] [params]  — sample was never saved
static constexpr int kSerializeMagicV2 = 0x434F4D32; // 'COM2'
static constexpr int kSerializeMagicV3 = 0x434F4D33; // 'COM3'
static constexpr int kSerializeMagicV4 = 0x434F4D34; // 'COM4'
static constexpr int kSerializeMagicV5 = 0x434F4D35; // 'COM5'
static constexpr int kSerializeMagicV6 = 0x434F4D36; // 'COM6'
static constexpr int kSerializeMagic   = 0x434F4D37; // 'COM7'

bool COMANCHE::SerializeState(IByteChunk& chunk) const
{
    ctrace("SerializeState");
    chunk.Put(&kSerializeMagic);
    WDL_String folder(mFolderPath.c_str());
    chunk.PutStr(folder.Get());
    WDL_String sp(mSamplePath.c_str());
    chunk.PutStr(sp.Get());
    for (int i = 0; i < kNumParams; i++) chunk.Put(&mMacroLink[i]);
    const int loop = mLoopEnabled ? 1 : 0, mono = mMono ? 1 : 0;
    chunk.Put(&loop);
    chunk.Put(&mono);
    const float glide = mGlideMs.load(), dyn = mVelDynamics.load();
    chunk.Put(&glide);
    chunk.Put(&dyn);
    const int manual = mLoopManual ? 1 : 0;
    const float zoneStart = mLoopStartN.load(), zoneEnd = mLoopEndN.load();
    chunk.Put(&manual);
    chunk.Put(&zoneStart);
    chunk.Put(&zoneEnd);
    const int curveOn = mMacroCurveOn ? 1 : 0, curveShape = mMacroShape.load(), curveRate = mMacroRate.load();
    const float curveMs = mMacroFreeMs.load();
    chunk.Put(&curveOn);
    chunk.Put(&curveShape);
    chunk.Put(&curveRate);
    chunk.Put(&curveMs);
    return SerializeParams(chunk);
}

int COMANCHE::UnserializeState(const IByteChunk& chunk, int pos)
{
    ctrace("UnserializeState size=%d pos=%d", chunk.Size(), pos);
    // IByteChunk::GetStr accepts any length and hands back a position past the data. Damaged state
    // must not bring the host down: check first, and leave the plugin as it is if it doesn't fit.
    auto stringFits = [&chunk](int at) {
        int len = 0;
        const int start = chunk.Get(&len, at);
        return start > at && len >= 0 && len <= chunk.Size() - start;
    };
    const int startPos = pos;
    int magic = 0;
    int afterMagic = chunk.Get(&magic, pos);

    if (afterMagic > pos && (magic == kSerializeMagic || magic == kSerializeMagicV6 || magic == kSerializeMagicV5 || magic == kSerializeMagicV4 || magic == kSerializeMagicV3 || magic == kSerializeMagicV2)) {
        // v2 onwards: full restore
        pos = afterMagic;
        if (!stringFits(pos)) return startPos;
        WDL_String folder;
        pos = chunk.GetStr(folder, pos);
        if (!stringFits(pos)) return startPos;
        mFolderPath = folder.Get();
        if (!mFolderPath.empty()) { mLibrary.setFolder(mFolderPath); startClassification(); }

        WDL_String sp;
        pos = chunk.GetStr(sp, pos);
        // Keep the path even if the file is missing right now, so re-saving the
        // project doesn't drop the reference (e.g. external drive not mounted)
        mSamplePath = sp.Get();
        mSelectedIdx = -1;

        if (mSamplePath.empty() || !loadSampleData(mSamplePath)) {
            std::lock_guard<std::mutex> lock(mSampleMutex);
            mSampleL.clear(); mSampleR.clear(); mSampleLen = 0;
        } else {
            const auto& paths = mLibrary.getSamplePaths();
            for (int i = 0; i < (int)paths.size(); i++) {
                if (paths[i] == mSamplePath) { mSelectedIdx = i; break; }
            }
        }
        // Point the preset manager at the sample so the save icon works after reopening.
        // Only reads the JSON — params come from the project chunk below, not the preset file.
        if (!mSamplePath.empty()) mPresets.onSampleLoaded(mSamplePath);

        for (int i = 0; i < kNumParams; i++) pos = chunk.Get(&mMacroLink[i], pos);
        const bool atLeastV6 = magic == kSerializeMagic || magic == kSerializeMagicV6;
        const bool atLeastV5 = atLeastV6 || magic == kSerializeMagicV5;
        const bool atLeastV4 = atLeastV5 || magic == kSerializeMagicV4;
        if (atLeastV4 || magic == kSerializeMagicV3) {
            int loop = 1, mono = 0;
            pos = chunk.Get(&loop, pos);
            pos = chunk.Get(&mono, pos);
            mMono = mono != 0;
            setLoopEnabled(loop != 0);
        }
        if (atLeastV4) {
            float glide = 80.0f;
            pos = chunk.Get(&glide, pos);
            mGlideMs = std::clamp(glide, 0.0f, SettingsPanel::kGlideMaxMs);
        }
        if (atLeastV5) {
            float dyn = 1.0f;
            pos = chunk.Get(&dyn, pos);
            mVelDynamics = std::clamp(dyn, 0.0f, 1.0f);
        }
        if (atLeastV6) {
            int manual = 0; float zoneStart = 0.5f, zoneEnd = 1.0f;
            pos = chunk.Get(&manual, pos);
            pos = chunk.Get(&zoneStart, pos);
            pos = chunk.Get(&zoneEnd, pos);
            if (manual) setLoopRegion(zoneStart, zoneEnd, true);
        }
        if (magic == kSerializeMagic) {
            int curveOn = 0, curveShape = (int)kDefaultCurveShape, curveRate = (int)kDefaultCurveRate; float curveMs = kDefaultCurveMs;
            pos = chunk.Get(&curveOn, pos);
            pos = chunk.Get(&curveShape, pos);
            pos = chunk.Get(&curveRate, pos);
            pos = chunk.Get(&curveMs, pos);
            mMacroCurveOn = curveOn != 0;
            mMacroShape = std::clamp(curveShape, 0, MacroCurves::kNumShapes - 1);
            mMacroRate  = std::clamp(curveRate, 0, MacroCurves::kNumRates - 1);
            mMacroFreeMs = std::clamp(curveMs, MacroCurves::kFreeMinMs, MacroCurves::kFreeMaxMs);
        }
    } else {
        // v1 (old saves): folder + params only, no sample
        if (!stringFits(pos)) return startPos;
        mSamplePath.clear();
        mSelectedIdx = -1;
        WDL_String folder;
        pos = chunk.GetStr(folder, pos);
        mFolderPath = folder.Get();
        if (!mFolderPath.empty()) { mLibrary.setFolder(mFolderPath); startClassification(); }
    }

    ctrace("UnserializeState: params at %d", pos);
    return UnserializeParams(chunk, pos);
}

#endif // IPLUG_DSP

// ─── Editor callbacks ─────────────────────────────────────────────────────────

#if IPLUG_EDITOR

void COMANCHE::OnUIOpen()
{
    ctrace("OnUIOpen");
    // Sync control values with params — controls cache their value for drag start
    SendCurrentParamValuesFromDelegate();
    if (GetUI()) GetUI()->SetAllControlsDirty();
}

void COMANCHE::OnRestoreState()
{
    ctrace("OnRestoreState");
    Plugin::OnRestoreState();
    // Sample list + macro links aren't params, redraw them too
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

bool COMANCHE::loadSampleData(const std::string& path)
{
    ctrace("loadSampleData %s", path.c_str());
    std::vector<float> left, right;
    int numCh; double sr;
    if (!mLibrary.loadSample(path, left, right, numCh, sr)) return false;

    // Type: cached from the library analysis, else name, else this audio
    SoundType type = SoundType::Pending;
    {
        std::lock_guard<std::mutex> lock(mTypesMutex);
        auto it = mTypes.find(path);
        if (it != mTypes.end()) type = it->second;
    }
    if (type == SoundType::Pending) {
        type = SoundClassifier::fromName(path);
        if (type == SoundType::Pending) type = SoundClassifier::fromAudio(left, right, sr);
        std::lock_guard<std::mutex> lock(mTypesMutex);
        mTypes[path] = type;
        mTypesVersion++;
    }
    // Loop copy: level evened out inside the loop and its end crossfaded into the audio just before
    // its start, so jumping from loopEnd back to loopStart is seamless
    int ls = 0, le = 0;
    std::vector<float> loopL, loopR;
    const bool hasLoop = SoundClassifier::findLoop(left, right, sr, ls, le);
    if (hasLoop) SoundClassifier::buildLoop(left, right, sr, ls, le, loopL, loopR);

    std::lock_guard<std::mutex> lock(mSampleMutex);
    mSampleL   = std::move(left);
    mSampleR   = std::move(right);
    mSampleLen = (int)mSampleL.size();
    mSampleSR  = sr;
    mLoopL     = std::move(loopL);
    mLoopR     = std::move(loopR);
    mLoopStart = ls;
    mLoopEnd   = le;
    mHasLoop   = hasLoop;
    mSampleType = type;
    mPrevLoopL.clear(); mPrevLoopR.clear(); mLoopFadeLeft = 0;
    mLoopManual = false;
    mLoopStartN = hasLoop ? (float)ls / (float)mSampleLen : 0.5f;
    mLoopEndN   = hasLoop ? (float)le / (float)mSampleLen : 1.0f;
    updateLoopActive();
    for (auto& v : mVoices) v.active = false;
    return true;
}

// Rebuilds the loop off the audio thread, then swaps it in under the lock. Voices beyond the new end
// jump inside it, and every sounding voice crossfades from its old stream for 20 ms, so moving the
// loop while a note is held doesn't click.
void COMANCHE::rebuildLoop(int ls, int le)
{
    std::vector<float> loopL, loopR;
    SoundClassifier::buildLoop(mSampleL, mSampleR, mSampleSR, ls, le, loopL, loopR);
    {
        std::lock_guard<std::mutex> lock(mSampleMutex);
        mPrevLoopL.swap(mLoopL); mPrevLoopR.swap(mLoopR);
        mPrevLoopStart = mLoopStart; mPrevLoopEnd = mLoopEnd;
        mLoopL.swap(loopL);      mLoopR.swap(loopR);       // loopL/R now hold the older copy, freed unlocked
        const bool canFade = mPrevLoopL.size() == mLoopL.size();
        mLoopFadeTotal = mLoopFadeLeft = canFade ? std::max(1, (int)(0.02 * GetSampleRate())) : 0;
        mLoopStart = ls;
        mLoopEnd   = le;
        mHasLoop   = true;
        updateLoopActive();
        for (auto& v : mVoices) {
            if (!v.active) continue;
            v.fadePos = canFade ? v.srcPos : -1.0;
            if (mLoopActive && v.srcPos >= le) v.srcPos = ls + std::fmod(v.srcPos - ls, (double)(le - ls));
        }
    }
}

// Loop zone chosen by hand, as fractions of the sound. While dragging, the audio is rebuilt at most
// every 30 ms; the last call (final) always applies.
void COMANCHE::setLoopRegion(float startN, float endN, bool final)
{
    mLoopManual = true;
    const double len = mSampleLen, sr = mSampleSR;
    if (len <= 0.0 || sr <= 0.0) { mLoopStartN = startN; mLoopEndN = endN; return; }
    const float minLen = (float)(sr * 0.07 / len);     // alignLoopEnd may take a little off the end
    if (minLen >= 0.98f) return;                        // too short to loop
    startN = std::clamp(startN, 0.01f, 1.0f - minLen);
    endN   = std::clamp(endN, startN + minLen, 1.0f);
    mLoopStartN = startN;
    mLoopEndN   = endN;

    const auto minGap = std::chrono::milliseconds(30);
    const auto since = std::chrono::steady_clock::now() - mLastLoopBuild;
    if (since < minGap) {
        if (!final) return;
        std::this_thread::sleep_for(minGap - since);   // let the running 20 ms fade end: swapping mid-fade would jump
    }
    mLastLoopBuild = std::chrono::steady_clock::now();
    const int ls = (int)(startN * len), le = (int)(endN * len);
    rebuildLoop(ls, SoundClassifier::alignLoopEnd(mSampleL, mSampleR, sr, ls, le));
}

void COMANCHE::resetLoopRegion()
{
    mLoopManual = false;
    int ls = 0, le = 0;
    if (mSampleLen <= 0 || !SoundClassifier::findLoop(mSampleL, mSampleR, mSampleSR, ls, le)) {
        mLoopStartN = 0.5f; mLoopEndN = 1.0f;
        std::lock_guard<std::mutex> lock(mSampleMutex);
        mHasLoop = false;
        updateLoopActive();
        return;
    }
    mLoopStartN = (float)ls / (float)mSampleLen;
    mLoopEndN   = (float)le / (float)mSampleLen;
    rebuildLoop(ls, le);
}

void COMANCHE::updateLoopActive()
{
    mLoopActive = mHasLoop && mLoopEnabled
               && mLoopL.size() == mSampleL.size() && mLoopEnd <= mSampleLen;
}

void COMANCHE::setLoopEnabled(bool on)
{
    mLoopEnabled = on;
    std::lock_guard<std::mutex> lock(mSampleMutex);
    updateLoopActive();
}

// ─── Library classification (background) ─────────────────────────────────────

SoundType COMANCHE::soundTypeOf(int libraryIdx)
{
    const std::string path = mLibrary.getSamplePath(libraryIdx);
    std::lock_guard<std::mutex> lock(mTypesMutex);
    auto it = mTypes.find(path);
    return it != mTypes.end() ? it->second : SoundType::Pending;
}

// Names first (instant), then the audio of the remaining samples one by one on a worker thread.
// Results land in mTypes; the list redraws when mTypesVersion changes.
void COMANCHE::startClassification()
{
    stopClassification();
    std::vector<std::string> todo;
    {
        std::lock_guard<std::mutex> lock(mTypesMutex);
        for (const auto& p : mLibrary.getSamplePaths()) {
            if (mTypes.count(p)) continue;
            const SoundType t = SoundClassifier::fromName(p);
            if (t != SoundType::Pending) mTypes[p] = t;
            else todo.push_back(p);
        }
    }
    mTypesVersion++;
    ctrace("startClassification: %d by name, %d to analyse", (int)mLibrary.getSamplePaths().size() - (int)todo.size(), (int)todo.size());
    if (todo.empty()) return;
    mClassifyStop = false;
    mClassifyThread = std::thread([this, todo = std::move(todo)]() {
        for (const auto& p : todo) {
            if (mClassifyStop) { ctrace("classify thread: stopped"); return; }
            ctrace("classify: %s", p.c_str());
            std::vector<float> l, r; int ch = 0; double sr = 0;
            const SoundType t = mLibrary.loadSample(p, l, r, ch, sr) ? SoundClassifier::fromAudio(l, r, sr)
                                                                      : SoundType::Other;
            {
                std::lock_guard<std::mutex> lock(mTypesMutex);
                mTypes[p] = t;
            }
            mTypesVersion++;
        }
    });
}

void COMANCHE::stopClassification()
{
    mClassifyStop = true;
    if (mClassifyThread.joinable()) mClassifyThread.join();
}

COMANCHE::~COMANCHE()
{
    ctrace("---- COMANCHE dtor (instance %p)", (void*)this);
    stopClassification();
}

void COMANCHE::loadSample(int idx)
{
    std::string path = mLibrary.getSamplePath(idx);
    if (path.empty() || !loadSampleData(path)) return;

    mSamplePath  = path;
    mSelectedIdx = idx;

    // Reset to defaults, then restore saved preset (if any)
    mPresets.onSampleLoaded(path);
    double norm[kNumParams];
    for (int pi = 0; pi < kNumParams; pi++)
        norm[pi] = GetParam(pi)->GetDefault(true);
    for (auto& kv : mPresets.getValues()) {
        for (int pi = 0; pi < kNumParams; pi++) {
            auto* p = GetParam(pi);
            if (std::string(p->GetName()) == kv.first) {
                norm[pi] = p->ToNormalized(kv.second);
                break;
            }
        }
    }
    for (int pi = 0; pi < kNumParams; pi++)
        setParamFromUI(pi, norm[pi]);
    // Loop: on by default for Bass and Lead / Pad, off for the rest, unless this sound's preset says otherwise
    const auto& saved = mPresets.getValues();
    const auto loopIt = saved.find(kPresetLoopKey);
    setLoopEnabled(loopIt != saved.end() ? loopIt->second > 0.5f : SoundClassifier::loops(mSampleType));
    const auto zoneStart = saved.find(kPresetLoopStartKey), zoneEnd = saved.find(kPresetLoopEndKey);
    if (zoneStart != saved.end() && zoneEnd != saved.end()) setLoopRegion(zoneStart->second, zoneEnd->second, true);
    // MACRO curve: from this sound's preset; without one it starts off, like the knobs start at 0
    auto savedOr = [&saved](const char* key, float fallback) {
        const auto it = saved.find(key);
        return it != saved.end() ? it->second : fallback;
    };
    mMacroCurveOn = savedOr(kPresetCurveOnKey, 0.0f) > 0.5f;
    mMacroShape   = std::clamp((int)savedOr(kPresetCurveShapeKey, kDefaultCurveShape), 0, MacroCurves::kNumShapes - 1);
    mMacroRate    = std::clamp((int)savedOr(kPresetCurveRateKey, kDefaultCurveRate), 0, MacroCurves::kNumRates - 1);
    mMacroFreeMs  = std::clamp(savedOr(kPresetCurveMsKey, kDefaultCurveMs), MacroCurves::kFreeMinMs, MacroCurves::kFreeMaxMs);
    markStateDirty();
    if (GetUI()) GetUI()->SetAllControlsDirty();
}

void COMANCHE::savePreset()
{
    std::map<std::string, float> vals;
    for (int i = 0; i < kNumParams; i++) {
        auto* p = GetParam(i);
        vals[p->GetName()] = (float)p->Value();
    }
    vals[kPresetLoopKey] = mLoopEnabled ? 1.0f : 0.0f;
    if (mLoopManual) { vals[kPresetLoopStartKey] = mLoopStartN.load(); vals[kPresetLoopEndKey] = mLoopEndN.load(); }
    vals[kPresetCurveOnKey]    = mMacroCurveOn ? 1.0f : 0.0f;
    vals[kPresetCurveShapeKey] = (float)mMacroShape.load();
    vals[kPresetCurveRateKey]  = (float)mMacroRate.load();
    vals[kPresetCurveMsKey]    = mMacroFreeMs.load();
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
            setParamFromUI(pi, GetParam(pi)->GetDefault(true));
        setLoopEnabled(SoundClassifier::loops(mSampleType));
        resetLoopRegion();
        resetMacroCurve();
        if (GetUI()) GetUI()->SetAllControlsDirty();
    }
}

void COMANCHE::resetMacroCurve()
{
    mMacroCurveOn = false;
    mMacroShape   = (int)kDefaultCurveShape;
    mMacroRate    = (int)kDefaultCurveRate;
    mMacroFreeMs  = kDefaultCurveMs;
}

void COMANCHE::resetPreset()
{
    for (int pi = 0; pi < kNumParams; pi++)
        setParamFromUI(pi, GetParam(pi)->GetDefault(true));
    for (int pi = 0; pi < kNumParams; pi++) mMacroLink[pi] = 0;
    setLoopEnabled(SoundClassifier::loops(mSampleType));
    resetLoopRegion();
    resetMacroCurve();
    markStateDirty();
    if (GetUI()) GetUI()->SetAllControlsDirty();
}

// ─── Host notification ────────────────────────────────────────────────────────
// Setting IParam values directly is invisible to the host: it neither marks the
// project as modified nor updates its own copy of the value. Always go through here.

void COMANCHE::setParamFromUI(int idx, double normalized)
{
    BeginInformHostOfParamChangeFromUI(idx);
    SendParameterValueFromUI(idx, normalized);
    EndInformHostOfParamChangeFromUI(idx);
    SendParameterValueFromDelegate(idx, normalized, true);
}

void COMANCHE::markStateDirty()
{
#if defined VST3_API
    setDirty(true);  // IComponentHandler2 — ignored by hosts that don't support it
#endif
    // Some hosts (Ableton Live) only flag the project as modified on parameter
    // edits, so re-send the macro's current value as a no-op edit.
    setParamFromUI(kMacro, GetParam(kMacro)->GetNormalized());
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
    ctrace("loadPersistentState");
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
        { mLibrary.setFolder(mFolderPath); startClassification(); }
}
