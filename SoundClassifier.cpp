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

// Fine-tune the end so the audio before it lines up with the audio before the start (same phase):
// the crossfade then blends matching waveforms instead of cancelling.
int SoundClassifier::alignLoopEnd(const std::vector<float>& left, const std::vector<float>& right, double sr,
                                  int start, int end)
{
    auto mono = [&](int i) { return right.empty() ? (double)left[i] : 0.5 * ((double)left[i] + right[i]); };
    end = std::min(end, (int)left.size());
    const int xf = std::max(1, (int)((end - start) * kLoopCrossfade));
    const int cmpLen = std::min({xf, (int)(sr * 0.05), start});
    if (cmpLen < 8) return end;
    const int range = (int)(sr * 0.025);   // covers a full period down to 40 Hz
    double best = -2.0; int bestEnd = end;
    for (int e = end; e >= std::max(start + (int)(sr * 0.06), end - range); e--) {
        double xy = 0, xx = 0, yy = 0;
        for (int i = 1; i <= cmpLen; i += 2) {
            const double x = mono(e - i), y = mono(start - i);
            xy += x * y; xx += x * x; yy += y * y;
        }
        const double c = xy / std::sqrt(xx * yy + 1e-20);
        if (c > best) { best = c; bestEnd = e; }
    }
    return bestEnd;
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

    // The sound ends at its last audible 10 ms window (-50 dB from the peak), less 30 ms so a final cut or
    // click stays out of the loop; the loop starts halfway. The peak can be anywhere (swells, risers, organs
    // that end on their loudest chord), so it is not used as a lower bound.
    int last = (int)env.size() - 1;
    while (last > 0 && env[last] < 0.003f * peak) last--;
    last = std::max(0, last - 3);
    const int startWin = std::max(1, last / 2);
    if (startWin >= last) return false;
    float startLevel = 0.0f;
    for (int i = std::max(0, startWin - 1); i <= std::min(last, startWin + 1); i++) startLevel = std::max(startLevel, env[i]);

    // The loop ends before the release tail, while the sound is within -10 dB of its level at the
    // loop start (buildLoop evens out that difference, so it never pumps).
    auto lastAbove = [&](float k) { int e = last; while (e > startWin && env[e] < k * startLevel) e--; return e; };
    int endWin = lastAbove(0.32f);
    if ((endWin - startWin) * win < (int)(sr * 0.10)) endWin = lastAbove(0.16f);
    const int start = startWin * win;
    int end = (endWin + 1) * win;
    if (end - start < (int)(sr * 0.06)) return false;

    end = alignLoopEnd(left, right, sr, start, end);
    if (start - (int)((end - start) * kLoopCrossfade) < 0) return false;
    loopStart = start;
    loopEnd = end;
    return true;
}

void SoundClassifier::buildLoop(const std::vector<float>& left, const std::vector<float>& right, double sr,
                                int ls, int le, std::vector<float>& loopL, std::vector<float>& loopR)
{
    loopL = left; loopR = right;
    const int xf = std::min(ls, (int)((le - ls) * kLoopCrossfade));
    if (xf < 2) return;

    // Even out the slow level changes inside the loop: most one-shots keep fading, so the end of the loop
    // is quieter than its start and every pass would swell back up. The gain follows the sound's own
    // envelope smoothed over ~100 ms (fast movement such as tremolo or detune beating is kept) and holds
    // it at the level of the loop start.
    {
        const int n = (int)left.size(), half = std::max(1, std::min((le - ls) / 8, (int)(sr * 0.05)));
        // The stretch before the loop start is what the loop end fades into, so it is levelled too,
        // easing in over the same length before it so the first pass through stays smooth.
        const int lead = std::min(xf, ls / 2), easeFrom = ls - 2 * lead;
        const int from = std::max(0, easeFrom - half), to = std::min(n, le + half);
        std::vector<double> pre(to - from + 1, 0.0);
        for (int i = from; i < to; i++) {
            const double v = right.empty() ? left[i] : 0.5 * (left[i] + right[i]);
            pre[i - from + 1] = pre[i - from] + v * v;
        }
        auto envAt = [&](int i) {
            const int a0 = std::max(from, i - half), a1 = std::min(to, i + half);
            return std::sqrt((pre[a1 - from] - pre[a0 - from]) / std::max(1, a1 - a0));
        };
        const double target = envAt(ls);
        if (target > 1e-6)
            for (int i = std::max(0, easeFrom); i < le; i++) {
                float g = (float)std::clamp(target / std::max(envAt(i), 1e-9), 0.25, 4.0);   // +-12 dB
                if (i < ls - lead) {   // ease in: 0 -> full levelling
                    const float w = (float)(i - easeFrom) / (float)std::max(1, lead);
                    g = 1.0f + (0.5f - 0.5f * std::cos(3.14159265f * w)) * (g - 1.0f);
                }
                loopL[i] *= g;
                if (!right.empty()) loopR[i] *= g;
            }
    }

    // How alike the two crossfaded stretches are around each point (1 = same waveform, 0 = unrelated,
    // < 0 = cancelling), over a sliding ~10 ms window. The phase between them can drift along the fade
    // (detuned or glitchy sounds), so a single figure for the whole fade is not enough.
    std::vector<double> sxy(xf + 1, 0.0), sxx(xf + 1, 0.0), syy(xf + 1, 0.0);
    for (int i = 0; i < xf; i++) {
        const double a = loopL[le - xf + i] + (right.empty() ? 0.0f : loopR[le - xf + i]);
        const double b = loopL[ls - xf + i] + (right.empty() ? 0.0f : loopR[ls - xf + i]);
        sxy[i + 1] = sxy[i] + a * b; sxx[i + 1] = sxx[i] + a * a; syy[i + 1] = syy[i] + b * b;
    }
    const int cw = std::max(1, (int)(sr * 0.005));
    auto corrAt = [&](int i) {
        const int a0 = std::max(0, i - cw), a1 = std::min(xf, i + cw);
        const double xy = sxy[a1] - sxy[a0], xx = sxx[a1] - sxx[a0], yy = syy[a1] - syy[a0];
        return std::clamp(xy / std::sqrt(xx * yy + 1e-20), -0.5, 1.0);
    };

    // S-shaped fade whose gains keep the power constant for that local correlation: no dip where the
    // waveforms cancel and no bump where they add up. The loop end then lands exactly on the audio just
    // before the loop start.
    for (int i = 0; i < xf; i++) {
        const float t = (float)(i + 1) / (float)xf;
        const float sIn = 0.5f - 0.5f * std::cos(3.14159265f * t);
        const float aOut = 1.0f - sIn, aIn = sIn;
        const float c = (float)corrAt(i);
        const float g = std::min(2.0f, 1.0f / std::sqrt(std::max(1e-4f, aOut * aOut + aIn * aIn + 2.0f * c * aOut * aIn)));
        const int d = le - xf + i, s = ls - xf + i;
        loopL[d] = (loopL[d] * aOut + loopL[s] * aIn) * g;
        if (!right.empty()) loopR[d] = (loopR[d] * aOut + loopR[s] * aIn) * g;
    }
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
