#include "audio_engine.h"
#include "dsp_utils.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <random>

AudioEngine::AudioEngine(int sampleRate, int blockSize)
    : sampleRate_(sampleRate), blockSize_(blockSize),
      preDelayWriteIdx_(0), smoothedDelaySamples_(0.0f),
      fullIRSampleRate_(sampleRate), debugCounter_(0) {
    convolver_ = std::make_unique<Convolver>(blockSize, sampleRate);
    parameters_ = std::make_unique<ParameterManager>();
    irLoader_ = std::make_unique<IRLoader>();

    // Pre-delay buffer: 500ms max (extended from v20's 200ms)
    int maxDelaySamples = (int)DSPUtils::msToSamples(500.0f, sampleRate);
    preDelayBuffer_.resize(maxDelaySamples + blockSize, 0.0f);

    // Pre-allocate all processing buffers
    gainBuffer_.resize(blockSize_, 0.0f);
    filterBuffer_.resize(blockSize_, 0.0f);
    wetBuffer_.resize(blockSize_, 0.0f);
    convolvedBuffer_.resize(blockSize_, 0.0f);

    // Initialize filters to pass-through
    lowPassFilter_.setCoefficients(BiquadFilter::Type::LowPass, 20000.0f, 0.707f, 0.0f, sampleRate);
    highPassFilter_.setCoefficients(BiquadFilter::Type::HighPass, 20.0f, 0.707f, 0.0f, sampleRate);
    eqBand100_.setCoefficients(BiquadFilter::Type::LowShelf, 100.0f, 0.707f, 0.0f, sampleRate);
    eqBand250_.setCoefficients(BiquadFilter::Type::PeakingEQ, 250.0f, 1.0f, 0.0f, sampleRate);
    eqBand1k_.setCoefficients(BiquadFilter::Type::PeakingEQ, 1000.0f, 1.0f, 0.0f, sampleRate);
    eqBand4k_.setCoefficients(BiquadFilter::Type::PeakingEQ, 4000.0f, 1.0f, 0.0f, sampleRate);
    eqBand10k_.setCoefficients(BiquadFilter::Type::HighShelf, 10000.0f, 0.707f, 0.0f, sampleRate);
    dampingFilter_.setCoefficients(BiquadFilter::Type::LowPass, 20000.0f, 0.707f, 0.0f, sampleRate);
}

AudioEngine::~AudioEngine() = default;

float AudioEngine::peakLevel(const float* buf, int n) {
    float peak = 0.0f;
    for (int i = 0; i < n; ++i) {
        float a = std::fabs(buf[i]);
        if (a > peak) peak = a;
    }
    return peak;
}

void AudioEngine::updateFilters() {
    float lp = parameters_->getParameterValue(ParameterID::LOW_PASS);
    float hp = parameters_->getParameterValue(ParameterID::HIGH_PASS);
    float eq100 = parameters_->getParameterValue(ParameterID::EQ_100);
    float eq250 = parameters_->getParameterValue(ParameterID::EQ_250);
    float eq1k = parameters_->getParameterValue(ParameterID::EQ_1K);
    float eq4k = parameters_->getParameterValue(ParameterID::EQ_4K);
    float eq10k = parameters_->getParameterValue(ParameterID::EQ_10K);

    if (lp != cachedLP_) {
        lowPassFilter_.setCoefficients(BiquadFilter::Type::LowPass, lp, 0.707f, 0.0f, sampleRate_);
        cachedLP_ = lp;
    }
    if (hp != cachedHP_) {
        highPassFilter_.setCoefficients(BiquadFilter::Type::HighPass, hp, 0.707f, 0.0f, sampleRate_);
        cachedHP_ = hp;
    }
    if (eq100 != cachedEQ100_) {
        eqBand100_.setCoefficients(BiquadFilter::Type::LowShelf, 100.0f, 0.707f, eq100, sampleRate_);
        cachedEQ100_ = eq100;
    }
    if (eq250 != cachedEQ250_) {
        eqBand250_.setCoefficients(BiquadFilter::Type::PeakingEQ, 250.0f, 1.0f, eq250, sampleRate_);
        cachedEQ250_ = eq250;
    }
    if (eq1k != cachedEQ1k_) {
        eqBand1k_.setCoefficients(BiquadFilter::Type::PeakingEQ, 1000.0f, 1.0f, eq1k, sampleRate_);
        cachedEQ1k_ = eq1k;
    }
    if (eq4k != cachedEQ4k_) {
        eqBand4k_.setCoefficients(BiquadFilter::Type::PeakingEQ, 4000.0f, 1.0f, eq4k, sampleRate_);
        cachedEQ4k_ = eq4k;
    }
    if (eq10k != cachedEQ10k_) {
        eqBand10k_.setCoefficients(BiquadFilter::Type::HighShelf, 10000.0f, 0.707f, eq10k, sampleRate_);
        cachedEQ10k_ = eq10k;
    }
}

