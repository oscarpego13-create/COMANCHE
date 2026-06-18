#pragma once

#include "IPlug_include_in_plug_hdr.h"
#include "IControls.h"
#include "IPlugMidi.h"

#include <vector>
#include <string>
#include <array>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <functional>

#include "EffectsChain.h"
#include "SampleLibrary.h"
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
    kDelayFeedback,
    kDelayLowcut,
    kDelayHighcut,
    kChorusAmount,
    kHpFreq,
    kLpFreq,
    kOutputVol,
    kMacro,
    kNumParams
};

// ─── Cross-thread message tags ───────────────────────────────────────────────
enum EMsgTags {
    kMsgLoadSample = 0,
};

// ─── Design system ───────────────────────────────────────────────────────────
namespace CT {
    static const IColor bgPrimary   {255,237,234,228};
    static const IColor bgPanel     {255,226,222,215};
    static const IColor fgPrimary   {255, 26, 26, 26};
    static const IColor knobGrey    {255,155,155,142};
    static const IColor knobBlue    {255,139,163,184};
    static const IColor knobGold    {255,184,168,130};
    static const IColor btnActive   {255, 26, 26, 26};
    static const IColor btnInactive {255,184,168,130};
    static const IColor textLight   {255,237,234,228};
    static const IColor boxReverb   {255,210,228,242};
    static const IColor boxDist     {255,245,228,205};
    static const IColor boxDelay    {255,225,215,242};
    static const IColor boxChorus   {255,210,238,222};
    static const IColor boxOutput   {255,232,230,226};
    static const IColor macroOff    {255,150,148,144};
    static const IColor macroPos    {255, 80,200,100};
    static const IColor macroNeg    {255,230,140, 50};
}

// ─── Small macro link button ──────────────────────────────────────────────────
class MacroLinkButton final : public IControl
{
public:
    MacroLinkButton(const IRECT& b, int& state)
        : IControl(b, kNoParameter), mState(state) {}

    void Draw(IGraphics& g) override {
        IColor c = (mState ==  1) ? CT::macroPos
                 : (mState == -1) ? CT::macroNeg
                                  : CT::macroOff;
        float r = mRECT.W() * 0.5f - 0.5f;
        g.FillCircle(c, mRECT.MW(), mRECT.MH(), r);
        g.DrawCircle(IColor(180,0,0,0), mRECT.MW(), mRECT.MH(), r, nullptr, 0.7f);
    }

    void OnMouseDown(float, float, const IMouseMod&) override {
        if (mState == 0)      mState =  1;
        else if (mState == 1) mState = -1;
        else                  mState =  0;
        if (GetUI()) GetUI()->SetAllControlsDirty();
    }

private:
    int& mState;
};

// ─── Flat knob with macro arc overlay ────────────────────────────────────────
class ComanacheKnob final : public IControl
{
public:
    ComanacheKnob(const IRECT& b, int param, const char* lbl,
                  IColor fill, bool isMacro = false, int* linkState = nullptr)
        : IControl(b, param), mLabel(lbl), mFill(fill),
          mIsMacro(isMacro), mLinkState(linkState) {}

