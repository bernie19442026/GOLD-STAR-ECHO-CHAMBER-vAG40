#include "audio_io.h"
#include <iostream>
#include <cstring>
#include <cmath>
#include <vector>

CoreAudioIO::CoreAudioIO()
    : outputDevice_(0), inputDevice_(0), sampleRate_(44100.0), bufferSize_(512),
      running_(false), inputEnabled_(false), audioUnit_(nullptr),
      aggregateDevice_(0), inputBufferList_(nullptr), inputBufferMaxFrames_(0) {
    outputDevice_ = getDefaultOutputDevice();
    inputDevice_ = getDefaultInputDevice();

    // Query actual hardware sample rate
    if (outputDevice_ != 0) {
        AudioObjectPropertyAddress rateAddr = {
            kAudioDevicePropertyNominalSampleRate,
            kAudioObjectPropertyScopeGlobal,
            kAudioObjectPropertyElementMain
        };
        Float64 hwRate = 0;
        UInt32 rateSize = sizeof(hwRate);
        if (AudioObjectGetPropertyData(outputDevice_, &rateAddr,
                                        0, nullptr, &rateSize, &hwRate) == noErr && hwRate > 0) {
            sampleRate_ = hwRate;
            std::cout << "Hardware sample rate: " << sampleRate_ << " Hz" << std::endl;
        }

        // Set hardware buffer size to match our processing block size
        AudioObjectPropertyAddress bufAddr = {
            kAudioDevicePropertyBufferFrameSize,
            kAudioObjectPropertyScopeGlobal,
            kAudioObjectPropertyElementMain
        };
        UInt32 frameSize = (UInt32)bufferSize_;
        OSStatus status = AudioObjectSetPropertyData(outputDevice_, &bufAddr,
                                                      0, nullptr, sizeof(frameSize), &frameSize);
        if (status == noErr) {
            std::cout << "Set hardware buffer size: " << bufferSize_ << " frames" << std::endl;
        } else {
            // Query what the hardware is actually using
            UInt32 actualSize = 0;
            UInt32 sz = sizeof(actualSize);
            AudioObjectGetPropertyData(outputDevice_, &bufAddr,
                                        0, nullptr, &sz, &actualSize);
            if (actualSize > 0) {
                bufferSize_ = (int)actualSize;
                std::cout << "Using hardware buffer size: " << bufferSize_ << " frames" << std::endl;
            }
        }
    }
}

CoreAudioIO::~CoreAudioIO() {
    stop();
    freeInputBuffer();
}

AudioDeviceID CoreAudioIO::getDefaultOutputDevice() {
    AudioObjectPropertyAddress addr = {
        kAudioHardwarePropertyDefaultOutputDevice,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain
    };

    AudioDeviceID deviceId = 0;
    UInt32 size = sizeof(deviceId);
    AudioObjectGetPropertyData(kAudioObjectSystemObject, &addr,
                                0, nullptr, &size, &deviceId);
    return deviceId;
}

AudioDeviceID CoreAudioIO::getDefaultInputDevice() {
    AudioObjectPropertyAddress addr = {
        kAudioHardwarePropertyDefaultInputDevice,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain
    };

    AudioDeviceID deviceId = 0;
    UInt32 size = sizeof(deviceId);
    AudioObjectGetPropertyData(kAudioObjectSystemObject, &addr,
                                0, nullptr, &size, &deviceId);
    return deviceId;
}

