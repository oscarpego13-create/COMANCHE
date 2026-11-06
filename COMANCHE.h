#pragma once

#include "IPlug_include_in_plug_hdr.h"
#include "IControls.h"
#include "IPlugMidi.h"
#include "Trace.h"
#include "MacroCurves.h"

#include <vector>
#include <string>
#include <array>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>

#include "EffectsChain.h"
#include "SampleLibrary.h"
#include "PresetManager.h"
#include "SoundClassifier.h"

using namespace iplug;
using namespace igraphics;

// ─── Parameter enum ──────────────────────────────────────────────────────────
enum EParams {
    kReverbAmount = 0,
    kReverbDecay,
    kDistAmount,
    kDistMode,
    kDelaySyncMode,
    kDelayTimeMs,
    kDelayFeedback,
    kDelayMix,
    kDelayLowcut,
    kDelayHighcut,
    kChorusAmount,
    kHpFreq,
    kLpFreq,
    kOutputVol,
    kMacro,
    kNumParams
};

enum EMsgTags { kMsgLoadSample = 0 };

// ─── Design system ───────────────────────────────────────────────────────────
namespace CT {
    static const IColor bgPrimary   {255,237,234,228};
    static const IColor bgPanel     {255,226,222,215};
    static const IColor fgPrimary   {255, 56, 56, 56};   // needles / lines
    static const IColor textInk     {255, 58, 57, 54};   // all text
    static const IColor knobGrey    {255,155,155,142};
    static const IColor knobBlue    {255,139,163,184};
    static const IColor knobGold    {255,184,168,130};
    static const IColor btnActive   {255, 26, 26, 26};
    static const IColor btnInactive {255,184,168,130};
    static const IColor textLight   {255,237,234,228};
    // Pastel FX box backgrounds (claude design palette)
    static const IColor boxReverb   {255,194,220,242};
    static const IColor boxDist     {255,248,224,196};
    static const IColor boxDelay    {255,218,208,242};
    static const IColor boxChorus   {255,198,236,212};
    static const IColor boxOutput   {255,238,234,226};
    // Macro link
    static const IColor macroOff    {255,150,148,144};
    static const IColor macroPos    {255, 80,200,100};
    static const IColor macroNeg    {255,230,140, 50};
    // Horse scene
    static const IColor horseGlow   {255,255,128, 30};
    static const IColor horseWind   {255, 60, 54, 48};
    static const IColor horseInk    {255, 22, 20, 18};
}

// Layout metrics from the UI Lab design
namespace UI {
    static constexpr float kValueSize = 8.5f;   // knob value text
    static constexpr float kLabelSize = 9.5f;   // knob label text
    static constexpr float kLabelGap  = 24.0f;  // label distance below the knob circle
    static constexpr float kRowH      = 19.0f;  // sample list row height
}

// 0..1 params without a unit read as a percentage ("42%")
inline void KnobValueText(const IParam* p, WDL_String& out)
{
    const char* u = p->GetLabel();
    if (p->GetMin() == 0.0 && p->GetMax() <= 1.0 && (!u || !u[0])) {
        out.SetFormatted(16, "%d%%", (int)std::lround(p->Value() * 100.0));
        return;
    }
    p->GetDisplay(out, false);
    if (u && u[0]) out.Append(u);
}

// ─── Small macro link button ──────────────────────────────────────────────────
class MacroLinkButton final : public IControl
{
public:
    MacroLinkButton(const IRECT& b, int& state, std::function<void()> onChange={})
        : IControl(b, kNoParameter), mState(state), mOnChange(std::move(onChange)) {}

    void Draw(IGraphics& g) override {
        IColor c = (mState== 1) ? CT::macroPos
                 : (mState==-1) ? CT::macroNeg
                                : CT::macroOff;
        float r = mRECT.W() * 0.5f - 0.5f;
        g.FillCircle(c, mRECT.MW(), mRECT.MH(), r);
        g.DrawCircle(IColor(180,0,0,0), mRECT.MW(), mRECT.MH(), r, nullptr, 0.7f);
    }
    void OnMouseDown(float, float, const IMouseMod&) override {
        if (mState == 0)      mState =  1;
        else if (mState == 1) mState = -1;
        else                  mState =  0;
        if (mOnChange) mOnChange();
        if (GetUI()) GetUI()->SetAllControlsDirty();
    }
private:
    int& mState;
    std::function<void()> mOnChange;
};

// ─── Flat knob with macro arc + continuous macro redraw ───────────────────────
class ComanacheKnob final : public IControl
{
public:
    ComanacheKnob(const IRECT& b, int param, const char* lbl,
                  IColor fill, bool isMacro=false, int* linkState=nullptr,
                  const float* modArray=nullptr)
        : IControl(b, param), mLabel(lbl), mFill(fill),
          mIsMacro(isMacro), mLinkState(linkState), mModArray(modArray) {}

    // shown: where a curve has the MACRO right now (< 0 when it isn't on a curve)
    void setMacroShown(const std::atomic<float>* shown) { mMacroShown = shown; }
    float macroNow() {
        const float s = mMacroShown ? mMacroShown->load() : -1.0f;
        return s >= 0.0f ? s : (float)GetDelegate()->GetParam(kMacro)->GetNormalized();
    }

    bool IsDirty() override {
        if (mIsMacro && mMacroShown) {
            const float s = mMacroShown->load();
            if (std::abs(s - mLastShown) > 0.001f) { mLastShown = s; return true; }
        }
        if (!mIsMacro && mLinkState && *mLinkState != 0) {
            float m = macroNow();
            if (std::abs(m - mLastMacro) > 0.001f) { mLastMacro = m; return true; }
        }
        if (!mIsMacro && mModArray) {
            float mod = mModArray[GetParamIdx()];
            if (std::abs(mod - mLastMod) > 0.001f) { mLastMod = mod; return true; }
        }
        return IControl::IsDirty();
    }

    void Draw(IGraphics& g) override {
        const float cx  = mRECT.MW();
        const float kR  = (mIsMacro ? 0.40f : 0.35f) * mRECT.W();
        const float kCy = mRECT.T + kR + 4.0f;
        static const IColor kNeedle(255,26,26,26);

        g.FillCircle(mFill,   cx, kCy, kR);
        g.DrawCircle(kNeedle, cx, kCy, kR-0.5f, nullptr, 1.8f);

        double baseNorm = GetParam() ? GetParam()->GetNormalized() : GetValue();
        if (mIsMacro) baseNorm = macroNow();
        // Needle stays at actual param value — LFO shown as arc, not needle movement
        double angle = (-135.0 + baseNorm*270.0) * (M_PI/180.0);
        float  reach = kR - 0.5f;
        g.DrawLine(kNeedle, cx, kCy,
                   cx+(float)(std::sin(angle)*reach),
                   kCy-(float)(std::cos(angle)*reach), nullptr, 2.2f);

        static const IColor kArc(220,80,140,255);
        const float arcR = kR + 1.8f;

        // Macro link arc + track where it ends (LFO arc chains from here)
        double macroEndNorm = baseNorm;
        if (!mIsMacro && mLinkState && *mLinkState != 0) {
            float macro = macroNow();
            if (macro > 0.001f) {
                macroEndNorm = std::clamp(baseNorm + macro*(double)(*mLinkState), 0.0, 1.0);
                double startA = (-135.0 + baseNorm    *270.0)*(M_PI/180.0);
                double endA   = (-135.0 + macroEndNorm*270.0)*(M_PI/180.0);
                if (std::abs(endA-startA) > 0.01) {
                    for (int s=1; s<=24; s++) {
                        double a0 = startA+(endA-startA)*(s-1.0)/24.0;
                        double a1 = startA+(endA-startA)*(double)s/24.0;
                        g.DrawLine(kArc,
                            cx+arcR*(float)std::sin(a0), kCy-arcR*(float)std::cos(a0),
                            cx+arcR*(float)std::sin(a1), kCy-arcR*(float)std::cos(a1),
                            nullptr, 3.0f);
                    }
                }
            }
        }
        // LFO arc — chains from end of macro arc (or from needle if no link)
        if (!mIsMacro && mModArray) {
            float lfoMod = mModArray[GetParamIdx()];
            if (std::abs(lfoMod) > 0.005f) {
                double effNorm = std::clamp(macroEndNorm + (double)lfoMod, 0.0, 1.0);
                double startA  = (-135.0 + macroEndNorm*270.0)*(M_PI/180.0);
                double endA    = (-135.0 + effNorm     *270.0)*(M_PI/180.0);
                if (std::abs(endA-startA) > 0.01) {
                    for (int s=1; s<=24; s++) {
                        double a0 = startA+(endA-startA)*(s-1.0)/24.0;
                        double a1 = startA+(endA-startA)*(double)s/24.0;
                        g.DrawLine(kArc,
                            cx+arcR*(float)std::sin(a0), kCy-arcR*(float)std::cos(a0),
                            cx+arcR*(float)std::sin(a1), kCy-arcR*(float)std::cos(a1),
                            nullptr, 3.0f);
                    }
                }
            }
        }

        WDL_String vs; KnobValueText(GetParam(), vs);
        if (mIsMacro) vs.SetFormatted(16, "%d%%", (int)std::lround(baseNorm * 100.0));
        IText vt(mIsMacro?13.0f:UI::kValueSize, CT::textInk, "RobotoMono", EAlign::Center, EVAlign::Middle);
        g.DrawText(vt, vs.Get(), IRECT(mRECT.L, kCy+kR+2, mRECT.R, kCy+kR+17));
        if (mLabel && mLabel[0]) {
            const float lg = mIsMacro ? 17.0f : UI::kLabelGap;
            IText lt(UI::kLabelSize, CT::textInk, "RobotoMono", EAlign::Center, EVAlign::Middle);
            g.DrawText(lt, mLabel, IRECT(mRECT.L, kCy+kR+lg, mRECT.R, kCy+kR+lg+14));
        }
    }

    void OnAttached() override { if (GetParam()) SetValue(GetParam()->GetNormalized()); }
    void OnMouseDown(float, float y, const IMouseMod& m) override
        { mDY=y; mSV=GetValue(); IControl::OnMouseDown(0,y,m); }
    void OnMouseDblClick(float, float, const IMouseMod&) override
        { SetValue(GetParam()->GetDefault(true)); SetDirty(true); }
    void OnMouseDrag(float, float y, float, float, const IMouseMod& m) override {
        double s = m.S ? 2000.0 : 200.0;
        SetValue(std::clamp(mSV-(y-mDY)/s,0.0,1.0)); SetDirty(true);
    }
    void OnMouseWheel(float, float, const IMouseMod& m, float d) override {
        SetValue(std::clamp(GetValue()+d*(m.S?0.002:0.01),0.0,1.0)); SetDirty(true);
    }
private:
    const std::atomic<float>* mMacroShown = nullptr;
    float mLastShown = -2.0f;
    const char* mLabel; IColor mFill; bool mIsMacro; int* mLinkState;
    const float* mModArray;
    float mDY{0}; double mSV{0};
    mutable float mLastMacro{-1.0f};
    mutable float mLastMod{0.0f};
};

