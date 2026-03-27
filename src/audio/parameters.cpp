#include "parameters.h"

ParameterManager::ParameterManager() {
    initializeParameters();
}

void ParameterManager::initializeParameters() {
    parameters_ = {
        {ParameterID::IR_SELECT,    "IR Select",    0.0f,   127.0f,  0.0f,   ""},
        {ParameterID::DRY_WET,      "Dry/Wet",      0.0f,   1.0f,    0.3f,   ""},
        {ParameterID::PRE_DELAY,    "Pre-Delay",    0.0f,   500.0f,  0.0f,   "ms"},
        {ParameterID::OUTPUT_LEVEL, "Output",       -80.0f, 6.0f,    0.0f,   "dB"},
        {ParameterID::REVERB_LENGTH,"Time",         0.1f,   100.0f,  100.0f, "s"},
        {ParameterID::INPUT_GAIN,   "Input Gain",   -80.0f, 12.0f,   0.0f,   "dB"},
        {ParameterID::LOW_PASS,     "Low Pass",     200.0f, 20000.0f,20000.0f,"Hz"},
        {ParameterID::HIGH_PASS,    "High Pass",    20.0f,  8000.0f, 20.0f,  "Hz"},
        {ParameterID::EQ_100,       "EQ 100Hz",    -12.0f,  12.0f,   0.0f,   "dB"},
        {ParameterID::EQ_250,       "EQ 250Hz",    -12.0f,  12.0f,   0.0f,   "dB"},
        {ParameterID::EQ_1K,        "EQ 1kHz",     -12.0f,  12.0f,   0.0f,   "dB"},
        {ParameterID::EQ_4K,        "EQ 4kHz",     -12.0f,  12.0f,   0.0f,   "dB"},
        {ParameterID::EQ_10K,       "EQ 10kHz",    -12.0f,  12.0f,   0.0f,   "dB"},
        {ParameterID::ROOM_SIZE,    "Size",         0.0f,   1.0f,    0.5f,   ""},
        {ParameterID::DIFFUSION,    "Diffusion",    0.0f,   1.0f,    0.5f,   ""},
        {ParameterID::DRY_LEVEL,    "Dry Level",   -80.0f,  0.0f,    -6.0f,  "dB"},
        {ParameterID::WET_LEVEL,    "Wet Level",   -80.0f,  0.0f,    -6.0f,  "dB"},
        {ParameterID::REVERSE,      "Reverse",      0.0f,   1.0f,    0.0f,   ""},
        {ParameterID::BYPASS,       "Bypass",       0.0f,   1.0f,    0.0f,   ""},
        {ParameterID::DAMPING,      "Damping",      0.0f,   1.0f,    0.0f,   ""},
    };
    for (size_t i = 0; i < parameters_.size(); ++i) {
        values_[i].store(parameters_[i].defaultValue, std::memory_order_relaxed);
    }
}

const Parameter& ParameterManager::getParameter(ParameterID id) const {
    return parameters_[(int)id];
}

void ParameterManager::setParameterValue(ParameterID id, float value) {
    int idx = (int)id;
    const auto& param = parameters_[idx];
    float clamped = std::clamp(value, param.minValue, param.maxValue);
    values_[idx].store(clamped, std::memory_order_relaxed);
    for (auto& cb : callbacks_) {
        cb(id, clamped);
    }
}

float ParameterManager::getParameterValue(ParameterID id) const {
    return values_[(int)id].load(std::memory_order_relaxed);
}

void ParameterManager::onParameterChange(ParameterChangeCallback callback) {
    callbacks_.push_back(callback);
}
