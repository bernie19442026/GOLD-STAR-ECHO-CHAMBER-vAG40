#pragma once

#include <cmath>

// Real-time safe biquad filter (Direct Form II Transposed)
// Supports: low pass, high pass, peaking EQ, low shelf, high shelf
class BiquadFilter {
public:
    enum class Type {
        LowPass,
        HighPass,
        PeakingEQ,
        LowShelf,
        HighShelf
    };

    BiquadFilter() { reset(); }

    void reset() {
        z1_ = z2_ = 0.0f;
    }

    void setCoefficients(Type type, float freq, float Q, float gainDB, float sampleRate) {
        if (freq <= 0.0f || sampleRate <= 0.0f) return;
        freq = std::min(freq, sampleRate * 0.499f);

        float w0 = 2.0f * M_PI * freq / sampleRate;
        float cosw0 = std::cos(w0);
        float sinw0 = std::sin(w0);
        float alpha = sinw0 / (2.0f * Q);
        float A = std::pow(10.0f, gainDB / 40.0f);

        float b0, b1, b2, a0, a1, a2;

        switch (type) {
            case Type::LowPass:
                b0 = (1.0f - cosw0) / 2.0f;
                b1 = 1.0f - cosw0;
                b2 = (1.0f - cosw0) / 2.0f;
                a0 = 1.0f + alpha;
                a1 = -2.0f * cosw0;
                a2 = 1.0f - alpha;
                break;

            case Type::HighPass:
                b0 = (1.0f + cosw0) / 2.0f;
                b1 = -(1.0f + cosw0);
                b2 = (1.0f + cosw0) / 2.0f;
                a0 = 1.0f + alpha;
                a1 = -2.0f * cosw0;
                a2 = 1.0f - alpha;
                break;

            case Type::PeakingEQ:
                b0 = 1.0f + alpha * A;
                b1 = -2.0f * cosw0;
                b2 = 1.0f - alpha * A;
                a0 = 1.0f + alpha / A;
                a1 = -2.0f * cosw0;
                a2 = 1.0f - alpha / A;
                break;

            case Type::LowShelf: {
                float sqrtA = std::sqrt(A);
                float twoSqrtAAlpha = 2.0f * sqrtA * alpha;
                b0 = A * ((A + 1.0f) - (A - 1.0f) * cosw0 + twoSqrtAAlpha);
                b1 = 2.0f * A * ((A - 1.0f) - (A + 1.0f) * cosw0);
                b2 = A * ((A + 1.0f) - (A - 1.0f) * cosw0 - twoSqrtAAlpha);
                a0 = (A + 1.0f) + (A - 1.0f) * cosw0 + twoSqrtAAlpha;
                a1 = -2.0f * ((A - 1.0f) + (A + 1.0f) * cosw0);
                a2 = (A + 1.0f) + (A - 1.0f) * cosw0 - twoSqrtAAlpha;
                break;
            }

            case Type::HighShelf: {
                float sqrtA = std::sqrt(A);
                float twoSqrtAAlpha = 2.0f * sqrtA * alpha;
                b0 = A * ((A + 1.0f) + (A - 1.0f) * cosw0 + twoSqrtAAlpha);
                b1 = -2.0f * A * ((A - 1.0f) + (A + 1.0f) * cosw0);
                b2 = A * ((A + 1.0f) + (A - 1.0f) * cosw0 - twoSqrtAAlpha);
                a0 = (A + 1.0f) - (A - 1.0f) * cosw0 + twoSqrtAAlpha;
                a1 = 2.0f * ((A - 1.0f) - (A + 1.0f) * cosw0);
                a2 = (A + 1.0f) - (A - 1.0f) * cosw0 - twoSqrtAAlpha;
                break;
            }
        }

        // Normalize
        float invA0 = 1.0f / a0;
        b0_ = b0 * invA0;
        b1_ = b1 * invA0;
        b2_ = b2 * invA0;
        a1_ = a1 * invA0;
        a2_ = a2 * invA0;
    }

    float process(float input) {
        float output = b0_ * input + z1_;
        z1_ = b1_ * input - a1_ * output + z2_;
        z2_ = b2_ * input - a2_ * output;
        return output;
    }

    void processBlock(const float* input, float* output, int numSamples) {
        for (int i = 0; i < numSamples; ++i) {
            output[i] = process(input[i]);
        }
    }

private:
    float b0_ = 1.0f, b1_ = 0.0f, b2_ = 0.0f;
    float a1_ = 0.0f, a2_ = 0.0f;
    float z1_ = 0.0f, z2_ = 0.0f;
};
