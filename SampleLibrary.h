#pragma once
#include <string>
#include <vector>

// Scans a folder recursively for WAV and MP3 files.
// Uses macOS AudioToolbox for decoding — handles both formats natively.
class SampleLibrary
{
public:
    void setFolder(const std::string& folderPath);
    std::string getFolder() const { return currentFolder; }

    const std::vector<std::string>& getSampleNames() const { return sampleNames; }
    std::string getSamplePath(int index) const;

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
};
