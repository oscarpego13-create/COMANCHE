#pragma once
#include <string>
#include <map>

// Reads/writes a flat JSON file alongside each .wav sample.
// No external JSON library — uses a minimal custom parser.
class PresetManager
{
public:
    // Auto-loads preset JSON if one exists next to the sample
    void onSampleLoaded(const std::string& samplePath);

    // Returns current param values from the loaded preset (empty if none)
    const std::map<std::string, float>& getValues() const { return values; }

    // Write current values map to the JSON file
    void save(const std::map<std::string, float>& paramValues);

    // Delete the JSON file
    void deletePreset();

    bool hasPreset() const;

private:
    std::string jsonPath;
    std::map<std::string, float> values;

    static std::string jsonFor(const std::string& samplePath);
    static std::map<std::string, float> parseJSON(const std::string& text);
    static std::string writeJSON(const std::map<std::string, float>& m);
};