std::vector<AudioDevice> CoreAudioIO::listOutputDevices() {
    std::vector<AudioDevice> devices;

    AudioObjectPropertyAddress addr = {
        kAudioHardwarePropertyDevices,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain
    };

    UInt32 size = 0;
    AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &addr,
                                    0, nullptr, &size);

    int numDevices = size / sizeof(AudioDeviceID);
    std::vector<AudioDeviceID> deviceIds(numDevices);
    AudioObjectGetPropertyData(kAudioObjectSystemObject, &addr,
                                0, nullptr, &size, deviceIds.data());

    for (auto id : deviceIds) {
        // Check if device has output channels
        AudioObjectPropertyAddress streamAddr = {
            kAudioDevicePropertyStreamConfiguration,
            kAudioObjectPropertyScopeOutput,
            kAudioObjectPropertyElementMain
        };
        UInt32 streamSize = 0;
        AudioObjectGetPropertyDataSize(id, &streamAddr, 0, nullptr, &streamSize);
        if (streamSize == 0) continue;

        auto* bufferList = (AudioBufferList*)malloc(streamSize);
        AudioObjectGetPropertyData(id, &streamAddr, 0, nullptr, &streamSize, bufferList);
        UInt32 outputChannels = 0;
        for (UInt32 i = 0; i < bufferList->mNumberBuffers; i++) {
            outputChannels += bufferList->mBuffers[i].mNumberChannels;
        }
        free(bufferList);
        if (outputChannels == 0) continue;

        AudioDevice dev;
        dev.deviceId = id;

        CFStringRef nameRef = nullptr;
        UInt32 nameSize = sizeof(nameRef);
        AudioObjectPropertyAddress nameAddr = {
            kAudioDevicePropertyDeviceNameCFString,
            kAudioObjectPropertyScopeGlobal,
            kAudioObjectPropertyElementMain
        };
        AudioObjectGetPropertyData(id, &nameAddr, 0, nullptr,
                                    &nameSize, &nameRef);
        if (nameRef) {
            char name[256];
            CFStringGetCString(nameRef, name, sizeof(name),
                               kCFStringEncodingUTF8);
            dev.name = name;
            CFRelease(nameRef);
        }

        devices.push_back(dev);
    }

    return devices;
}

std::vector<AudioDevice> CoreAudioIO::listInputDevices() {
    std::vector<AudioDevice> devices;

    AudioObjectPropertyAddress addr = {
        kAudioHardwarePropertyDevices,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain
    };

    UInt32 size = 0;
    AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &addr,
                                    0, nullptr, &size);

    int numDevices = size / sizeof(AudioDeviceID);
    std::vector<AudioDeviceID> deviceIds(numDevices);
    AudioObjectGetPropertyData(kAudioObjectSystemObject, &addr,
                                0, nullptr, &size, deviceIds.data());

    for (auto id : deviceIds) {
        // Check if device has input channels
        AudioObjectPropertyAddress streamAddr = {
            kAudioDevicePropertyStreamConfiguration,
            kAudioObjectPropertyScopeInput,
            kAudioObjectPropertyElementMain
        };
        UInt32 streamSize = 0;
        AudioObjectGetPropertyDataSize(id, &streamAddr, 0, nullptr, &streamSize);
        if (streamSize == 0) continue;

        auto* bufferList = (AudioBufferList*)malloc(streamSize);
        AudioObjectGetPropertyData(id, &streamAddr, 0, nullptr, &streamSize, bufferList);
        UInt32 inputChannels = 0;
        for (UInt32 i = 0; i < bufferList->mNumberBuffers; i++) {
            inputChannels += bufferList->mBuffers[i].mNumberChannels;
        }
        free(bufferList);
        if (inputChannels == 0) continue;

        AudioDevice dev;
        dev.deviceId = id;

        CFStringRef nameRef = nullptr;
        UInt32 nameSize = sizeof(nameRef);
        AudioObjectPropertyAddress nameAddr = {
            kAudioDevicePropertyDeviceNameCFString,
            kAudioObjectPropertyScopeGlobal,
            kAudioObjectPropertyElementMain
        };
        AudioObjectGetPropertyData(id, &nameAddr, 0, nullptr,
                                    &nameSize, &nameRef);
        if (nameRef) {
            char name[256];
            CFStringGetCString(nameRef, name, sizeof(name),
                               kCFStringEncodingUTF8);
            dev.name = name;
            CFRelease(nameRef);
        }

        devices.push_back(dev);
    }

    return devices;
}

void CoreAudioIO::setSampleRate(double sampleRate) {
    sampleRate_ = sampleRate;
}

void CoreAudioIO::setBufferSize(int bufferSize) {
    bufferSize_ = bufferSize;
}

void CoreAudioIO::enableInput(bool enable) {
    bool wasRunning = running_.load();
    AudioCallback savedCallback = callback_;

    if (wasRunning) {
        stop();
    }

    inputEnabled_ = enable;

    if (wasRunning && savedCallback) {
        start(savedCallback);
    }
}

