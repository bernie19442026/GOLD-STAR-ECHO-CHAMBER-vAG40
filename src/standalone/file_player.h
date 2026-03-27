#pragma once

#include <vector>
#include <string>
#include <atomic>
#include <cstdint>
#include <iosfwd>

class FilePlayer {
public:
    enum class State { Stopped, Playing, Paused };

    FilePlayer();
    ~FilePlayer() = default;

    void setOutputSampleRate(int sampleRate) { outputSampleRate_ = sampleRate; }

    // Load a WAV file into memory (call from main thread only)
    bool loadFile(const std::string& path);

    // Transport controls (call from main thread)
    void play();
    void pause();
    void stop();

    // Pull audio samples for the render callback (real-time safe)
    // Writes numSamples into output. Returns true if audio was written.
    bool pullSamples(float* output, int numSamples);

    State getState() const { return state_.load(std::memory_order_acquire); }
    bool isPlaying() const { return state_.load(std::memory_order_acquire) == State::Playing; }
    bool isLoaded() const { return !samples_.empty(); }

    const std::string& getLoadedFileName() const { return fileName_; }
    int getFileSampleRate() const { return fileSampleRate_; }

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

    std::vector<float> samples_;         // Pre-loaded mono audio data
    std::atomic<int64_t> playPosition_;  // Current read position (atomic for RT safety)
    std::atomic<State> state_;
    std::string filePath_;
    std::string fileName_;
    int fileSampleRate_;
    int outputSampleRate_ = 0;

    static std::vector<float> resample(const std::vector<float>& input,
                                        int fromRate, int toRate);
    void readWAVHeader(std::ifstream& file, WAVHeader& header);
    std::vector<float> readWAVData(std::ifstream& file, const WAVHeader& header,
                                    uint32_t dataSize);
    std::vector<float> stereoToMono(const std::vector<float>& stereo, int numChannels);
};