void AudioEngine::processBlock(const float* input, float* output, int numSamples) {
    // Read parameters
    float inputGainDB = parameters_->getParameterValue(ParameterID::INPUT_GAIN);
    float inputGain = DSPUtils::dBToLinear(inputGainDB);
    float dryLevelDB = parameters_->getParameterValue(ParameterID::DRY_LEVEL);
    float dryLevel = (dryLevelDB <= -79.0f) ? 0.0f : DSPUtils::dBToLinear(dryLevelDB);
    float wetLevelDB = parameters_->getParameterValue(ParameterID::WET_LEVEL);
    float wetLevel = (wetLevelDB <= -79.0f) ? 0.0f : DSPUtils::dBToLinear(wetLevelDB);
    float outputLevelDB = parameters_->getParameterValue(ParameterID::OUTPUT_LEVEL);
    float outputLevelGain = DSPUtils::dBToLinear(outputLevelDB);
    bool bypassed = parameters_->getParameterValue(ParameterID::BYPASS) > 0.5f;

    // Update filter coefficients if parameters changed
    updateFilters();

    // Check if IR parameters changed and need rebuild
    float roomSize = parameters_->getParameterValue(ParameterID::ROOM_SIZE);
    float diffusion = parameters_->getParameterValue(ParameterID::DIFFUSION);
    float reverseVal = parameters_->getParameterValue(ParameterID::REVERSE);
    float damping = parameters_->getParameterValue(ParameterID::DAMPING);

    bool needIRRebuild = false;
    if (roomSize != cachedRoomSize_ || diffusion != cachedDiffusion_ ||
        reverseVal != cachedReverse_ || damping != cachedDamping_) {
        cachedRoomSize_ = roomSize;
        cachedDiffusion_ = diffusion;
        cachedReverse_ = reverseVal;
        cachedDamping_ = damping;
        needIRRebuild = true;
    }

    if (needIRRebuild && !fullIR_.empty()) {
        std::lock_guard<std::mutex> lock(irMutex_);
        rebuildModifiedIR();
    }

    // Process in blockSize_-sized chunks
    int processed = 0;
    while (processed < numSamples) {
        int chunkSize = std::min(blockSize_, numSamples - processed);

        if (bypassed) {
            // Bypass: copy input directly to output
            std::copy(input + processed, input + processed + chunkSize,
                      output + processed);
            processed += chunkSize;
            continue;
        }

        // 1. Input gain
        for (int i = 0; i < chunkSize; ++i) {
            gainBuffer_[i] = input[processed + i] * inputGain;
        }

        // 2. High pass filter
        highPassFilter_.processBlock(gainBuffer_.data(), filterBuffer_.data(), chunkSize);

        // 3. Low pass filter
        lowPassFilter_.processBlock(filterBuffer_.data(), gainBuffer_.data(), chunkSize);

        // 4. Five-band EQ chain
        eqBand100_.processBlock(gainBuffer_.data(), filterBuffer_.data(), chunkSize);
        eqBand250_.processBlock(filterBuffer_.data(), gainBuffer_.data(), chunkSize);
        eqBand1k_.processBlock(gainBuffer_.data(), filterBuffer_.data(), chunkSize);
        eqBand4k_.processBlock(filterBuffer_.data(), gainBuffer_.data(), chunkSize);
        eqBand10k_.processBlock(gainBuffer_.data(), filterBuffer_.data(), chunkSize);

        // filterBuffer_ now has the fully processed input signal
        // Update input meter
        inputLevel_.store(peakLevel(filterBuffer_.data(), chunkSize), std::memory_order_relaxed);

        if (wetLevel > 0.0001f) {
            // 5. Pre-delay
            std::fill(wetBuffer_.begin(), wetBuffer_.end(), 0.0f);
            applyPreDelay(filterBuffer_.data(), wetBuffer_.data(), chunkSize);

            // 6. Convolution
            std::fill(convolvedBuffer_.begin(), convolvedBuffer_.end(), 0.0f);
            convolver_->process(wetBuffer_.data(), convolvedBuffer_.data(), blockSize_);

            // Update process meter (post-convolution)
            processLevel_.store(peakLevel(convolvedBuffer_.data(), chunkSize), std::memory_order_relaxed);

            // 7. Dry/Wet mix with separate level controls
            for (int i = 0; i < chunkSize; ++i) {
                float wet = convolvedBuffer_[i];
                if (!std::isfinite(wet)) wet = 0.0f;
                float dry = filterBuffer_[i];
                output[processed + i] = (dry * dryLevel + wet * wetLevel) * outputLevelGain;
            }
        } else {
            // Pure dry path
            processLevel_.store(0.0f, std::memory_order_relaxed);
            for (int i = 0; i < chunkSize; ++i) {
                output[processed + i] = filterBuffer_[i] * dryLevel * outputLevelGain;
            }
        }

        processed += chunkSize;
    }

    // Update output meter
    outputLevel_.store(peakLevel(output, numSamples), std::memory_order_relaxed);

    // Diagnostic logging every ~2 seconds
    if (++debugCounter_ % 172 == 1) {
        std::cerr << "[DSP] in=" << inputLevel_.load()
                  << " proc=" << processLevel_.load()
                  << " out=" << outputLevel_.load()
                  << " dry=" << dryLevel << " wet=" << wetLevel
                  << " outLvl=" << outputLevelGain
                  << std::endl;
    }
}

