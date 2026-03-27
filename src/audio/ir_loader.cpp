#include "ir_loader.h"
#include <fstream>
#include <algorithm>
#include <cstring>
#include <cmath>

IRData IRLoader::loadIR(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("Cannot open IR file: " + path);
    }

    WAVHeader header;
    readWAVHeader(file, header);

    // Find data chunk with bounds-checking
    char chunkId[4];
    uint32_t chunkSize = 0;
    bool foundData = false;

    auto currentPos = file.tellg();
    file.seekg(0, std::ios::end);
    auto fileSize = file.tellg();
    file.seekg(currentPos);

    while (file.read(chunkId, 4)) {
        if (!file.read(reinterpret_cast<char*>(&chunkSize), 4)) {
            throw std::runtime_error("WAV: unexpected EOF reading chunk size");
        }

        if (std::string(chunkId, 4) == "data") {
            foundData = true;
            break;
        }

        if (chunkSize > (uint32_t)(fileSize - file.tellg())) {
            throw std::runtime_error("WAV: malformed chunk size exceeds file");
        }
        file.seekg(chunkSize, std::ios::cur);
    }

    if (!foundData) {
        throw std::runtime_error("WAV: no data chunk found");
    }

    auto samples = readWAVData(file, header, chunkSize);

    if (header.numChannels == 2) {
        samples = stereoToMono(samples);
    }

    IRData result;
    result.samples = std::move(samples);
    result.sampleRate = header.sampleRate;
    result.channels = 1;
    result.bitDepth = header.bitsPerSample;
    result.filename = path;

    return result;
}

void IRLoader::readWAVHeader(std::ifstream& file, WAVHeader& header) {
    // Read RIFF header (first 12 bytes)
    file.read(header.riff, 4);
    file.read(reinterpret_cast<char*>(&header.fileSize), 4);
    file.read(header.wave, 4);

    if (std::string(header.riff, 4) != "RIFF") {
        throw std::runtime_error("Invalid WAV: missing RIFF header");
    }
    if (std::string(header.wave, 4) != "WAVE") {
        throw std::runtime_error("Invalid WAV: missing WAVE identifier");
    }

    // Scan chunks to find "fmt " — skip JUNK, bext, iXML, etc.
    char chunkId[4];
    uint32_t chunkSize;
    bool foundFmt = false;

    while (file.read(chunkId, 4)) {
        file.read(reinterpret_cast<char*>(&chunkSize), 4);

        if (std::string(chunkId, 4) == "fmt ") {
            foundFmt = true;
            // Read the fmt chunk contents
            file.read(reinterpret_cast<char*>(&header.audioFormat), 2);
            file.read(reinterpret_cast<char*>(&header.numChannels), 2);
            file.read(reinterpret_cast<char*>(&header.sampleRate), 4);
            file.read(reinterpret_cast<char*>(&header.byteRate), 4);
            file.read(reinterpret_cast<char*>(&header.blockAlign), 2);
            file.read(reinterpret_cast<char*>(&header.bitsPerSample), 2);
            header.fmtSize = chunkSize;

            // Skip any extended fmt data beyond the 16 core bytes
            if (chunkSize > 16) {
                file.seekg(chunkSize - 16, std::ios::cur);
            }
            break;
        }

        // Skip unknown chunk (pad to even boundary per WAV spec)
        uint32_t skipSize = chunkSize + (chunkSize & 1);
        file.seekg(skipSize, std::ios::cur);
    }

    if (!foundFmt) {
        throw std::runtime_error("Invalid WAV: no fmt chunk found");
    }
    if (header.audioFormat != 1 && header.audioFormat != 3) {
        throw std::runtime_error("Unsupported WAV format (only PCM and float supported)");
    }
}

std::vector<float> IRLoader::readWAVData(std::ifstream& file,
                                          const WAVHeader& header,
                                          uint32_t dataSize) {
    std::vector<float> samples;

    if (header.audioFormat == 3 && header.bitsPerSample == 32) {
        int numSamples = dataSize / sizeof(float);
        samples.resize(numSamples);
        file.read(reinterpret_cast<char*>(samples.data()), dataSize);
    } else if (header.bitsPerSample == 16) {
        int numSamples = dataSize / sizeof(int16_t);
        std::vector<int16_t> raw(numSamples);
        file.read(reinterpret_cast<char*>(raw.data()), dataSize);
        samples.resize(numSamples);
        for (int i = 0; i < numSamples; ++i) {
            samples[i] = raw[i] / 32768.0f;
        }
    } else if (header.bitsPerSample == 24) {
        int numSamples = dataSize / 3;
        samples.resize(numSamples);
        for (int i = 0; i < numSamples; ++i) {
            uint8_t bytes[3];
            file.read(reinterpret_cast<char*>(bytes), 3);
            int32_t val = (bytes[2] << 24) | (bytes[1] << 16) | (bytes[0] << 8);
            samples[i] = val / 2147483648.0f;
        }
    } else {
        throw std::runtime_error("Unsupported bit depth: " +
                                  std::to_string(header.bitsPerSample));
    }

    return samples;
}

std::vector<float> IRLoader::stereoToMono(const std::vector<float>& stereo) {
    std::vector<float> mono(stereo.size() / 2);
    for (size_t i = 0; i < mono.size(); ++i) {
        mono[i] = (stereo[i * 2] + stereo[i * 2 + 1]) * 0.5f;
    }
    return mono;
}

void IRLoader::normalizeIR(std::vector<float>& ir, float targetPeak) {
    float peak = 0.0f;
    for (float s : ir) {
        peak = std::max(peak, std::abs(s));
    }
    if (peak > 0.0f) {
        float scale = targetPeak / peak;
        for (float& s : ir) {
            s *= scale;
        }
    }
}

std::vector<float> IRLoader::resampleIR(const std::vector<float>& ir,
                                         int originalRate, int targetRate) {
    if (originalRate == targetRate) return ir;

    double ratio = (double)originalRate / targetRate;
    size_t newLength = (size_t)(ir.size() / ratio);
    std::vector<float> resampled(newLength);

    for (size_t i = 0; i < newLength; ++i) {
        double srcIdx = i * ratio;
        size_t idx = (size_t)srcIdx;
        double frac = srcIdx - idx;

        if (idx + 1 < ir.size()) {
            resampled[i] = (float)(ir[idx] * (1.0 - frac) + ir[idx + 1] * frac);
        } else if (idx < ir.size()) {
            resampled[i] = ir[idx];
        }
    }

    return resampled;
}
