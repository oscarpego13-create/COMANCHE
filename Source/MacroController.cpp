#include "MacroController.h"
#include <JuceHeader.h>

float MacroController::apply(float macro, float baseValue, Target target) const noexcept
{
    float gain = 0.0f;
    switch (target)
    {
        case Target::Reverb: gain = kReverbGain; break;
        case Target::Dist:   gain = kDistGain;   break;
        case Target::Chorus: gain = kChorusGain; break;
    }
    return juce::jlimit(0.0f, 1.0f, baseValue + macro * gain);
}
