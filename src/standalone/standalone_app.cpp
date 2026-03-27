#include "standalone_app.h"
#include <filesystem>
#include <algorithm>
#include <iostream>
#include <cmath>
#include <mach-o/dyld.h>

namespace fs = std::filesystem;

StandaloneApp::StandaloneApp()
    : currentIRIndex_(-1), inputMode_(InputMode::SILENCE) {}
StandaloneApp::~StandaloneApp() { shutdown(); }

bool StandaloneApp::initialize() {
    audioIO_ = std::make_unique<CoreAudioIO>();
    audioEngine_ = std::make_unique<AudioEngine>(
        (int)audioIO_->getSampleRate(),
        audioIO_->getBufferSize()
    );

    int maxFrames = std::max(8192, audioIO_->getBufferSize() * 4);
    fileBuffer_.resize(maxFrames, 0.0f);
    processedBuffer_.resize(maxFrames, 0.0f);
    filePlayer_.setOutputSampleRate((int)audioIO_->getSampleRate());

    // Scan for IR files
    {
        char exePath[4096];
        uint32_t exePathSize = sizeof(exePath);
        if (_NSGetExecutablePath(exePath, &exePathSize) == 0) {
            std::string exe(exePath);
            auto macosPos = exe.rfind("/Contents/MacOS/");
            if (macosPos != std::string::npos) {
                std::string bundleResources = exe.substr(0, macosPos) + "/Contents/Resources/ir_samples";
                scanIRDirectory(bundleResources);
            }
        }
    }
    if (irPaths_.empty()) {
        std::string homeDir = getenv("HOME") ? getenv("HOME") : "";
        if (!homeDir.empty()) {
            scanIRDirectory(homeDir + "/GOLD STAR ECHO CHAMBER vAG40/resources/ir_samples");
            if (irPaths_.empty()) {
                scanIRDirectory(homeDir + "/IR Reverb/resources/ir_samples");
            }
        }
    }
    if (irPaths_.empty()) {
        scanIRDirectory("resources/ir_samples");
    }

    if (!irPaths_.empty()) {
        selectIR(0);
        std::cout << "Loaded " << irPaths_.size() << " impulse responses." << std::endl;
    } else {
        std::cout << "No impulse responses found." << std::endl;
    }

    return true;
}

void StandaloneApp::run() {
    audioIO_->start([this](const float* in, float* out, int n, bool hasLive) {
        this->audioCallback(in, out, n, hasLive);
    });
}

void StandaloneApp::shutdown() {
    if (audioIO_ && audioIO_->isRunning()) {
        audioIO_->stop();
    }
}

void StandaloneApp::loadIR(const std::string& path) {
    std::lock_guard<std::mutex> lock(engineMutex_);
    try {
        audioEngine_->loadIR(path);
        std::cout << "Loaded IR: " << path << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Failed to load IR: " << e.what() << std::endl;
    }
}

void StandaloneApp::setParameter(ParameterID id, float value) {
    audioEngine_->setParameter(id, value);
}

float StandaloneApp::getParameter(ParameterID id) const {
    return audioEngine_->getParameter(id);
}

std::vector<std::string> StandaloneApp::getAvailableIRs() {
    return irPaths_;
}

void StandaloneApp::selectIR(int index) {
    if (index >= 0 && index < (int)irPaths_.size()) {
        currentIRIndex_ = index;
        loadIR(irPaths_[index]);
    }
}

void StandaloneApp::scanIRDirectory(const std::string& path) {
    if (!fs::exists(path)) return;
    for (const auto& entry : fs::directory_iterator(path)) {
        if (entry.is_regular_file()) {
            auto ext = entry.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            if (ext == ".wav" || ext == ".aif" || ext == ".aiff") {
                auto fullPath = entry.path().string();
                if (std::find(irPaths_.begin(), irPaths_.end(), fullPath) == irPaths_.end()) {
                    irPaths_.push_back(fullPath);
                }
            }
        }
    }
    std::sort(irPaths_.begin(), irPaths_.end());
}

float StandaloneApp::getInputLevel() const {
    return audioEngine_ ? audioEngine_->getInputLevel() : 0.0f;
}

float StandaloneApp::getProcessLevel() const {
    return audioEngine_ ? audioEngine_->getProcessLevel() : 0.0f;
}

float StandaloneApp::getOutputLevel() const {
    return audioEngine_ ? audioEngine_->getOutputLevel() : 0.0f;
}

bool StandaloneApp::loadAudioFile(const std::string& path) {
    return filePlayer_.loadFile(path);
}

void StandaloneApp::playFile() {
    filePlayer_.play();
    setInputMode(InputMode::FILE);
}

void StandaloneApp::pauseFile() {
    filePlayer_.pause();
}

void StandaloneApp::stopFile() {
    filePlayer_.stop();
    setInputMode(InputMode::SILENCE);
}

bool StandaloneApp::isFilePlaying() const {
    return filePlayer_.isPlaying();
}

bool StandaloneApp::isFileLoaded() const {
    return filePlayer_.isLoaded();
}

std::string StandaloneApp::getLoadedFileName() const {
    return filePlayer_.getLoadedFileName();
}

void StandaloneApp::setInputMode(InputMode mode) {
    InputMode previousMode = inputMode_.exchange(mode);
    if (mode == InputMode::MIC && previousMode != InputMode::MIC) {
        audioIO_->enableInput(true);
        filePlayer_.stop();
    } else if (mode != InputMode::MIC && previousMode == InputMode::MIC) {
        audioIO_->enableInput(false);
    }
}

void StandaloneApp::enableMicInput(bool enable) {
    if (enable) {
        setInputMode(InputMode::MIC);
    } else {
        if (filePlayer_.isPlaying()) {
            setInputMode(InputMode::FILE);
        } else {
            setInputMode(InputMode::SILENCE);
        }
    }
}

static float peakLevel(const float* buf, int n) {
    float peak = 0.0f;
    for (int i = 0; i < n; ++i) {
        float a = std::fabs(buf[i]);
        if (a > peak) peak = a;
    }
    return peak;
}

void StandaloneApp::audioCallback(const float* input, float* output,
                                   int numSamples, bool hasLiveInput) {
    InputMode mode = inputMode_.load();
    bool bypassed = bypass_.load(std::memory_order_relaxed);
    const float* sourceInput = nullptr;

    if (mode == InputMode::MIC && hasLiveInput) {
        sourceInput = input;
    } else if (mode == InputMode::FILE) {
        std::fill(fileBuffer_.begin(), fileBuffer_.begin() + std::min(numSamples, (int)fileBuffer_.size()), 0.0f);
        if (filePlayer_.pullSamples(fileBuffer_.data(), numSamples)) {
            sourceInput = fileBuffer_.data();
        } else {
            sourceInput = input;
        }
    } else {
        sourceInput = input;
    }

    // Process through engine (engine handles its own metering)
    audioEngine_->processBlock(sourceInput, processedBuffer_.data(), numSamples);

    // Bypass crossfade
    float targetRamp = bypassed ? 1.0f : 0.0f;
    float rampStart = bypassRamp_;
    float rampStep = (targetRamp - rampStart) / (float)numSamples;

    for (int i = 0; i < numSamples; ++i) {
        float r = rampStart + rampStep * (float)i;
        output[i] = processedBuffer_[i] * (1.0f - r) + sourceInput[i] * r;
    }
    bypassRamp_ = targetRamp;

    float outPeak = peakLevel(output, numSamples);
    outputLevelL_.store(outPeak, std::memory_order_relaxed);
    outputLevelR_.store(outPeak, std::memory_order_relaxed);
}
