#pragma once
#include <algorithm>

// Applies additive macro offsets, result clamped to [0, 1]
struct MacroController
{
    enum class Target { Reverb, Dist, Chorus };

    float apply(float macro, float base, Target t) const noexcept
    {
        float gain = 0.0f;
        switch (t) {
            case Target::Reverb: gain = 0.70f; break;
            case Target::Dist:   gain = 0.40f; break;
            case Target::Chorus: gain = 0.60f; break;
        }
        return std::clamp(base + macro * gain, 0.0f, 1.0f);
    }
};
