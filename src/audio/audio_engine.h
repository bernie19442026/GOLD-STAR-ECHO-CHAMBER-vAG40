#pragma once

#include "convolver.h"
#include "ir_loader.h"
#include "parameters.h"
#include "biquad.h"
#include <vector>
#include <memory>
#include <string>
#include <mutex>
#include <atomic>

class AudioEngine {
public:
    AudioEngine(int sampleRate, int blockSize);
    ~AudioEngine();

    void processBlock(const float* input, float* output, int numSamples);
    void loadIR(const std::string& path);
    void loadIRFromData(const float* ir, int irLength, int irSampleRate);

    void setParameter(ParameterID id, float value);
    float getParameter(ParameterID id) const;

    int getSampleRate() const { return sampleRate_; }
    int getBlockSize() const { return blockSize_; }
    int getLatency() const;

    void reset();

    // Metering (read from UI thread, written from audio thread)
    float getInputLevel() const { return inputLevel_.load(std::memory_order_relaxed); }
    float getProcessLevel() const { return processLevel_.load(std::memory_order_relaxed); }
    float getOutputLevel() const { return outputLevel_.load(std::memory_order_relaxed); }

private:
    int sampleRate_;
    int blockSize_;

    std::unique_ptr<Convolver> convolver_;
    std::unique_ptr<ParameterManager> parameters_;
    std::unique_ptr<IRLoader> irLoader_;

    // Pre-delay circular buffer
    std::vector<float> preDelayBuffer_;
    int preDelayWriteIdx_;
    float smoothedDelaySamples_;

    // Pre-allocated processing buffers
    std::vector<float> gainBuffer_;     // After input gain
    std::vector<float> filterBuffer_;   // After HP/LP/EQ
    std::vector<float> wetBuffer_;      // After pre-delay
    std::vector<float> convolvedBuffer_;

    // Full-length IR stored for reverb length / room size / reverse control
    std::vector<float> fullIR_;
    int fullIRSampleRate_;
    std::mutex irMutex_;

    // Input filters
    BiquadFilter lowPassFilter_;
    BiquadFilter highPassFilter_;

    // 5-band EQ
    BiquadFilter eqBand100_;   // Low shelf at 100Hz
    BiquadFilter eqBand250_;   // Peaking at 250Hz
    BiquadFilter eqBand1k_;    // Peaking at 1kHz
    BiquadFilter eqBand4k_;    // Peaking at 4kHz
    BiquadFilter eqBand10k_;   // High shelf at 10kHz

    // Damping filter (applied to IR tail)
    BiquadFilter dampingFilter_;

    // Cached parameter values for filter update detection
    float cachedLP_ = 20000.0f;
    float cachedHP_ = 20.0f;
    float cachedEQ100_ = 0.0f;
    float cachedEQ250_ = 0.0f;
    float cachedEQ1k_ = 0.0f;
    float cachedEQ4k_ = 0.0f;
    float cachedEQ10k_ = 0.0f;
    float cachedRoomSize_ = 0.5f;
    float cachedDiffusion_ = 0.5f;
    float cachedReverse_ = 0.0f;
    float cachedDamping_ = 0.0f;

    // Metering (atomic for lock-free reading)
    std::atomic<float> inputLevel_{0.0f};
    std::atomic<float> processLevel_{0.0f};
    std::atomic<float> outputLevel_{0.0f};

    int debugCounter_;

    void applyPreDelay(const float* input, float* output, int numSamples);
    void applyReverbLength(float lengthSeconds);
    void rebuildModifiedIR();
    void updateFilters();
    static float peakLevel(const float* buf, int n);
};
