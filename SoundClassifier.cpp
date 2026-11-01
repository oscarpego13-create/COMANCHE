#include "SoundClassifier.h"
#include "EffectsChain.h"   // SvfFilter
#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>

namespace fs = std::filesystem;

// ─── Name ────────────────────────────────────────────────────────────────────

namespace {

// Word-start match, like S1 Asistente's (?<![a-z0-9])keyword
// Short keywords must be a whole word ("sub" yes, "subtle" no)
bool hasWord(const std::string& name, const char* kw)
{
    const size_t n = std::char_traits<char>::length(kw);
    const bool whole = n <= 3;
    for (size_t pos = name.find(kw); pos != std::string::npos; pos = name.find(kw, pos + 1)) {
        const bool startOk = pos == 0 || !std::isalpha((unsigned char)name[pos - 1]);
        const bool endOk = !whole || pos + n >= name.size() || !std::isalpha((unsigned char)name[pos + n]);
        if (startOk && endOk) return true;
    }
    return false;
}

bool hasAny(const std::string& name, std::initializer_list<const char*> kws)
{
    for (const char* k : kws) if (hasWord(name, k)) return true;
    return false;
}

std::vector<float> toMono(const std::vector<float>& l, const std::vector<float>& r, size_t maxLen)
{
    const size_t n = std::min(l.size(), maxLen);
    std::vector<float> m(n);
    for (size_t i = 0; i < n; i++) m[i] = r.size() > i ? 0.5f * (l[i] + r[i]) : l[i];
    return m;
}

// 10 ms RMS envelope
std::vector<float> envelope(const std::vector<float>& m, int win)
{
    std::vector<float> env;
    for (size_t i = 0; i + win <= m.size(); i += win) {
        double s = 0;
        for (int j = 0; j < win; j++) s += (double)m[i + j] * m[i + j];
        env.push_back((float)std::sqrt(s / win));
    }
    return env;
}

} // namespace

SoundType SoundClassifier::fromName(const std::string& path)
{
    std::string name = fs::path(path).stem().string();
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return (char)std::tolower(c); });

    // Bass first, as in S1 Asistente
    if (hasAny(name, {"bass", "bajo", "sub", "reese", "808"})) return SoundType::Bass;
    if (hasAny(name, {"kick", "drum", "perc", "snare", "clap", "hihat", "hat", "hhat", "shaker", "rim",
                      "cym", "tom", "fx", "riser", "sweep", "impact", "impacto", "noise", "vox", "vocal", "voz"}))
        return SoundType::Other;
    if (hasAny(name, {"keys", "key", "piano", "rhodes", "bell", "pluck", "arp", "guitar", "guitarra",
                      "marimba", "mallet", "chord", "kalimba", "harp"}))
        return SoundType::Keys;
    if (hasAny(name, {"lead", "pad", "synth", "string", "choir", "organ", "brass", "drone", "atmos", "saw"}))
        return SoundType::LeadPad;
    return SoundType::Pending;
}

// ─── Audio ───────────────────────────────────────────────────────────────────

SoundType SoundClassifier::fromAudio(const std::vector<float>& left, const std::vector<float>& right, double sr)
{
    if (sr <= 0) return SoundType::Other;
    const std::vector<float> m = toMono(left, right, (size_t)(sr * 10.0));   // first 10 s is plenty
    if (m.size() < (size_t)(sr * 0.12)) return SoundType::Other;            // too short to be tonal

    const int win = std::max(1, (int)(sr * 0.01));
    const std::vector<float> env = envelope(m, win);
    if (env.size() < 12) return SoundType::Other;
    const auto pkIt = std::max_element(env.begin(), env.end());
    const float peak = *pkIt;
    if (peak < 1e-4f) return SoundType::Other;
    const int pk = (int)(pkIt - env.begin());

    // Bass: energy below 100 Hz (two cascaded 2-pole low-passes) vs everything else
    {
        SvfFilter lp1, lp2;
        lp1.mode = lp2.mode = SvfFilter::Mode::LP;
        lp1.setCutoff(sr, 100.0f); lp2.setCutoff(sr, 100.0f);
        double eLow = 0, eAll = 0;
        for (float x : m) { const float y = lp2.process(lp1.process(x)); eLow += (double)y * y; eAll += (double)x * x; }
        if (eAll > 0 && eLow / eAll > 0.5) return SoundType::Bass;
    }

    // Envelope shape: level 150–600 ms after the peak, relative to the peak
    const int a = std::min((int)env.size() - 1, pk + 15), b = std::min((int)env.size(), pk + 60);
    double sus = 0; int cnt = 0;
    for (int i = a; i < b; i++) { sus += env[i]; cnt++; }
    const float sustain = cnt > 0 ? (float)(sus / cnt) / peak : 0.0f;
    const bool fastAttack = pk <= 4;   // peak within 40 ms

    const int afterPeak = (int)env.size() - pk;
    if (fastAttack && afterPeak < 30) return SoundType::Other;   // fast and gone within 300 ms: a hit
    if (cnt < 5) return SoundType::Keys;
    if (sustain >= 0.45f) return SoundType::LeadPad;
    return SoundType::Keys;   // transient that decays
}

bool SoundClassifier::findLoop(const std::vector<float>& left, const std::vector<float>& right, double sr,
                               int& loopStart, int& loopEnd)
{
    if (sr <= 0 || left.empty()) return false;
    const std::vector<float> m = toMono(left, right, left.size());
    const int win = std::max(1, (int)(sr * 0.01));
    const std::vector<float> env = envelope(m, win);
    if (env.size() < 20) return false;
    const auto pkIt = std::max_element(env.begin(), env.end());
    const float peak = *pkIt;
    if (peak < 1e-4f) return false;
    const int pk = (int)(pkIt - env.begin());

    // End of the sustained part: last window still above 35 % of the peak (before the release tail)
    int last = (int)env.size() - 1;
    while (last > pk && env[last] < 0.35f * peak) last--;
    const int attackEnd = (pk + 5) * win;   // 50 ms after the peak
    int end = last * win;
    if (end - attackEnd < (int)(sr * 0.08)) return false;

    // Loop the last part of the sustain: up to 500 ms, at most 60 % of it
    const int len = (int)std::min(sr * 0.5, (end - attackEnd) * 0.6);
    int start = end - len;

    // Snap both points to rising zero crossings (within ±5 ms) so the jump is continuous
    auto snap = [&](int p) {
        const int r = (int)(sr * 0.005);
        for (int d = 0; d <= r; d++) {
            for (int s : {p + d, p - d})
                if (s > 0 && s < (int)m.size() && m[s - 1] <= 0.0f && m[s] > 0.0f) return s;
        }
        return p;
    };
    start = snap(start);
    end = snap(end);
    if (end - start < (int)(sr * 0.04) || start < (int)(sr * 0.03)) return false;
    loopStart = start;
    loopEnd = end;
    return true;
}

const char* SoundClassifier::badge(SoundType t)
{
    switch (t) {
        case SoundType::Bass:    return "BASS";
        case SoundType::LeadPad: return "LEAD";
        case SoundType::Keys:    return "KEYS";
        case SoundType::Pending: return "\xC2\xB7\xC2\xB7\xC2\xB7";   // "···" while analysing
        default:                 return nullptr;
    }
}