// ─── Mode toggle button ───────────────────────────────────────────────────────
class ModeButton final : public IControl
{
public:
    ModeButton(const IRECT& b, int param, int modeValue, const char* lbl)
        : IControl(b, param), mMode(modeValue), mLabel(lbl) {}
    void Draw(IGraphics& g) override {
        bool active = ((int)(GetParam()->Value()+0.5)==mMode);
        g.FillRoundRect(active?CT::btnActive:CT::btnInactive, mRECT, 2.0f);
        IText t(9.0f, active?CT::textLight:CT::textInk, "RobotoMono", EAlign::Center, EVAlign::Middle);
        g.DrawText(t, mLabel, mRECT);
    }
    void OnMouseDown(float, float, const IMouseMod&) override {
        SetValue(GetParam()->ToNormalized((double)mMode)); SetDirty(true);
    }
private:
    int mMode; const char* mLabel;
};

// ─── TEMPO toggle button (on=black/sync, off=grey/free) ──────────────────────
class TempoToggleButton final : public IControl
{
public:
    TempoToggleButton(const IRECT& b, int param) : IControl(b, param) {}
    void OnAttached() override { if(GetParam()) SetValue(GetParam()->GetNormalized()); }
    void Draw(IGraphics& g) override {
        bool on = (GetParam() && GetParam()->Value() > 0.5);
        g.FillRoundRect(on ? CT::btnActive : IColor(255,170,165,158), mRECT, 3.0f);
        IText t(9.0f, on ? CT::textLight : CT::textInk, "RobotoMono", EAlign::Center, EVAlign::Middle);
        g.DrawText(t, "TEMPO", mRECT);
    }
    void OnMouseDown(float,float,const IMouseMod&) override {
        bool on = (GetParam() && GetParam()->Value() > 0.5);
        SetValue(on ? 0.0 : 1.0); SetDirty(true);
    }
};

// ─── Delay TIME knob: ms display when free, note-division when sync on ────────
class DelayTimeKnob final : public IControl
{
    static constexpr const char* kSubLabels[5] = {"1/4","1/8","1/16","1/4T","1/8T"};
    static int stepFromMs(float ms) { return std::clamp((int)(ms / 400.0f), 0, 4); }
    static double centerMsForStep(int s) { return s * 400.0 + 200.0; }

public:
    DelayTimeKnob(const IRECT& b, IColor fill)
        : IControl(b, kDelayTimeMs), mFill(fill) {}

    bool IsDirty() override {
        int cur = (int)(GetDelegate()->GetParam(kDelaySyncMode)->Value() + 0.5);
        if (cur != mLastSync) { mLastSync = cur; return true; }
        return IControl::IsDirty();
    }

    void Draw(IGraphics& g) override {
        const float cx  = mRECT.MW();
        const float kR  = 0.35f * mRECT.W();
        const float kCy = mRECT.T + kR + 4.0f;
        static const IColor kNeedle(255,26,26,26);
        g.FillCircle(mFill,   cx, kCy, kR);
        g.DrawCircle(kNeedle, cx, kCy, kR-0.5f, nullptr, 1.8f);

        double baseNorm = GetParam() ? GetParam()->GetNormalized() : GetValue();
        double angle = (-135.0 + baseNorm*270.0) * (M_PI/180.0);
        g.DrawLine(kNeedle, cx, kCy,
                   cx+(float)(std::sin(angle)*(kR-0.5f)),
                   kCy-(float)(std::cos(angle)*(kR-0.5f)), nullptr, 2.2f);

        // Value label: ms or note division
        bool syncOn = (GetDelegate()->GetParam(kDelaySyncMode)->Value() > 0.5);
        char valBuf[16];
        if (!syncOn) {
            WDL_String vs; GetParam()->GetDisplay(vs, false);
            snprintf(valBuf, sizeof(valBuf), "%s ms", vs.Get());
        } else {
            snprintf(valBuf, sizeof(valBuf), "%s", kSubLabels[stepFromMs((float)GetParam()->Value())]);
        }
        IText vt(UI::kValueSize, CT::textInk, "RobotoMono", EAlign::Center, EVAlign::Middle);
        g.DrawText(vt, valBuf, IRECT(mRECT.L, kCy+kR+2, mRECT.R, kCy+kR+17));
    }

    void OnAttached() override { if(GetParam()) SetValue(GetParam()->GetNormalized()); }

    void OnMouseDown(float, float y, const IMouseMod& m) override
        { mDY=y; mSV=GetValue(); IControl::OnMouseDown(0,y,m); }

    void OnMouseDblClick(float, float, const IMouseMod&) override
        { SetValue(GetParam()->GetDefault(true)); SetDirty(true); }

    void OnMouseDrag(float, float y, float, float, const IMouseMod& m) override {
        bool syncOn = (GetDelegate()->GetParam(kDelaySyncMode)->Value() > 0.5);
        double s = m.S ? 2000.0 : 200.0;
        double raw = std::clamp(mSV - (y-mDY)/s, 0.0, 1.0);
        if (!syncOn) {
            SetValue(raw);
        } else {
            int step = std::clamp((int)(raw * 5.0), 0, 4);
            SetValue(GetParam()->ToNormalized(centerMsForStep(step)));
        }
        SetDirty(true);
    }

    void OnMouseWheel(float, float, const IMouseMod& m, float d) override {
        bool syncOn = (GetDelegate()->GetParam(kDelaySyncMode)->Value() > 0.5);
        if (!syncOn) {
            SetValue(std::clamp(GetValue()+d*(m.S?0.002:0.01),0.0,1.0));
        } else {
            int step = stepFromMs((float)GetParam()->Value());
            step = std::clamp(step + (d > 0 ? 1 : -1), 0, 4);
            SetValue(GetParam()->ToNormalized(centerMsForStep(step)));
        }
        SetDirty(true);
    }

private:
    IColor mFill;
    float mDY{0}; double mSV{0};
    mutable int mLastSync{-1};
};

// ─── Scrollable sample list with scrollbar, type badges and type filter ──────
// Rows show only the samples matching the current filter; mSelected stays a library index.
class SampleListControl final : public IControl
{
public:
    using OnDropFn        = std::function<void(const std::string&)>;
    using OnRemoveFn      = std::function<void(int)>;
    using OnResetPresetFn = std::function<void(int)>;
    using TypeOfFn        = std::function<SoundType(int)>;

    SampleListControl(const IRECT& b, const std::vector<std::string>& names, int& sel,
                      TypeOfFn typeOf, const int& filter, const std::atomic<int>& typesVersion,
                      OnDropFn dropFn={}, OnRemoveFn removeFn={}, OnResetPresetFn resetFn={})
        : IControl(b, kNoParameter), mNames(names), mSelected(sel),
          mTypeOf(std::move(typeOf)), mFilter(filter), mTypesVersion(typesVersion),
          mDropFn(std::move(dropFn)), mRemoveFn(std::move(removeFn)), mResetPresetFn(std::move(resetFn)) {}

    // Redraw when the background analysis classifies more samples
    bool IsDirty() override {
        const int v = mTypesVersion.load();
        if (v != mLastVersion) { mLastVersion = v; return true; }
        return IControl::IsDirty();
    }

    void Draw(IGraphics& g) override {
        const std::vector<int> vis = visible();
        const float sbW=12.0f, rowH=UI::kRowH;
        IRECT listR(mRECT.L, mRECT.T, mRECT.R-sbW-1, mRECT.B);
        IRECT sbR  (mRECT.R-sbW, mRECT.T, mRECT.R, mRECT.B);
        g.FillRect(CT::bgPanel, listR);

        const int maxRows=(int)(listR.H()/rowH);
        mScroll = std::clamp(mScroll, 0, std::max(0, (int)vis.size()-maxRows));
        for (int r=mScroll; r<(int)vis.size()&&(r-mScroll)<maxRows; r++) {
            const int i = vis[r];
            IRECT row(listR.L, listR.T+(r-mScroll)*rowH, listR.R, listR.T+(r-mScroll+1)*rowH);
            const bool sel=(i==mSelected);
            if (sel) g.FillRect(CT::fgPrimary, row);
            // Type badge on the right of the row
            float nameR = row.R - 4;
            const SoundType t = mTypeOf(i);
            if (const char* b = SoundClassifier::badge(t)) {
                const IRECT chip(row.R-38, row.MH()-6, row.R-4, row.MH()+6);
                const IColor fill = t==SoundType::Bass ? CT::boxReverb : t==SoundType::LeadPad ? CT::boxDelay
                                  : t==SoundType::Keys ? CT::boxDist : IColor(0,0,0,0);
                if (fill.A) g.FillRoundRect(fill, chip, 3.0f);
                IText bt(7.5f, t==SoundType::Pending ? (sel?CT::textLight:CT::textInk) : CT::textInk, "RobotoMono",
                         EAlign::Center, EVAlign::Middle);
                g.DrawText(bt, b, chip);
                nameR = chip.L - 4;
            }
            IText tx(11.0f, sel?CT::textLight:CT::textInk, "RobotoMono", EAlign::Near, EVAlign::Middle);
            const IRECT nameRect(row.L+6, row.T, nameR, row.B);
            g.PathClipRegion(nameRect);
            g.DrawText(tx, mNames[i].c_str(), nameRect);
            g.PathClipRegion();
        }
        g.FillRoundRect(IColor(255,120,118,114), sbR, 6.0f);
        if ((int)vis.size()>maxRows) {
            float thumbH=std::max(20.0f, sbR.H()*(float)maxRows/(float)vis.size());
            float maxSc=(float)(vis.size()-maxRows);
            float tp=(maxSc>0)?(float)mScroll/maxSc:0.0f;
            float ty=sbR.T+tp*(sbR.H()-thumbH);
            g.FillRoundRect(IColor(255,190,185,180), IRECT(sbR.L+2,ty,sbR.R-2,ty+thumbH), 5.0f);
        }
    }
    void OnMouseDown(float x, float y, const IMouseMod& mod) override {
        const std::vector<int> vis = visible();
        const float sbW=12.0f, rowH=UI::kRowH;
        const int r=mScroll+(int)((y-mRECT.T)/rowH);
        const int idx = (r>=0 && r<(int)vis.size()) ? vis[r] : -1;
        if (mod.R) {
            if (idx>=0) {
                mContextRow=idx;
                mContextMenu = std::make_unique<IPopupMenu>();
                mContextMenu->AddItem("Eliminar Sonido");
                mContextMenu->AddItem("Eliminar Preset");
                GetUI()->CreatePopupMenu(*this, *mContextMenu, IRECT(x,y,x+1,y+1));
            }
            return;
        }
        if (x>=mRECT.R-sbW){mSbDrag=true;mSbY=y;return;}
        if (idx>=0){
            mSelected=idx;
            GetDelegate()->SendArbitraryMsgFromUI(kMsgLoadSample,kNoTag,sizeof(int),&mSelected);
            SetDirty(false);
        }
    }
    void OnPopupMenuSelection(IPopupMenu* pMenu, int) override {
        if (!pMenu) return;
        int chosen = pMenu->GetChosenItemIdx();
        if (chosen==0 && mRemoveFn)      mRemoveFn(mContextRow);
        else if (chosen==1 && mResetPresetFn) mResetPresetFn(mContextRow);
        SetDirty(false);
    }
    void OnMouseUp(float,float,const IMouseMod&) override {mSbDrag=false;}
    void OnMouseDrag(float,float y,float,float dy,const IMouseMod&) override {
        const float rowH=UI::kRowH;
        const int total=(int)visible().size();
        int maxRows=(int)(mRECT.H()/rowH), maxSc=std::max(0,total-maxRows);
        if (mSbDrag){
            float thumbH=std::max(20.0f,mRECT.H()*(float)maxRows/std::max(1,total));
            float range=mRECT.H()-thumbH;
            if(range>0) mScroll=std::clamp((int)(((y-mRECT.T)/range)*maxSc),0,maxSc);
        } else mScroll=std::clamp(mScroll+(int)(-dy/rowH),0,maxSc);
        SetDirty(false);
    }
    void OnMouseWheel(float,float,const IMouseMod&,float d) override {
        const float rowH=UI::kRowH;
        const int total=(int)visible().size();
        int maxRows=(int)(mRECT.H()/rowH),maxSc=std::max(0,total-maxRows);
        mScroll=std::clamp(mScroll-(int)d,0,maxSc); SetDirty(false);
    }
    void refresh(){mScroll=0;SetDirty(false);}
    // Keyboard: step through the rows on show (they follow the type filter), load it and keep it in view
    void moveSelection(int delta) {
        const std::vector<int> vis = visible();
        if (vis.empty()) return;
        const auto it = std::find(vis.begin(), vis.end(), mSelected);
        const int r = it != vis.end() ? std::clamp((int)(it - vis.begin()) + delta, 0, (int)vis.size() - 1)
                                      : (delta > 0 ? 0 : (int)vis.size() - 1);
        if (vis[r] == mSelected) return;
        mSelected = vis[r];
        const int maxRows = std::max(1, (int)(mRECT.H() / UI::kRowH));
        if (r < mScroll) mScroll = r;
        else if (r >= mScroll + maxRows) mScroll = r - maxRows + 1;
        GetDelegate()->SendArbitraryMsgFromUI(kMsgLoadSample, kNoTag, sizeof(int), &mSelected);
        SetDirty(false);
    }
    void OnDrop(const char* str) override {
        if (mDropFn && str) { mScroll=0; mDropFn(std::string(str)); }
    }
private:
    // Library indices shown with the current filter (0 = all, else a SoundType)
    std::vector<int> visible() const {
        std::vector<int> v;
        for (int i=0; i<(int)mNames.size(); i++)
            if (mFilter==0 || (int)mTypeOf(i)==mFilter) v.push_back(i);
        return v;
    }
    const std::vector<std::string>& mNames; int& mSelected;
    TypeOfFn mTypeOf;
    const int& mFilter;
    const std::atomic<int>& mTypesVersion;
    int mLastVersion{-1};
    OnDropFn        mDropFn;
    OnRemoveFn      mRemoveFn;
    OnResetPresetFn mResetPresetFn;
    std::unique_ptr<IPopupMenu> mContextMenu;
    int mContextRow{-1};
    int mScroll{0}; bool mSbDrag{false}; float mSbY{0};
};

