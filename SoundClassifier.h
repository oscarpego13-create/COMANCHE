#pragma once
#include <string>
#include <vector>
#include <cstdint>

// Sound type of a one-shot, as shown in the sample list and used for auto-looping.
// Pending = not analysed yet; Other = drums / FX / vocals (no badge, never looped).
enum class SoundType : uint8_t { Pending = 0, Other, Bass, LeadPad, Keys };

// Classifier modelled on S1 Asistente (s1_premix.py + audio_classifier.py):
// the file name decides first; only when it says nothing is the audio analysed.
namespace SoundClassifier
{
    // Keyword match at word start (same lists/order idea as S1 Asistente). Pending if no keyword.
    SoundType fromName(const std::string& path);

    // Spectrum + volume envelope:
    //  - Bass:     more energy below 100 Hz than in the rest of the spectrum
    //  - Keys:     strong initial transient that then decays
    //  - Lead/Pad: level holds after the attack
    //  - Other:    too short / silent
    SoundType fromAudio(const std::vector<float>& left, const std::vector<float>& right, double sampleRate);

    // Loop region at the end of the sustained part (before the release tail), snapped to
    // rising zero crossings. Returns false when there is no usable sustain.
    bool findLoop(const std::vector<float>& left, const std::vector<float>& right, double sampleRate,
                  int& loopStart, int& loopEnd);

    // True for the types that loop while the key is held
    inline bool loops(SoundType t) { return t == SoundType::Bass || t == SoundType::LeadPad; }

    const char* badge(SoundType t);   // "BASS", "LEAD", "KEYS", "···" (pending) or nullptr
}
