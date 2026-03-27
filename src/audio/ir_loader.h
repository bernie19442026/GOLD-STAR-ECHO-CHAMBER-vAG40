#pragma once

#include <vector>
#include <string>
#include <stdexcept>
#include <cstdint>

struct IRData {
    std::vector<float> samples;
    int sampleRate;
    int channels;
    int bitDepth;
    std::string filename;
};

class IRLoader {
public:
    IRLoader() = default;

    IRData loadIR(const std::string& path);
    static void normalizeIR(std::vector<float>& ir, float targetPeak = 0.95f);
    static std::vector<float> resampleIR(const std::vector<float>& ir,
                                          int originalRate, int targetRate);

private:
    struct WAVHeader {
        char riff[4];
        uint32_t fileSize;
        char wave[4];
        char fmt[4];
        uint32_t fmtSize;
        uint16_t audioFormat;
        uint16_t numChannels;
        uint32_t sampleRate;
        uint32_t byteRate;
        uint16_t blockAlign;
        uint16_t bitsPerSample;
    };

    void readWAVHeader(std::ifstream& file, WAVHeader& header);
    std::vector<float> readWAVData(std::ifstream& file, const WAVHeader& header,
                                    uint32_t dataSize);
    std::vector<float> stereoToMono(const std::vector<float>& stereo);
};
