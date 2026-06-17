#pragma once

#include "IPlug_include_in_plug_hdr.h"
#include "IControls.h"
#include "IPlugMidi.h"

#include <vector>
#include <string>
#include <array>
#include <algorithm>
#include <cmath>
#include <functional>

#include "EffectsChain.h"
#include "SampleLibrary.h"
#include "MacroController.h"
#include "PresetManager.h"

using namespace iplug;
using namespace igraphics;

// ─── Parameter enum ──────────────────────────────────────────────────────────
enum EParams {
    kReverbAmount = 0,
    kDistAmount,
    kDistMode,
    kDelaySyncMode,
    kDelayTimeMs,
    kDelayLowcut,
    kDelayHighcut,
    kChorusAmount,
    kHpFreq,
    kLpFreq,
    kMacro,
    kNumParams
};

// ─── Cross-thread message tags ───────────────────────────────────────────────
enum EMsgTags {
    kMsgLoadSample = 0,
};

// ─── Design system colours ───────────────────────────────────────────────────
namespace CT {
    static const IColor bgPrimary  {255,237,234,228};
    static const IColor bgPanel    {255,226,222,215};
    static const IColor fgPrimary  {255, 26, 26, 26};
    static const IColor knobGrey   {255,155,155,142};
    static const IColor knobBlue   {255,139,163,184};
    static const IColor knobGold   {255,184,168,130};
    static const IColor btnActive  {255, 26, 26, 26};
    static const IColor btnInactive{255,184,168,130};
    static const IColor textLight  {255,237,234,228};
}

// ─── Flat knob (same style as NoisePanner's BrutalistKnob) ───────────────────
class ComanacheKnob final : public IControl
{
public:
    ComanacheKnob(const IRECT& b, int param, const char* lbl, IColor fill, bool isMacro=false)
        : IControl(b, param), mLabel(lbl), mFill(fill), mIsMacro(isMacro) {}

    void Draw(IGraphics& g) override {
        const float cx  = mRECT.MW();
        const float kR  = (mIsMacro ? 0.40f : 0.35f) * mRECT.W();
        const float kCy = mRECT.T + kR + 4.0f;
        static const IColor kNeedle(255,26,26,26);
        g.FillCircle(mFill,   cx, kCy, kR);
        g.DrawCircle(kNeedle, cx, kCy, kR-0.5f, nullptr, 1.8f);
        double angle = (-135.0 + GetValue()*270.0) * (M_PI/180.0);
        float  reach = kR - 0.5f;
        g.DrawLine(kNeedle, cx, kCy,
                   cx + (float)(std::sin(angle)*reach),
                   kCy - (float)(std::cos(angle)*reach),
                   nullptr, 2.2f);
        WDL_String vs;
        GetParam()->GetDisplay(vs, false);
        const char* u = GetParam()->GetLabel();
        if (u && u[0]) vs.Append(u);
        IText vt(mIsMacro ? 12.0f : 10.0f, CT::fgPrimary, "RobotoMono", EAlign::Center, EVAlign::Middle);
        g.DrawText(vt, vs.Get(), IRECT(mRECT.L, kCy+kR+2, mRECT.R, kCy+kR+16));
        IText lt(8.0f, CT::fgPrimary, "RobotoMono", EAlign::Center, EVAlign::Middle);
        g.DrawText(lt, mLabel, IRECT(mRECT.L, kCy+kR+16, mRECT.R, kCy+kR+30));
    }

    void OnAttached() override { if (GetParam()) SetValue(GetParam()->GetNormalized()); }
    void OnMouseDown(float, float y, const IMouseMod& m) override { mDY=y; mSV=GetValue(); IControl::OnMouseDown(0,y,m); }
    void OnMouseDblClick(float, float, const IMouseMod&) override { SetValue(GetParam()->GetDefault(true)); SetDirty(true); }
    void OnMouseDrag(float, float y, float, float, const IMouseMod& m) override {
        double s = m.S ? 2000.0 : 200.0;
        SetValue(std::clamp(mSV-(y-mDY)/s, 0.0, 1.0)); SetDirty(true);
    }
    void OnMouseWheel(float, float, const IMouseMod& m, float d) override {
        SetValue(std::clamp(GetValue()+d*(m.S?0.002:0.01),0.0,1.0)); SetDirty(true);
    }
private:
    const char* mLabel; IColor mFill; bool mIsMacro;
    float mDY{0}; double mSV{0};
};

// ─── Mode toggle button (one of an exclusive group, maps to int param) ────────
class ModeButton final : public IControl
{
public:
    ModeButton(const IRECT& b, int param, int modeValue, const char* lbl)
        : IControl(b, param), mMode(modeValue), mLabel(lbl) {}

    void Draw(IGraphics& g) override {
        bool active = ((int)(GetParam()->Value()+0.5) == mMode);
        g.FillRoundRect(active ? CT::btnActive : CT::btnInactive, mRECT, 2.0f);
        IText t(8.0f, active ? CT::textLight : CT::fgPrimary, "RobotoMono", EAlign::Center, EVAlign::Middle);
        g.DrawText(t, mLabel, mRECT);
    }
    void OnMouseDown(float, float, const IMouseMod&) override {
        SetValue(GetParam()->ToNormalized((double)mMode)); SetDirty(true);
    }
private:
    int mMode; const char* mLabel;
};