    void Draw(IGraphics& g) override {
        const float cx  = mRECT.MW();
        const float kR  = (mIsMacro ? 0.40f : 0.35f) * mRECT.W();
        const float kCy = mRECT.T + kR + 4.0f;
        static const IColor kNeedle(255,26,26,26);

        g.FillCircle(mFill,   cx, kCy, kR);
        g.DrawCircle(kNeedle, cx, kCy, kR-0.5f, nullptr, 1.8f);

        double baseNorm = GetValue();
        double angle = (-135.0 + baseNorm*270.0) * (M_PI/180.0);
        float  reach = kR - 0.5f;
        g.DrawLine(kNeedle, cx, kCy,
                   cx + (float)(std::sin(angle)*reach),
                   kCy - (float)(std::cos(angle)*reach),
                   nullptr, 2.2f);

        // Macro arc overlay (blue ring showing contribution)
        if (!mIsMacro && mLinkState && *mLinkState != 0) {
            float macro = (float)GetDelegate()->GetParam(kMacro)->GetNormalized();
            if (macro > 0.001f) {
                double effNorm = std::clamp(baseNorm + macro * (double)(*mLinkState), 0.0, 1.0);
                double startA  = (-135.0 + baseNorm * 270.0) * (M_PI/180.0);
                double endA    = (-135.0 + effNorm  * 270.0) * (M_PI/180.0);
                if (std::abs(endA - startA) > 0.01) {
                    static const IColor kArc(220, 80,140,255);
                    float arcR = kR + 1.8f;
                    int steps  = 24;
                    for (int s = 1; s <= steps; s++) {
                        double a0 = startA + (endA-startA)*(s-1.0)/steps;
                        double a1 = startA + (endA-startA)*(double)s/steps;
                        g.DrawLine(kArc,
                            cx + arcR*(float)std::sin(a0), kCy - arcR*(float)std::cos(a0),
                            cx + arcR*(float)std::sin(a1), kCy - arcR*(float)std::cos(a1),
                            nullptr, 3.0f);
                    }
                }
            }
        }

        // Value + unit text
        WDL_String vs;
        GetParam()->GetDisplay(vs, false);
        const char* u = GetParam()->GetLabel();
        if (u && u[0]) vs.Append(u);
        IText vt(mIsMacro ? 13.0f : 11.0f, CT::fgPrimary, "RobotoMono",
                 EAlign::Center, EVAlign::Middle);
        g.DrawText(vt, vs.Get(), IRECT(mRECT.L, kCy+kR+2, mRECT.R, kCy+kR+17));
        IText lt(9.0f, CT::fgPrimary, "RobotoMono", EAlign::Center, EVAlign::Middle);
        g.DrawText(lt, mLabel, IRECT(mRECT.L, kCy+kR+17, mRECT.R, kCy+kR+31));
    }

    void OnAttached() override { if (GetParam()) SetValue(GetParam()->GetNormalized()); }
    void OnMouseDown(float, float y, const IMouseMod& m) override
        { mDY=y; mSV=GetValue(); IControl::OnMouseDown(0,y,m); }
    void OnMouseDblClick(float, float, const IMouseMod&) override
        { SetValue(GetParam()->GetDefault(true)); SetDirty(true); }
    void OnMouseDrag(float, float y, float, float, const IMouseMod& m) override {
        double s = m.S ? 2000.0 : 200.0;
        SetValue(std::clamp(mSV-(y-mDY)/s, 0.0, 1.0)); SetDirty(true);
    }
    void OnMouseWheel(float, float, const IMouseMod& m, float d) override {
        SetValue(std::clamp(GetValue()+d*(m.S?0.002:0.01),0.0,1.0)); SetDirty(true);
    }

private:
    const char* mLabel; IColor mFill; bool mIsMacro; int* mLinkState;
    float mDY{0}; double mSV{0};
};

// ─── Mode toggle button ───────────────────────────────────────────────────────
class ModeButton final : public IControl
{
public:
    ModeButton(const IRECT& b, int param, int modeValue, const char* lbl)
        : IControl(b, param), mMode(modeValue), mLabel(lbl) {}

    void Draw(IGraphics& g) override {
        bool active = ((int)(GetParam()->Value()+0.5) == mMode);
        g.FillRoundRect(active ? CT::btnActive : CT::btnInactive, mRECT, 2.0f);
        IText t(9.0f, active ? CT::textLight : CT::fgPrimary,
                "RobotoMono", EAlign::Center, EVAlign::Middle);
        g.DrawText(t, mLabel, mRECT);
    }
    void OnMouseDown(float, float, const IMouseMod&) override {
        SetValue(GetParam()->ToNormalized((double)mMode)); SetDirty(true);
    }
private:
    int mMode; const char* mLabel;
};

// ─── Scrollable sample list with scrollbar ───────────────────────────────────
class SampleListControl final : public IControl
{
public:
    SampleListControl(const IRECT& b, const std::vector<std::string>& names, int& selectedIdx)
        : IControl(b, kNoParameter), mNames(names), mSelected(selectedIdx) {}

