#pragma once

// Computes additive macro offsets for reverb, dist, and chorus.
// Returned value is clamped to [0, 1].
class MacroController
{
public:
    enum class Target { Reverb, Dist, Chorus };

    // Returns: clamp(baseValue + macro * gainForTarget, 0, 1)
    float apply(float macro, float baseValue, Target target) const noexcept;

private:
    static constexpr float kReverbGain = 0.70f;  // +70% at macro=1
    static constexpr float kDistGain   = 0.40f;  // +40%
    static constexpr float kChorusGain = 0.60f;  // +60%
};