void CoreAudioIO::setInputDevice(AudioDeviceID deviceId) {
    bool wasRunning = running_.load();
    AudioCallback savedCallback = callback_;

    if (wasRunning) {
        stop();
    }

    inputDevice_ = deviceId;

    if (wasRunning && savedCallback) {
        start(savedCallback);
    }
}

void CoreAudioIO::allocateInputBuffer(UInt32 maxFrames) {
    freeInputBuffer();

    inputBufferList_ = (AudioBufferList*)calloc(1,
        sizeof(AudioBufferList));
    inputBufferList_->mNumberBuffers = 1;
    inputBufferList_->mBuffers[0].mNumberChannels = 1;
    inputBufferList_->mBuffers[0].mDataByteSize = maxFrames * sizeof(float);
    inputBufferList_->mBuffers[0].mData = calloc(maxFrames, sizeof(float));
    inputBufferMaxFrames_ = maxFrames;
}

void CoreAudioIO::freeInputBuffer() {
    if (inputBufferList_) {
        if (inputBufferList_->mBuffers[0].mData) {
            free(inputBufferList_->mBuffers[0].mData);
        }
        free(inputBufferList_);
        inputBufferList_ = nullptr;
    }
}

void CoreAudioIO::setupAudioUnit() {
    AudioComponentDescription desc = {
        kAudioUnitType_Output,
        kAudioUnitSubType_DefaultOutput,
        kAudioUnitManufacturer_Apple,
        0, 0
    };

    AudioComponent comp = AudioComponentFindNext(nullptr, &desc);
    if (!comp) {
        std::cerr << "Failed to find audio output component" << std::endl;
        return;
    }
    AudioComponentInstanceNew(comp, &audioUnit_);

    // Set stream format (mono float)
    AudioStreamBasicDescription format = {};
    format.mSampleRate = sampleRate_;
    format.mFormatID = kAudioFormatLinearPCM;
    format.mFormatFlags = kAudioFormatFlagIsFloat |
                          kAudioFormatFlagIsPacked |
                          kAudioFormatFlagIsNonInterleaved;
    format.mBitsPerChannel = 32;
    format.mChannelsPerFrame = 1;
    format.mFramesPerPacket = 1;
    format.mBytesPerFrame = sizeof(float);
    format.mBytesPerPacket = sizeof(float);

    OSStatus fmtStatus = AudioUnitSetProperty(audioUnit_, kAudioUnitProperty_StreamFormat,
                          kAudioUnitScope_Input, 0,
                          &format, sizeof(format));
    if (fmtStatus != noErr) {
        std::cerr << "WARNING: Failed to set mono format on output unit: " << fmtStatus << std::endl;
    }

    // Verify the format was actually set
    AudioStreamBasicDescription actualFormat = {};
    UInt32 fmtSize = sizeof(actualFormat);
    AudioUnitGetProperty(audioUnit_, kAudioUnitProperty_StreamFormat,
                          kAudioUnitScope_Input, 0,
                          &actualFormat, &fmtSize);
    std::cerr << "[AudioIO] Output format: " << actualFormat.mChannelsPerFrame << " ch, "
              << actualFormat.mSampleRate << " Hz, "
              << actualFormat.mBitsPerChannel << " bit, "
              << "interleaved=" << !(actualFormat.mFormatFlags & kAudioFormatFlagIsNonInterleaved)
              << std::endl;

    // Set render callback
    AURenderCallbackStruct callbackStruct;
    callbackStruct.inputProc = renderCallback;
    callbackStruct.inputProcRefCon = this;

    AudioUnitSetProperty(audioUnit_, kAudioUnitProperty_SetRenderCallback,
                          kAudioUnitScope_Input, 0,
                          &callbackStruct, sizeof(callbackStruct));

    AudioUnitInitialize(audioUnit_);
}