void AudioEngine::loadIR(const std::string& path) {
    auto irData = irLoader_->loadIR(path);

    if (irData.sampleRate != sampleRate_) {
        irData.samples = IRLoader::resampleIR(irData.samples,
                                               irData.sampleRate, sampleRate_);
    }

    std::lock_guard<std::mutex> lock(irMutex_);
    fullIR_ = irData.samples;
    fullIRSampleRate_ = sampleRate_;

    rebuildModifiedIR();
}

void AudioEngine::loadIRFromData(const float* ir, int irLength, int irSampleRate) {
    std::vector<float> irVec(ir, ir + irLength);
    if (irSampleRate != sampleRate_) {
        irVec = IRLoader::resampleIR(irVec, irSampleRate, sampleRate_);
    }

    std::lock_guard<std::mutex> lock(irMutex_);
    fullIR_ = irVec;
    fullIRSampleRate_ = sampleRate_;

    rebuildModifiedIR();
}

void AudioEngine::rebuildModifiedIR() {
    // Called with irMutex_ held
    if (fullIR_.empty()) return;

    std::vector<float> ir = fullIR_;

    // 1. Room size: scale IR length (0.0 = tiny room / short IR, 1.0 = full IR)
    float roomSize = cachedRoomSize_;
    float lengthSec = parameters_->getParameterValue(ParameterID::REVERB_LENGTH);

    // Room size scales the effective IR length before reverb length is applied
    // At roomSize=0, use 10% of IR. At roomSize=1, use full IR.
    float sizeScale = 0.1f + 0.9f * roomSize;
    int scaledLength = std::max(blockSize_, (int)(ir.size() * sizeScale));
    if (scaledLength < (int)ir.size()) {
        ir.resize(scaledLength);
        // Fade out last 10ms
        int fadeSamples = std::min((int)(0.01f * sampleRate_), scaledLength / 2);
        for (int i = 0; i < fadeSamples; ++i) {
            float gain = 1.0f - (float)i / (float)fadeSamples;
            ir[scaledLength - fadeSamples + i] *= gain;
        }
    }

    // 2. Diffusion: smear early reflections using allpass-style randomization
    float diffusion = cachedDiffusion_;
    if (diffusion > 0.01f) {
        // Apply time-domain smearing: scatter samples with short random delays
        int maxScatter = (int)(diffusion * 0.02f * sampleRate_); // up to 20ms scatter
        if (maxScatter > 0) {
            std::mt19937 rng(12345); // deterministic
            std::uniform_int_distribution<int> dist(0, maxScatter);
            std::vector<float> diffused(ir.size() + maxScatter, 0.0f);
            for (int i = 0; i < (int)ir.size(); ++i) {
                int offset = dist(rng);
                diffused[i + offset] += ir[i];
            }
            // Mix original and diffused based on amount
            ir.resize(diffused.size());
            for (int i = 0; i < (int)ir.size(); ++i) {
                ir[i] = ir[i] * (1.0f - diffusion * 0.7f) +
                        diffused[i] * diffusion * 0.7f;
            }
        }
    }

    // 3. Damping: apply a lowpass to the IR tail (higher damping = more HF rolloff)
    float damping = cachedDamping_;
    if (damping > 0.01f) {
        float dampFreq = 20000.0f * std::pow(0.05f, damping); // 20kHz -> 1kHz
        BiquadFilter dampLP;
        dampLP.setCoefficients(BiquadFilter::Type::LowPass, dampFreq, 0.707f, 0.0f, sampleRate_);
        // Apply damping increasingly to the tail (first 10% untouched)
        int dampStart = (int)(ir.size() * 0.1f);
        for (int i = dampStart; i < (int)ir.size(); ++i) {
            float dampAmount = (float)(i - dampStart) / (float)(ir.size() - dampStart);
            float filtered = dampLP.process(ir[i]);
            ir[i] = ir[i] * (1.0f - dampAmount * damping) +
                    filtered * dampAmount * damping;
        }
    }

    // 4. Reverse
    bool reversed = cachedReverse_ > 0.5f;
    if (reversed) {
        std::reverse(ir.begin(), ir.end());
    }

    // 5. Apply reverb length (truncation or extension)
    int targetSamples = (int)(lengthSec * sampleRate_);
    targetSamples = std::max(targetSamples, blockSize_);

    if (targetSamples < (int)ir.size()) {
        ir.resize(targetSamples);
        int fadeSamples = std::min((int)(0.01f * sampleRate_), targetSamples / 2);
        if (fadeSamples > 0) {
            int fadeStart = targetSamples - fadeSamples;
            for (int i = 0; i < fadeSamples; ++i) {
                float gain = 1.0f - (float)i / (float)fadeSamples;
                ir[fadeStart + i] *= gain;
            }
        }
    } else if (targetSamples > (int)ir.size()) {
        int origSize = (int)ir.size();
        ir.resize(targetSamples, 0.0f);

        // Measure tail RMS
        int measureSamples = std::min((int)(0.05f * sampleRate_), origSize / 2);
        int measureStart = origSize - measureSamples;
        float tailRMS = 0.0f;
        for (int i = measureStart; i < origSize; ++i) {
            tailRMS += ir[i] * ir[i];
        }
        tailRMS = std::sqrt(tailRMS / measureSamples);

        // Generate synthetic extension
        int extensionLength = targetSamples - origSize;
        float totalDecayTime = (float)extensionLength / sampleRate_;
        float decayRate = -6.9078f / totalDecayTime;
        std::mt19937 rng(42);
        std::normal_distribution<float> noise(0.0f, 1.0f);
        float lpCoeff = std::exp(-2.0f * 3.14159f * 800.0f / sampleRate_);
        float lpState = 0.0f;
        int crossfadeSamples = std::min((int)(0.05f * sampleRate_), extensionLength / 2);

        for (int i = 0; i < extensionLength; ++i) {
            float t = (float)i / sampleRate_;
            float envelope = tailRMS * std::exp(decayRate * t);
            float n = noise(rng);
            lpState = lpState * lpCoeff + n * (1.0f - lpCoeff);
            float sample = lpState * envelope * 2.5f;

            int idx = origSize + i;
            if (i < crossfadeSamples) {
                float blend = (float)i / (float)crossfadeSamples;
                ir[idx] = ir[idx] * (1.0f - blend) + sample * blend;
            } else {
                ir[idx] = sample;
            }
        }

        // Final fade-out
        int fadeOutSamples = std::min((int)(0.1f * sampleRate_), targetSamples / 4);
        if (fadeOutSamples > 0) {
            int fadeStart = targetSamples - fadeOutSamples;
            for (int i = 0; i < fadeOutSamples; ++i) {
                float t = (float)i / (float)fadeOutSamples;
                float gain = 0.5f * (1.0f + std::cos(3.14159f * t));
                ir[fadeStart + i] *= gain;
            }
        }
    }

    convolver_->setImpulseResponse(ir.data(), (int)ir.size());

    std::cerr << "[IR] Rebuilt: " << ir.size() << " samples"
              << " (size=" << cachedRoomSize_ << " diff=" << cachedDiffusion_
              << " rev=" << cachedReverse_ << " damp=" << cachedDamping_ << ")"
              << std::endl;
}