    void Draw(IGraphics& g) override {
        const float sbW  = 12.0f;
        const float rowH = 22.0f;
        const IRECT listR(mRECT.L, mRECT.T, mRECT.R - sbW - 1, mRECT.B);
        const IRECT sbR  (mRECT.R - sbW, mRECT.T, mRECT.R, mRECT.B);

        g.FillRect(CT::bgPanel, listR);

        int maxRows = (int)(listR.H() / rowH);
        for (int i = mScroll; i < (int)mNames.size() && (i-mScroll) < maxRows; i++) {
            IRECT row(listR.L, listR.T+(i-mScroll)*rowH,
                      listR.R, listR.T+(i-mScroll+1)*rowH);
            bool sel = (i == mSelected);
            if (sel) g.FillRect(CT::fgPrimary, row);
            IText t(11.0f, sel ? CT::textLight : CT::fgPrimary,
                    "RobotoMono", EAlign::Near, EVAlign::Middle);
            g.DrawText(t, mNames[i].c_str(), IRECT(row.L+6, row.T, row.R, row.B));
        }

        // Scrollbar track
        g.FillRoundRect(IColor(255,120,118,114), sbR, 6.0f);
        if (total() > maxRows) {
            float thumbH = std::max(20.0f, sbR.H() * (float)maxRows / (float)total());
            float maxSc  = (float)(total() - maxRows);
            float tp     = (maxSc > 0) ? (float)mScroll / maxSc : 0.0f;
            float thumbY = sbR.T + tp * (sbR.H() - thumbH);
            g.FillRoundRect(IColor(255,190,185,180),
                IRECT(sbR.L+2, thumbY, sbR.R-2, thumbY+thumbH), 5.0f);
        }
    }

    void OnMouseDown(float x, float y, const IMouseMod&) override {
        const float sbW  = 12.0f;
        const float rowH = 22.0f;
        if (x >= mRECT.R - sbW) { mSbDrag=true; mSbY=y; return; }
        int row = mScroll + (int)((y - mRECT.T) / rowH);
        if (row >= 0 && row < total()) {
            mSelected = row;
            GetDelegate()->SendArbitraryMsgFromUI(kMsgLoadSample, kNoTag,
                sizeof(int), &mSelected);
            SetDirty(false);
        }
    }
    void OnMouseUp(float, float, const IMouseMod&) override { mSbDrag = false; }
    void OnMouseDrag(float, float y, float, float dy, const IMouseMod&) override {
        const float rowH = 22.0f;
        int maxRows = (int)(mRECT.H() / rowH);
        int maxSc   = std::max(0, total() - maxRows);
        if (mSbDrag) {
            float sbH    = mRECT.H();
            float thumbH = std::max(20.0f, sbH * (float)maxRows / std::max(1, total()));
            float range  = sbH - thumbH;
            if (range > 0)
                mScroll = std::clamp((int)(((y-mRECT.T)/range) * maxSc), 0, maxSc);
        } else {
            mScroll = std::clamp(mScroll + (int)(-dy / rowH), 0, maxSc);
        }
        SetDirty(false);
    }
    void OnMouseWheel(float, float, const IMouseMod&, float d) override {
        const float rowH = 22.0f;
        int maxRows = (int)(mRECT.H() / rowH);
        int maxSc   = std::max(0, total() - maxRows);
        mScroll = std::clamp(mScroll - (int)d, 0, maxSc);
        SetDirty(false);
    }

    void refresh() { mScroll=0; SetDirty(false); }

private:
    int total() const { return (int)mNames.size(); }
    const std::vector<std::string>& mNames;
    int& mSelected;
    int  mScroll{0};
    bool mSbDrag{false};
    float mSbY{0};
};

// ─── Save icon button (floppy disk) ──────────────────────────────────────────
class SaveIconButton final : public IControl
{
public:
    using OnClickFn = std::function<void()>;
    SaveIconButton(const IRECT& b, OnClickFn fn)
        : IControl(b, kNoParameter), mFn(std::move(fn)) {}