void CoreAudioIO::setupAudioUnitWithInput() {
    // Use HALOutput to support both input and output
    AudioComponentDescription desc = {
        kAudioUnitType_Output,
        kAudioUnitSubType_HALOutput,
        kAudioUnitManufacturer_Apple,
        0, 0
    };

    AudioComponent comp = AudioComponentFindNext(nullptr, &desc);
    if (!comp) {
        std::cerr << "[Mic] Failed to find HAL output component" << std::endl;
        return;
    }
    AudioComponentInstanceNew(comp, &audioUnit_);

    // Enable input on bus 1
    UInt32 enableFlag = 1;
    OSStatus status = AudioUnitSetProperty(audioUnit_,
        kAudioOutputUnitProperty_EnableIO,
        kAudioUnitScope_Input, 1,
        &enableFlag, sizeof(enableFlag));
    std::cerr << "[Mic] Enable input on bus 1: " << (status == noErr ? "OK" : std::to_string(status)) << std::endl;

    // Enable output on bus 0
    status = AudioUnitSetProperty(audioUnit_,
        kAudioOutputUnitProperty_EnableIO,
        kAudioUnitScope_Output, 0,
        &enableFlag, sizeof(enableFlag));
    std::cerr << "[Mic] Enable output on bus 0: " << (status == noErr ? "OK" : std::to_string(status)) << std::endl;

    // HALOutput uses a single device globally. If input and output devices differ,
    // we need to create an aggregate device. For now, check if they match.
    // Set the output device first (element 0 is the only valid element for CurrentDevice)
    status = AudioUnitSetProperty(audioUnit_,
        kAudioOutputUnitProperty_CurrentDevice,
        kAudioUnitScope_Global, 0,
        &outputDevice_, sizeof(outputDevice_));
    std::cerr << "[Mic] Set output device (ID=" << outputDevice_ << "): "
              << (status == noErr ? "OK" : std::to_string(status)) << std::endl;

    // Check if input device differs from output device
    if (inputDevice_ != outputDevice_) {
        std::cerr << "[Mic] Input device (ID=" << inputDevice_ << ") differs from output (ID="
                  << outputDevice_ << ")." << std::endl;
        // Try to create an aggregate device combining both
        if (!createAggregateDevice()) {
            std::cerr << "[Mic] Aggregate device creation failed. "
                      << "Trying output device for input too." << std::endl;
            // Fall back: use output device for both (may work if it has a mic, e.g. MacBook)
        }
    }

    // Set stream format (mono float) for output bus 0 (what we render to speakers)
    AudioStreamBasicDescription format = {};
    format.mSampleRate = sampleRate_;
    format.mFormatID = kAudioFormatLinearPCM;
    format.mFormatFlags = kAudioFormatFlagIsFloat |
                          kAudioFormatFlagIsPacked |
                          kAudioFormatFlagIsNonInterleaved;
    format.mBitsPerChannel = 32;
    format.mChannelsPerFrame = 1;
    format.mFramesPerPacket = 1;
    format.mBytesPerFrame = sizeof(float);
    format.mBytesPerPacket = sizeof(float);

    status = AudioUnitSetProperty(audioUnit_, kAudioUnitProperty_StreamFormat,
                          kAudioUnitScope_Input, 0,
                          &format, sizeof(format));
    std::cerr << "[Mic] Set output format (bus 0 input scope): "
              << (status == noErr ? "OK" : std::to_string(status)) << std::endl;

    // Set the format for input bus 1 output scope
    // (the format we want to receive captured audio in)
    status = AudioUnitSetProperty(audioUnit_, kAudioUnitProperty_StreamFormat,
                          kAudioUnitScope_Output, 1,
                          &format, sizeof(format));
    std::cerr << "[Mic] Set input format (bus 1 output scope): "
              << (status == noErr ? "OK" : std::to_string(status)) << std::endl;

    // Allocate input buffer for capturing mic data (generous size for safety)
    allocateInputBuffer(std::max(8192, bufferSize_ * 4));

    // Set render callback on output bus 0
    AURenderCallbackStruct callbackStruct;
    callbackStruct.inputProc = renderCallback;
    callbackStruct.inputProcRefCon = this;

    AudioUnitSetProperty(audioUnit_, kAudioUnitProperty_SetRenderCallback,
                          kAudioUnitScope_Input, 0,
                          &callbackStruct, sizeof(callbackStruct));

    status = AudioUnitInitialize(audioUnit_);
    std::cerr << "[Mic] AudioUnitInitialize: "
              << (status == noErr ? "OK" : std::to_string(status)) << std::endl;

    // Verify what format the input is actually delivering
    AudioStreamBasicDescription actualInputFmt = {};
    UInt32 fmtSize = sizeof(actualInputFmt);
    AudioUnitGetProperty(audioUnit_, kAudioUnitProperty_StreamFormat,
                          kAudioUnitScope_Output, 1,
                          &actualInputFmt, &fmtSize);
    std::cerr << "[Mic] Actual input format: " << actualInputFmt.mChannelsPerFrame << " ch, "
              << actualInputFmt.mSampleRate << " Hz, "
              << actualInputFmt.mBitsPerChannel << " bit" << std::endl;
}

