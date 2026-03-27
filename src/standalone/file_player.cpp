#include "file_player.h"
#include <fstream>
#include <cstring>
#include <iostream>
#include <algorithm>
#include <filesystem>

FilePlayer::FilePlayer()
    : playPosition_(0), state_(State::Stopped), fileSampleRate_(0) {}

bool FilePlayer::loadFile(const std::string& path) {
    // Stop playback before loading
    stop();

    std::ifstream file(path, std::ios::binary);
    if (!file) {
        std::cerr << "FilePlayer: cannot open file: " << path << std::endl;
        return false;
    }

    try {
        WAVHeader header;
        readWAVHeader(file, header);

        // Find the data chunk
        char chunkId[4];
        uint32_t chunkSize = 0;
        bool foundData = false;

        auto currentPos = file.tellg();
        file.seekg(0, std::ios::end);
        auto fileSize = file.tellg();
        file.seekg(currentPos);

        while (file.read(chunkId, 4)) {
            if (!file.read(reinterpret_cast<char*>(&chunkSize), 4)) {
                std::cerr << "FilePlayer: unexpected EOF reading chunk size" << std::endl;
                return false;
            }

            if (std::string(chunkId, 4) == "data") {
                foundData = true;
                break;
            }

            if (chunkSize > (uint32_t)(fileSize - file.tellg())) {
                std::cerr << "FilePlayer: malformed chunk size" << std::endl;
                return false;
            }
            file.seekg(chunkSize, std::ios::cur);
        }

        if (!foundData) {
            std::cerr << "FilePlayer: no data chunk found" << std::endl;
            return false;
        }

        auto samples = readWAVData(file, header, chunkSize);

        // Convert to mono if needed
        if (header.numChannels > 1) {
            samples = stereoToMono(samples, header.numChannels);
        }

        // Resample if needed
        fileSampleRate_ = header.sampleRate;
        if (outputSampleRate_ > 0 && fileSampleRate_ != outputSampleRate_) {
            std::cout << "FilePlayer: resampling from " << fileSampleRate_
                      << " to " << outputSampleRate_ << " Hz..." << std::endl;
            samples = resample(samples, fileSampleRate_, outputSampleRate_);
        }

        // Store the loaded data
        samples_ = std::move(samples);
        filePath_ = path;
        fileName_ = std::filesystem::path(path).filename().string();
        playPosition_.store(0, std::memory_order_release);

        std::cout << "FilePlayer: loaded " << fileName_
                  << " (" << samples_.size() << " samples, "
                  << outputSampleRate_ << " Hz)" << std::endl;

        return true;

    } catch (const std::exception& e) {
        std::cerr << "FilePlayer: error loading file: " << e.what() << std::endl;
        return false;
    }
}

void FilePlayer::play() {
    if (!samples_.empty()) {
        state_.store(State::Playing, std::memory_order_release);
    }
}

void FilePlayer::pause() {
    if (state_.load(std::memory_order_acquire) == State::Playing) {
        state_.store(State::Paused, std::memory_order_release);
    }
}

void FilePlayer::stop() {
    state_.store(State::Stopped, std::memory_order_release);
    playPosition_.store(0, std::memory_order_release);
}

bool FilePlayer::pullSamples(float* output, int numSamples) {
    if (state_.load(std::memory_order_acquire) != State::Playing || samples_.empty()) {
        return false;
    }

    int64_t pos = playPosition_.load(std::memory_order_acquire);
    int64_t totalSamples = static_cast<int64_t>(samples_.size());

    for (int i = 0; i < numSamples; ++i) {
        output[i] = samples_[pos % totalSamples];
        ++pos;
    }

    // Wrap position to avoid overflow on very long playback
    playPosition_.store(pos % totalSamples, std::memory_order_release);
    return true;
}

void FilePlayer::readWAVHeader(std::ifstream& file, WAVHeader& header) {
    // Read RIFF header (12 bytes)
    file.read(header.riff, 4);
    file.read(reinterpret_cast<char*>(&header.fileSize), 4);
    file.read(header.wave, 4);

    if (std::string(header.riff, 4) != "RIFF") {
        throw std::runtime_error("Invalid WAV: missing RIFF header");
    }
    if (std::string(header.wave, 4) != "WAVE") {
        throw std::runtime_error("Invalid WAV: missing WAVE identifier");
    }

    // Scan chunks to find "fmt " - handles JUNK/bext/iXML chunks before fmt
    char chunkId[4];
    uint32_t chunkSize;
    bool foundFmt = false;

    while (file.read(chunkId, 4)) {
        file.read(reinterpret_cast<char*>(&chunkSize), 4);

        if (std::string(chunkId, 4) == "fmt ") {
            foundFmt = true;
            header.fmtSize = chunkSize;
            file.read(reinterpret_cast<char*>(&header.audioFormat), 2);
            file.read(reinterpret_cast<char*>(&header.numChannels), 2);
            file.read(reinterpret_cast<char*>(&header.sampleRate), 4);
            file.read(reinterpret_cast<char*>(&header.byteRate), 4);
            file.read(reinterpret_cast<char*>(&header.blockAlign), 2);
            file.read(reinterpret_cast<char*>(&header.bitsPerSample), 2);
            // Skip any extra fmt bytes
            if (chunkSize > 16) {
                file.seekg(chunkSize - 16, std::ios::cur);
            }
            break;
        }

        // Skip unknown chunk (pad to even boundary)
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

std::vector<float> FilePlayer::readWAVData(std::ifstream& file,
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

std::vector<float> FilePlayer::stereoToMono(const std::vector<float>& stereo,
                                             int numChannels) {
    int monoLength = static_cast<int>(stereo.size()) / numChannels;
    std::vector<float> mono(monoLength);
    for (int i = 0; i < monoLength; ++i) {
        float sum = 0.0f;
        for (int ch = 0; ch < numChannels; ++ch) {
            sum += stereo[i * numChannels + ch];
        }
        mono[i] = sum / numChannels;
    }
    return mono;
}

std::vector<float> FilePlayer::resample(const std::vector<float>& input,
                                         int fromRate, int toRate) {
    double ratio = (double)toRate / (double)fromRate;
    auto outputLen = (size_t)((double)input.size() * ratio);
    std::vector<float> output(outputLen);

    for (size_t i = 0; i < outputLen; ++i) {
        double srcPos = (double)i / ratio;
        size_t idx = (size_t)srcPos;
        double frac = srcPos - (double)idx;

        if (idx + 1 < input.size()) {
            output[i] = (float)((1.0 - frac) * input[idx] + frac * input[idx + 1]);
        } else if (idx < input.size()) {
            output[i] = input[idx];
        }
    }
    return output;
}
