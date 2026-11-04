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

    // Share of the loop length crossfaded from its end into the audio before its start
    constexpr float kLoopCrossfade = 0.15f;

    // Loop region from the middle of the sound to the end of its sustained part (before the release
    // tail), with the end phase-aligned to the start. Returns false when the sound is too short to loop.
    bool findLoop(const std::vector<float>& left, const std::vector<float>& right, double sampleRate,
                  int& loopStart, int& loopEnd);

    // Moves a loop end back (up to 25 ms) to where the audio lines up in phase with the loop start
    int alignLoopEnd(const std::vector<float>& left, const std::vector<float>& right, double sampleRate,
                     int loopStart, int loopEnd);

    // Copy of the sample whose last kLoopCrossfade of the loop fades into the audio before loopStart,
    // with a constant-power S-curve, so jumping from loopEnd back to loopStart is seamless.
    void buildLoop(const std::vector<float>& left, const std::vector<float>& right, double sampleRate,
                   int loopStart, int loopEnd, std::vector<float>& loopL, std::vector<float>& loopR);

    // Types whose loop is on by default when the sound is loaded (any sound can loop if switched on)
    inline bool loops(SoundType t) { return t == SoundType::Bass || t == SoundType::LeadPad; }

    const char* badge(SoundType t);   // "BASS", "LEAD", "KEYS", "···" (pending) or nullptr
}