bool CoreAudioIO::createAggregateDevice() {
    // Create an aggregate device that combines the input and output devices
    // so HALOutput can use different hardware for mic and speakers

    CFMutableDictionaryRef aggDesc = CFDictionaryCreateMutable(
        kCFAllocatorDefault, 0,
        &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);

    // Unique UID for our aggregate device
    CFStringRef aggUID = CFSTR("com.goldstarreverb.aggregate");
    CFDictionarySetValue(aggDesc, CFSTR(kAudioAggregateDeviceUIDKey), aggUID);
    CFDictionarySetValue(aggDesc, CFSTR(kAudioAggregateDeviceNameKey),
                         CFSTR("Echo Chamber Aggregate"));

    // Don't show in system UI
    int isPrivate = 1;
    CFNumberRef privateRef = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &isPrivate);
    CFDictionarySetValue(aggDesc, CFSTR(kAudioAggregateDeviceIsPrivateKey), privateRef);
    CFRelease(privateRef);

    // Get UIDs for both devices
    auto getDeviceUID = [](AudioDeviceID devId) -> CFStringRef {
        AudioObjectPropertyAddress uidAddr = {
            kAudioDevicePropertyDeviceUID,
            kAudioObjectPropertyScopeGlobal,
            kAudioObjectPropertyElementMain
        };
        CFStringRef uid = nullptr;
        UInt32 uidSize = sizeof(uid);
        AudioObjectGetPropertyData(devId, &uidAddr, 0, nullptr, &uidSize, &uid);
        return uid;
    };

    CFStringRef inputUID = getDeviceUID(inputDevice_);
    CFStringRef outputUID = getDeviceUID(outputDevice_);

    if (!inputUID || !outputUID) {
        std::cerr << "[Mic] Could not get device UIDs for aggregate" << std::endl;
        if (inputUID) CFRelease(inputUID);
        if (outputUID) CFRelease(outputUID);
        CFRelease(aggDesc);
        return false;
    }

    // Build sub-device list
    CFMutableArrayRef subDevices = CFArrayCreateMutable(kCFAllocatorDefault, 0,
                                                         &kCFTypeArrayCallBacks);

    // Input sub-device
    CFMutableDictionaryRef inputSub = CFDictionaryCreateMutable(
        kCFAllocatorDefault, 0,
        &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    CFDictionarySetValue(inputSub, CFSTR(kAudioSubDeviceUIDKey), inputUID);
    CFArrayAppendValue(subDevices, inputSub);
    CFRelease(inputSub);

    // Output sub-device
    CFMutableDictionaryRef outputSub = CFDictionaryCreateMutable(
        kCFAllocatorDefault, 0,
        &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    CFDictionarySetValue(outputSub, CFSTR(kAudioSubDeviceUIDKey), outputUID);
    CFArrayAppendValue(subDevices, outputSub);
    CFRelease(outputSub);

    CFDictionarySetValue(aggDesc, CFSTR(kAudioAggregateDeviceSubDeviceListKey), subDevices);
    CFRelease(subDevices);

    // Create the aggregate device
    OSStatus status = AudioHardwareCreateAggregateDevice(aggDesc, &aggregateDevice_);
    CFRelease(aggDesc);
    CFRelease(inputUID);
    CFRelease(outputUID);

    if (status != noErr) {
        std::cerr << "[Mic] Failed to create aggregate device: " << status << std::endl;
        return false;
    }

    std::cerr << "[Mic] Created aggregate device (ID=" << aggregateDevice_ << ")" << std::endl;

    // Set the aggregate device on the HAL unit
    status = AudioUnitSetProperty(audioUnit_,
        kAudioOutputUnitProperty_CurrentDevice,
        kAudioUnitScope_Global, 0,
        &aggregateDevice_, sizeof(aggregateDevice_));
    std::cerr << "[Mic] Set aggregate device on HAL unit: "
              << (status == noErr ? "OK" : std::to_string(status)) << std::endl;

    return status == noErr;
}

OSStatus CoreAudioIO::renderCallback(void* inRefCon,
                                      AudioUnitRenderActionFlags* ioActionFlags,
                                      const AudioTimeStamp* inTimeStamp,
                                      UInt32 inBusNumber,
                                      UInt32 inNumberFrames,
                                      AudioBufferList* ioData) {
    auto* self = static_cast<CoreAudioIO*>(inRefCon);
    (void)inBusNumber;  // Always 0 (output bus)
    float* outputBuffer = (float*)ioData->mBuffers[0].mData;

    if (self->inputEnabled_.load() && self->inputBufferList_
        && inNumberFrames <= self->inputBufferMaxFrames_) {
        // Prepare the input buffer for capture
        self->inputBufferList_->mBuffers[0].mDataByteSize =
            inNumberFrames * sizeof(float);

        // Capture audio from input bus 1
        OSStatus status = AudioUnitRender(self->audioUnit_, ioActionFlags,
                                           inTimeStamp, 1, inNumberFrames,
                                           self->inputBufferList_);
        if (status == noErr) {
            const float* inputData =
                (const float*)self->inputBufferList_->mBuffers[0].mData;

            if (self->callback_) {
                self->callback_(inputData, outputBuffer, inNumberFrames, true);
            } else {
                memset(outputBuffer, 0, inNumberFrames * sizeof(float));
            }
            return noErr;
        } else {
            // Log error periodically (not every callback — would flood stderr)
            static int errCount = 0;
            if (++errCount <= 5 || errCount % 1000 == 0) {
                std::cerr << "[Mic] AudioUnitRender failed: " << status
                          << " (count=" << errCount << ")" << std::endl;
            }
        }
    }

    // No input enabled or input capture failed: provide silence as input
    // Pre-allocated silence (avoid heap alloc in audio thread)
    static thread_local std::vector<float> silenceBuffer;
    if ((int)silenceBuffer.size() < (int)inNumberFrames) {
        silenceBuffer.resize(inNumberFrames, 0.0f);
    } else {
        std::fill(silenceBuffer.begin(), silenceBuffer.begin() + inNumberFrames, 0.0f);
    }

    if (self->callback_) {
        self->callback_(silenceBuffer.data(), outputBuffer, inNumberFrames, false);
    } else {
        memset(outputBuffer, 0, inNumberFrames * sizeof(float));
    }

    // Safety: scrub NaN/Inf from output (Core Audio mutes on NaN)
    for (UInt32 i = 0; i < inNumberFrames; ++i) {
        if (!std::isfinite(outputBuffer[i])) {
            outputBuffer[i] = 0.0f;
        }
    }

    return noErr;
}

bool CoreAudioIO::start(AudioCallback callback) {
    callback_ = callback;

    if (inputEnabled_.load()) {
        setupAudioUnitWithInput();
    } else {
        setupAudioUnit();
    }

    OSStatus status = AudioOutputUnitStart(audioUnit_);
    if (status == noErr) {
        running_ = true;
        std::cout << "Audio engine started (sample rate: " << sampleRate_
                  << ", buffer: " << bufferSize_
                  << ", input: " << (inputEnabled_.load() ? "enabled" : "disabled")
                  << ")" << std::endl;
        return true;
    }
    std::cerr << "Failed to start audio unit: " << status << std::endl;
    return false;
}

void CoreAudioIO::stop() {
    if (audioUnit_) {
        AudioOutputUnitStop(audioUnit_);
        AudioComponentInstanceDispose(audioUnit_);
        audioUnit_ = nullptr;
    }
    destroyAggregateDevice();
    running_ = false;
}

void CoreAudioIO::destroyAggregateDevice() {
    if (aggregateDevice_ != 0) {
        AudioHardwareDestroyAggregateDevice(aggregateDevice_);
        aggregateDevice_ = 0;
    }
}
