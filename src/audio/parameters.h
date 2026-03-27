#pragma once

#include <string>
#include <vector>
#include <functional>
#include <algorithm>
#include <atomic>

enum class ParameterID {
    IR_SELECT = 0,
    DRY_WET = 1,        // Legacy: kept for compatibility, mapped from dry/wet faders
    PRE_DELAY = 2,
    OUTPUT_LEVEL = 3,
    REVERB_LENGTH = 4,   // "Time" knob on GUI
    // --- New in D31 ---
    INPUT_GAIN = 5,
    LOW_PASS = 6,
    HIGH_PASS = 7,
    EQ_100 = 8,
    EQ_250 = 9,
    EQ_1K = 10,
    EQ_4K = 11,
    EQ_10K = 12,
    ROOM_SIZE = 13,
    DIFFUSION = 14,
    DRY_LEVEL = 15,
    WET_LEVEL = 16,
    REVERSE = 17,
    BYPASS = 18,
    DAMPING = 19,
    NUM_PARAMETERS
};

struct Parameter {
    ParameterID id;
    std::string name;
    float minValue;
    float maxValue;
    float defaultValue;
    std::string unit;
};

class ParameterManager {
public:
    ParameterManager();

    const Parameter& getParameter(ParameterID id) const;
    void setParameterValue(ParameterID id, float value);
    float getParameterValue(ParameterID id) const;

    using ParameterChangeCallback = std::function<void(ParameterID, float)>;
    void onParameterChange(ParameterChangeCallback callback);

private:
    std::vector<Parameter> parameters_;
    std::atomic<float> values_[static_cast<int>(ParameterID::NUM_PARAMETERS)];
    std::vector<ParameterChangeCallback> callbacks_;
    void initializeParameters();
};