    void Draw(IGraphics& g) override {
        g.FillRoundRect(CT::btnInactive, mRECT, 3.0f);
        float L=mRECT.L+3, T=mRECT.T+3, R=mRECT.R-3, B=mRECT.B-3;
        float W=R-L, H=B-T;
        static const IColor kIcon(255,40,40,40);
        g.DrawRect(kIcon, IRECT(L,T,R,B), nullptr, 1.5f);
        // Label area (top 40%)
        g.FillRect(IColor(255,220,216,208), IRECT(L+1,T+1,R-1,T+H*0.42f));
        // Notch top-right
        g.FillRect(CT::btnInactive, IRECT(R-W*0.28f,T,R,T+H*0.30f));
        // Disk slot
        float ds=W*0.18f;
        g.FillRect(IColor(255,100,96,90), IRECT(L+ds,T+1,R-ds,T+H*0.40f));
        g.DrawLine(IColor(255,60,58,54), L+ds+1,T+H*0.15f, R-ds-1,T+H*0.15f, nullptr,1.0f);
        g.DrawLine(IColor(255,60,58,54), L+ds+1,T+H*0.27f, R-ds-1,T+H*0.27f, nullptr,1.0f);
        // Storage area
        float sl=L+W*0.22f, sr=R-W*0.22f;
        g.FillRect(IColor(255,190,188,182), IRECT(sl,T+H*0.52f,sr,B-1));
        g.DrawRect(kIcon, IRECT(sl,T+H*0.52f,sr,B-1), nullptr,1.0f);
    }

    void OnMouseDown(float, float, const IMouseMod&) override { if (mFn) mFn(); }

private:
    OnClickFn mFn;
};

// ─── 2D delay bandpass filter control ────────────────────────────────────────
class DelayFilterControl final : public IControl
{
public:
    DelayFilterControl(const IRECT& b, int loCutParam, int hiCutParam)
        : IControl(b, kNoParameter), mLoP(loCutParam), mHiP(hiCutParam) {}

    void Draw(IGraphics& g) override {
        g.FillRoundRect(IColor(255,28,28,32), mRECT, 5.0f);

        float loN = (float)GetDelegate()->GetParam(mLoP)->GetNormalized();
        float hiN = (float)GetDelegate()->GetParam(mHiP)->GetNormalized();
        float loX = mRECT.L + loN * mRECT.W();
        float hiX = mRECT.L + hiN * mRECT.W();
        float bw  = hiN - loN;
        float cy  = mRECT.B - 5.0f - bw * (mRECT.H() - 12.0f);
        cy = std::clamp(cy, mRECT.T+8.0f, mRECT.B-5.0f);

        static const IColor kBand(140, 90,195,175);
        static const IColor kEdge(220, 90,195,175);

        float slope = std::min(26.0f, (hiX-loX)*0.3f);

        if (hiX > loX)
            g.FillRect(kBand, IRECT(loX, cy, hiX, mRECT.B-2));
        if (loX - slope > mRECT.L)
            g.FillTriangle(kBand, loX-slope,mRECT.B-2, loX,mRECT.B-2, loX,cy);
        if (hiX + slope < mRECT.R)
            g.FillTriangle(kBand, hiX,cy, hiX,mRECT.B-2, hiX+slope,mRECT.B-2);

        g.DrawLine(kEdge, loX,cy, loX,mRECT.B-2, nullptr,1.5f);
        g.DrawLine(kEdge, hiX,cy, hiX,mRECT.B-2, nullptr,1.5f);
        g.DrawLine(kEdge, loX,cy, hiX,cy,          nullptr,1.5f);

        float cx = (loX + hiX) * 0.5f;
        g.FillCircle(IColor(255,120,220,200), cx,cy,5.0f);
        g.DrawCircle(IColor(220,255,255,255), cx,cy,5.0f, nullptr,1.0f);

        IText lt(9.0f, IColor(255,160,160,160), "RobotoMono", EAlign::Near,  EVAlign::Middle);
        IText rt(9.0f, IColor(255,160,160,160), "RobotoMono", EAlign::Far,   EVAlign::Middle);
        WDL_String loS, hiS;
        GetDelegate()->GetParam(mLoP)->GetDisplay(loS,false);
        GetDelegate()->GetParam(mHiP)->GetDisplay(hiS,false);
        g.DrawText(lt, loS.Get(), IRECT(mRECT.L+3, mRECT.T+2, mRECT.MW(), mRECT.T+14));
        g.DrawText(rt, hiS.Get(), IRECT(mRECT.MW(),mRECT.T+2, mRECT.R-3,  mRECT.T+14));
    }