void AudioEngine::setParameter(ParameterID id, float value) {
    parameters_->setParameterValue(id, value);

    // Parameters that require IR rebuild
    if ((id == ParameterID::REVERB_LENGTH || id == ParameterID::ROOM_SIZE ||
         id == ParameterID::DIFFUSION || id == ParameterID::REVERSE ||
         id == ParameterID::DAMPING) && !fullIR_.empty()) {
        cachedRoomSize_ = parameters_->getParameterValue(ParameterID::ROOM_SIZE);
        cachedDiffusion_ = parameters_->getParameterValue(ParameterID::DIFFUSION);
        cachedReverse_ = parameters_->getParameterValue(ParameterID::REVERSE);
        cachedDamping_ = parameters_->getParameterValue(ParameterID::DAMPING);
        std::lock_guard<std::mutex> lock(irMutex_);
        rebuildModifiedIR();
    }
}

float AudioEngine::getParameter(ParameterID id) const {
    return parameters_->getParameterValue(id);
}

int AudioEngine::getLatency() const {
    return convolver_->getLatency();
}

void AudioEngine::reset() {
    convolver_->reset();
    std::fill(preDelayBuffer_.begin(), preDelayBuffer_.end(), 0.0f);
    preDelayWriteIdx_ = 0;
    lowPassFilter_.reset();
    highPassFilter_.reset();
    eqBand100_.reset();
    eqBand250_.reset();
    eqBand1k_.reset();
    eqBand4k_.reset();
    eqBand10k_.reset();
}