// ─── Type filter under the library buttons: Todo · Bass · Lead / Pad · Keys ──
class TypeFilterBar final : public IControl
{
public:
    TypeFilterBar(const IRECT& b, int& filter, std::function<void()> onChange)
        : IControl(b, kNoParameter), mFilter(filter), mOnChange(std::move(onChange)) {}
    void Draw(IGraphics& g) override {
        static const char* kLabels[4] = {"TODO", "BASS", "LEAD / PAD", "KEYS"};
        static const int   kValues[4] = {0, (int)SoundType::Bass, (int)SoundType::LeadPad, (int)SoundType::Keys};
        for (int i=0; i<4; i++) {
            const IRECT r = cell(i);
            const bool on = mFilter==kValues[i];
            g.FillRoundRect(on ? CT::btnActive : CT::btnInactive, r, 2.0f);
            IText t(8.0f, on ? CT::textLight : CT::textInk, "RobotoMono", EAlign::Center, EVAlign::Middle);
            g.DrawText(t, kLabels[i], r);
        }
    }
    void OnMouseDown(float x, float y, const IMouseMod&) override {
        static const int kValues[4] = {0, (int)SoundType::Bass, (int)SoundType::LeadPad, (int)SoundType::Keys};
        for (int i=0; i<4; i++) if (cell(i).Contains(x, y)) { mFilter=kValues[i]; if (mOnChange) mOnChange(); SetDirty(false); return; }
    }
private:
    // "LEAD / PAD" gets a wider cell than the others
    IRECT cell(int i) const {
        static const float w[4] = {0.22f, 0.22f, 0.34f, 0.22f};
        float x = mRECT.L;
        for (int k=0; k<i; k++) x += w[k]*mRECT.W();
        return IRECT(x+1, mRECT.T, x+w[i]*mRECT.W()-1, mRECT.B);
    }
    int& mFilter;
    std::function<void()> mOnChange;
};