    void OnMouseDown(float x, float y, const IMouseMod&) override {
        mDX=x; mDY=y;
        mStartLo=(float)GetDelegate()->GetParam(mLoP)->GetNormalized();
        mStartHi=(float)GetDelegate()->GetParam(mHiP)->GetNormalized();
    }

    void OnMouseDrag(float x, float y, float, float, const IMouseMod&) override {
        float dx = (x-mDX) / mRECT.W();
        float dy = (y-mDY) / mRECT.H();
        float curBw  = mStartHi - mStartLo;
        float newBw  = std::clamp(curBw - dy*1.4f, 0.02f, 1.0f);
        float center = (mStartLo+mStartHi)*0.5f + dx;
        float newLo  = std::clamp(center - newBw*0.5f, 0.0f, 1.0f);
        float newHi  = std::clamp(center + newBw*0.5f, 0.0f, 1.0f);
        GetDelegate()->GetParam(mLoP)->SetNormalized(newLo);
        GetDelegate()->GetParam(mHiP)->SetNormalized(newHi);
        SetDirty(true);
    }

    void OnMouseDblClick(float, float, const IMouseMod&) override {
        GetDelegate()->GetParam(mLoP)->SetToDefault();
        GetDelegate()->GetParam(mHiP)->SetToDefault();
        SetDirty(true);
    }

private:
    int mLoP, mHiP;
    float mDX{0}, mDY{0}, mStartLo{0}, mStartHi{1};
};

// ─── Vertical volume slider with VU meter ─────────────────────────────────────
class VolumeSlider final : public IControl
{
public:
    VolumeSlider(const IRECT& b, int param,
                 std::atomic<float>* vuL, std::atomic<float>* vuR)
        : IControl(b, param), mVuL(vuL), mVuR(vuR) {}

    void Draw(IGraphics& g) override {
        float val = (float)GetValue();
        float vuL = mVuL ? std::min(1.0f, mVuL->load()) : 0.0f;
        float vuR = mVuR ? std::min(1.0f, mVuR->load()) : 0.0f;

        const float vuW = 9.0f;
        const float slW = 14.0f;
        const float h   = mRECT.H() - 16.0f;
        const float top = mRECT.T + 3.0f;
        const float bot = top + h;

        // VU Left
        float lx = mRECT.L;
        g.FillRoundRect(IColor(255,50,50,54), IRECT(lx,top,lx+vuW,bot),3.0f);
        float lH = vuL * h;
        if (lH > 0.5f) {
            IColor vc = vuL>0.85f ? IColor(255,240,60,50) :
                        vuL>0.65f ? IColor(255,220,200,50) : IColor(255,80,200,100);
            g.FillRoundRect(vc, IRECT(lx+1,bot-lH,lx+vuW-1,bot-1),2.0f);
        }

        // VU Right
        float rx = lx+vuW+2.0f;
        g.FillRoundRect(IColor(255,50,50,54), IRECT(rx,top,rx+vuW,bot),3.0f);
        float rH = vuR * h;
        if (rH > 0.5f) {
            IColor vc = vuR>0.85f ? IColor(255,240,60,50) :
                        vuR>0.65f ? IColor(255,220,200,50) : IColor(255,80,200,100);
            g.FillRoundRect(vc, IRECT(rx+1,bot-rH,rx+vuW-1,bot-1),2.0f);
        }

        // Slider track
        float sx = lx+2*vuW+2.0f+4.0f;
        g.FillRoundRect(IColor(255,55,55,60), IRECT(sx,top,sx+slW,bot),4.0f);
        float fillH = val * h;
        if (fillH > 0.5f)
            g.FillRoundRect(IColor(255,180,175,168),
                IRECT(sx+1,bot-fillH,sx+slW-1,bot-1),3.0f);
        float hy = bot - fillH;
        g.FillRect(IColor(255,228,224,218), IRECT(sx-2,hy-1.5f,sx+slW+2,hy+1.5f));

        // Value label
        WDL_String vs; GetParam()->GetDisplay(vs,false);
        IText vt(9.0f,CT::fgPrimary,"RobotoMono",EAlign::Center,EVAlign::Middle);
        g.DrawText(vt,vs.Get(),IRECT(mRECT.L,bot+3,mRECT.R,bot+14));
    }

