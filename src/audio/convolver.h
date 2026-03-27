#pragma once

#include <vector>
#include <cstdint>
#include <mutex>

class Convolver {
public:
    Convolver(int blockSize, int sampleRate);
    ~Convolver();

    void setImpulseResponse(const float* ir, int irLength);
    void process(const float* input, float* output, int numSamples);
    void reset();
    int getLatency() const;

private:
    int blockSize_;
    int fftSize_;
    int numPartitions_;

    // Partitioned convolution
    std::vector<std::vector<float>> irPartitionsReal_;
    std::vector<std::vector<float>> irPartitionsImag_;
    std::vector<std::vector<float>> inputHistoryReal_;
    std::vector<std::vector<float>> inputHistoryImag_;
    std::vector<float> overlapBuffer_;
    int currentPartition_;

    // Pre-allocated scratch buffers (NO heap allocs in audio thread)
    std::vector<float> scratchInputReal_;
    std::vector<float> scratchInputImag_;
    std::vector<float> scratchAccumReal_;
    std::vector<float> scratchAccumImag_;
    std::vector<float> scratchTimeResult_;
    std::vector<float> scratchFFTBuffer_;
    std::vector<float> scratchIFFTReal_;
    std::vector<float> scratchIFFTImag_;
    std::vector<float> scratchIFFTBuffer_;
    std::vector<float> scratchMulReal_;
    std::vector<float> scratchMulImag_;

    // Thread safety: protects IR data from concurrent access
    // Audio thread uses try_lock (never blocks), UI thread locks normally
    std::mutex irMutex_;

    // Accelerate FFT setup
    void* fftSetup_;
    unsigned long log2n_;

    void initFFT();
    void destroyFFT();
    void allocateScratchBuffers();
    void forwardFFT(const float* input, float* real, float* imag);
    void inverseFFT(const float* real, const float* imag, float* output);
};