// ─── Settings panel: loop + mono / poly, opens to the left of the gear ──────
// Drawn by the plugin (not a native menu) so it stays inside the window and can carry a close X
class SettingsPanel final : public IControl
{
public:
    // The loop zone on the timeline, as fractions of the sound
    struct LoopZone {
        std::function<float()> start, end, seconds;      // seconds: length of the whole sound
        std::function<bool()>  manual;
        std::function<void(float, float, bool)> set;     // start, end, final (mouse released)
        std::function<void()>  reset;                    // back to the detected loop
    };
    SettingsPanel(const IRECT& b, std::atomic<bool>& mono, std::atomic<float>& glideMs, std::atomic<float>& velDynamics,
                  LoopZone zone, std::function<bool()> loopOn, std::function<void(bool)> setLoop, std::function<void()> changed)
        : IControl(b, kNoParameter), mMono(mono), mGlideMs(glideMs), mVelDynamics(velDynamics), mZone(std::move(zone)), mLoopOn(std::move(loopOn)),
          mSetLoop(std::move(setLoop)), mChanged(std::move(changed)) { Hide(true); }
    void Draw(IGraphics& g) override {
        g.FillRoundRect(CT::bgPrimary, mRECT, 4.0f);
        g.DrawRoundRect(CT::fgPrimary, mRECT, 4.0f, nullptr, 1.5f);
        const IText title(9.0f, CT::textInk, "RobotoMono", EAlign::Near, EVAlign::Middle);
        g.DrawText(title, "AJUSTES", IRECT(mRECT.L+8, mRECT.T, mRECT.R, mRECT.T+kHead));
        const IRECT x = closeRect();
        const IColor xc = mOverClose ? CT::fgPrimary : CT::textInk;
        g.DrawLine(xc, x.L+4, x.T+4, x.R-4, x.B-4, nullptr, 1.6f);
        g.DrawLine(xc, x.R-4, x.T+4, x.L+4, x.B-4, nullptr, 1.6f);
        g.DrawLine(CT::btnInactive, mRECT.L+6, mRECT.T+kHead, mRECT.R-6, mRECT.T+kHead, nullptr, 1.0f);
        static const char* kLabels[3] = {"Loop (mantener nota)", "Polifónico", "Monofónico"};
        const bool on[3] = {mLoopOn(), !mMono, mMono};
        const IText t(9.0f, CT::textInk, "RobotoMono", EAlign::Near, EVAlign::Middle);
        for (int i=0; i<3; i++) {
            const IRECT r = row(i);
            const IRECT box(r.L+8, r.MH()-5, r.L+18, r.MH()+5);
            if (i==0) g.DrawRect(CT::fgPrimary, box, nullptr, 1.2f);      // checkbox
            else g.DrawCircle(CT::fgPrimary, box.MW(), box.MH(), 5.0f, nullptr, 1.2f);   // radio
            if (on[i]) { if (i==0) g.FillRect(CT::fgPrimary, box.GetPadded(-2.5f));
                         else g.FillCircle(CT::fgPrimary, box.MW(), box.MH(), 2.5f); }
            g.DrawText(t, kLabels[i], IRECT(box.R+6, r.T, r.R, r.B));
        }

        drawTimeline(g);

        // Sliders: label + value, then a bar to drag (separator above each).
        // Glide is dimmed while it can't act (needs mono and loop).
        const bool live = mMono && mLoopOn();
        WDL_String ms; ms.SetFormatted(16, "%d ms", (int)std::lround(mGlideMs.load()));
        drawSlider(g, kGlide, live ? "Glide" : "Glide (mono + loop)", ms.Get(), glideNorm(), live);
        WDL_String pc; pc.SetFormatted(16, "%d%%", (int)std::lround(mVelDynamics.load() * 100.0f));
        drawSlider(g, kVelocity, "Velocidad -> din\xC3\xA1mica", pc.Get(), mVelDynamics.load(), true);
    }
    // The whole sound as a bar, with the loop zone between two handles; "auto" until it's moved by hand
    void drawTimeline(IGraphics& g) {
        const bool live = mLoopOn();
        const IRECT bar = timelineBar();
        const float s = mZone.start(), e = mZone.end(), sec = mZone.seconds();
        WDL_String v;
        if (mZone.manual() && sec > 0.0f) v.SetFormatted(24, "%.2f-%.2f s", s * sec, e * sec);
        else v.Set("auto");
        const IText vt(9.0f, live ? CT::textInk : CT::btnInactive, "RobotoMono", EAlign::Far, EVAlign::Middle);
        const IRECT r0 = row(0);
        g.DrawText(vt, v.Get(), IRECT(r0.L, r0.T, r0.R-8, r0.B));
        g.FillRoundRect(CT::btnInactive, bar, 2.0f);
        const float xs = bar.L + s*bar.W(), xe = bar.L + e*bar.W();
        g.FillRect(CT::textInk.WithOpacity(live ? 0.35f : 0.15f), IRECT(xs, bar.T, xe, bar.B));
        const IColor hc = live ? CT::fgPrimary : CT::textInk.WithOpacity(0.35f);
        g.FillRect(hc, IRECT(xs-1.5f, bar.T-1.0f, xs+1.5f, bar.B+1.0f));
        g.FillRect(hc, IRECT(xe-1.5f, bar.T-1.0f, xe+1.5f, bar.B+1.0f));
    }
    void drawSlider(IGraphics& g, int i, const char* label, const char* value, float n, bool live) {
        const IRECT gr = sliderRow(i);
        const IColor ink = live ? CT::textInk : CT::btnInactive;
        g.DrawLine(CT::btnInactive, mRECT.L+6, gr.T-3, mRECT.R-6, gr.T-3, nullptr, 1.0f);
        const IText gl(9.0f, ink, "RobotoMono", EAlign::Near, EVAlign::Middle);
        const IText gv(9.0f, ink, "RobotoMono", EAlign::Far, EVAlign::Middle);
        g.DrawText(gl, label, IRECT(gr.L+8, gr.T, gr.R-8, gr.T+14));
        g.DrawText(gv, value, IRECT(gr.L+8, gr.T, gr.R-8, gr.T+14));
        const IRECT bar = sliderBar(i);
        g.FillRoundRect(CT::btnInactive, bar, 3.0f);
        if (n > 0.0f) g.FillRoundRect(live ? CT::fgPrimary : CT::textInk.WithOpacity(0.35f), IRECT(bar.L, bar.T, bar.L + n*bar.W(), bar.B), 3.0f);
    }
    void OnMouseDown(float x, float y, const IMouseMod&) override {
        if (closeRect().Contains(x, y)) { setOpen(false); return; }
        if (timelineHit().Contains(x, y)) { grabTimeline(x); return; }
        for (int i : {kGlide, kVelocity})
            if (sliderRow(i).Contains(x, y)) { mDragSlider = i; setSliderFromX(i, x); return; }
        if      (row(0).Contains(x, y)) mSetLoop(!mLoopOn());
        else if (row(1).Contains(x, y)) mMono = false;
        else if (row(2).Contains(x, y)) mMono = true;
        else return;
        if (mChanged) mChanged();
        SetDirty(false);
    }
    void OnMouseOver(float x, float y, const IMouseMod&) override {
        const bool o = closeRect().Contains(x, y);
        if (o != mOverClose) { mOverClose = o; SetDirty(false); }
    }
    void OnMouseOut() override { mOverClose = false; SetDirty(false); }
    void OnMouseDrag(float x, float, float, float, const IMouseMod&) override {
        if (mDragSlider >= 0) setSliderFromX(mDragSlider, x);
        else if (mDragZone != kZoneNone) { mZoneMoved = true; moveZone(x, false); }
    }
    void OnMouseUp(float x, float, const IMouseMod&) override {
        if (mDragSlider >= 0) { mDragSlider = -1; if (mChanged) mChanged(); }
        if (mDragZone != kZoneNone) {
            if (mZoneMoved) { moveZone(x, true); if (mChanged) mChanged(); }
            mDragZone = kZoneNone;
        }
    }
    void OnMouseDblClick(float x, float y, const IMouseMod&) override {
        if (timelineHit().Contains(x, y)) { mZone.reset(); if (mChanged) mChanged(); SetDirty(false); }
    }
    void Toggle() { setOpen(IsHidden()); }
    void setOnVisibility(std::function<void()> fn) { mOnVisibility = std::move(fn); }
    static constexpr float kGlideMaxMs = 1000.0f;
    static constexpr float kW = 196.0f, kHead = 20.0f, kRow = 19.0f, kGlideH = 30.0f;
    static constexpr float kTimelineH = 15.0f;
    static constexpr float kH = kHead + 3.0f + 3*kRow + 4.0f + kTimelineH + 2*(7.0f + kGlideH) + 5.0f;
private:
    void setOpen(bool open) { Hide(!open); if (mOnVisibility) mOnVisibility(); }
    IRECT closeRect() const { return IRECT(mRECT.R-18, mRECT.T+3, mRECT.R-4, mRECT.T+17); }
    // Loop row and its timeline, then a gap, then the two playback rows
    IRECT row(int i) const {
        const float t = mRECT.T + kHead + 3.0f + i*kRow + (i>0 ? 4.0f + kTimelineH : 0.0f);
        return IRECT(mRECT.L, t, mRECT.R, t+kRow);
    }
    IRECT timelineBar() const { const IRECT r = row(0); return IRECT(r.L+10, r.B+2, r.R-10, r.B+12); }
    IRECT timelineHit() const { return timelineBar().GetPadded(-6.0f, -2.0f, 6.0f, 2.0f); }
    float timelineNorm(float x) const { const IRECT b = timelineBar(); return std::clamp((x - b.L) / b.W(), 0.0f, 1.0f); }
    // Near a handle: take it. Inside the zone: move all of it. Outside: the nearest handle jumps there.
    void grabTimeline(float x) {
        const IRECT b = timelineBar();
        const float n = timelineNorm(x), s = mZone.start(), e = mZone.end();
        const float near = 7.0f / b.W();
        mZoneMoved = false;
        if (std::abs(n - s) <= near && std::abs(n - s) <= std::abs(n - e)) { mDragZone = kZoneStart; mGrab = n - s; }
        else if (std::abs(n - e) <= near) { mDragZone = kZoneEnd; mGrab = n - e; }
        else if (n > s && n < e) { mDragZone = kZoneBoth; mGrab = n - s; }
        else { mDragZone = n < s ? kZoneStart : kZoneEnd; mGrab = 0.0f; mZoneMoved = true; moveZone(x, false); }
    }
    void moveZone(float x, bool final) {
        const float n = timelineNorm(x) - mGrab;
        float s = mZone.start(), e = mZone.end();
        if (mDragZone == kZoneStart) s = n;
        else if (mDragZone == kZoneEnd) e = n;
        else { const float w = e - s; s = std::clamp(n, 0.0f, 1.0f - w); e = s + w; }
        mZone.set(s, e, final);
        SetDirty(false);
    }
    enum { kGlide = 0, kVelocity = 1 };
    // Slider rows below the playback rows, each after a separator
    IRECT sliderRow(int i) const {
        const float t = mRECT.T + kHead + 3.0f + 3*kRow + 4.0f + kTimelineH + 7.0f + i * (kGlideH + 7.0f);
        return IRECT(mRECT.L, t, mRECT.R, t + kGlideH);
    }
    IRECT sliderBar(int i) const { const IRECT r = sliderRow(i); return IRECT(r.L+8, r.T+18, r.R-8, r.T+25); }
    // Glide uses a square law: finer steps at the short times that get used most
    float glideNorm() const { return std::sqrt(std::clamp(mGlideMs.load() / kGlideMaxMs, 0.0f, 1.0f)); }
    void setSliderFromX(int i, float x) {
        const IRECT b = sliderBar(i);
        const float n = std::clamp((x - b.L) / b.W(), 0.0f, 1.0f);
        if (i == kGlide) mGlideMs = std::round(n * n * kGlideMaxMs);
        else             mVelDynamics = std::round(n * 100.0f) / 100.0f;
        SetDirty(false);
    }
    std::atomic<bool>& mMono;
    std::atomic<float>& mGlideMs;
    std::atomic<float>& mVelDynamics;
    LoopZone mZone;
    enum { kZoneNone, kZoneStart, kZoneEnd, kZoneBoth };
    int   mDragZone = kZoneNone;
    float mGrab = 0.0f;
    bool  mZoneMoved = false;
    int mDragSlider = -1;
    std::function<bool()> mLoopOn;
    std::function<void(bool)> mSetLoop;
    std::function<void()> mChanged, mOnVisibility;
    bool mOverClose = false;
};

// ─── Invisible layer over the MACRO: takes its right click, passes the rest to the knob ───
// iPlug2 sends a right click on any control with a parameter straight to the host's context menu, so the
// knob itself never sees it. This layer has no parameter, and tells the host about the gestures it forwards.
class MacroClickLayer final : public IControl
{
public:
    MacroClickLayer(const IRECT& b, IControl* knob, std::function<void()> onRightClick)
        : IControl(b, kNoParameter), mKnob(knob), mOnRightClick(std::move(onRightClick)) {}
    void Draw(IGraphics&) override {}
    void OnMouseDown(float x, float y, const IMouseMod& m) override {
        if (m.R) { mOnRightClick(); return; }
        GetDelegate()->BeginInformHostOfParamChangeFromUI(kMacro);
        mDragging = true;
        mKnob->OnMouseDown(x, y, m);
    }
    void OnMouseDrag(float x, float y, float dX, float dY, const IMouseMod& m) override {
        if (mDragging) mKnob->OnMouseDrag(x, y, dX, dY, m);
    }
    void OnMouseUp(float x, float y, const IMouseMod& m) override {
        if (!mDragging) return;
        mDragging = false;
        mKnob->OnMouseUp(x, y, m);
        GetDelegate()->EndInformHostOfParamChangeFromUI(kMacro);
    }
    void OnMouseDblClick(float x, float y, const IMouseMod& m) override {
        GetDelegate()->BeginInformHostOfParamChangeFromUI(kMacro);
        mKnob->OnMouseDblClick(x, y, m);
        GetDelegate()->EndInformHostOfParamChangeFromUI(kMacro);
    }
    void OnMouseWheel(float x, float y, const IMouseMod& m, float d) override { mKnob->OnMouseWheel(x, y, m, d); }
private:
    IControl* mKnob;
    std::function<void()> mOnRightClick;
    bool mDragging = false;
};

// ─── MACRO curve window: right click on the MACRO. Same look as the settings panel ───
class MacroCurvePanel final : public IControl
{
public:
    MacroCurvePanel(const IRECT& b, std::atomic<bool>& on, std::atomic<int>& shape, std::atomic<int>& rate,
                    std::atomic<float>& freeMs, const std::atomic<float>& phase, std::function<void()> changed)
        : IControl(b, kNoParameter), mOn(on), mShape(shape), mRate(rate), mFreeMs(freeMs), mPhase(phase),
          mChanged(std::move(changed)) { Hide(true); }
    static constexpr float kW = 296.0f, kHead = 20.0f, kBtnH = 18.0f, kFreeH = 28.0f, kViewH = 46.0f, kCellH = 21.0f;
    static constexpr float kH = kHead + 5.0f + kBtnH + 4.0f + kBtnH + 5.0f + kFreeH + 3.0f + kViewH + 5.0f + 2*kCellH + 3.0f + 7.0f;
    void Toggle() { setOpen(IsHidden()); }
    void setOnVisibility(std::function<void()> fn) { mOnVisibility = std::move(fn); }
    // The cursor moves while the curve runs
    bool IsDirty() override { return (!IsHidden() && mOn.load()) || IControl::IsDirty(); }

