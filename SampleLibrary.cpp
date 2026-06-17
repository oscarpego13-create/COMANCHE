#include "SampleLibrary.h"
#include <filesystem>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cstdint>

namespace fs = std::filesystem;

// ─── Folder management ───────────────────────────────────────────────────────

void SampleLibrary::setFolder(const std::string& folderPath)
{
    currentFolder = folderPath;
    scan();
}

std::string SampleLibrary::getSamplePath(int index) const
{
    if (index >= 0 && index < (int)samplePaths.size())
        return samplePaths[index];
    return {};
}

void SampleLibrary::scan()
{
    samplePaths.clear();
    sampleNames.clear();
    if (currentFolder.empty()) return;

    std::error_code ec;
    if (!fs::is_directory(currentFolder, ec)) return;

    std::vector<fs::path> found;
    for (auto& e : fs::directory_iterator(currentFolder, ec))
    {
        if (!e.is_regular_file()) continue;
        auto ext = e.path().extension().string();
        // Case-insensitive .wav check
        for (auto& c : ext) c = (char)std::tolower((unsigned char)c);
        if (ext == ".wav")
            found.push_back(e.path());
    }

    std::sort(found.begin(), found.end());

    for (auto& p : found) {
        samplePaths.push_back(p.string());
        sampleNames.push_back(p.stem().string());
    }
}

bool SampleLibrary::loadSample(const std::string& path,
                               std::vector<float>& left,
                               std::vector<float>& right,
                               int& numChannels,
                               double& sampleRate)
{
    return readWav(path, left, right, numChannels, sampleRate);
}

// ─── Minimal WAV decoder (16-bit, 24-bit, 32-bit float/int PCM) ─────────────

static uint16_t read16(FILE* f) { uint16_t v; fread(&v, 2, 1, f); return v; }
static uint32_t read32(FILE* f) { uint32_t v; fread(&v, 4, 1, f); return v; }

bool SampleLibrary::readWav(const std::string& path,
                            std::vector<float>& left,
                            std::vector<float>& right,
                            int& numChannels,
                            double& sampleRate)
{
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;

    // RIFF header
    char id[4];
    fread(id, 1, 4, f);
    if (memcmp(id, "RIFF", 4) != 0) { fclose(f); return false; }
    read32(f); // file size
    fread(id, 1, 4, f);
    if (memcmp(id, "WAVE", 4) != 0) { fclose(f); return false; }

    uint16_t audioFmt = 1, numCh = 2, bitsPerSample = 16;
    uint32_t sr = 44100;

    while (fread(id, 1, 4, f) == 4) {
        uint32_t chunkSize = read32(f);
        long chunkStart = ftell(f);

        if (memcmp(id, "fmt ", 4) == 0) {
            audioFmt     = read16(f);
            numCh        = read16(f);
            sr           = read32(f);
            read32(f);               // byte rate
            read16(f);               // block align
            bitsPerSample = read16(f);
            fseek(f, chunkStart + chunkSize, SEEK_SET);
        }
        else if (memcmp(id, "data", 4) == 0) {
            sampleRate  = (double)sr;
            numChannels = (int)numCh;
            int bytesPerSample = bitsPerSample / 8;
            int numFrames = (int)chunkSize / (bytesPerSample * numCh);

            left.resize(numFrames);
            right.resize(numFrames);

            for (int i = 0; i < numFrames; i++) {
                for (int ch = 0; ch < (int)numCh && ch < 2; ch++) {
                    float s = 0.0f;
                    if (bitsPerSample == 16) {
                        int16_t v; fread(&v, 2, 1, f); s = v / 32768.0f;
                    } else if (bitsPerSample == 24) {
                        uint8_t b[3]; fread(b, 1, 3, f);
                        int32_t v = ((int32_t)b[2] << 16) | ((int32_t)b[1] << 8) | b[0];
                        if (v & 0x800000) v |= 0xFF000000;
                        s = v / 8388608.0f;
                    } else if (bitsPerSample == 32) {
                        if (audioFmt == 3) { fread(&s, 4, 1, f); }      // IEEE float
                        else { int32_t v; fread(&v, 4, 1, f); s = (float)(v / 2147483648.0); }
                    }
                    if (ch == 0) left[i]  = s;
                    else         right[i] = s;
                }
                // skip extra channels beyond stereo
                if (numCh > 2) fseek(f, (numCh-2) * bytesPerSample, SEEK_CUR);
            }
            // If mono, copy left to right
            if (numCh == 1) right = left;

            fclose(f);
            return true;
        }
        else {
            fseek(f, chunkStart + chunkSize, SEEK_SET);
        }
    }

    fclose(f);
    return false;
}