void AudioEngine::applyPreDelay(const float* input, float* output, int numSamples) {
    float preDelayMs = parameters_->getParameterValue(ParameterID::PRE_DELAY);
    float targetDelaySamples = DSPUtils::msToSamples(preDelayMs, sampleRate_);
    int bufSize = (int)preDelayBuffer_.size();

    if (targetDelaySamples <= 0.0f && smoothedDelaySamples_ <= 0.5f) {
        smoothedDelaySamples_ = 0.0f;
        std::copy(input, input + numSamples, output);
        return;
    }

    float smoothCoeff = 1.0f - std::exp(-1.0f / (0.005f * sampleRate_));

    for (int i = 0; i < numSamples; ++i) {
        preDelayBuffer_[preDelayWriteIdx_] = input[i];
        smoothedDelaySamples_ += smoothCoeff * (targetDelaySamples - smoothedDelaySamples_);
        float readPos = (float)preDelayWriteIdx_ - smoothedDelaySamples_;
        if (readPos < 0.0f) readPos += (float)bufSize;
        int readIdx = (int)readPos;
        float frac = readPos - (float)readIdx;
        int nextIdx = (readIdx + 1) % bufSize;
        output[i] = preDelayBuffer_[readIdx] * (1.0f - frac) +
                     preDelayBuffer_[nextIdx] * frac;
        preDelayWriteIdx_ = (preDelayWriteIdx_ + 1) % bufSize;
    }
}