    void Draw(IGraphics& g) override {
        g.FillRoundRect(CT::bgPrimary, mRECT, 4.0f);
        g.DrawRoundRect(CT::fgPrimary, mRECT, 4.0f, nullptr, 1.5f);
        const IText title(9.0f, CT::textInk, "RobotoMono", EAlign::Near, EVAlign::Middle);
        g.DrawText(title, "MOVIMIENTO DEL MACRO", IRECT(mRECT.L+8, mRECT.T, mRECT.R, mRECT.T+kHead));
        const IRECT x = closeRect();
        const IColor xc = mOverClose ? CT::fgPrimary : CT::textInk;
        g.DrawLine(xc, x.L+4, x.T+4, x.R-4, x.B-4, nullptr, 1.6f);
        g.DrawLine(xc, x.R-4, x.T+4, x.L+4, x.B-4, nullptr, 1.6f);
        g.DrawLine(CT::btnInactive, mRECT.L+6, mRECT.T+kHead, mRECT.R-6, mRECT.T+kHead, nullptr, 1.0f);

        const bool on = mOn.load();
        const IRECT sw = switchRect();
        g.FillRoundRect(on ? CT::btnActive : CT::btnInactive, sw, 2.0f);
        g.DrawText(IText(9.0f, on ? CT::textLight : CT::textInk, "RobotoMono", EAlign::Center, EVAlign::Middle),
                   on ? "ACTIVADO" : "ACTIVAR", sw);
        const int rate = mRate.load();
        const IText hint(8.5f, CT::textInk, "RobotoMono", EAlign::Far, EVAlign::Middle);
        g.DrawText(hint, rate == MacroCurves::kFreeRate ? "libre, sin tempo" : "sincronizado al tempo",
                   IRECT(sw.R, sw.T, mRECT.R-8, sw.B));

        for (int i = 0; i < MacroCurves::kNumRates; i++) {
            const IRECT r = rateCell(i);
            const bool sel = i == rate;
            g.FillRoundRect(sel ? CT::btnActive : CT::btnInactive, r, 2.0f);
            g.DrawText(IText(9.0f, sel ? CT::textLight : CT::textInk, "RobotoMono", EAlign::Center, EVAlign::Middle),
                       MacroCurves::rateLabel(i), r);
        }

        // Free time: label + ms, then a bar to drag. Dimmed unless MS is chosen.
        const bool free = rate == MacroCurves::kFreeRate;
        const IColor ink = free ? CT::textInk : CT::btnInactive;
        const IRECT fr = freeRow();
        WDL_String ms; ms.SetFormatted(16, "%d ms", (int)std::lround(mFreeMs.load()));
        g.DrawText(IText(9.0f, ink, "RobotoMono", EAlign::Near, EVAlign::Middle), "Tiempo libre", IRECT(fr.L, fr.T, fr.R, fr.T+14));
        g.DrawText(IText(9.0f, ink, "RobotoMono", EAlign::Far, EVAlign::Middle), ms.Get(), IRECT(fr.L, fr.T, fr.R, fr.T+14));
        const IRECT bar = freeBar();
        g.FillRoundRect(CT::btnInactive, bar, 3.0f);
        const float n = freeNorm();
        if (n > 0.0f) g.FillRoundRect(free ? CT::fgPrimary : CT::textInk.WithOpacity(0.35f), IRECT(bar.L, bar.T, bar.L + n*bar.W(), bar.B), 3.0f);

        // The chosen curve large, with where it is now
        const IRECT v = viewRect();
        g.FillRoundRect(CT::bgPanel, v, 3.0f);
        drawCurve(g, v.GetPadded(-5.0f), mShape.load(), on ? CT::fgPrimary : CT::textInk.WithOpacity(0.45f), 96, 2.0f);
        if (on) {
            const IRECT c = v.GetPadded(-5.0f);
            const float ph = mPhase.load(), cx = c.L + ph * c.W();
            g.DrawLine(CT::horseGlow, cx, v.T+2, cx, v.B-2, nullptr, 1.5f);
            g.FillCircle(CT::horseGlow, cx, c.B - MacroCurves::value(mShape.load(), ph) * c.H(), 3.5f);
        }

        const int shape = mShape.load();
        for (int i = 0; i < MacroCurves::kNumShapes; i++) {
            const IRECT r = shapeCell(i);
            const bool sel = i == shape;
            g.FillRoundRect(sel ? CT::btnActive : CT::btnInactive, r, 2.0f);
            drawCurve(g, r.GetPadded(-4.0f), i, sel ? CT::textLight : CT::textInk, 24, 1.3f);
        }
    }
    void OnMouseDown(float x, float y, const IMouseMod&) override {
        if (closeRect().Contains(x, y)) { setOpen(false); return; }
        if (switchRect().Contains(x, y)) { mOn = !mOn.load(); commit(); return; }
        for (int i = 0; i < MacroCurves::kNumRates; i++)
            if (rateCell(i).Contains(x, y)) { mRate = i; commit(); return; }
        for (int i = 0; i < MacroCurves::kNumShapes; i++)
            if (shapeCell(i).Contains(x, y)) { mShape = i; commit(); return; }
        if (freeRow().GetPadded(0.0f, 0.0f, 0.0f, 2.0f).Contains(x, y)) { mDragFree = true; setFreeFromX(x); }
    }
    void OnMouseDrag(float x, float, float, float, const IMouseMod&) override { if (mDragFree) setFreeFromX(x); }
    void OnMouseUp(float, float, const IMouseMod&) override { if (mDragFree) { mDragFree = false; if (mChanged) mChanged(); } }
    void OnMouseOver(float x, float y, const IMouseMod&) override {
        const bool o = closeRect().Contains(x, y);
        if (o != mOverClose) { mOverClose = o; SetDirty(false); }
    }
    void OnMouseOut() override { mOverClose = false; SetDirty(false); }
private:
    void setOpen(bool open) { Hide(!open); if (mOnVisibility) mOnVisibility(); }
    void commit() { if (mChanged) mChanged(); SetDirty(false); }
    void drawCurve(IGraphics& g, const IRECT& r, int shape, const IColor& c, int segs, float thick) {
        float px = r.L, py = r.B - MacroCurves::value(shape, 0.0f) * r.H();
        for (int i = 1; i <= segs; i++) {
            const float t = (float)i / (float)segs, x = r.L + t * r.W();
            const float y = r.B - MacroCurves::value(shape, std::min(t, 0.9999f)) * r.H();
            g.DrawLine(c, px, py, x, y, nullptr, thick);
            px = x; py = y;
        }
    }
    IRECT closeRect() const { return IRECT(mRECT.R-18, mRECT.T+3, mRECT.R-4, mRECT.T+17); }
    IRECT switchRect() const { const float t = mRECT.T + kHead + 5.0f; return IRECT(mRECT.L+8, t, mRECT.L+98, t+kBtnH); }
    IRECT rateCell(int i) const {
        const float t = mRECT.T + kHead + 5.0f + kBtnH + 4.0f, w = (mRECT.W() - 16.0f + 3.0f) / MacroCurves::kNumRates;
        const float l = mRECT.L + 8.0f + i*w;
        return IRECT(l, t, l+w-3.0f, t+kBtnH);
    }
    IRECT freeRow() const {
        const float t = mRECT.T + kHead + 5.0f + kBtnH + 4.0f + kBtnH + 5.0f;
        return IRECT(mRECT.L+8, t, mRECT.R-8, t+kFreeH);
    }
    IRECT freeBar() const { const IRECT r = freeRow(); return IRECT(r.L, r.T+16, r.R, r.T+23); }
    IRECT viewRect() const { const float t = freeRow().B + 3.0f; return IRECT(mRECT.L+8, t, mRECT.R-8, t+kViewH); }
    IRECT shapeCell(int i) const {
        const float t = viewRect().B + 5.0f + (i/8)*(kCellH+3.0f), w = (mRECT.W() - 16.0f + 3.0f) / 8.0f;
        const float l = mRECT.L + 8.0f + (i%8)*w;
        return IRECT(l, t, l+w-3.0f, t+kCellH);
    }
    // Square law between 20 ms and 8 s: finer steps at the short times
    float freeNorm() const {
        using namespace MacroCurves;
        return std::sqrt(std::clamp((mFreeMs.load() - kFreeMinMs) / (kFreeMaxMs - kFreeMinMs), 0.0f, 1.0f));
    }
    void setFreeFromX(float x) {
        using namespace MacroCurves;
        const IRECT b = freeBar();
        const float n = std::clamp((x - b.L) / b.W(), 0.0f, 1.0f);
        mFreeMs = std::round(kFreeMinMs + n * n * (kFreeMaxMs - kFreeMinMs));
        SetDirty(false);
    }
    std::atomic<bool>& mOn;
    std::atomic<int>& mShape;
    std::atomic<int>& mRate;
    std::atomic<float>& mFreeMs;
    const std::atomic<float>& mPhase;
    std::function<void()> mChanged, mOnVisibility;
    bool mDragFree = false, mOverClose = false;
};

// ─── Settings gear (left of the signature): opens / closes the SettingsPanel ─
class SettingsGear final : public IControl
{
public:
    SettingsGear(const IRECT& b, std::function<void()> onClick)
        : IControl(b, kNoParameter), mOnClick(std::move(onClick)) {}
    void Draw(IGraphics& g) override {
        const float cx = mRECT.MW(), cy = mRECT.MH(), r = std::min(mRECT.W(), mRECT.H()) * 0.5f;
        const IColor c = mMouseIsOver ? CT::fgPrimary : CT::textInk;
        for (int i = 0; i < 8; i++) {   // teeth
            const float a = i * 3.14159265f / 4.0f;
            g.DrawLine(c, cx + std::cos(a)*r*0.55f, cy + std::sin(a)*r*0.55f, cx + std::cos(a)*r, cy + std::sin(a)*r, nullptr, 2.0f);
        }
        g.FillCircle(c, cx, cy, r*0.62f);
        g.FillCircle(CT::bgPrimary, cx, cy, r*0.28f);
    }
    void OnMouseDown(float, float, const IMouseMod&) override { if (mOnClick) mOnClick(); }
    void OnMouseOver(float, float, const IMouseMod&) override { mMouseIsOver = true; SetDirty(false); }
    void OnMouseOut() override { mMouseIsOver = false; SetDirty(false); }
private:
    std::function<void()> mOnClick;
};

// ─── Save icon button (floppy disk) with 2s confirmation tick ────────────────
class SaveIconButton final : public IControl
{
public:
    using OnClickFn = std::function<void()>;
    SaveIconButton(const IRECT& b, OnClickFn fn)
        : IControl(b, kNoParameter), mFn(std::move(fn)) {}

    bool IsDirty() override {
        if (mShowTick) {
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - mSavedAt).count();
            if (ms > 2000) mShowTick = false;
            return true;
        }
        return IControl::IsDirty();
    }

    void Draw(IGraphics& g) override {
        g.FillRoundRect(CT::btnInactive, mRECT, 3.0f);
        float L=mRECT.L+3,T=mRECT.T+3,R=mRECT.R-3,B=mRECT.B-3,W=R-L,H=B-T;
        static const IColor kI(255,40,40,40);
        g.DrawRect(kI, IRECT(L,T,R,B), nullptr,1.5f);
        g.FillRect(IColor(255,220,216,208), IRECT(L+1,T+1,R-1,T+H*0.42f));
        g.FillRect(CT::btnInactive, IRECT(R-W*0.28f,T,R,T+H*0.30f));
        float ds=W*0.18f;
        g.FillRect(IColor(255,100,96,90), IRECT(L+ds,T+1,R-ds,T+H*0.40f));
        g.DrawLine(IColor(255,60,58,54), L+ds+1,T+H*0.15f, R-ds-1,T+H*0.15f, nullptr,1.0f);
        g.DrawLine(IColor(255,60,58,54), L+ds+1,T+H*0.27f, R-ds-1,T+H*0.27f, nullptr,1.0f);
        float sl=L+W*0.22f,sr=R-W*0.22f;
        g.FillRect(IColor(255,190,188,182), IRECT(sl,T+H*0.52f,sr,B-1));
        g.DrawRect(kI, IRECT(sl,T+H*0.52f,sr,B-1), nullptr,1.0f);
        // Green tick overlay for 2 seconds after save
        if (mShowTick) {
            float cx=mRECT.R-7, cy=mRECT.T+7;
            g.FillCircle(IColor(240,45,195,75), cx, cy, 6.5f);
            g.DrawLine(IColor(255,255,255,255), cx-3.5f,cy+0.5f, cx-1.0f,cy+3.0f, nullptr,2.0f);
            g.DrawLine(IColor(255,255,255,255), cx-1.0f,cy+3.0f, cx+3.5f,cy-2.5f, nullptr,2.0f);
        }
    }
    void OnMouseDown(float,float,const IMouseMod&) override {
        if (mFn) {
            mFn();
            mSavedAt = std::chrono::steady_clock::now();
            mShowTick = true;
        }
    }
