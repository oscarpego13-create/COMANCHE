#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>

// Shapes the MACRO can follow, and the note lengths it can sync to.
// value() takes the position in the cycle (0..1) and returns the MACRO (0..1).
namespace MacroCurves
{
    constexpr int kNumShapes = 17;
    constexpr int kDrawnShape = 16;   // drawn by hand
    // What the window offers, in order. Shape 14 is no longer offered, but saved sounds that use it keep it.
    constexpr int kGridCells = 16;
    constexpr int kGridShapes[kGridCells] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 15, kDrawnShape};
    constexpr int kNumRates  = 7;
    constexpr int kFreeRate  = 6;   // last rate: free running, in milliseconds
    constexpr float kFreeMinMs = 20.0f, kFreeMaxMs = 8000.0f;

    inline const char* rateLabel(int i) {
        static const char* kLabels[kNumRates] = {"1/16", "1/8", "1/4", "1/2", "1/1", "2/1", "MS"};
        return kLabels[i];
    }
    // Quarter notes per cycle (the free rate has none)
    inline double rateBeats(int i) {
        static const double kBeats[kNumRates] = {0.25, 0.5, 1.0, 2.0, 4.0, 8.0, 1.0};
        return kBeats[i];
    }

    inline float ease(float a, float b, float x) {
        const float t = std::clamp((x - a) / (b - a), 0.0f, 1.0f);
        return t * t * (3.0f - 2.0f * t);
    }
    // Rises over a..b, holds, and falls back over the last stretch so the cycle joins up
    inline float rise(float a, float b, float x) { return ease(a, b, x) * (1.0f - ease(0.96f, 1.0f, x)); }

    // A hand-drawn cycle. The points wrap around, so the end of the cycle IS its start: the two can't
    // differ. Atomic because the audio thread reads while the window draws.
    struct DrawnCurve
    {
        static constexpr int kPoints = 128;
        static constexpr float kFlat = 0.5f;
        DrawnCurve() { clear(); }
        void clear() { for (auto& p : mPoints) p.store(kFlat, std::memory_order_relaxed); }
        float point(int i) const { return mPoints[(size_t)(((i % kPoints) + kPoints) % kPoints)].load(std::memory_order_relaxed); }
        void setPoint(int i, float v) { mPoints[(size_t)(((i % kPoints) + kPoints) % kPoints)].store(std::clamp(v, 0.0f, 1.0f), std::memory_order_relaxed); }
        float at(float x) const {
            const float f = std::clamp(x, 0.0f, 1.0f) * kPoints;
            const int i = (int)f;
            const float a = point(i), b = point(i + 1);
            return a + (f - (float)i) * (b - a);
        }
        bool isFlat() const {
            for (int i = 0; i < kPoints; i++) if (point(i) != kFlat) return false;
            return true;
        }
    private:
        std::array<std::atomic<float>, kPoints> mPoints;
    };

    inline float value(int shape, float x, const DrawnCurve& drawn)
    {
        constexpr float kPi = 3.14159265f;
        switch (shape) {
            // Top row: ramps and ducking
            case 0:  return rise(0.00f, 0.35f, x);
            case 1:  return rise(0.00f, 0.60f, x);
            case 2:  return rise(0.20f, 0.60f, x);
            case 3:  return rise(0.35f, 0.75f, x);
            case 4:  return ease(0.40f, 0.55f, x) * (1.0f - ease(0.80f, 0.95f, x));
            case 5:  return 1.0f - rise(0.04f, 0.22f, x);
            case 6:  return 1.0f - ease(0.45f, 0.65f, x) * (1.0f - ease(0.80f, 1.0f, x));
            case 7:  return x < 0.70f ? ease(0.30f, 0.70f, x) : 1.0f - ease(0.70f, 0.95f, x);
            // Bottom row: classic LFO shapes
            case 8:  return 0.5f - 0.5f * std::cos(2.0f * kPi * x);
            case 9:  return 1.0f - std::abs(2.0f * x - 1.0f);
            case 10: return x * (1.0f - ease(0.97f, 1.0f, x));
            case 11: return (1.0f - x) * ease(0.0f, 0.03f, x);
            case 12: return x < 0.5f ? 1.0f : 0.0f;
            case 13: return x < 0.25f ? 1.0f : 0.0f;
            case 14: return rise(0.0f, 0.3f, std::fmod(x * 2.0f, 1.0f));
            case 15: return std::floor(x * 4.0f) / 3.0f;
            case kDrawnShape: return drawn.at(x);
            default: return 0.0f;
        }
    }
}