// ─── Scrollable sample list ───────────────────────────────────────────────────
class SampleListControl final : public IControl
{
public:
    SampleListControl(const IRECT& b, const std::vector<std::string>& names, int& selectedIdx)
        : IControl(b, kNoParameter), mNames(names), mSelected(selectedIdx) {}

    void Draw(IGraphics& g) override {
        g.FillRect(CT::bgPanel, mRECT);
        const float rowH = 22.0f;
        int maxRows = (int)((mRECT.H()) / rowH);
        for (int i = mScroll; i < (int)mNames.size() && (i-mScroll) < maxRows; i++) {
            IRECT row(mRECT.L, mRECT.T + (i-mScroll)*rowH, mRECT.R, mRECT.T + (i-mScroll+1)*rowH);
            bool sel = (i == mSelected);
            if (sel) g.FillRect(CT::fgPrimary, row);
            IText t(10.0f, sel ? CT::textLight : CT::fgPrimary, "RobotoMono", EAlign::Near, EVAlign::Middle);
            IRECT tr(row.L+8, row.T, row.R, row.B);
            g.DrawText(t, mNames[i].c_str(), tr);
        }
    }

    void OnMouseDown(float, float y, const IMouseMod&) override {
        const float rowH = 22.0f;
        int row = mScroll + (int)((y - mRECT.T) / rowH);
        if (row >= 0 && row < (int)mNames.size()) {
            mSelected = row;
            GetDelegate()->SendArbitraryMsgFromUI(kMsgLoadSample, kNoTag, sizeof(int), &mSelected);
            SetDirty(false);
        }
    }
    void OnMouseWheel(float, float, const IMouseMod&, float d) override {
        mScroll = std::clamp(mScroll - (int)d, 0, std::max(0,(int)mNames.size()-1));
        SetDirty(false);
    }

    void refresh() { mScroll = 0; SetDirty(false); }

private:
    const std::vector<std::string>& mNames;
    int& mSelected;
    int mScroll{0};
};

// ─── Simple clickable button (replaces ILambdaControl for mouse handling) ─────
class ButtonControl final : public IControl
{
public:
    using OnClickFn = std::function<void(IGraphics*)>;
    ButtonControl(const IRECT& b, const char* lbl, OnClickFn fn)
        : IControl(b, kNoParameter), mLabel(lbl), mFn(std::move(fn)) {}
    void Draw(IGraphics& g) override {
        g.FillRoundRect(CT::btnInactive, mRECT, 2.0f);
        IText t(9.0f, CT::fgPrimary, "RobotoMono", EAlign::Center, EVAlign::Middle);
        g.DrawText(t, mLabel, mRECT);
    }
    void OnMouseDown(float, float, const IMouseMod&) override {
        if (mFn) mFn(GetUI());
    }
private:
    const char* mLabel;
    OnClickFn mFn;
};

// ─── Main plugin class ────────────────────────────────────────────────────────
class COMANCHE final : public Plugin
{
public:
    COMANCHE(const InstanceInfo& info);

#if IPLUG_DSP
    void ProcessBlock(sample** inputs, sample** outputs, int nFrames) override;
    void ProcessMidiMsg(const IMidiMsg& msg) override;
    void OnReset() override;
    bool SerializeState(IByteChunk& chunk) const override;
    int  UnserializeState(const IByteChunk& chunk, int startPos) override;
#endif
#if IPLUG_EDITOR
    void OnUIOpen() override;
    bool OnMessage(int msgTag, int ctrlTag, int dataSize, const void* pData) override;
#endif

    // Accessible from UI controls
    SampleLibrary mLibrary;
    PresetManager mPresets;
    int mSelectedIdx { -1 };

    void loadSample(int idx);
    void savePreset();
    void deletePreset();

private:
    // ── Voice engine ──
    struct Voice {
        bool   active{false}, inRelease{false};
        int    noteNum{-1}, startSample{0};
        double srcPos{0.0}, pitchRatio{1.0};
        float  level{0.0f}, relLeft{0.0f}, relTotal{0.0f};
    };
    static constexpr int kNumVoices = 8;
    std::array<Voice, kNumVoices> mVoices;
    int mVoiceTimer{0};

    // ── Sample data ──
    std::vector<float> mSampleL, mSampleR;
    int    mSampleLen{0};
    double mSampleSR {44100.0};

    // ── Effects ──
    EffectsChain   mFX;
    MacroController mMacro;

    // ── MIDI ──
    IMidiQueue mMidiQueue;

    // ── State ──
    std::string mFolderPath;

    // ── Temp buffers (avoid heap alloc in ProcessBlock) ──
    static constexpr int kMaxBlock = 4096;
    float mTmpL[kMaxBlock]{}, mTmpR[kMaxBlock]{};

    // ── Voice helpers ──
    void noteOn(int note, float vel);
    void noteOff(int note);
    int  stealVoice();

    static float lerp(const std::vector<float>& b, double pos);
};