private:
    OnClickFn mFn;
    bool mShowTick{false};
    std::chrono::steady_clock::time_point mSavedAt;
};

// ─── Vertical band filter slider (HP/LP range selector) ──────────────────────
class BandFilterSlider final : public IControl
{
public:
    BandFilterSlider(const IRECT& b, int loParam, int hiParam,
                     float scaleMinHz=20.0f, float scaleMaxHz=20000.0f)
        : IControl(b, kNoParameter), mLoP(loParam), mHiP(hiParam),
          mLogMin(std::log10(scaleMinHz)), mLogMax(std::log10(scaleMaxHz)) {}

    void Draw(IGraphics& g) override {
        g.FillRoundRect(IColor(255,28,28,32), mRECT, 4.0f);
        const float lblH=13.0f, pad=2.0f;
        const float tT=mRECT.T+lblH+pad, tB=mRECT.B-lblH-pad, tH=tB-tT;

        float loHz=(float)GetDelegate()->GetParam(mLoP)->Value();
        float hiHz=(float)GetDelegate()->GetParam(mHiP)->Value();
        float loY=hzToY(loHz,tT,tH);
        float hiY=hzToY(hiHz,tT,tH);

        static const IColor kBand(150,90,195,175), kEdge(230,90,195,175);
        if (loY>hiY) g.FillRect(kBand, IRECT(mRECT.L+3,hiY,mRECT.R-3,loY));
        g.DrawLine(kEdge, mRECT.L+2,hiY,mRECT.R-2,hiY, nullptr,2.0f);
        g.DrawLine(kEdge, mRECT.L+2,loY,mRECT.R-2,loY, nullptr,2.0f);

        // LP label (top)
        char lb[16];
        if (hiHz>=1000) snprintf(lb,sizeof(lb),"%.0fk",hiHz/1000.0f);
        else            snprintf(lb,sizeof(lb),"%.0f",hiHz);
        IText lt(9.0f,IColor(255,185,185,185),"RobotoMono",EAlign::Center,EVAlign::Middle);
        g.DrawText(lt, lb, IRECT(mRECT.L,mRECT.T+1,mRECT.R,mRECT.T+lblH));

        // HP label (bottom)
        snprintf(lb,sizeof(lb),"%.0f",loHz);
        g.DrawText(lt, lb, IRECT(mRECT.L,mRECT.B-lblH,mRECT.R,mRECT.B));
    }

    void OnMouseDown(float,float y,const IMouseMod&) override {
        const float lblH=13.0f,pad=2.0f;
        const float tT=mRECT.T+lblH+pad,tB=mRECT.B-lblH-pad,tH=tB-tT;
        float loY=hzToY((float)GetDelegate()->GetParam(mLoP)->Value(),tT,tH);
        float hiY=hzToY((float)GetDelegate()->GetParam(mHiP)->Value(),tT,tH);
        mDragLo = (std::abs(y-loY) < std::abs(y-hiY));
        mDragY=y;
        mDragging=true;
        GetDelegate()->BeginInformHostOfParamChangeFromUI(mDragLo?mLoP:mHiP);
    }
    void OnMouseUp(float,float,const IMouseMod&) override {
        if (!mDragging) return;
        mDragging=false;
        GetDelegate()->EndInformHostOfParamChangeFromUI(mDragLo?mLoP:mHiP);
    }
    void OnMouseDrag(float,float y,float,float,const IMouseMod&) override {
        const float lblH=13.0f,pad=2.0f;
        const float tT=mRECT.T+lblH+pad,tB=mRECT.B-lblH-pad,tH=tB-tT;
        float hz=yToHz(y,tT,tH);
        const int pi=mDragLo?mLoP:mHiP;
        auto* p=GetDelegate()->GetParam(pi);
        // Go through the delegate so the host sees the change and saves it
        GetDelegate()->SendParameterValueFromUI(pi, std::clamp(p->ToNormalized(hz),0.0,1.0));
        SetDirty(false);
    }
    void OnMouseDblClick(float,float,const IMouseMod&) override {
        for (int pi : {mLoP, mHiP}) {
            GetDelegate()->BeginInformHostOfParamChangeFromUI(pi);
            GetDelegate()->SendParameterValueFromUI(pi, GetDelegate()->GetParam(pi)->GetDefault(true));
            GetDelegate()->EndInformHostOfParamChangeFromUI(pi);
        }
        SetDirty(false);
    }

private:
    float hzToY(float hz, float tT, float tH) const {
        float minHz=std::pow(10.0f,mLogMin), maxHz=std::pow(10.0f,mLogMax);
        float ln=std::log10(std::clamp(hz,minHz,maxHz));
        return tT+(1.0f-(ln-mLogMin)/(mLogMax-mLogMin))*tH;
    }
    float yToHz(float y, float tT, float tH) const {
        float n=std::clamp(1.0f-(y-tT)/tH,0.0f,1.0f);
        return std::pow(10.0f, mLogMin+n*(mLogMax-mLogMin));
    }
    int mLoP,mHiP; bool mDragLo{false}, mDragging{false}; float mDragY{0};
    float mLogMin, mLogMax;
};

// ─── Tiny drag-value control (for reverb decay etc.) ─────────────────────────
class TinyDragValue final : public IControl
{
public:
    TinyDragValue(const IRECT& b, int param)
        : IControl(b, param) {}

    void Draw(IGraphics& g) override {
        g.FillRoundRect(CT::btnInactive, mRECT, 3.0f);
        WDL_String vs; GetParam()->GetDisplay(vs,false);
        const char* u=GetParam()->GetLabel();
        if (u&&u[0]) vs.Append(u);
        IText t(10.0f,CT::textInk,"RobotoMono",EAlign::Center,EVAlign::Middle);
        g.DrawText(t, vs.Get(), IRECT(mRECT.L,mRECT.T,mRECT.R,mRECT.B));
        // drag arrows
        float ax=mRECT.R-6.0f, ay=mRECT.MH();
        g.DrawLine(CT::fgPrimary,ax-2,ay+3,ax,ay-1,nullptr,1.0f);
        g.DrawLine(CT::fgPrimary,ax,ay-1,ax+2,ay+3,nullptr,1.0f);
    }
    void OnAttached() override {if(GetParam())SetValue(GetParam()->GetNormalized());}
    void OnMouseDown(float,float y,const IMouseMod& m) override
        {mDY=y;mSV=GetValue();IControl::OnMouseDown(0,y,m);}
    void OnMouseDblClick(float,float,const IMouseMod&) override
        {SetValue(GetParam()->GetDefault(true));SetDirty(true);}
    void OnMouseDrag(float,float y,float,float,const IMouseMod& m) override {
        double s=m.S?2000.0:300.0;
        SetValue(std::clamp(mSV-(y-mDY)/s,0.0,1.0));SetDirty(true);
    }
    void OnMouseWheel(float,float,const IMouseMod& m,float d) override {
        SetValue(std::clamp(GetValue()+d*(m.S?0.001:0.005),0.0,1.0));SetDirty(true);
    }
private:
    float mDY{0}; double mSV{0};
};

// ─── Vertical volume slider with VU meter ─────────────────────────────────────
class VolumeSlider final : public IControl
{
public:
    VolumeSlider(const IRECT& b, int param,
                 std::atomic<float>* vuL, std::atomic<float>* vuR)
        : IControl(b, param), mVuL(vuL), mVuR(vuR) {}

    bool IsDirty() override {
        float nl=mVuL?mVuL->load():0.0f, nr=mVuR?mVuR->load():0.0f;
        if (std::abs(nl-mLastL)>0.005f||std::abs(nr-mLastR)>0.005f){
            mLastL=nl;mLastR=nr;return true;
        }
        return IControl::IsDirty();
    }

    void Draw(IGraphics& g) override {
        float val = GetParam() ? (float)GetParam()->GetNormalized() : (float)GetValue();
        float linL=mVuL?mVuL->load():0.0f;
        float linR=mVuR?mVuR->load():0.0f;

        // Convert linear peak to dB-normalised for display (range -60 to +6 dB)
        static constexpr float kDbMin=-60.0f, kDbMax=6.0f, kDbRng=66.0f;
        auto toNorm=[](float lin)->float{
            if(lin<1e-6f) return 0.0f;
            float db=20.0f*std::log10(lin);
            return std::clamp((db-kDbMin)/kDbRng, 0.0f, 1.0f);
        };
        float vuNL=toNorm(linL), vuNR=toNorm(linR);
        // Color thresholds: yellow starts at -6dB (54/66≈0.818), red above 0dB (60/66≈0.909)
        static constexpr float kYel=54.0f/66.0f, kRed=60.0f/66.0f;

        const float vuW=9.0f, slW=14.0f;
        const float h=mRECT.H()-4.0f, top=mRECT.T+2.0f, bot=top+h;

        // VU Left
        float lx=mRECT.L;
        g.FillRoundRect(IColor(255,50,50,54), IRECT(lx,top,lx+vuW,bot),3.0f);
        float lH=vuNL*h;
        if(lH>0.5f){
            IColor vc=vuNL>kRed?IColor(255,240,60,50):vuNL>kYel?IColor(255,220,200,50):IColor(255,80,200,100);
            g.FillRoundRect(vc,IRECT(lx+1,bot-lH,lx+vuW-1,bot-1),2.0f);
        }
        // VU Right
        float rx=lx+vuW+2.0f;
        g.FillRoundRect(IColor(255,50,50,54), IRECT(rx,top,rx+vuW,bot),3.0f);
        float rH=vuNR*h;
        if(rH>0.5f){
            IColor vc=vuNR>kRed?IColor(255,240,60,50):vuNR>kYel?IColor(255,220,200,50):IColor(255,80,200,100);
            g.FillRoundRect(vc,IRECT(rx+1,bot-rH,rx+vuW-1,bot-1),2.0f);
        }
        // Slider track
        float sx=rx+vuW+2.0f+3.0f;
        g.FillRoundRect(IColor(255,55,55,60),IRECT(sx,top,sx+slW,bot),4.0f);
        float fillH=val*h;
        if(fillH>0.5f)
            g.FillRoundRect(IColor(255,180,175,168),IRECT(sx+1,bot-fillH,sx+slW-1,bot-1),3.0f);
        // Handle
        float hy=bot-fillH;
        g.FillRect(IColor(255,228,224,218),IRECT(sx-2,hy-1.5f,sx+slW+2,hy+1.5f));
        // 0dB mark: param range -60..+6dB, so norm(0dB) = 60/66
        float zY=bot-kRed*h;
        g.DrawLine(IColor(255,200,170,60),sx-3,zY,sx+slW+3,zY,nullptr,1.2f);
    }

