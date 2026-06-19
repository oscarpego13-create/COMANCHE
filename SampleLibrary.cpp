#include "SampleLibrary.h"
#include <filesystem>
#include <algorithm>
#include <AudioToolbox/AudioToolbox.h>
#include <CoreFoundation/CoreFoundation.h>

namespace fs = std::filesystem;

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

void SampleLibrary::removeAt(int idx)
{
    if (idx < 0 || idx >= (int)samplePaths.size()) return;
    samplePaths.erase(samplePaths.begin() + idx);
    sampleNames.erase(sampleNames.begin() + idx);
}

void SampleLibrary::scan()
{
    samplePaths.clear();
    sampleNames.clear();
    if (currentFolder.empty()) return;

    std::error_code ec;
    if (!fs::is_directory(currentFolder, ec)) return;

    std::vector<fs::path> found;
    for (auto& e : fs::recursive_directory_iterator(
             currentFolder,
             fs::directory_options::skip_permission_denied, ec))
    {
        if (!e.is_regular_file()) continue;
        auto ext = e.path().extension().string();
        for (auto& c : ext) c = (char)std::tolower((unsigned char)c);
        if (ext == ".wav" || ext == ".mp3")
            found.push_back(e.path());
    }

    std::sort(found.begin(), found.end());

    for (auto& p : found) {
        samplePaths.push_back(p.string());
        auto rel = fs::relative(p, currentFolder, ec);
        sampleNames.push_back(ec ? p.stem().string() : rel.string());
    }
}

bool SampleLibrary::loadSample(const std::string& path,
                               std::vector<float>& left,
                               std::vector<float>& right,
                               int& numChannels,
                               double& sampleRate)
{
    CFStringRef cfStr = CFStringCreateWithCString(nullptr, path.c_str(), kCFStringEncodingUTF8);
    CFURLRef url = CFURLCreateWithFileSystemPath(nullptr, cfStr, kCFURLPOSIXPathStyle, false);
    CFRelease(cfStr);

    ExtAudioFileRef file = nullptr;
    OSStatus err = ExtAudioFileOpenURL(url, &file);
    CFRelease(url);
    if (err != noErr || !file) return false;

    AudioStreamBasicDescription origFmt = {};
    UInt32 propSize = sizeof(origFmt);
    ExtAudioFileGetProperty(file, kExtAudioFileProperty_FileDataFormat, &propSize, &origFmt);

    sampleRate  = origFmt.mSampleRate;
    numChannels = (int)origFmt.mChannelsPerFrame;
    const int outCh = std::min(numChannels, 2);

    AudioStreamBasicDescription outFmt = {};
    outFmt.mSampleRate       = origFmt.mSampleRate;
    outFmt.mFormatID         = kAudioFormatLinearPCM;
    outFmt.mFormatFlags      = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked;
    outFmt.mBitsPerChannel   = 32;
    outFmt.mChannelsPerFrame = (UInt32)outCh;
    outFmt.mBytesPerFrame    = (UInt32)(outCh * 4);
    outFmt.mFramesPerPacket  = 1;
    outFmt.mBytesPerPacket   = (UInt32)(outCh * 4);
    ExtAudioFileSetProperty(file, kExtAudioFileProperty_ClientDataFormat, sizeof(outFmt), &outFmt);

    SInt64 numFrames = 0;
    propSize = sizeof(numFrames);
    ExtAudioFileGetProperty(file, kExtAudioFileProperty_FileLengthFrames, &propSize, &numFrames);

    left.assign((size_t)numFrames, 0.0f);
    right.assign((size_t)numFrames, 0.0f);

    const int kChunk = 4096;
    std::vector<float> buf((size_t)(kChunk * outCh));
    int pos = 0;

    while (pos < (int)numFrames) {
        UInt32 toRead = (UInt32)std::min(kChunk, (int)numFrames - pos);
        AudioBufferList abl;
        abl.mNumberBuffers              = 1;
        abl.mBuffers[0].mNumberChannels = (UInt32)outCh;
        abl.mBuffers[0].mDataByteSize   = toRead * (UInt32)(outCh * 4);
        abl.mBuffers[0].mData           = buf.data();

        if (ExtAudioFileRead(file, &toRead, &abl) != noErr || toRead == 0) break;

        for (UInt32 f = 0; f < toRead; f++) {
            left[pos + f]  = buf[f * (UInt32)outCh];
            right[pos + f] = (outCh > 1) ? buf[f * (UInt32)outCh + 1] : buf[f * (UInt32)outCh];
        }
        pos += (int)toRead;
    }

    left.resize((size_t)pos);
    right.resize((size_t)pos);
    ExtAudioFileDispose(file);
    return pos > 0;
}
