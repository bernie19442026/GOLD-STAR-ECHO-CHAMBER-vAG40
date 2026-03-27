#pragma once

#include <CoreAudio/CoreAudio.h>
#include <AudioToolbox/AudioToolbox.h>
#include <functional>
#include <vector>
#include <string>
#include <atomic>

struct AudioDevice {
    AudioDeviceID deviceId;
    std::string name;
};

class CoreAudioIO {
public:
    using AudioCallback = std::function<void(const float*, float*, int, bool)>;

    CoreAudioIO();
    ~CoreAudioIO();

    std::vector<AudioDevice> listOutputDevices();
    std::vector<AudioDevice> listInputDevices();

    void setSampleRate(double sampleRate);
    void setBufferSize(int bufferSize);

    void enableInput(bool enable);
    bool isInputEnabled() const { return inputEnabled_.load(); }
    void setInputDevice(AudioDeviceID deviceId);

    bool start(AudioCallback callback);
    void stop();
    bool isRunning() const { return running_.load(); }

    double getSampleRate() const { return sampleRate_; }
    int getBufferSize() const { return bufferSize_; }

private:
    AudioDeviceID outputDevice_;
    AudioDeviceID inputDevice_;
    double sampleRate_;
    int bufferSize_;
    std::atomic<bool> running_;
    std::atomic<bool> inputEnabled_;
    AudioCallback callback_;

    AudioUnit audioUnit_;
    AudioDeviceID aggregateDevice_;  // Aggregate device when input != output

    // Buffer for capturing input when input is enabled
    AudioBufferList* inputBufferList_;
    UInt32 inputBufferMaxFrames_;  // Track allocated capacity

    static OSStatus renderCallback(void* inRefCon,
                                    AudioUnitRenderActionFlags* ioActionFlags,
                                    const AudioTimeStamp* inTimeStamp,
                                    UInt32 inBusNumber,
                                    UInt32 inNumberFrames,
                                    AudioBufferList* ioData);

    void setupAudioUnit();
    void setupAudioUnitWithInput();
    bool createAggregateDevice();
    void destroyAggregateDevice();
    void allocateInputBuffer(UInt32 maxFrames);
    void freeInputBuffer();
    AudioDeviceID getDefaultOutputDevice();
    AudioDeviceID getDefaultInputDevice();
};
