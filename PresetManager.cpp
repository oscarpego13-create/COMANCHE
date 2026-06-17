#include "PresetManager.h"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <cctype>

namespace fs = std::filesystem;

// ─── Path helpers ─────────────────────────────────────────────────────────────

std::string PresetManager::jsonFor(const std::string& samplePath)
{
    fs::path p(samplePath);
    return p.replace_extension(".json").string();
}

// ─── Public interface ────────────────────────────────────────────────────────

void PresetManager::onSampleLoaded(const std::string& samplePath)
{
    jsonPath = jsonFor(samplePath);
    values.clear();
    if (!fs::exists(jsonPath)) return;

    std::ifstream f(jsonPath);
    if (!f.is_open()) return;
    std::string text((std::istreambuf_iterator<char>(f)),
                      std::istreambuf_iterator<char>());
    values = parseJSON(text);
}

void PresetManager::save(const std::map<std::string, float>& paramValues)
{
    if (jsonPath.empty()) return;
    std::ofstream f(jsonPath);
    if (!f.is_open()) return;
    f << writeJSON(paramValues);
}

void PresetManager::deletePreset()
{
    if (!jsonPath.empty() && fs::exists(jsonPath))
        fs::remove(jsonPath);
    values.clear();
}

bool PresetManager::hasPreset() const
{
    return !jsonPath.empty() && fs::exists(jsonPath);
}

// ─── Minimal JSON parser (flat object only) ──────────────────────────────────

std::map<std::string, float> PresetManager::parseJSON(const std::string& text)
{
    std::map<std::string, float> out;
    size_t pos = 0;

    while ((pos = text.find('"', pos)) != std::string::npos) {
        size_t ks = pos + 1;
        size_t ke = text.find('"', ks);
        if (ke == std::string::npos) break;
        std::string key = text.substr(ks, ke - ks);
        pos = ke + 1;

        pos = text.find(':', pos);
        if (pos == std::string::npos) break;
        pos++;

        while (pos < text.size() && std::isspace((unsigned char)text[pos])) pos++;

        try {
            size_t adv;
            float val = std::stof(text.substr(pos), &adv);
            out[key] = val;
            pos += adv;
        } catch (...) { break; }
    }
    return out;
}

std::string PresetManager::writeJSON(const std::map<std::string, float>& m)
{
    std::ostringstream ss;
    ss << "{\n";
    bool first = true;
    for (auto& kv : m) {
        if (!first) ss << ",\n";
        ss << "  \"" << kv.first << "\": " << kv.second;
        first = false;
    }
    ss << "\n}\n";
    return ss.str();
}
