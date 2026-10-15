#pragma once

#include <atomic>
#include <mutex>
#include <string>
#include <thread>

#include <juce_core/juce_core.h>

#include "SpscStereoFifo.h"

class WasapiLoopbackCapture final
{
public:
    WasapiLoopbackCapture() = default;
    ~WasapiLoopbackCapture();

    WasapiLoopbackCapture(const WasapiLoopbackCapture&) = delete;
    WasapiLoopbackCapture& operator=(const WasapiLoopbackCapture&) = delete;

    // Captures the current Windows default render endpoint in shared-mode WASAPI.
    bool start(SpscStereoFifo& destination, juce::String& error);
    void stop();

    bool isConnected() const noexcept { return connected_.load(std::memory_order_relaxed); }
    juce::String getSourceDeviceName() const;
    juce::String getLastError() const;

private:
    void run();
    void setStatus(const juce::String& deviceName, const juce::String& error);

    SpscStereoFifo* destination_ = nullptr;
    void* stopEvent_ = nullptr;
    std::thread thread_;
    std::atomic<bool> stopping_ { false };
    std::atomic<bool> connected_ { false };
    mutable std::mutex statusMutex_;
    juce::String sourceDeviceName_;
    juce::String lastError_;
};
