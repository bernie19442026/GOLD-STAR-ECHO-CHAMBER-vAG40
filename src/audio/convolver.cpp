#include "convolver.h"
#include <Accelerate/Accelerate.h>
#include <algorithm>
#include <cstring>
#include <cmath>

Convolver::Convolver(int blockSize, int /*sampleRate*/)
    : blockSize_(blockSize),
      fftSize_(blockSize * 2), numPartitions_(0),
      currentPartition_(0), fftSetup_(nullptr) {
    log2n_ = (unsigned long)std::log2(fftSize_);
    initFFT();
    allocateScratchBuffers();
    overlapBuffer_.resize(blockSize_, 0.0f);
}

Convolver::~Convolver() {
    destroyFFT();
}

void Convolver::initFFT() {
    fftSetup_ = (void*)vDSP_create_fftsetup((vDSP_Length)log2n_, kFFTRadix2);
}

void Convolver::destroyFFT() {
    if (fftSetup_) {
        vDSP_destroy_fftsetup((FFTSetup)fftSetup_);
        fftSetup_ = nullptr;
    }
}

void Convolver::allocateScratchBuffers() {
    int halfFFT = fftSize_ / 2;
    scratchInputReal_.resize(halfFFT, 0.0f);
    scratchInputImag_.resize(halfFFT, 0.0f);
    scratchAccumReal_.resize(halfFFT, 0.0f);
    scratchAccumImag_.resize(halfFFT, 0.0f);
    scratchTimeResult_.resize(fftSize_, 0.0f);
    scratchFFTBuffer_.resize(fftSize_, 0.0f);
    scratchIFFTReal_.resize(halfFFT, 0.0f);
    scratchIFFTImag_.resize(halfFFT, 0.0f);
    scratchIFFTBuffer_.resize(fftSize_, 0.0f);
    scratchMulReal_.resize(halfFFT, 0.0f);
    scratchMulImag_.resize(halfFFT, 0.0f);
}

void Convolver::forwardFFT(const float* input, float* real, float* imag) {
    std::fill(scratchFFTBuffer_.begin(), scratchFFTBuffer_.end(), 0.0f);
    std::copy(input, input + blockSize_, scratchFFTBuffer_.begin());

    DSPSplitComplex split;
    split.realp = real;
    split.imagp = imag;

    vDSP_ctoz((DSPComplex*)scratchFFTBuffer_.data(), 2, &split, 1, fftSize_ / 2);
    vDSP_fft_zrip((FFTSetup)fftSetup_, &split, 1, (vDSP_Length)log2n_, kFFTDirection_Forward);
}

void Convolver::inverseFFT(const float* real, const float* imag, float* output) {
    std::copy(real, real + fftSize_ / 2, scratchIFFTReal_.begin());
    std::copy(imag, imag + fftSize_ / 2, scratchIFFTImag_.begin());

    DSPSplitComplex split;
    split.realp = scratchIFFTReal_.data();
    split.imagp = scratchIFFTImag_.data();

    vDSP_fft_zrip((FFTSetup)fftSetup_, &split, 1, (vDSP_Length)log2n_, kFFTDirection_Inverse);

    vDSP_ztoc(&split, 1, (DSPComplex*)scratchIFFTBuffer_.data(), 2, fftSize_ / 2);

    float scale = 1.0f / (2.0f * fftSize_);
    vDSP_vsmul(scratchIFFTBuffer_.data(), 1, &scale, output, 1, fftSize_);
}

