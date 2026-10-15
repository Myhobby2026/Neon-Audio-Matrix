#pragma once

#include <atomic>
#include <array>

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_basics/juce_audio_basics.h>

#include "EngineLibrary.h"
#include "SpscStereoFifo.h"

class HostAudioCallback final : public juce::AudioIODeviceCallback
{
public:
    enum class CaptureMode : int
    {
        selectedWasapiInput = 0,
        defaultRenderLoopback = 1
    };

    HostAudioCallback(EngineLibrary& engine, SpscStereoFifo& loopbackFifo);

    void setCaptureMode(CaptureMode mode) noexcept { captureMode_.store(mode, std::memory_order_relaxed); }
    CaptureMode getCaptureMode() const noexcept { return captureMode_.load(std::memory_order_relaxed); }
    void setFeedbackGuard(bool shouldMute) noexcept { feedbackGuard_.store(shouldMute, std::memory_order_relaxed); }
    bool isFeedbackGuardActive() const noexcept { return feedbackGuard_.load(std::memory_order_relaxed); }
    void setListenOriginalOutput(bool shouldListen) noexcept
    {
        listenOriginalOutput_.store(shouldListen, std::memory_order_relaxed);
    }
    bool isListeningToOriginalOutput() const noexcept
    {
        return listenOriginalOutput_.load(std::memory_order_relaxed);
    }
    void setPreviewSource(juce::AudioSource* source) noexcept
    {
        previewSource_.store(source, std::memory_order_relaxed);
    }
    void setPreviewActive(bool active) noexcept
    {
        previewActive_.store(active, std::memory_order_relaxed);
    }
    void setPreviewEffectsEnabled(bool enabled) noexcept
    {
        previewEffectsEnabled_.store(enabled, std::memory_order_relaxed);
    }
    float getInputPeak() const noexcept { return inputPeak_.load(std::memory_order_relaxed); }
    float getOutputPeak() const noexcept { return outputPeak_.load(std::memory_order_relaxed); }

    void audioDeviceAboutToStart(juce::AudioIODevice* device) override;
    void audioDeviceIOCallbackWithContext(const float* const* inputChannelData,
                                          int numInputChannels,
                                          float* const* outputChannelData,
                                          int numOutputChannels,
                                          int numSamples,
                                          const juce::AudioIODeviceCallbackContext& context) override;
    void audioDeviceStopped() override;

private:
    static constexpr int scratchCapacity = 8192;

    EngineLibrary& engine_;
    SpscStereoFifo& loopbackFifo_;
    juce::AudioBuffer<float> loopbackScratch_ { 2, scratchCapacity };
    juce::AudioBuffer<float> previewScratch_ { 2, scratchCapacity };
    juce::AudioBuffer<float> outputScratch_ { 2, scratchCapacity };
    std::atomic<CaptureMode> captureMode_ { CaptureMode::defaultRenderLoopback };
    std::atomic<bool> feedbackGuard_ { false };
    std::atomic<bool> listenOriginalOutput_ { false };
    std::atomic<juce::AudioSource*> previewSource_ { nullptr };
    std::atomic<bool> previewActive_ { false };
    std::atomic<bool> previewEffectsEnabled_ { true };
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> originalOutputMix_;
    std::atomic<float> inputPeak_ { 0.0f };
    std::atomic<float> outputPeak_ { 0.0f };
};
