#pragma once
#include <string>
#include <vector>

// Scans a folder for WAV files and decodes them on demand.
// No JUCE dependency — uses std::filesystem + a minimal WAV reader.
class SampleLibrary
{
public:
    void setFolder(const std::string& folderPath);
    std::string getFolder() const { return currentFolder; }

    const std::vector<std::string>& getSampleNames() const { return sampleNames; }
    std::string getSamplePath(int index) const;

    // Returns true and fills left/right on success (mono → left only, right = left)
    bool loadSample(const std::string& path,
                    std::vector<float>& left,
                    std::vector<float>& right,
                    int& numChannels,
                    double& sampleRate);

private:
    std::string              currentFolder;
    std::vector<std::string> samplePaths;
    std::vector<std::string> sampleNames;

    void scan();

    static bool readWav(const std::string& path,
                        std::vector<float>& left,
                        std::vector<float>& right,
                        int& numChannels,
                        double& sampleRate);
};