void Convolver::setImpulseResponse(const float* ir, int irLength) {
    // Prepare new IR partitions in temporary storage
    int newNumPartitions = (irLength + blockSize_ - 1) / blockSize_;

    std::vector<std::vector<float>> newIRReal(newNumPartitions);
    std::vector<std::vector<float>> newIRImag(newNumPartitions);
    std::vector<std::vector<float>> newHistReal(newNumPartitions);
    std::vector<std::vector<float>> newHistImag(newNumPartitions);

    for (int p = 0; p < newNumPartitions; ++p) {
        newIRReal[p].resize(fftSize_ / 2, 0.0f);
        newIRImag[p].resize(fftSize_ / 2, 0.0f);
        newHistReal[p].resize(fftSize_ / 2, 0.0f);
        newHistImag[p].resize(fftSize_ / 2, 0.0f);

        std::vector<float> partition(blockSize_, 0.0f);
        int start = p * blockSize_;
        int count = std::min(blockSize_, irLength - start);
        if (count > 0) {
            std::copy(ir + start, ir + start + count, partition.begin());
        }

        forwardFFT(partition.data(),
                    newIRReal[p].data(),
                    newIRImag[p].data());
    }

    // Lock and swap — audio thread will skip this block if locked
    std::lock_guard<std::mutex> lock(irMutex_);
    irPartitionsReal_ = std::move(newIRReal);
    irPartitionsImag_ = std::move(newIRImag);
    inputHistoryReal_ = std::move(newHistReal);
    inputHistoryImag_ = std::move(newHistImag);
    numPartitions_ = newNumPartitions;
    currentPartition_ = 0;
    overlapBuffer_.assign(blockSize_, 0.0f);
}

void Convolver::process(const float* input, float* output, int numSamples) {
    // try_lock: never block the audio thread. If IR is being updated,
    // pass through dry signal for this block (inaudible glitch-free)
    std::unique_lock<std::mutex> lock(irMutex_, std::try_to_lock);
    if (!lock.owns_lock() || numPartitions_ == 0) {
        std::copy(input, input + numSamples, output);
        return;
    }

    int halfFFT = fftSize_ / 2;

    forwardFFT(input, scratchInputReal_.data(), scratchInputImag_.data());

    std::copy(scratchInputReal_.begin(), scratchInputReal_.end(),
              inputHistoryReal_[currentPartition_].begin());
    std::copy(scratchInputImag_.begin(), scratchInputImag_.end(),
              inputHistoryImag_[currentPartition_].begin());

    std::fill(scratchAccumReal_.begin(), scratchAccumReal_.end(), 0.0f);
    std::fill(scratchAccumImag_.begin(), scratchAccumImag_.end(), 0.0f);

    for (int p = 0; p < numPartitions_; ++p) {
        int histIdx = (currentPartition_ - p + numPartitions_) % numPartitions_;

        DSPSplitComplex inputSplit = {
            inputHistoryReal_[histIdx].data(),
            inputHistoryImag_[histIdx].data()
        };
        DSPSplitComplex irSplit = {
            irPartitionsReal_[p].data(),
            irPartitionsImag_[p].data()
        };
        DSPSplitComplex mulResult = {
            scratchMulReal_.data(),
            scratchMulImag_.data()
        };

        vDSP_zvmul(&inputSplit, 1, &irSplit, 1, &mulResult, 1, halfFFT, 1);

        vDSP_vadd(scratchAccumReal_.data(), 1, scratchMulReal_.data(), 1,
                   scratchAccumReal_.data(), 1, halfFFT);
        vDSP_vadd(scratchAccumImag_.data(), 1, scratchMulImag_.data(), 1,
                   scratchAccumImag_.data(), 1, halfFFT);
    }

    inverseFFT(scratchAccumReal_.data(), scratchAccumImag_.data(),
               scratchTimeResult_.data());

    for (int i = 0; i < blockSize_; ++i) {
        output[i] = scratchTimeResult_[i] + overlapBuffer_[i];
    }
    std::copy(scratchTimeResult_.begin() + blockSize_,
              scratchTimeResult_.begin() + fftSize_,
              overlapBuffer_.begin());

    currentPartition_ = (currentPartition_ + 1) % numPartitions_;
}

void Convolver::reset() {
    std::lock_guard<std::mutex> lock(irMutex_);
    for (auto& buf : inputHistoryReal_) std::fill(buf.begin(), buf.end(), 0.0f);
    for (auto& buf : inputHistoryImag_) std::fill(buf.begin(), buf.end(), 0.0f);
    overlapBuffer_.assign(blockSize_, 0.0f);
    currentPartition_ = 0;
}

int Convolver::getLatency() const {
    return blockSize_;
}
