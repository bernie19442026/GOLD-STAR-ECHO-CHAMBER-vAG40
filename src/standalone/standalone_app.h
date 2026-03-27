#pragma once

#include "../audio/audio_engine.h"
#include "audio_io.h"
#include "file_player.h"
#include <memory>
#include <string>
#include <vector>
#include <mutex>
#include <atomic>

enum class InputMode {
    SILENCE,
    FILE,
    MIC
};

class StandaloneApp {
public:
    StandaloneApp();
    ~StandaloneApp();

    bool initialize();
    void run();
    void shutdown();

    void loadIR(const std::string& path);
    void setParameter(ParameterID id, float value);
    float getParameter(ParameterID id) const;

    std::vector<std::string> getAvailableIRs();
    void selectIR(int index);
    int getCurrentIRIndex() const { return currentIRIndex_; }

    AudioEngine* getAudioEngine() { return audioEngine_.get(); }
    CoreAudioIO* getAudioIO() { return audioIO_.get(); }

    // File player controls
    bool loadAudioFile(const std::string& path);
    void playFile();
    void pauseFile();
    void stopFile();
    bool isFilePlaying() const;
    bool isFileLoaded() const;
    std::string getLoadedFileName() const;
    FilePlayer* getFilePlayer() { return &filePlayer_; }

    // Input mode
    void setInputMode(InputMode mode);
    InputMode getInputMode() const { return inputMode_.load(); }

    // Mic input
    void enableMicInput(bool enable);
    bool isMicEnabled() const { return inputMode_.load() == InputMode::MIC; }

    // Bypass
    void setBypass(bool bypass) { bypass_.store(bypass, std::memory_order_relaxed); }
    bool isBypassed() const { return bypass_.load(std::memory_order_relaxed); }

    // Signal level metering (peak, 0.0-1.0) — reads from AudioEngine's atomics
    float getInputLevel() const;
    float getProcessLevel() const;
    float getOutputLevel() const;
    float getOutputLevelL() const { return outputLevelL_.load(std::memory_order_relaxed); }
    float getOutputLevelR() const { return outputLevelR_.load(std::memory_order_relaxed); }

private:
    std::unique_ptr<AudioEngine> audioEngine_;
    std::unique_ptr<CoreAudioIO> audioIO_;
    std::vector<std::string> irPaths_;
    int currentIRIndex_;
    std::mutex engineMutex_;
    FilePlayer filePlayer_;
    std::atomic<InputMode> inputMode_;
    std::atomic<bool> bypass_{false};
    float bypassRamp_{0.0f};
    std::vector<float> processedBuffer_;
    std::atomic<float> outputLevelL_{0.0f};
    std::atomic<float> outputLevelR_{0.0f};

    void scanIRDirectory(const std::string& path);
    void audioCallback(const float* input, float* output, int numSamples,
                       bool hasLiveInput);
    std::vector<float> fileBuffer_;
};