    void OnIdle() override {
        float nl = mVuL ? mVuL->load() : 0.0f;
        float nr = mVuR ? mVuR->load() : 0.0f;
        if (std::abs(nl-mLastL)>0.005f || std::abs(nr-mLastR)>0.005f) {
            mLastL=nl; mLastR=nr;
            SetDirty(false);
        }
    }

    void OnAttached() override { if (GetParam()) SetValue(GetParam()->GetNormalized()); }
    void OnMouseDown(float, float y, const IMouseMod& m) override
        { mDY=y; mSV=GetValue(); IControl::OnMouseDown(0,y,m); }
    void OnMouseDblClick(float, float, const IMouseMod&) override
        { SetValue(GetParam()->GetDefault(true)); SetDirty(true); }
    void OnMouseDrag(float, float y, float, float, const IMouseMod& m) override {
        double s = m.S ? 2000.0 : 400.0;
        SetValue(std::clamp(mSV-(y-mDY)/s,0.0,1.0)); SetDirty(true);
    }
    void OnMouseWheel(float, float, const IMouseMod& m, float d) override {
        SetValue(std::clamp(GetValue()+d*(m.S?0.002:0.01),0.0,1.0)); SetDirty(true);
    }

private:
    std::atomic<float>* mVuL; std::atomic<float>* mVuR;
    float mDY{0}; double mSV{0};
    float mLastL{-1}, mLastR{-1};
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
        IText t(10.0f, CT::fgPrimary, "RobotoMono", EAlign::Center, EVAlign::Middle);
        g.DrawText(t, mLabel, mRECT);
    }
    void OnMouseDown(float, float, const IMouseMod&) override { if (mFn) mFn(GetUI()); }
private:
    const char* mLabel; OnClickFn mFn;
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

    SampleLibrary mLibrary;
    PresetManager mPresets;
    int mSelectedIdx { -1 };

    // Macro link state: 0=off, 1=positive, -1=negative
    int mMacroLink[kNumParams] {};

    // VU peaks written by DSP thread
    std::atomic<float> mVuPeakL{0.0f};
    std::atomic<float> mVuPeakR{0.0f};

    void loadSample(int idx);
    void savePreset();

private:
    struct Voice {
        bool   active{false}, inRelease{false};
        int    noteNum{-1}, startSample{0};
        double srcPos{0.0}, pitchRatio{1.0};
        float  level{0.0f}, relLeft{0.0f}, relTotal{0.0f};
    };
    static constexpr int kNumVoices = 8;
    std::array<Voice, kNumVoices> mVoices;
    int mVoiceTimer{0};

    std::vector<float> mSampleL, mSampleR;
    int    mSampleLen{0};
    double mSampleSR {44100.0};

    EffectsChain mFX;
    IMidiQueue   mMidiQueue;
    std::string  mFolderPath;

    static constexpr int kMaxBlock = 4096;
    float mTmpL[kMaxBlock]{}, mTmpR[kMaxBlock]{};

    void noteOn(int note, float vel);
    void noteOff(int note);
    int  stealVoice();

    static float lerp(const std::vector<float>& b, double pos);
};
