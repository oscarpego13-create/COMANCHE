#pragma once

#include "IPlug_include_in_plug_hdr.h"
#include "IControls.h"
#include "IPlugMidi.h"

#include <vector>
#include <string>
#include <array>
#include <algorithm>
#include <atomic>
#include <chrono>
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
    static const IColor fgPrimary   {255, 26, 26, 26};
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
}

// ─── Small macro link button ──────────────────────────────────────────────────
class MacroLinkButton final : public IControl
{
public:
    MacroLinkButton(const IRECT& b, int& state)
        : IControl(b, kNoParameter), mState(state) {}

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
        if (GetUI()) GetUI()->SetAllControlsDirty();
    }
private:
    int& mState;
};

// ─── Flat knob with macro arc + continuous macro redraw ───────────────────────
class ComanacheKnob final : public IControl
{
public:
    ComanacheKnob(const IRECT& b, int param, const char* lbl,
                  IColor fill, bool isMacro=false, int* linkState=nullptr)
        : IControl(b, param), mLabel(lbl), mFill(fill),
          mIsMacro(isMacro), mLinkState(linkState) {}

    bool IsDirty() override {
        if (!mIsMacro && mLinkState && *mLinkState != 0) {
            float m = (float)GetDelegate()->GetParam(kMacro)->GetNormalized();
            if (std::abs(m - mLastMacro) > 0.001f) { mLastMacro = m; return true; }
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

        double baseNorm = GetValue();
        double angle = (-135.0 + baseNorm*270.0) * (M_PI/180.0);
        float  reach = kR - 0.5f;
        g.DrawLine(kNeedle, cx, kCy,
                   cx+(float)(std::sin(angle)*reach),
                   kCy-(float)(std::cos(angle)*reach), nullptr, 2.2f);

        // Macro arc overlay
        if (!mIsMacro && mLinkState && *mLinkState != 0) {
            float macro = (float)GetDelegate()->GetParam(kMacro)->GetNormalized();
            if (macro > 0.001f) {
                double effNorm = std::clamp(baseNorm + macro*(double)(*mLinkState), 0.0, 1.0);
                double startA  = (-135.0 + baseNorm*270.0)*(M_PI/180.0);
                double endA    = (-135.0 + effNorm *270.0)*(M_PI/180.0);
                if (std::abs(endA-startA) > 0.01) {
                    static const IColor kArc(220,80,140,255);
                    float arcR = kR + 1.8f;
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

        WDL_String vs; GetParam()->GetDisplay(vs,false);
        const char* u = GetParam()->GetLabel();
        if (u && u[0]) vs.Append(u);
        IText vt(mIsMacro?13.0f:11.0f, CT::fgPrimary, "RobotoMono", EAlign::Center, EVAlign::Middle);
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
        SetValue(std::clamp(mSV-(y-mDY)/s,0.0,1.0)); SetDirty(true);
    }
    void OnMouseWheel(float, float, const IMouseMod& m, float d) override {
        SetValue(std::clamp(GetValue()+d*(m.S?0.002:0.01),0.0,1.0)); SetDirty(true);
    }
private:
    const char* mLabel; IColor mFill; bool mIsMacro; int* mLinkState;
    float mDY{0}; double mSV{0};
    mutable float mLastMacro{-1.0f};
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
        IText t(9.0f, active?CT::textLight:CT::fgPrimary, "RobotoMono", EAlign::Center, EVAlign::Middle);
        g.DrawText(t, mLabel, mRECT);
    }
    void OnMouseDown(float, float, const IMouseMod&) override {
        SetValue(GetParam()->ToNormalized((double)mMode)); SetDirty(true);
    }
private:
    int mMode; const char* mLabel;
};

// ─── Single cycling button for delay sync mode ────────────────────────────────
class SyncCycleButton final : public IControl
{
public:
    SyncCycleButton(const IRECT& b, int param, std::vector<std::string> labels)
        : IControl(b, param), mLabels(std::move(labels)) {}
    void OnAttached() override { if(GetParam()) SetValue(GetParam()->GetNormalized()); }
    void Draw(IGraphics& g) override {
        int mode=std::clamp((int)(GetParam()->Value()+0.5),0,(int)mLabels.size()-1);
        bool active=(mode>0);
        g.FillRoundRect(active?CT::btnActive:CT::btnInactive, mRECT, 3.0f);
        IText t(9.0f, active?CT::textLight:CT::fgPrimary,"RobotoMono",EAlign::Center,EVAlign::Middle);
        g.DrawText(t, mLabels[mode].c_str(), mRECT);
    }
    void OnMouseDown(float,float,const IMouseMod&) override {
        int cur=std::clamp((int)(GetParam()->Value()+0.5),0,(int)mLabels.size()-1);
        int next=(cur+1)%(int)mLabels.size();
        SetValue(GetParam()->ToNormalized((double)next)); SetDirty(true);
    }
private:
    std::vector<std::string> mLabels;
};

// ─── Scrollable sample list with scrollbar ───────────────────────────────────
class SampleListControl final : public IControl
{
public:
    using OnDropFn        = std::function<void(const std::string&)>;
    using OnRemoveFn      = std::function<void(int)>;
    using OnResetPresetFn = std::function<void(int)>;

    SampleListControl(const IRECT& b, const std::vector<std::string>& names, int& sel,
                      OnDropFn dropFn={}, OnRemoveFn removeFn={}, OnResetPresetFn resetFn={})
        : IControl(b, kNoParameter), mNames(names), mSelected(sel),
          mDropFn(std::move(dropFn)), mRemoveFn(std::move(removeFn)), mResetPresetFn(std::move(resetFn)) {}

    void Draw(IGraphics& g) override {
        const float sbW=12.0f, rowH=22.0f;
        IRECT listR(mRECT.L, mRECT.T, mRECT.R-sbW-1, mRECT.B);
        IRECT sbR  (mRECT.R-sbW, mRECT.T, mRECT.R, mRECT.B);
        g.FillRect(CT::bgPanel, listR);

        int maxRows=(int)(listR.H()/rowH);
        for (int i=mScroll; i<(int)mNames.size()&&(i-mScroll)<maxRows; i++) {
            IRECT row(listR.L, listR.T+(i-mScroll)*rowH, listR.R, listR.T+(i-mScroll+1)*rowH);
            bool sel=(i==mSelected);
            if (sel) g.FillRect(CT::fgPrimary, row);
            IText t(11.0f, sel?CT::textLight:CT::fgPrimary, "RobotoMono", EAlign::Near, EVAlign::Middle);
            g.DrawText(t, mNames[i].c_str(), IRECT(row.L+6,row.T,row.R,row.B));
        }
        g.FillRoundRect(IColor(255,120,118,114), sbR, 6.0f);
        if (total()>maxRows) {
            float thumbH=std::max(20.0f, sbR.H()*(float)maxRows/(float)total());
            float maxSc=(float)(total()-maxRows);
            float tp=(maxSc>0)?(float)mScroll/maxSc:0.0f;
            float ty=sbR.T+tp*(sbR.H()-thumbH);
            g.FillRoundRect(IColor(255,190,185,180), IRECT(sbR.L+2,ty,sbR.R-2,ty+thumbH), 5.0f);
        }
    }
    void OnMouseDown(float x, float y, const IMouseMod& mod) override {
        const float sbW=12.0f, rowH=22.0f;
        int row=mScroll+(int)((y-mRECT.T)/rowH);
        if (mod.R) {
            if (row>=0&&row<total()) {
                mContextRow=row;
                mContextMenu=IPopupMenu();
                mContextMenu.AddItem("Eliminar Sonido");
                mContextMenu.AddItem("Eliminar Preset");
                GetUI()->CreatePopupMenu(*this, mContextMenu, IRECT(x,y,x+1,y+1));
            }
            return;
        }
        if (x>=mRECT.R-sbW){mSbDrag=true;mSbY=y;return;}
        if (row>=0&&row<total()){
            mSelected=row;
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
        const float rowH=22.0f;
        int maxRows=(int)(mRECT.H()/rowH), maxSc=std::max(0,total()-maxRows);
        if (mSbDrag){
            float thumbH=std::max(20.0f,mRECT.H()*(float)maxRows/std::max(1,total()));
            float range=mRECT.H()-thumbH;
            if(range>0) mScroll=std::clamp((int)(((y-mRECT.T)/range)*maxSc),0,maxSc);
        } else mScroll=std::clamp(mScroll+(int)(-dy/rowH),0,maxSc);
        SetDirty(false);
    }
    void OnMouseWheel(float,float,const IMouseMod&,float d) override {
        const float rowH=22.0f;
        int maxRows=(int)(mRECT.H()/rowH),maxSc=std::max(0,total()-maxRows);
        mScroll=std::clamp(mScroll-(int)d,0,maxSc); SetDirty(false);
    }
    void refresh(){mScroll=0;SetDirty(false);}
    void OnDrop(const char* str) override {
        if (mDropFn && str) { mScroll=0; mDropFn(std::string(str)); }
    }
private:
    int total() const {return (int)mNames.size();}
    const std::vector<std::string>& mNames; int& mSelected;
    OnDropFn        mDropFn;
    OnRemoveFn      mRemoveFn;
    OnResetPresetFn mResetPresetFn;
    IPopupMenu mContextMenu;
    int mContextRow{-1};
    int mScroll{0}; bool mSbDrag{false}; float mSbY{0};
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
    BandFilterSlider(const IRECT& b, int loParam, int hiParam)
        : IControl(b, kNoParameter), mLoP(loParam), mHiP(hiParam) {}

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
    }
    void OnMouseDrag(float,float y,float,float,const IMouseMod&) override {
        const float lblH=13.0f,pad=2.0f;
        const float tT=mRECT.T+lblH+pad,tB=mRECT.B-lblH-pad,tH=tB-tT;
        float hz=yToHz(y,tT,tH);
        auto* p=GetDelegate()->GetParam(mDragLo?mLoP:mHiP);
        p->SetNormalized(std::clamp((float)p->ToNormalized(hz),0.0f,1.0f));
        SetDirty(true);
    }
    void OnMouseDblClick(float,float,const IMouseMod&) override {
        GetDelegate()->GetParam(mLoP)->SetToDefault();
        GetDelegate()->GetParam(mHiP)->SetToDefault();
        SetDirty(true);
    }

private:
    static constexpr float kLogMin=1.30103f, kLogMax=4.30103f; // log10(20), log10(20000)
    float hzToY(float hz, float tT, float tH) const {
        float ln=std::log10(std::clamp(hz,20.0f,20000.0f));
        return tT+(1.0f-(ln-kLogMin)/(kLogMax-kLogMin))*tH;
    }
    float yToHz(float y, float tT, float tH) const {
        float n=std::clamp(1.0f-(y-tT)/tH,0.0f,1.0f);
        return std::pow(10.0f, kLogMin+n*(kLogMax-kLogMin));
    }
    int mLoP,mHiP; bool mDragLo{false}; float mDragY{0};
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
        IText t(10.0f,CT::fgPrimary,"RobotoMono",EAlign::Center,EVAlign::Middle);
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
        float val=(float)GetValue();
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
        IText t(10.0f,CT::fgPrimary,"RobotoMono",EAlign::Center,EVAlign::Middle);
        g.DrawText(t, mLabel, mRECT);
    }
    void OnMouseDown(float,float,const IMouseMod&) override {if(mFn)mFn(GetUI());}
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
    int mSelectedIdx{-1};
    int mMacroLink[kNumParams]{};
    std::atomic<float> mVuPeakL{0.0f}, mVuPeakR{0.0f};
    void loadSample(int idx);
    void savePreset();
    void removeFromLibrary(int idx);
    void resetPresetForSample(int idx);
    void savePersistentState() const;
    void loadPersistentState();
private:
    struct Voice {
        bool   active{false},inRelease{false};
        int    noteNum{-1},startSample{0};
        double srcPos{0.0},pitchRatio{1.0};
        float  level{0.0f},relLeft{0.0f},relTotal{0.0f};
    };
    static constexpr int kNumVoices=8;
    std::array<Voice,kNumVoices> mVoices;
    int mVoiceTimer{0};
    std::vector<float> mSampleL,mSampleR;
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
    static float lerp(const std::vector<float>& b,double pos);
};