    void OnAttached() override {if(GetParam())SetValue(GetParam()->GetNormalized());}
    void OnMouseDown(float,float y,const IMouseMod& m) override
        {mDY=y;mSV=GetValue();IControl::OnMouseDown(0,y,m);}
    void OnMouseDblClick(float,float,const IMouseMod&) override
        {SetValue(GetParam()->GetDefault(true));SetDirty(true);}
    void OnMouseDrag(float,float y,float,float,const IMouseMod& m) override {
        double s=m.S?2000.0:400.0;
        SetValue(std::clamp(mSV-(y-mDY)/s,0.0,1.0));SetDirty(true);
    }
    void OnMouseWheel(float,float,const IMouseMod& m,float d) override {
        SetValue(std::clamp(GetValue()+d*(m.S?0.002:0.01),0.0,1.0));SetDirty(true);
    }
private:
    std::atomic<float>* mVuL; std::atomic<float>* mVuR;
    float mDY{0}; double mSV{0};
    float mLastL{-1},mLastR{-1};
};

// ─── Simple clickable button ──────────────────────────────────────────────────
class ButtonControl final : public IControl
{
public:
    using OnClickFn = std::function<void(IGraphics*)>;
    ButtonControl(const IRECT& b, const char* lbl, OnClickFn fn)
        : IControl(b, kNoParameter), mLabel(lbl), mFn(std::move(fn)) {}
    void Draw(IGraphics& g) override {
        g.FillRoundRect(CT::btnInactive, mRECT, 2.0f);
        IText t(10.0f,CT::textInk,"RobotoMono",EAlign::Center,EVAlign::Middle);
        g.PathClipRegion(mRECT);   // narrow panel: keep the label inside the button
        g.DrawText(t, mLabel, mRECT);
        g.PathClipRegion();
    }
    void OnMouseDown(float,float,const IMouseMod&) override {if(mFn)mFn(GetUI());}
private:
    const char* mLabel; OnClickFn mFn;
};

// ─── DIST mode: one button under the knob, each click moves to the next mode ──
class CycleModeButton final : public IControl
{
public:
    CycleModeButton(const IRECT& b, int param, const char* m0, const char* m1, const char* m2)
        : IControl(b, param), mNames{m0, m1, m2} {}
    void Draw(IGraphics& g) override {
        const int m = std::clamp((int)(GetParam()->Value() + 0.5), 0, 2);
        g.FillRoundRect(CT::btnInactive, mRECT, 3.0f);
        IText t(9.0f, CT::textInk, "RobotoMono", EAlign::Center, EVAlign::Middle);
        g.DrawText(t, mNames[m], mRECT);
    }
    void OnMouseDown(float, float, const IMouseMod&) override {
        const int next = ((int)(GetParam()->Value() + 0.5) + 1) % 3;
        SetValue(GetParam()->ToNormalized((double)next)); SetDirty(true);
    }
private:
    const char* mNames[3];
};

// ─── Reset preset: black X, same size and style as the save icon ─────────────
class ResetIconButton final : public IControl
{
public:
    ResetIconButton(const IRECT& b, std::function<void()> fn)
        : IControl(b, kNoParameter), mFn(std::move(fn)) {}
    void Draw(IGraphics& g) override {
        g.FillRoundRect(CT::btnInactive, mRECT, 3.0f);
        const float in = 8.0f;
        g.DrawLine(CT::fgPrimary, mRECT.L+in, mRECT.T+in, mRECT.R-in, mRECT.B-in, nullptr, 2.0f);
        g.DrawLine(CT::fgPrimary, mRECT.R-in, mRECT.T+in, mRECT.L+in, mRECT.B-in, nullptr, 2.0f);
    }
    void OnMouseDown(float, float, const IMouseMod&) override { if (mFn) mFn(); }
private:
    std::function<void()> mFn;
};

// ─── Static image (COMANCHERIA logo) ─────────────────────────────────────────
class LogoControl final : public IControl
{
public:
    LogoControl(const IRECT& b, const IBitmap& bmp) : IControl(b, kNoParameter), mBitmap(bmp) { SetIgnoreMouse(true); }
    void Draw(IGraphics& g) override { if (mBitmap.IsValid()) g.DrawFittedBitmap(mBitmap, mRECT); }
private:
    IBitmap mBitmap;
};

// ─── Horse scene: open band behind the MACRO with a horse whose gait follows it ─
// The horse is a pre-rendered sprite sheet (12 frames x 3 gaits: walk, trot, gallop,
// baked by the UI Lab at its exact on-screen size). Per frame this only picks a cell
// and draws a handful of lines, so the cost is one bitmap blit.
class HorseScene final : public IControl
{
public:
    HorseScene(const IRECT& band, const IRECT& horse, const IBitmap& sheet, const std::atomic<float>& macroShown)
        : IControl(band, kNoParameter), mHorse(horse), mSheet(sheet), mMacroShown(macroShown)
    { SetIgnoreMouse(true); mLast = std::chrono::steady_clock::now(); }

    bool IsDirty() override { return true; }   // always animating: the horse walks even at MACRO 0

    void Draw(IGraphics& g) override {
        step();
        const float L = mRECT.L, T = mRECT.T, R = mRECT.R, B = mRECT.B, w = mRECT.W(), h = mRECT.H();
        const float k = mHorse.W() / kFW, gy = mHorse.T + kGround * k;
        const float edge = std::min(70.0f, w * 0.18f);
        auto fade = [&](float x) { float f = std::clamp(std::min(x - L, R - x) / edge, 0.0f, 1.0f); return f*f*(3.0f - 2.0f*f); };
        auto withA = [](IColor c, float a) { c.A = (int)std::clamp(a * 255.0f, 0.0f, 255.0f); return c; };
        auto wrap = [](float v, float m) { return v - m * std::floor(v / m); };
        g.PathClipRegion(mRECT);

        // Halo: wide soft ellipse behind the horse, fades in with the MACRO
        if (mSmooth > 0.01f) {
            const float m = mSmooth, cx = mHorse.L + mHorse.W() * 0.55f, cy = (T + B) * 0.5f, ry = h * 0.5f;
            const float rx = std::min(std::max(mHorse.W() * 1.1f, h), std::min(cx - L, R - cx) + mHorse.W() * 0.4f);
            const IColor c = CT::horseGlow;
            g.PathTransformSave();
            g.PathTransformTranslate(cx, cy);
            g.PathTransformScale(rx / ry, 1.0f);
            g.PathCircle(0, 0, ry);
            g.PathFill(IPattern::CreateRadialGradient(0, 0, ry, {
                {withA(c, 0.70f*m), 0.0f}, {withA(c, 0.48f*m), 0.3f}, {withA(c, 0.22f*m), 0.55f},
                {withA(c, 0.06f*m), 0.8f}, {withA(c, 0.0f), 1.0f} }));
            g.PathTransformRestore();
        }
        // Ground line, faded towards both ends
        for (float x = L; x < R; x += 6.0f) {
            const float a = fade(x + 3.0f);
            if (a > 0.01f) g.DrawLine(withA(CT::horseInk, 0.85f*a), x, gy + 0.5f, x + 6.5f, gy + 0.5f + std::sin(x * 0.07f) * 0.4f, nullptr, 1.4f);
        }
        // Ground marks: move against the horse, locked to the frames (parallax rows)
        const float sc = mScroll * k;
        const int nMarks = (int)std::ceil(w / 40.0f);
        for (int row = 0; row < 3; row++) {
            const float sp = row == 0 ? 1.0f : row == 1 ? 0.7f : 0.45f, y = gy + 3.0f + row * 3.5f;
            for (int i = 0; i < nMarks; i++) {
                const float x = L + wrap(i * 41.0f + row * 17.0f - sc * sp, w + 20.0f) - 10.0f;
                g.DrawLine(withA(CT::horseWind, (0.55f - row * 0.15f) * fade(x)), x, y, x + 6.0f - row * 1.5f, y, nullptr, 1.0f);
            }
        }
        // Wind streaks: appear and lengthen as the MACRO opens (deterministic pseudo-random layout)
        if (mRide > 0.25f) {
            const float a = (mRide - 0.25f) / 0.75f;
            uint32_t seed = 23;
            auto rnd = [&seed]() { seed = seed * 1664525u + 1013904223u; return (float)(seed >> 8) / 16777216.0f; };
            const int n = (int)std::ceil(w / 22.0f);
            for (int i = 0; i < n; i++) {
                const float y = T + 6.0f + rnd() * (gy - T - 10.0f), len = 8.0f + 34.0f * a * (0.5f + rnd());
                const float v = (1.6f + rnd()) * mWind * k, x = L + wrap(rnd() * (w + len) - v, w + len) - len;
                const float al = (0.15f + 0.55f * a) * (0.5f + rnd() * 0.5f) * fade(x + len * 0.5f), lw = 0.9f + rnd() * 0.7f;
                g.DrawLine(withA(CT::horseWind, al), x, y, x + len, y + (rnd() - 0.5f), nullptr, lw);
            }
        }
        // Horse: one cell of the sprite sheet
        if (mSheet.IsValid()) {
            const int col = (int)(mPhase * kN) % kN;
            g.DrawBitmap(mSheet, mHorse, (int)(col * mHorse.W()), (int)(mGait * mHorse.H()));
        }
        g.PathClipRegion();
    }

private:
    struct Gait { float duty, stride, spsLo, spsHi; };
    static constexpr int   kN = 12;
    static constexpr float kFW = 180.0f, kGround = 110.0f;   // horse model units (UI Lab)

    void step() {
        const auto now = std::chrono::steady_clock::now();
        const float dt = std::min(0.1f, std::chrono::duration<float>(now - mLast).count());
        mLast = now;
        const float shown = mMacroShown.load();
        const float m = shown >= 0.0f ? shown : (float)GetDelegate()->GetParam(kMacro)->GetNormalized();
        mSmooth += (1.0f - std::exp(-dt / 0.05f)) * (m - mSmooth);   // same 50 ms smoothing as the DSP
        // The ride eases on its own, through two stages for an S-shaped change: it takes about half a
        // second to settle, so a curve on the MACRO doesn't snap the horse between standstill and gallop
        const float ease = 1.0f - std::exp(-dt / 0.12f);
        mRideIn += ease * (m - mRideIn);
        mRide   += ease * (mRideIn - mRide);
        static const Gait kGaits[3] = { {0.64f, 22.0f, 0.7f, 1.0f}, {0.42f, 34.0f, 1.2f, 1.6f}, {0.30f, 52.0f, 1.8f, 2.6f} };
        static const float lo[3] = {0.0f, 0.34f, 0.67f}, hi[3] = {0.34f, 0.67f, 1.0f};
        // Walk / trot / gallop bands with hysteresis, stride rate rising inside each band
        if (mGait < 2 && mRide > hi[mGait] + 0.02f) mGait++;
        else if (mGait > 0 && mRide < lo[mGait] - 0.02f) mGait--;
        const Gait& gt = kGaits[mGait];
        const float f = std::clamp((mRide - lo[mGait]) / (hi[mGait] - lo[mGait]), 0.0f, 1.0f);
        const float sps = gt.spsLo + (gt.spsHi - gt.spsLo) * f;
        // Ground advances only when the frame changes, by what a planted hoof moves in one frame
        const int prev = (int)(mPhase * kN);
        mPhase = std::fmod(mPhase + dt * sps, 1.0f);
        const int steps = ((int)(mPhase * kN) - prev + kN) % kN;
        mScroll += steps * gt.stride / (gt.duty * kN);
        mWind += dt * sps * gt.stride / gt.duty;
    }

