#include "HostAudioCallback.h"

#include <algorithm>
#include <cmath>

HostAudioCallback::HostAudioCallback(EngineLibrary& engine, SpscStereoFifo& loopbackFifo)
    : engine_(engine), loopbackFifo_(loopbackFifo)
{
}

void HostAudioCallback::audioDeviceAboutToStart(juce::AudioIODevice* device)
{
    if (device != nullptr)
    {
        const auto rate = device->getCurrentSampleRate();
        const auto block = juce::jmax(64, device->getCurrentBufferSizeSamples());
        engine_.prepare(rate, block);
        originalOutputMix_.reset(rate > 0.0 ? rate : 48000.0, 0.015);
        const auto initialMix = listenOriginalOutput_.load(std::memory_order_relaxed) ? 1.0f : 0.0f;
        originalOutputMix_.setCurrentAndTargetValue(initialMix);
        if (auto* previewSource = previewSource_.load(std::memory_order_relaxed))
            previewSource->prepareToPlay(block, rate > 0.0 ? rate : 48000.0);
    }
}

void HostAudioCallback::audioDeviceIOCallbackWithContext(const float* const* inputChannelData,
                                                         int numInputChannels,
                                                         float* const* outputChannelData,
                                                         int numOutputChannels,
                                                         int numSamples,
                                                         const juce::AudioIODeviceCallbackContext& context)
{
    juce::ignoreUnused(context);
    if (numSamples <= 0)
        return;

    for (int channel = 0; channel < numOutputChannels; ++channel)
        if (outputChannelData != nullptr && outputChannelData[channel] != nullptr)
            juce::FloatVectorOperations::clear(outputChannelData[channel], numSamples);

    if (!engine_.isLoaded() || outputChannelData == nullptr)
    {
        inputPeak_.store(0.0f, std::memory_order_relaxed);
        outputPeak_.store(0.0f, std::memory_order_relaxed);
        return;
    }

    const auto useLoopback = captureMode_.load(std::memory_order_relaxed)
                          == CaptureMode::defaultRenderLoopback;
    auto* previewSource = previewSource_.load(std::memory_order_relaxed);
    const auto usePreview = previewActive_.load(std::memory_order_relaxed) && previewSource != nullptr;
    const auto mutedForFeedback = !usePreview && feedbackGuard_.load(std::memory_order_relaxed);
    const auto listenOriginal = listenOriginalOutput_.load(std::memory_order_relaxed);
    const auto previewFxEnabled = previewEffectsEnabled_.load(std::memory_order_relaxed);
    const auto bypassOutput = listenOriginal || (usePreview && !previewFxEnabled);
    originalOutputMix_.setTargetValue(bypassOutput ? 1.0f : 0.0f);
    auto inputPeak = 0.0f;
    auto outputPeak = 0.0f;
    auto offset = 0;

    while (offset < numSamples)
    {
        const auto frames = juce::jmin(scratchCapacity, numSamples - offset);
        std::array<const float*, 2> inputPointers { nullptr, nullptr };
        std::array<float*, 2> outputPointers
        {
            outputScratch_.getWritePointer(0),
            outputScratch_.getWritePointer(1)
        };

        if (usePreview)
        {
            if (useLoopback)
                loopbackFifo_.pop(loopbackScratch_.getWritePointer(0),
                                  loopbackScratch_.getWritePointer(1), frames);
            previewScratch_.clear(0, frames);
            juce::AudioSourceChannelInfo previewInfo(&previewScratch_, 0, frames);
            previewSource->getNextAudioBlock(previewInfo);
            inputPointers[0] = previewScratch_.getReadPointer(0);
            inputPointers[1] = previewScratch_.getReadPointer(1);
        }
        else if (useLoopback)
        {
            loopbackFifo_.pop(loopbackScratch_.getWritePointer(0),
                              loopbackScratch_.getWritePointer(1), frames);
            inputPointers[0] = loopbackScratch_.getReadPointer(0);
            inputPointers[1] = loopbackScratch_.getReadPointer(1);
        }
        else if (inputChannelData != nullptr && numInputChannels > 0)
        {
            if (inputChannelData[0] != nullptr)
                inputPointers[0] = inputChannelData[0] + offset;

            if (numInputChannels > 1 && inputChannelData[1] != nullptr)
                inputPointers[1] = inputChannelData[1] + offset;
            else
                inputPointers[1] = inputPointers[0];
        }

        for (const auto* channel : inputPointers)
        {
            if (channel == nullptr)
                continue;
            for (int i = 0; i < frames; ++i)
                inputPeak = juce::jmax(inputPeak, std::abs(channel[i]));
        }

        engine_.process(inputPointers.data(), 2, outputPointers.data(), 2, frames);

        if (!mutedForFeedback)
        {
            const auto* processedL = outputScratch_.getReadPointer(0);
            const auto* processedR = outputScratch_.getReadPointer(1);
            const auto* originalL = inputPointers[0];
            const auto* originalR = inputPointers[1] != nullptr ? inputPointers[1] : originalL;
            auto* destinationMono = numOutputChannels == 1 && outputChannelData[0] != nullptr
                                 ? outputChannelData[0] + offset : nullptr;
            auto* destinationL = numOutputChannels > 0 && outputChannelData[0] != nullptr
                              ? outputChannelData[0] + offset : nullptr;
            auto* destinationR = numOutputChannels > 1 && outputChannelData[1] != nullptr
                              ? outputChannelData[1] + offset : nullptr;

            for (int i = 0; i < frames; ++i)
            {
                const auto originalMix = originalOutputMix_.getNextValue();
                const auto rawL = originalL != nullptr ? originalL[i] : 0.0f;
                const auto rawR = originalR != nullptr ? originalR[i] : rawL;
                const auto selectedL = processedL[i] + originalMix * (rawL - processedL[i]);
                const auto selectedR = processedR[i] + originalMix * (rawR - processedR[i]);

                if (destinationMono != nullptr)
                {
                    const auto mono = 0.5f * (selectedL + selectedR);
                    destinationMono[i] = mono;
                    outputPeak = juce::jmax(outputPeak, std::abs(mono));
                }
                else
                {
                    if (destinationL != nullptr)
                        destinationL[i] = selectedL;
                    if (destinationR != nullptr)
                        destinationR[i] = selectedR;
                    outputPeak = juce::jmax(outputPeak,
                                            juce::jmax(std::abs(selectedL), std::abs(selectedR)));
                }
            }
        }

        offset += frames;
    }

    inputPeak_.store(inputPeak, std::memory_order_relaxed);
    outputPeak_.store(outputPeak, std::memory_order_relaxed);
}

void HostAudioCallback::audioDeviceStopped()
{
    if (auto* previewSource = previewSource_.load(std::memory_order_relaxed))
        previewSource->releaseResources();
    engine_.release();
}
