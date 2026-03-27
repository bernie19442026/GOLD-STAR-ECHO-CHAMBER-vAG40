#pragma once

#include <cmath>
#include <algorithm>

namespace DSPUtils {
    inline float dBToLinear(float dB) {
        return std::pow(10.0f, dB / 20.0f);
    }

    inline float linearToDb(float linear) {
        return 20.0f * std::log10(std::max(linear, 1e-10f));
    }

    inline float msToSamples(float ms, int sampleRate) {
        return ms * sampleRate / 1000.0f;
    }
}