    IRECT mHorse;
    IBitmap mSheet;
    std::chrono::steady_clock::time_point mLast;
    const std::atomic<float>& mMacroShown;
    float mSmooth{0.0f}, mRideIn{0.0f}, mRide{0.0f}, mPhase{0.0f}, mScroll{0.0f}, mWind{0.0f};
    int mGait{0};
};

// ─── Main plugin class ────────────────────────────────────────────────────────
class COMANCHE final : public Plugin
{
public:
    COMANCHE(const InstanceInfo& info);
    ~COMANCHE() override;
#if COMANCHE_TRACE && defined VST3_API
    // Diagnostic: log every host call before passing it to iPlug2
    using tresult = Steinberg::tresult;
    tresult PLUGIN_API initialize(Steinberg::FUnknown* c) override { ctrace("VST3 initialize"); auto r = Plugin::initialize(c); ctrace("VST3 initialize -> %d", (int)r); return r; }
    tresult PLUGIN_API terminate() override { ctrace("VST3 terminate"); return Plugin::terminate(); }
    tresult PLUGIN_API setBusArrangements(Steinberg::Vst::SpeakerArrangement* i, Steinberg::int32 ni, Steinberg::Vst::SpeakerArrangement* o, Steinberg::int32 no) override
        { ctrace("VST3 setBusArrangements ins=%d outs=%d", (int)ni, (int)no); auto r = Plugin::setBusArrangements(i, ni, o, no); ctrace("VST3 setBusArrangements -> %d", (int)r); return r; }
    tresult PLUGIN_API setActive(Steinberg::TBool s) override { ctrace("VST3 setActive %d", (int)s); auto r = Plugin::setActive(s); ctrace("VST3 setActive -> %d", (int)r); return r; }
    tresult PLUGIN_API setupProcessing(Steinberg::Vst::ProcessSetup& s) override
        { ctrace("VST3 setupProcessing sr=%.0f maxBlock=%d mode=%d size=%d", s.sampleRate, (int)s.maxSamplesPerBlock, (int)s.processMode, (int)s.symbolicSampleSize); auto r = Plugin::setupProcessing(s); ctrace("VST3 setupProcessing -> %d", (int)r); return r; }
    tresult PLUGIN_API setProcessing(Steinberg::TBool s) override { ctrace("VST3 setProcessing %d", (int)s); return Plugin::setProcessing(s); }
    tresult PLUGIN_API setState(Steinberg::IBStream* s) override { ctrace("VST3 setState"); auto r = Plugin::setState(s); ctrace("VST3 setState -> %d", (int)r); return r; }
    tresult PLUGIN_API getState(Steinberg::IBStream* s) override { ctrace("VST3 getState"); auto r = Plugin::getState(s); ctrace("VST3 getState -> %d", (int)r); return r; }
    tresult PLUGIN_API setComponentState(Steinberg::IBStream* s) override { ctrace("VST3 setComponentState"); return Plugin::setComponentState(s); }
    Steinberg::IPlugView* PLUGIN_API createView(const char* n) override { ctrace("VST3 createView %s", n ? n : "(null)"); auto v = Plugin::createView(n); ctrace("VST3 createView -> %p", (void*)v); return v; }
    tresult PLUGIN_API setEditorState(Steinberg::IBStream* s) override { ctrace("VST3 setEditorState"); return Plugin::setEditorState(s); }
    tresult PLUGIN_API getEditorState(Steinberg::IBStream* s) override { ctrace("VST3 getEditorState"); return Plugin::getEditorState(s); }
#endif
#if IPLUG_DSP
    void ProcessBlock(sample** inputs, sample** outputs, int nFrames) override;
    void ProcessMidiMsg(const IMidiMsg& msg) override;
    void OnReset() override;
    bool SerializeState(IByteChunk& chunk) const override;
    int  UnserializeState(const IByteChunk& chunk, int startPos) override;
#endif
#if IPLUG_EDITOR
    void OnUIOpen() override;
    void OnRestoreState() override;
    bool OnMessage(int msgTag, int ctrlTag, int dataSize, const void* pData) override;
#endif
    SampleLibrary mLibrary;
    PresetManager mPresets;
    int mSelectedIdx{-1};
    int mMacroLink[kNumParams]{};
    std::atomic<float> mVuPeakL{0.0f}, mVuPeakR{0.0f};
    // Slow per-param LFO modulation driven by Macro (written audio thread, read UI thread)
    float mModPhase[kNumParams]{};
    float mParamMod[kNumParams]{};
    float mSmoothMacro{0.0f};
    void loadSample(int idx);
    void savePreset();
    void removeFromLibrary(int idx);
    void resetPresetForSample(int idx);
    // Reset button (X): all params back to their defaults, macro links cleared
    void resetPreset();

    // ── Sound types (badges, filter, auto-loop) ──
    SoundType soundTypeOf(int libraryIdx);
    int  mTypeFilter{0};                 // 0 = all, else (int)SoundType
    std::atomic<int> mTypesVersion{0};   // bumped when the background analysis classifies a sample
    // Settings (gear): loop Bass / Lead-Pad while the key is held, mono or poly playback
    std::atomic<bool> mLoopEnabled{true};   // set per sound on load: on for Bass / Lead-Pad, off otherwise
    static constexpr const char* kPresetLoopKey = "Loop";   // per-sound preset entry for the loop switch
    static constexpr const char* kPresetLoopStartKey = "LoopStart";   // hand-placed loop zone (fractions)
    static constexpr const char* kPresetLoopEndKey   = "LoopEnd";
    std::atomic<bool> mMono{false};
    std::atomic<float> mGlideMs{80.0f};   // portamento between legato notes in mono + loop (settings only)
    // MACRO on a curve (right click on the MACRO). The knob's own value stays untouched underneath.
    std::atomic<bool>  mMacroCurveOn{false};
    static constexpr float kDefaultCurveShape = 2.0f, kDefaultCurveRate = 2.0f, kDefaultCurveMs = 500.0f;
    static constexpr const char* kPresetCurveOnKey    = "CurveOn";   // per-sound preset entries
    static constexpr const char* kPresetCurveShapeKey = "CurveShape";
    static constexpr const char* kPresetCurveRateKey  = "CurveRate";
    static constexpr const char* kPresetCurveMsKey    = "CurveMs";
    std::atomic<int>   mMacroShape{(int)kDefaultCurveShape}, mMacroRate{(int)kDefaultCurveRate};
    std::atomic<float> mMacroFreeMs{kDefaultCurveMs};
    void resetMacroCurve();
    std::atomic<float> mMacroShown{-1.0f};   // where the curve has it now, < 0 when it isn't on a curve
    std::atomic<float> mMacroPhase{0.0f};
    double mMacroPhaseRun{0.0};
    float  macroCurveValue(int nFrames);
    std::atomic<float> mVelDynamics{1.0f};   // how much key velocity shapes note level: 1 = fully, 0 = always full level
    void setLoopEnabled(bool on);
    void savePersistentState() const;
    void loadPersistentState();
    // Set a param from plugin code on the UI thread: informs the host + syncs controls
    void setParamFromUI(int idx, double normalized);
    // Tell the host that non-param state (sample, macro links, folder) changed
    void markStateDirty();
    int  mTraceBlocks = 0;   // diagnostic: log the first audio blocks
private:
    struct Voice {
        bool   active{false},inRelease{false};
        int    noteNum{-1},startSample{0};
        double srcPos{0.0},pitchRatio{1.0};
        float  level{0.0f},relLeft{0.0f},relTotal{0.0f};
        // Pitch in semitones from the root (C3); glides toward targetSemis by glideStep per sample
        double semis{0.0},targetSemis{0.0},glideStep{0.0};
        double fadePos{-1.0};   // position in the previous loop while crossfading to a moved loop (< 0: none)
    };
    double ratioFor(double semis) const;   // playback rate for a pitch, incl. sample-rate conversion
    // Keys held down, oldest first: mono glide returns to the previous one when the newest is released
    std::array<int,16> mHeld{}; int mNumHeld{0};
    void glideVoice(Voice& v, int note);
    static constexpr int kNumVoices=8;
    std::array<Voice,kNumVoices> mVoices;
    int mVoiceTimer{0};
    std::vector<float> mSampleL,mSampleR;
    std::mutex   mSampleMutex;   // guards sample buffers (audio thread only try_locks)
    std::string  mSamplePath;    // loaded sample — saved with the project
    bool loadSampleData(const std::string& path);
    // Crossfaded copy for looping + loop points; used when the loaded sample's type loops
    std::vector<float> mLoopL, mLoopR;
    int  mLoopStart{0}, mLoopEnd{0};
    bool mHasLoop{false}, mLoopActive{false};
    // Loop zone as fractions of the sound, for the settings timeline. Manual = placed by hand, else detected
    std::atomic<float> mLoopStartN{0.5f}, mLoopEndN{1.0f};
    std::atomic<bool>  mLoopManual{false};
    std::vector<float> mPrevLoopL, mPrevLoopR;   // copy being faded out after the loop moved
    int  mPrevLoopStart{0}, mPrevLoopEnd{0};
    int  mLoopFadeLeft{0}, mLoopFadeTotal{1};
    std::chrono::steady_clock::time_point mLastLoopBuild{};
    std::chrono::steady_clock::time_point mLastArrowKey{};
    void rebuildLoop(int loopStart, int loopEnd);
    void setLoopRegion(float startN, float endN, bool final);
    void resetLoopRegion();
    SoundType mSampleType{SoundType::Pending};
    void updateLoopActive();   // call with mSampleMutex held

    // Background classification of the library (name first, audio only when needed)
    std::mutex mTypesMutex;
    std::unordered_map<std::string, SoundType> mTypes;   // by sample path
    std::thread mClassifyThread;
    std::atomic<bool> mClassifyStop{false};
    void startClassification();
    void stopClassification();
    int    mSampleLen{0};
    double mSampleSR{44100.0};
    EffectsChain mFX;
    IMidiQueue   mMidiQueue;
    std::string  mFolderPath;
    static constexpr int kMaxBlock=4096;
    float mTmpL[kMaxBlock]{},mTmpR[kMaxBlock]{};
    void noteOn(int note,float vel);
    void noteOff(int note);
    int  stealVoice();
    static float lerp(const std::vector<float>& b, double pos, int loopEnd = -1, int loopStart = 0);
};
