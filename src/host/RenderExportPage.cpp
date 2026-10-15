#include "RenderExportPage.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <numeric>
#include <utility>

#if defined(_WIN32)
#include <objbase.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wrl/client.h>
#endif

namespace
{
constexpr int renderBlockSize = 2048;
constexpr int renderMargin = 14;

void styleRenderButton(juce::TextButton& button)
{
    button.setColour(juce::TextButton::buttonColourId, NeonLookAndFeel::panelRaised());
    button.setColour(juce::TextButton::buttonOnColourId, NeonLookAndFeel::cyan().withAlpha(0.20f));
    button.setColour(juce::TextButton::textColourOffId, NeonLookAndFeel::textPrimary());
    button.setColour(juce::TextButton::textColourOnId, NeonLookAndFeel::cyan());
}

void styleRenderCombo(juce::ComboBox& combo)
{
    combo.setColour(juce::ComboBox::backgroundColourId, NeonLookAndFeel::panelRaised());
    combo.setColour(juce::ComboBox::outlineColourId, NeonLookAndFeel::textMuted().withAlpha(0.30f));
    combo.setColour(juce::ComboBox::textColourId, NeonLookAndFeel::textPrimary());
    combo.setColour(juce::ComboBox::arrowColourId, NeonLookAndFeel::cyan());
    combo.setColour(juce::PopupMenu::backgroundColourId, NeonLookAndFeel::panelRaised());
    combo.setColour(juce::PopupMenu::textColourId, NeonLookAndFeel::textPrimary());
    combo.setColour(juce::PopupMenu::highlightedBackgroundColourId, NeonLookAndFeel::cyan().withAlpha(0.25f));
}

juce::String formatLoudness(double lufs)
{
    if (!std::isfinite(lufs) || lufs <= -70.0)
        return "silent";
    return juce::String(lufs, 1) + " LUFS";
}

#if defined(_WIN32)
class MediaFoundationMp3Writer final
{
public:
    ~MediaFoundationMp3Writer() { shutdown(); }

    bool open(const juce::File& outputFile, int sampleRate, int bitRateKbps,
              juce::String& error)
    {
        const auto comResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (FAILED(comResult))
        {
            error = "Could not initialize COM for MP3 encoding.";
            return false;
        }
        comInitialized_ = true;

        Microsoft::WRL::ComPtr<IMFMediaType> outputType;
        Microsoft::WRL::ComPtr<IMFMediaType> inputType;
        const auto failOpen = [this, &outputType, &inputType, &error]
                              (const char* operation, HRESULT result)
        {
            error = juce::String(operation) + " (HRESULT 0x"
                   + juce::String::toHexString(static_cast<int>(result)) + ").";
            outputType.Reset();
            inputType.Reset();
            shutdown();
            return false;
        };

        auto result = MFStartup(MF_VERSION, MFSTARTUP_FULL);
        if (FAILED(result))
            return failOpen("Could not start Windows Media Foundation", result);
        mediaFoundationStarted_ = true;

        result = MFCreateAttributes(attributes_.GetAddressOf(), 2);
        if (SUCCEEDED(result))
            result = attributes_->SetGUID(MF_TRANSCODE_CONTAINERTYPE, MFTranscodeContainerType_MP3);
        if (SUCCEEDED(result))
            result = attributes_->SetUINT32(MF_SINK_WRITER_DISABLE_THROTTLING, TRUE);
        if (FAILED(result))
            return failOpen("Could not configure the MP3 output container", result);

        const auto outputPath = outputFile.getFullPathName();
        result = MFCreateSinkWriterFromURL(outputPath.toWideCharPointer(), nullptr,
                                            attributes_.Get(), sinkWriter_.GetAddressOf());
        if (FAILED(result))
            return failOpen("Could not open the Windows MP3 encoder", result);

        result = MFCreateMediaType(outputType.GetAddressOf());
        if (SUCCEEDED(result))
            result = outputType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
        if (SUCCEEDED(result))
            result = outputType->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_MP3);
        if (SUCCEEDED(result))
            result = outputType->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND,
                                            static_cast<UINT32>(bitRateKbps * 1000 / 8));
        if (SUCCEEDED(result))
            result = outputType->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, 2);
        if (SUCCEEDED(result))
            result = outputType->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND,
                                            static_cast<UINT32>(sampleRate));
        if (FAILED(result))
            return failOpen("Could not configure the MP3 output format", result);

        result = sinkWriter_->AddStream(outputType.Get(), &streamIndex_);
        if (FAILED(result))
            return failOpen("Could not add an MP3 output stream", result);

        result = MFCreateMediaType(inputType.GetAddressOf());
        if (SUCCEEDED(result))
            result = inputType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
        if (SUCCEEDED(result))
            result = inputType->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
        if (SUCCEEDED(result))
            result = inputType->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, 2);
        if (SUCCEEDED(result))
            result = inputType->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND,
                                           static_cast<UINT32>(sampleRate));
        if (SUCCEEDED(result))
            result = inputType->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
        if (SUCCEEDED(result))
            result = inputType->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, 4);
        if (SUCCEEDED(result))
            result = inputType->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND,
                                           static_cast<UINT32>(sampleRate * 4));
        if (SUCCEEDED(result))
            result = inputType->SetUINT32(MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE);
        if (SUCCEEDED(result))
            result = inputType->SetUINT32(MF_MT_AUDIO_PREFER_WAVEFORMATEX, TRUE);
        if (FAILED(result))
            return failOpen("Could not configure PCM input for the MP3 encoder", result);

        result = sinkWriter_->SetInputMediaType(streamIndex_, inputType.Get(), nullptr);
        if (FAILED(result))
            return failOpen("The MP3 encoder rejected the selected sample rate", result);

        result = sinkWriter_->BeginWriting();
        if (FAILED(result))
            return failOpen("Could not begin MP3 encoding", result);

        sampleRate_ = sampleRate;
        return true;
    }

    bool write(const std::int16_t* interleavedStereo, int frames, juce::String& error)
    {
        if (sinkWriter_ == nullptr || interleavedStereo == nullptr || frames <= 0)
        {
            error = "The MP3 encoder is not ready to accept audio.";
            return false;
        }

        const auto byteCount = static_cast<DWORD>(frames * 2 * static_cast<int>(sizeof(std::int16_t)));
        Microsoft::WRL::ComPtr<IMFSample> sample;
        Microsoft::WRL::ComPtr<IMFMediaBuffer> buffer;
        auto result = MFCreateSample(sample.GetAddressOf());
        if (SUCCEEDED(result))
            result = MFCreateMemoryBuffer(byteCount, buffer.GetAddressOf());
        if (FAILED(result))
        {
            error = "Could not allocate an MP3 encoder sample.";
            return false;
        }

        BYTE* destination = nullptr;
        DWORD maximumLength = 0;
        DWORD currentLength = 0;
        result = buffer->Lock(&destination, &maximumLength, &currentLength);
        if (FAILED(result))
        {
            error = "Could not lock an MP3 encoder sample.";
            return false;
        }
        if (maximumLength < byteCount)
        {
            buffer->Unlock();
            error = "The MP3 encoder sample buffer is too small.";
            return false;
        }
        std::memcpy(destination, interleavedStereo, byteCount);
        result = buffer->Unlock();
        if (SUCCEEDED(result))
            result = buffer->SetCurrentLength(byteCount);
        if (SUCCEEDED(result))
            result = sample->AddBuffer(buffer.Get());
        if (FAILED(result))
        {
            error = "Could not prepare an MP3 encoder sample.";
            return false;
        }

        constexpr LONGLONG ticksPerSecond = 10000000;
        const auto startFrame = framesWritten_;
        const auto endFrame = startFrame + static_cast<std::int64_t>(frames);
        const auto startTime = startFrame * ticksPerSecond / sampleRate_;
        const auto endTime = endFrame * ticksPerSecond / sampleRate_;
        result = sample->SetSampleTime(startTime);
        if (SUCCEEDED(result))
            result = sample->SetSampleDuration(endTime - startTime);
        if (SUCCEEDED(result))
            result = sinkWriter_->WriteSample(streamIndex_, sample.Get());
        if (FAILED(result))
        {
            error = "Windows Media Foundation failed while encoding MP3 audio.";
            return false;
        }

        framesWritten_ = endFrame;
        return true;
    }

    bool finish(juce::String& error)
    {
        const auto result = sinkWriter_ != nullptr ? sinkWriter_->Finalize() : E_FAIL;
        shutdown();
        if (FAILED(result))
        {
            error = "Windows Media Foundation could not finalize the MP3 file.";
            return false;
        }
        return true;
    }

private:
    void shutdown() noexcept
    {
        sinkWriter_.Reset();
        attributes_.Reset();
        if (mediaFoundationStarted_)
        {
            MFShutdown();
            mediaFoundationStarted_ = false;
        }
        if (comInitialized_)
        {
            CoUninitialize();
            comInitialized_ = false;
        }
    }

    Microsoft::WRL::ComPtr<IMFSinkWriter> sinkWriter_;
    Microsoft::WRL::ComPtr<IMFAttributes> attributes_;
    DWORD streamIndex_ = 0;
    int sampleRate_ = 0;
    std::int64_t framesWritten_ = 0;
    bool mediaFoundationStarted_ = false;
    bool comInitialized_ = false;
};
#else
class MediaFoundationMp3Writer final
{
public:
    bool open(const juce::File&, int, int, juce::String& error)
    {
        error = "MP3 export is only available on Windows through Media Foundation.";
        return false;
    }
    bool write(const std::int16_t*, int, juce::String& error)
    {
        error = "MP3 export is only available on Windows through Media Foundation.";
        return false;
    }
    bool finish(juce::String& error)
    {
        error = "MP3 export is only available on Windows through Media Foundation.";
        return false;
    }
};
#endif

// Lightweight stereo-only crossfeed and decorrelated-side effect; this is not HRTF binaural decoding.
class VirtualSurroundStereoProcessor final
{
public:
    void prepare(double sampleRate)
    {
        const auto rate = sampleRate > 0.0 ? sampleRate : 48000.0;
        crossfeedDelay_ = juce::jmax(1, juce::roundToInt(rate * 0.0045));
        diffuseDelayLeft_ = juce::jmax(crossfeedDelay_ + 1, juce::roundToInt(rate * 0.011));
        diffuseDelayRight_ = juce::jmax(diffuseDelayLeft_ + 1, juce::roundToInt(rate * 0.017));
        const auto size = diffuseDelayRight_ + 1;
        leftHistory_.assign(static_cast<std::size_t>(size), 0.0f);
        rightHistory_.assign(static_cast<std::size_t>(size), 0.0f);
        sideHistory_.assign(static_cast<std::size_t>(size), 0.0f);
        writePosition_ = 0;
        crossfeedLowpassLeft_ = 0.0f;
        crossfeedLowpassRight_ = 0.0f;
        crossfeedLowpassCoefficient_ = static_cast<float>(
            1.0 - std::exp(-6.283185307179586 * 900.0 / rate));
    }

    void processSample(float inputLeft, float inputRight, float& outputLeft, float& outputRight) noexcept
    {
        const auto size = static_cast<int>(sideHistory_.size());
        if (size <= 0)
        {
            outputLeft = inputLeft;
            outputRight = inputRight;
            return;
        }

        const auto crossPosition = delayedIndex(crossfeedDelay_, size);
        const auto diffuseLeftPosition = delayedIndex(diffuseDelayLeft_, size);
        const auto diffuseRightPosition = delayedIndex(diffuseDelayRight_, size);
        const auto delayedLeft = leftHistory_[static_cast<std::size_t>(crossPosition)];
        const auto delayedRight = rightHistory_[static_cast<std::size_t>(crossPosition)];
        crossfeedLowpassLeft_ += crossfeedLowpassCoefficient_ * (delayedRight - crossfeedLowpassLeft_);
        crossfeedLowpassRight_ += crossfeedLowpassCoefficient_ * (delayedLeft - crossfeedLowpassRight_);
        const auto side = 0.5f * (inputLeft - inputRight);
        const auto diffuseLeft = sideHistory_[static_cast<std::size_t>(diffuseLeftPosition)];
        const auto diffuseRight = sideHistory_[static_cast<std::size_t>(diffuseRightPosition)];

        outputLeft = 0.78f * inputLeft + 0.10f * crossfeedLowpassLeft_ + 0.12f * diffuseLeft;
        outputRight = 0.78f * inputRight + 0.10f * crossfeedLowpassRight_ - 0.12f * diffuseRight;

        leftHistory_[static_cast<std::size_t>(writePosition_)] = inputLeft;
        rightHistory_[static_cast<std::size_t>(writePosition_)] = inputRight;
        sideHistory_[static_cast<std::size_t>(writePosition_)] = side;
        writePosition_ = (writePosition_ + 1) % size;
    }

private:
    int delayedIndex(int delay, int size) const noexcept
    {
        return (writePosition_ + size - delay) % size;
    }

    std::vector<float> leftHistory_;
    std::vector<float> rightHistory_;
    std::vector<float> sideHistory_;
    int writePosition_ = 0;
    int crossfeedDelay_ = 1;
    int diffuseDelayLeft_ = 2;
    int diffuseDelayRight_ = 3;
    float crossfeedLowpassLeft_ = 0.0f;
    float crossfeedLowpassRight_ = 0.0f;
    float crossfeedLowpassCoefficient_ = 0.0f;
};

struct Surround51Frame
{
    float frontLeft = 0.0f;
    float frontRight = 0.0f;
    float centre = 0.0f;
    float lfe = 0.0f;
    float surroundLeft = 0.0f;
    float surroundRight = 0.0f;
};

// Derives six channels from stereo; it cannot recreate discrete surround content absent from the source.
class StereoTo5point1Upmixer final
{
public:
    void prepare(double sampleRate)
    {
        const auto rate = sampleRate > 0.0 ? sampleRate : 48000.0;
        delayLeft_ = juce::jmax(1, juce::roundToInt(rate * 0.013));
        delayRight_ = juce::jmax(delayLeft_ + 1, juce::roundToInt(rate * 0.019));
        sideHistory_.assign(static_cast<std::size_t>(delayRight_ + 1), 0.0f);
        writePosition_ = 0;
        lfeState_ = 0.0f;
        lfeCoefficient_ = static_cast<float>(1.0 - std::exp(-6.283185307179586 * 120.0 / rate));
    }

    Surround51Frame processSample(float inputLeft, float inputRight) noexcept
    {
        const auto size = static_cast<int>(sideHistory_.size());
        const auto side = 0.5f * (inputLeft - inputRight);
        const auto mono = 0.5f * (inputLeft + inputRight);
        const auto delayedLeft = sideHistory_[static_cast<std::size_t>((writePosition_ + size - delayLeft_) % size)];
        const auto delayedRight = sideHistory_[static_cast<std::size_t>((writePosition_ + size - delayRight_) % size)];
        sideHistory_[static_cast<std::size_t>(writePosition_)] = side;
        writePosition_ = (writePosition_ + 1) % size;
        lfeState_ += lfeCoefficient_ * (mono - lfeState_);

        return { 0.90f * inputLeft,
                 0.90f * inputRight,
                 0.70710678f * mono,
                 0.50f * lfeState_,
                 0.55f * side + 0.18f * delayedLeft,
                -0.55f * side - 0.18f * delayedRight };
    }

private:
    std::vector<float> sideHistory_;
    int writePosition_ = 0;
    int delayLeft_ = 1;
    int delayRight_ = 2;
    float lfeState_ = 0.0f;
    float lfeCoefficient_ = 0.0f;
};
} // namespace

class RenderExportPage::IntegratedLoudnessMeter
{
public:
    IntegratedLoudnessMeter(double sampleRate, const juce::AudioChannelSet& channelLayout)
        : blockFrames_(juce::jmax(1, juce::roundToInt(sampleRate * 0.4))),
          channelCount_(juce::jmin(6, channelLayout.size()))
    {
        const auto safeRate = sampleRate > 0.0 ? sampleRate : 48000.0;
        const auto shelf = juce::IIRCoefficients::makeHighShelf(
            safeRate, 1681.974, 0.707, juce::Decibels::decibelsToGain(4.0f));
        const auto highPass = juce::IIRCoefficients::makeHighPass(safeRate, 38.135, 0.5003);
        for (int channel = 0; channel < channelCount_; ++channel)
        {
            const auto index = static_cast<std::size_t>(channel);
            shelf_[index].setCoefficients(shelf);
            highPass_[index].setCoefficients(highPass);
            shelf_[index].reset();
            highPass_[index].reset();

            const auto type = channelLayout.getTypeOfChannel(channel);
            const auto isLfe = type == juce::AudioChannelSet::LFE
                            || type == juce::AudioChannelSet::LFE2;
            const auto isSurround = type == juce::AudioChannelSet::leftSurround
                                 || type == juce::AudioChannelSet::rightSurround
                                 || type == juce::AudioChannelSet::leftSurroundRear
                                 || type == juce::AudioChannelSet::rightSurroundRear;
            channelWeights_[index] = isLfe ? 0.0f : (isSurround ? 1.4125375f : 1.0f);
        }
    }

    void process(const juce::AudioBuffer<float>& buffer, int frames)
    {
        const auto channels = juce::jmin(channelCount_, buffer.getNumChannels());
        for (int sample = 0; sample < frames; ++sample)
        {
            for (int channel = 0; channel < channels; ++channel)
            {
                const auto index = static_cast<std::size_t>(channel);
                if (channelWeights_[index] <= 0.0f)
                    continue;
                auto value = buffer.getSample(channel, sample);
                if (!std::isfinite(value))
                    value = 0.0f;
                value = shelf_[index].processSingleSampleRaw(value);
                value = highPass_[index].processSingleSampleRaw(value);
                blockEnergy_ += static_cast<double>(channelWeights_[index])
                              * static_cast<double>(value) * static_cast<double>(value);
            }

            ++blockSampleCount_;
            if (blockSampleCount_ >= blockFrames_)
                finishBlock();
        }
    }

    double getIntegratedLufs()
    {
        if (blockSampleCount_ > 0)
            finishBlock();
        if (blockEnergies_.empty())
            return -70.0;

        double absoluteEnergy = 0.0;
        int absoluteCount = 0;
        for (const auto energy : blockEnergies_)
        {
            if (toLufs(energy) > -70.0)
            {
                absoluteEnergy += energy;
                ++absoluteCount;
            }
        }
        if (absoluteCount == 0)
            return -70.0;

        const auto preliminaryLufs = toLufs(absoluteEnergy / static_cast<double>(absoluteCount));
        const auto relativeGate = preliminaryLufs - 10.0;
        double gatedEnergy = 0.0;
        int gatedCount = 0;
        for (const auto energy : blockEnergies_)
        {
            if (toLufs(energy) > juce::jmax(-70.0, relativeGate))
            {
                gatedEnergy += energy;
                ++gatedCount;
            }
        }
        if (gatedCount == 0)
            return preliminaryLufs;
        return toLufs(gatedEnergy / static_cast<double>(gatedCount));
    }

private:
    static double toLufs(double meanSquare) noexcept
    {
        if (!(meanSquare > 1.0e-20) || !std::isfinite(meanSquare))
            return -120.0;
        return -0.691 + 10.0 * std::log10(meanSquare);
    }

    void finishBlock()
    {
        if (blockSampleCount_ > 0)
        {
            blockEnergies_.push_back(blockEnergy_ / static_cast<double>(blockSampleCount_));
            blockEnergy_ = 0.0;
            blockSampleCount_ = 0;
        }
    }

    int blockFrames_ = 1;
    int channelCount_ = 2;
    int blockSampleCount_ = 0;
    double blockEnergy_ = 0.0;
    std::vector<double> blockEnergies_;
    std::array<float, 6> channelWeights_ {};
    std::array<juce::IIRFilter, 6> shelf_;
    std::array<juce::IIRFilter, 6> highPass_;
};

class RenderExportPage::RenderWorker final : public juce::Thread
{
public:
    RenderWorker(RenderExportPage& owner, BatchSettings settings)
        : juce::Thread("Neon offline render"), owner_(owner), settings_(std::move(settings))
    {
    }

private:
    void run() override { owner_.renderBatch(*this, settings_); }

    RenderExportPage& owner_;
    BatchSettings settings_;
};

RenderExportPage::RenderExportPage(EngineLibrary& liveEngine,
                                   HostAudioCallback& audioCallback,
                                   const juce::File& engineDll)
    : liveEngine_(liveEngine), audioCallback_(audioCallback), engineDll_(engineDll),
      readAheadThread_("Neon Render Read Ahead"), filesList_("FILES", this),
      progressBar_(progressValue_)
{
    previewFormats_.registerBasicFormats();
    readAheadThread_.startThread();
    audioCallback_.setPreviewSource(&transportSource_);
    audioCallback_.setPreviewEffectsEnabled(true);

    if (!engineDll_.existsAsFile() || !offlineEngine_.open(engineDll_))
        setWorkerStatus("Offline effects engine unavailable; turn FX off to export a dry copy.");
    else
        setWorkerStatus("Add files, choose settings, press RENDER ALL.");

    outputFolder_ = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                        .getChildFile("AudioFX Renders");
    outputFolder_.createDirectory();

    filesList_.setRowHeight(36);
    filesList_.setMultipleSelectionEnabled(false);
    filesList_.setColour(juce::ListBox::backgroundColourId, NeonLookAndFeel::panel().darker(0.25f));
    filesList_.setColour(juce::ListBox::outlineColourId, NeonLookAndFeel::cyan().withAlpha(0.16f));

    styleRenderButton(addFilesButton_);
    styleRenderButton(clearFilesButton_);
    styleRenderButton(previousButton_);
    styleRenderButton(playPauseButton_);
    styleRenderButton(stopButton_);
    styleRenderButton(nextButton_);
    styleRenderButton(fastForwardButton_);
    styleRenderButton(wavButton_);
    styleRenderButton(flacButton_);
    styleRenderButton(mp3Button_);
    styleRenderButton(openYoutubeButton_);
    styleRenderButton(chooseFolderButton_);
    styleRenderButton(renderAllButton_);
    styleRenderButton(openFolderButton_);

    wavButton_.setClickingTogglesState(true);
    flacButton_.setClickingTogglesState(true);
    mp3Button_.setClickingTogglesState(true);
    wavButton_.setRadioGroupId(6041);
    flacButton_.setRadioGroupId(6041);
    mp3Button_.setRadioGroupId(6041);
    wavButton_.setToggleState(true, juce::dontSendNotification);

    surroundModeBox_.addItem("Output: Stereo (standard)", static_cast<int>(SurroundOutputMode::stereo));
    surroundModeBox_.addItem("Output: Virtual surround (headphones)", static_cast<int>(SurroundOutputMode::virtualStereo));
    surroundModeBox_.addItem("Output: 5.1 upmix (speakers)", static_cast<int>(SurroundOutputMode::surround51));
    surroundModeBox_.setSelectedId(static_cast<int>(SurroundOutputMode::stereo), juce::dontSendNotification);
    surroundModeBox_.setTooltip("Stereo is unchanged. Virtual surround adds a stereo headphone effect. 5.1 creates six synthetic channels from stereo; it cannot restore genuine discrete surround from a stereo source. 5.1 export is WAV/FLAC only.");
    styleRenderCombo(surroundModeBox_);

    sampleRateBox_.addItem("44.1 kHz (CD)", 44100);
    sampleRateBox_.addItem("48 kHz (Standard)", 48000);
    sampleRateBox_.addItem("96 kHz (High Res)", 96000);
    sampleRateBox_.setSelectedId(48000, juce::dontSendNotification);
    styleRenderCombo(sampleRateBox_);

    bitDepthBox_.addItem("16-bit", 16);
    bitDepthBox_.addItem("24-bit", 24);
    bitDepthBox_.setSelectedId(24, juce::dontSendNotification);
    styleRenderCombo(bitDepthBox_);

    bitRateBox_.addItem("128 kbps", 128);
    bitRateBox_.addItem("192 kbps", 192);
    bitRateBox_.addItem("256 kbps", 256);
    bitRateBox_.addItem("320 kbps", 320);
    bitRateBox_.setSelectedId(192, juce::dontSendNotification);
    styleRenderCombo(bitRateBox_);
    bitRateBox_.setTooltip("MP3 encoding bitrate; higher settings produce larger files.");
    mp3Button_.setTooltip("Encode local files with the Windows Media Foundation MP3 encoder. YouTube links are never downloaded.");

    youtubeUrlEditor_.setMultiLine(false);
    youtubeUrlEditor_.setReturnKeyStartsNewLine(false);
    youtubeUrlEditor_.setTextToShowWhenEmpty("Paste YouTube URL (opens browser only)",
                                              NeonLookAndFeel::textMuted());
    youtubeUrlEditor_.setColour(juce::TextEditor::backgroundColourId, NeonLookAndFeel::panelRaised());
    youtubeUrlEditor_.setColour(juce::TextEditor::outlineColourId, NeonLookAndFeel::cyan().withAlpha(0.22f));
    youtubeUrlEditor_.setColour(juce::TextEditor::textColourId, NeonLookAndFeel::textPrimary());
    youtubeUrlEditor_.setColour(juce::TextEditor::highlightColourId, NeonLookAndFeel::cyan().withAlpha(0.25f));
    youtubeUrlEditor_.setFont(juce::Font(juce::FontOptions(12.0f)));
    youtubeUrlEditor_.setTooltip("Opens the official YouTube page in your default browser. It does not download audio or add it to the render queue.");
    openYoutubeButton_.setTooltip("Open the pasted YouTube link in your default browser; no audio is downloaded.");
    youtubeUrlEditor_.onReturnKey = [this] { openYoutubeLink(); };

    effectsToggle_.setToggleState(true, juce::dontSendNotification);
    effectsToggle_.setColour(juce::ToggleButton::textColourId, NeonLookAndFeel::textPrimary());
    effectsToggle_.setColour(juce::ToggleButton::tickColourId, NeonLookAndFeel::cyan());
    effectsToggle_.setTooltip("Apply the current Neon Audio Matrix effects during preview and export.");
    effectsToggle_.onClick = [this]
    {
        audioCallback_.setPreviewEffectsEnabled(effectsToggle_.getToggleState());
    };

    normalizeToggle_.setToggleState(true, juce::dontSendNotification);
    normalizeToggle_.setColour(juce::ToggleButton::textColourId, NeonLookAndFeel::textPrimary());
    normalizeToggle_.setColour(juce::ToggleButton::tickColourId, NeonLookAndFeel::cyan());
    normalizeToggle_.setTooltip("K-weighted integrated loudness target: approximately -14 LUFS, with sample peaks kept below -1 dBFS.");

    timeline_.setSliderStyle(juce::Slider::LinearHorizontal);
    timeline_.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    timeline_.setRange(0.0, 1.0, 0.01);
    timeline_.setColour(juce::Slider::trackColourId, NeonLookAndFeel::panelRaised());
    timeline_.setColour(juce::Slider::thumbColourId, NeonLookAndFeel::cyan());
    timeline_.onDragStart = [this] { isSeeking_ = true; };
    timeline_.onDragEnd = [this] { isSeeking_ = false; };
    timeline_.onValueChange = [this]
    {
        if (isSeeking_ && previewReaderSource_ != nullptr)
            transportSource_.setPosition(timeline_.getValue());
    };

    outputFolderLabel_.setColour(juce::Label::textColourId, NeonLookAndFeel::textMuted());
    outputFolderLabel_.setJustificationType(juce::Justification::centredLeft);
    outputFolderLabel_.setText(outputFolder_.getFullPathName(), juce::dontSendNotification);
    outputFolderLabel_.setTooltip(outputFolder_.getFullPathName());

    fileCountLabel_.setColour(juce::Label::textColourId, NeonLookAndFeel::textMuted());
    fileCountLabel_.setJustificationType(juce::Justification::centredRight);
    fileCountLabel_.setText("0 loaded", juce::dontSendNotification);

    trackTimeLabel_.setColour(juce::Label::textColourId, NeonLookAndFeel::textMuted());
    trackTimeLabel_.setJustificationType(juce::Justification::centred);
    trackTimeLabel_.setText("--:-- / --:--", juce::dontSendNotification);

    formatsLabel_.setColour(juce::Label::textColourId, NeonLookAndFeel::textMuted().withAlpha(0.8f));
    formatsLabel_.setFont(juce::Font(juce::FontOptions(10.0f)));
    formatsLabel_.setJustificationType(juce::Justification::centredLeft);
    formatsLabel_.setText("Import: WAV, MP3, FLAC, OGG, AIFF  |  Export: WAV/FLAC (stereo or 5.1), MP3 (stereo only)",
                          juce::dontSendNotification);

    statusLabel_.setColour(juce::Label::textColourId, NeonLookAndFeel::lime());
    statusLabel_.setFont(juce::Font(juce::FontOptions(12.0f)));
    statusLabel_.setJustificationType(juce::Justification::centredLeft);

    progressBar_.setColour(juce::ProgressBar::backgroundColourId, NeonLookAndFeel::panelRaised());
    progressBar_.setColour(juce::ProgressBar::foregroundColourId, NeonLookAndFeel::cyan());

    addFilesButton_.onClick = [this] { addFiles(); };
    clearFilesButton_.onClick = [this] { clearFiles(); };
    previousButton_.onClick = [this] { playPreviousTrack(); };
    playPauseButton_.onClick = [this] { togglePlayback(); };
    stopButton_.onClick = [this] { stopPreview(); };
    nextButton_.onClick = [this] { playNextTrack(); };
    fastForwardButton_.onClick = [this] { fastForward(); };
    wavButton_.onClick = [this]
    {
        wavButton_.setToggleState(true, juce::dontSendNotification);
        updateFormatControls();
    };
    flacButton_.onClick = [this]
    {
        flacButton_.setToggleState(true, juce::dontSendNotification);
        updateFormatControls();
    };
    mp3Button_.onClick = [this]
    {
        mp3Button_.setToggleState(true, juce::dontSendNotification);
        updateFormatControls();
    };
    surroundModeBox_.onChange = [this]
    {
        if (surroundModeBox_.getSelectedId() == static_cast<int>(SurroundOutputMode::surround51)
            && mp3Button_.getToggleState())
            wavButton_.setToggleState(true, juce::dontSendNotification);
        updateFormatControls();
    };
    openYoutubeButton_.onClick = [this] { openYoutubeLink(); };
    chooseFolderButton_.onClick = [this] { chooseOutputFolder(); };
    renderAllButton_.onClick = [this] { renderAll(); };
    openFolderButton_.onClick = [this] { openOutputFolder(); };

    addAndMakeVisible(addFilesButton_);
    addAndMakeVisible(clearFilesButton_);
    addAndMakeVisible(fileCountLabel_);
    addAndMakeVisible(filesList_);
    addAndMakeVisible(previousButton_);
    addAndMakeVisible(playPauseButton_);
    addAndMakeVisible(stopButton_);
    addAndMakeVisible(nextButton_);
    addAndMakeVisible(fastForwardButton_);
    addAndMakeVisible(effectsToggle_);
    addAndMakeVisible(timeline_);
    addAndMakeVisible(trackTimeLabel_);
    addAndMakeVisible(formatsLabel_);
    addAndMakeVisible(youtubeUrlEditor_);
    addAndMakeVisible(openYoutubeButton_);
    addAndMakeVisible(wavButton_);
    addAndMakeVisible(flacButton_);
    addAndMakeVisible(mp3Button_);
    addAndMakeVisible(surroundModeBox_);
    addAndMakeVisible(sampleRateBox_);
    addAndMakeVisible(bitDepthBox_);
    addAndMakeVisible(bitRateBox_);
    addAndMakeVisible(chooseFolderButton_);
    addAndMakeVisible(outputFolderLabel_);
    addAndMakeVisible(normalizeToggle_);
    addAndMakeVisible(renderAllButton_);
    addAndMakeVisible(progressBar_);
    addAndMakeVisible(statusLabel_);
    addAndMakeVisible(openFolderButton_);

    updateFormatControls();
    statusLabel_.setText(getWorkerStatus(), juce::dontSendNotification);
    setOpaque(true);
    startTimerHz(15);
}

RenderExportPage::~RenderExportPage()
{
    stopTimer();
    if (renderWorker_ != nullptr)
    {
        renderWorker_->signalThreadShouldExit();
        renderWorker_->stopThread(10000);
        renderWorker_.reset();
    }
    audioCallback_.setPreviewActive(false);
    audioCallback_.setPreviewSource(nullptr);
    transportSource_.stop();
    transportSource_.setSource(nullptr);
    previewReaderSource_.reset();
    readAheadThread_.stopThread(2000);
}

int RenderExportPage::getNumRows()
{
    return static_cast<int>(files_.size());
}

void RenderExportPage::paintListBoxItem(int rowNumber, juce::Graphics& g,
                                        int width, int height, bool rowIsSelected)
{
    if (rowNumber < 0 || rowNumber >= static_cast<int>(files_.size()))
        return;

    if (rowIsSelected || rowNumber == currentTrackIndex_)
    {
        g.setColour(NeonLookAndFeel::cyan().withAlpha(0.14f));
        g.fillRoundedRectangle(2.0f, 2.0f, static_cast<float>(width) - 4.0f,
                               static_cast<float>(height) - 4.0f, 7.0f);
    }

    g.setColour(rowNumber == currentTrackIndex_ ? NeonLookAndFeel::cyan()
                                                 : NeonLookAndFeel::textPrimary());
    g.setFont(juce::Font(juce::FontOptions(11.5f, juce::Font::bold)));
    g.drawFittedText(files_[static_cast<std::size_t>(rowNumber)].getFileName(),
                     12, 0, width - 24, height, juce::Justification::centredLeft, 1);
}

void RenderExportPage::selectedRowsChanged(int lastRowSelected)
{
    juce::ignoreUnused(lastRowSelected);
    repaint();
}

void RenderExportPage::listBoxItemDoubleClicked(int row, const juce::MouseEvent&)
{
    playTrack(row, true);
}

void RenderExportPage::paint(juce::Graphics& g)
{
    g.fillAll(NeonLookAndFeel::background());
    auto area = getLocalBounds().reduced(12, 10);
    const auto leftWidth = static_cast<int>(static_cast<float>(area.getWidth()) * 0.44f);
    auto leftCard = area.removeFromLeft(leftWidth).toFloat();
    const auto gap = 12.0f;
    leftCard = leftCard.reduced(0.0f, 0.0f);
    auto rightCard = area.toFloat();
    rightCard.setX(rightCard.getX() + gap);
    rightCard.setWidth(juce::jmax(0.0f, rightCard.getWidth() - gap));

    const std::array<juce::Rectangle<float>, 2> cards { leftCard, rightCard };
    for (const auto& card : cards)
    {
        g.setColour(NeonLookAndFeel::panel());
        g.fillRoundedRectangle(card, 16.0f);
        g.setColour(NeonLookAndFeel::cyan().withAlpha(0.13f));
        g.drawRoundedRectangle(card.reduced(0.5f), 16.0f, 1.0f);
    }

    g.setColour(NeonLookAndFeel::textPrimary());
    g.setFont(juce::Font(juce::FontOptions(13.0f, juce::Font::bold)));
    g.drawText("FILES", juce::roundToInt(leftCard.getX() + 16.0f),
               juce::roundToInt(leftCard.getY() + 10.0f), 170, 22,
               juce::Justification::centredLeft);
    g.drawText("RENDER & EXPORT", juce::roundToInt(rightCard.getX() + 16.0f),
               juce::roundToInt(rightCard.getY() + 10.0f), 220, 22,
               juce::Justification::centredLeft);

    g.setColour(NeonLookAndFeel::textMuted());
    g.setFont(juce::Font(juce::FontOptions(10.0f)));
    g.drawText("offline batch", juce::roundToInt(rightCard.getRight() - 120.0f),
               juce::roundToInt(rightCard.getY() + 10.0f), 104, 20,
               juce::Justification::centredRight);
}

void RenderExportPage::resized()
{
    auto area = getLocalBounds().reduced(12, 10);
    const auto gap = 12;
    const auto leftWidth = static_cast<int>(static_cast<float>(area.getWidth()) * 0.44f);
    auto left = area.removeFromLeft(leftWidth).reduced(14, 12);
    area.removeFromLeft(gap);
    auto right = area.reduced(14, 12);

    auto leftHeader = left.removeFromTop(30);
    addFilesButton_.setBounds(left.getX(), leftHeader.getY() + 34, 160, 34);
    clearFilesButton_.setBounds(left.getX() + 170, leftHeader.getY() + 34, 82, 34);
    fileCountLabel_.setBounds(left.getRight() - 126, leftHeader.getY() + 35, 126, 32);

    constexpr int playbackHeight = 175;
    filesList_.setBounds(left.getX(), left.getY() + 76,
                         left.getWidth(), juce::jmax(100, left.getHeight() - 76 - playbackHeight));

    auto playback = left.withY(left.getBottom() - playbackHeight).withHeight(playbackHeight);
    auto row = playback.removeFromTop(38);
    constexpr int buttonGap = 5;
    const std::array<int, 5> preferredWidths { 44, 68, 52, 64, 70 };
    auto buttonArea = row;
    const auto effectsWidth = juce::jmin(66, juce::jmax(50, row.getWidth() / 7));
    buttonArea.removeFromRight(effectsWidth + 8);
    const auto preferredTotal = std::accumulate(preferredWidths.begin(), preferredWidths.end(), 0);
    const auto availableButtons = juce::jmax(0, buttonArea.getWidth() - buttonGap * 4);
    const auto buttonScale = preferredTotal > 0
                           ? juce::jmin(1.0, static_cast<double>(availableButtons) / preferredTotal)
                           : 1.0;
    int x = buttonArea.getX();
    const std::array<juce::TextButton*, 5> transportButtons
    {{ &previousButton_, &playPauseButton_, &stopButton_, &nextButton_, &fastForwardButton_ }};
    for (std::size_t i = 0; i < transportButtons.size(); ++i)
    {
        const auto width = juce::jmax(28, juce::roundToInt(preferredWidths[i] * buttonScale));
        transportButtons[i]->setBounds(x, row.getY(), width, 34);
        x += width + buttonGap;
    }
    effectsToggle_.setBounds(x, row.getY() + 4, effectsWidth, 30);

    playback.removeFromTop(8);
    timeline_.setBounds(playback.removeFromTop(26));
    playback.removeFromTop(3);
    trackTimeLabel_.setBounds(playback.removeFromTop(20));
    formatsLabel_.setBounds(playback.getX(), left.getBottom() - 54, playback.getWidth(), 20);

    auto youtubeRow = right.withY(right.getY() + 34).withHeight(36);
    const auto youtubeButtonWidth = juce::jmin(144, juce::jmax(112, youtubeRow.getWidth() / 4));
    openYoutubeButton_.setBounds(youtubeRow.removeFromRight(youtubeButtonWidth));
    youtubeRow.removeFromRight(8);
    youtubeUrlEditor_.setBounds(youtubeRow);

    auto formatRow = right.withY(right.getY() + 80).withHeight(40);
    const auto formatGap = 8;
    const auto formatWidth = (formatRow.getWidth() - formatGap * 2) / 3;
    wavButton_.setBounds(formatRow.getX(), formatRow.getY(), formatWidth, formatRow.getHeight());
    flacButton_.setBounds(formatRow.getX() + formatWidth + formatGap, formatRow.getY(),
                          formatWidth, formatRow.getHeight());
    mp3Button_.setBounds(formatRow.getX() + (formatWidth + formatGap) * 2,
                         formatRow.getY(), formatWidth, formatRow.getHeight());

    auto controls = right.withY(right.getY() + 132);
    surroundModeBox_.setBounds(controls.removeFromTop(32));
    controls.removeFromTop(8);
    sampleRateBox_.setBounds(controls.removeFromTop(34));
    controls.removeFromTop(10);
    auto qualityRow = controls.removeFromTop(34);
    bitDepthBox_.setBounds(qualityRow);
    bitRateBox_.setBounds(qualityRow);
    controls.removeFromTop(14);
    auto folderRow = controls.removeFromTop(36);
    chooseFolderButton_.setBounds(folderRow.removeFromLeft(148));
    folderRow.removeFromLeft(14);
    outputFolderLabel_.setBounds(folderRow);
    controls.removeFromTop(14);
    normalizeToggle_.setBounds(controls.removeFromTop(34));
    controls.removeFromTop(10);
    renderAllButton_.setBounds(controls.removeFromTop(48));
    controls.removeFromTop(12);
    progressBar_.setBounds(controls.removeFromTop(24));
    controls.removeFromTop(8);
    statusLabel_.setBounds(controls.removeFromTop(44));

    openFolderButton_.setBounds(right.getX(), right.getBottom() - 40,
                                right.getWidth(), 36);
}

void RenderExportPage::addFiles()
{
    activeChooser_ = std::make_unique<juce::FileChooser>(
        "Add audio files", outputFolder_, "*.wav;*.mp3;*.flac;*.ogg;*.aiff;*.aif", true, false, this);
    const juce::Component::SafePointer<RenderExportPage> safeThis(this);
    activeChooser_->launchAsync(juce::FileBrowserComponent::openMode
                                    | juce::FileBrowserComponent::canSelectFiles
                                    | juce::FileBrowserComponent::canSelectMultipleItems,
        [safeThis](const juce::FileChooser& chooser)
        {
            if (safeThis == nullptr)
                return;
            const auto results = chooser.getResults();
            for (const auto& file : results)
            {
                if (file.existsAsFile()
                    && std::find(safeThis->files_.begin(), safeThis->files_.end(), file) == safeThis->files_.end())
                    safeThis->files_.push_back(file);
            }
            safeThis->filesList_.updateContent();
            safeThis->fileCountLabel_.setText(juce::String(safeThis->files_.size()) + " loaded",
                                               juce::dontSendNotification);
            if (safeThis->currentTrackIndex_ < 0 && !safeThis->files_.empty())
                safeThis->filesList_.selectRow(0);
            safeThis->repaint();
        });
}

void RenderExportPage::chooseOutputFolder()
{
    activeChooser_ = std::make_unique<juce::FileChooser>(
        "Choose render output folder", outputFolder_, juce::String(), true, true, this);
    const juce::Component::SafePointer<RenderExportPage> safeThis(this);
    activeChooser_->launchAsync(juce::FileBrowserComponent::openMode
                                    | juce::FileBrowserComponent::canSelectDirectories,
        [safeThis](const juce::FileChooser& chooser)
        {
            if (safeThis == nullptr)
                return;
            const auto folder = chooser.getResult();
            if (folder.isDirectory())
            {
                safeThis->outputFolder_ = folder;
                safeThis->outputFolderLabel_.setText(folder.getFullPathName(), juce::dontSendNotification);
                safeThis->outputFolderLabel_.setTooltip(folder.getFullPathName());
            }
        });
}

void RenderExportPage::clearFiles()
{
    stopPreview();
    transportSource_.setSource(nullptr);
    previewReaderSource_.reset();
    currentTrackIndex_ = -1;
    files_.clear();
    filesList_.deselectAllRows();
    filesList_.updateContent();
    fileCountLabel_.setText("0 loaded", juce::dontSendNotification);
    trackTimeLabel_.setText("--:-- / --:--", juce::dontSendNotification);
    setWorkerStatus("Add files, choose settings, press RENDER ALL.");
    repaint();
}

bool RenderExportPage::loadPreviewTrack(int index)
{
    if (index < 0 || index >= static_cast<int>(files_.size()))
        return false;

    audioCallback_.setPreviewActive(false);
    previewIsActive_ = false;
    transportSource_.stop();
    transportSource_.setSource(nullptr);
    previewReaderSource_.reset();
    currentTrackIndex_ = index;
    filesList_.selectRow(index, false, true);

    std::unique_ptr<juce::AudioFormatReader> reader(previewFormats_.createReaderFor(files_[static_cast<std::size_t>(index)]));
    if (reader == nullptr)
    {
        timeline_.setRange(0.0, 1.0, 0.01);
        timeline_.setValue(0.0, juce::dontSendNotification);
        trackTimeLabel_.setText("--:-- / --:--", juce::dontSendNotification);
        filesList_.repaintRow(index);
        setWorkerStatus("Could not decode " + files_[static_cast<std::size_t>(index)].getFileName());
        return false;
    }

    const auto sourceRate = reader->sampleRate;
    previewReaderSource_ = std::make_unique<juce::AudioFormatReaderSource>(reader.release(), true);
    previewReaderSource_->setLooping(false);
    transportSource_.setSource(previewReaderSource_.get(), 65536, &readAheadThread_, sourceRate, 2);
    transportSource_.setPosition(0.0);
    timeline_.setRange(0.0, juce::jmax(0.01, transportSource_.getLengthInSeconds()), 0.01);
    timeline_.setValue(0.0, juce::dontSendNotification);
    trackTimeLabel_.setText("00:00 / " + formatTime(transportSource_.getLengthInSeconds()),
                             juce::dontSendNotification);
    filesList_.repaintRow(index);
    return true;
}

void RenderExportPage::playTrack(int index, bool restartFromBeginning)
{
    if (index < 0 || index >= static_cast<int>(files_.size()))
        return;

    if (index != currentTrackIndex_ || previewReaderSource_ == nullptr)
    {
        if (!loadPreviewTrack(index))
            return;
    }
    else if (restartFromBeginning)
    {
        transportSource_.setPosition(0.0);
    }

    audioCallback_.setPreviewEffectsEnabled(effectsToggle_.getToggleState());
    audioCallback_.setPreviewActive(true);
    previewIsActive_ = true;
    transportSource_.start();
    playPauseButton_.setButtonText("PAUSE");
    filesList_.repaintRow(index);
    setWorkerStatus("Previewing " + files_[static_cast<std::size_t>(index)].getFileName());
}

void RenderExportPage::togglePlayback()
{
    if (transportSource_.isPlaying())
    {
        pausePreview();
        return;
    }

    auto index = filesList_.getSelectedRow();
    if (index < 0)
        index = currentTrackIndex_ >= 0 ? currentTrackIndex_ : 0;
    playTrack(index, false);
}

void RenderExportPage::pausePreview()
{
    transportSource_.stop();
    previewIsActive_ = false;
    audioCallback_.setPreviewActive(false);
    playPauseButton_.setButtonText("PLAY");
    if (currentTrackIndex_ >= 0)
        setWorkerStatus("Paused " + files_[static_cast<std::size_t>(currentTrackIndex_)].getFileName());
}

void RenderExportPage::stopPreview()
{
    pausePreview();
    transportSource_.setPosition(0.0);
    timeline_.setValue(0.0, juce::dontSendNotification);
    playPauseButton_.setButtonText("PLAY");
}

void RenderExportPage::playNextTrack()
{
    const auto base = currentTrackIndex_ >= 0 ? currentTrackIndex_ : filesList_.getSelectedRow();
    const auto next = base < 0 ? 0 : base + 1;
    if (next >= 0 && next < static_cast<int>(files_.size()))
        playTrack(next, true);
    else
        stopPreview();
}

void RenderExportPage::playPreviousTrack()
{
    if (previewReaderSource_ != nullptr && transportSource_.getCurrentPosition() > 3.0)
    {
        transportSource_.setPosition(0.0);
        return;
    }
    const auto previous = currentTrackIndex_ > 0 ? currentTrackIndex_ - 1 : 0;
    if (!files_.empty())
        playTrack(previous, true);
}

void RenderExportPage::fastForward()
{
    if (previewReaderSource_ == nullptr)
        return;
    const auto length = transportSource_.getLengthInSeconds();
    const auto position = juce::jmin(length, transportSource_.getCurrentPosition() + 10.0);
    transportSource_.setPosition(position);
    timeline_.setValue(position, juce::dontSendNotification);
}

void RenderExportPage::renderAll()
{
    if (renderWorker_ != nullptr && renderWorker_->isThreadRunning())
        return;
    if (files_.empty())
    {
        setWorkerStatus("Add at least one audio file before rendering.");
        return;
    }
    if (!outputFolder_.isDirectory())
    {
        const auto directoryResult = outputFolder_.createDirectory();
        if (directoryResult.failed())
        {
            setWorkerStatus("Cannot create the selected output folder.");
            return;
        }
    }

    BatchSettings settings;
    settings.files = files_;
    settings.outputFolder = outputFolder_;
    settings.sampleRate = sampleRateBox_.getSelectedId() > 0 ? sampleRateBox_.getSelectedId() : 48000;
    settings.bitDepth = bitDepthBox_.getSelectedId() > 0 ? bitDepthBox_.getSelectedId() : 24;
    settings.flac = flacButton_.getToggleState();
    settings.mp3 = mp3Button_.getToggleState();
    settings.bitRateKbps = bitRateBox_.getSelectedId() > 0 ? bitRateBox_.getSelectedId() : 192;
    settings.surroundMode = static_cast<SurroundOutputMode>(surroundModeBox_.getSelectedId());
    if (settings.surroundMode == SurroundOutputMode::surround51 && settings.mp3)
    {
        setWorkerStatus("5.1 output requires WAV or FLAC; MP3 remains stereo-only.");
        return;
    }
    settings.normalize = normalizeToggle_.getToggleState();
    settings.effectsEnabled = effectsToggle_.getToggleState();
    for (int id = 0; id < NEON_PARAMETER_COUNT; ++id)
        settings.parameters[static_cast<std::size_t>(id)] = liveEngine_.getParameter(id);

    workerProgress_.store(0.0, std::memory_order_relaxed);
    setWorkerStatus("Starting offline batch render...");
    setRenderControlsEnabled(false);
    renderWorker_ = std::make_unique<RenderWorker>(*this, std::move(settings));
    if (!renderWorker_->startThread())
    {
        renderWorker_.reset();
        setRenderControlsEnabled(true);
        setWorkerStatus("Could not start the render worker thread.");
    }
}

void RenderExportPage::openOutputFolder()
{
    if (outputFolder_.isDirectory())
        outputFolder_.startAsProcess();
    else
        setWorkerStatus("The output folder does not exist yet.");
}

void RenderExportPage::openYoutubeLink()
{
    auto linkText = youtubeUrlEditor_.getText().trim();
    if (linkText.isEmpty())
    {
        setWorkerStatus("Paste a YouTube link first. It will open in your browser only.");
        return;
    }

    if (linkText.startsWithIgnoreCase("http://"))
        linkText = "https://" + linkText.substring(7);
    else if (!linkText.startsWithIgnoreCase("https://"))
        linkText = "https://" + linkText;

    const juce::URL url(linkText);
    const auto host = url.getDomain().toLowerCase();
    const std::array<juce::String, 5> allowedHosts
    {{ "youtube.com", "www.youtube.com", "m.youtube.com", "music.youtube.com", "youtu.be" }};
    if (!url.isWellFormed() || std::find(allowedHosts.begin(), allowedHosts.end(), host) == allowedHosts.end())
    {
        setWorkerStatus("Enter a valid YouTube URL. Other websites are not opened here.");
        return;
    }

    if (!url.launchInDefaultBrowser())
    {
        setWorkerStatus("Could not open the YouTube link in your default browser.");
        return;
    }

    setWorkerStatus("Opened YouTube in your browser. Audio is not downloaded or added to the render queue.");
}

void RenderExportPage::updateFormatControls()
{
    const auto surround51 = surroundModeBox_.getSelectedId()
                          == static_cast<int>(SurroundOutputMode::surround51);
    const auto isMp3 = mp3Button_.getToggleState();
    const auto controlsEnabled = renderAllButton_.isEnabled();
    mp3Button_.setEnabled(controlsEnabled && !surround51);

    sampleRateBox_.setTooltip(isMp3 ? "The Windows MP3 encoder supports 44.1 or 48 kHz."
                                   : "Choose the output sample rate.");
    sampleRateBox_.setItemEnabled(96000, !isMp3);
    if (isMp3 && sampleRateBox_.getSelectedId() == 96000)
        sampleRateBox_.setSelectedId(48000, juce::dontSendNotification);

    bitDepthBox_.setVisible(!isMp3);
    bitRateBox_.setVisible(isMp3);
    sampleRateBox_.setEnabled(controlsEnabled);
    surroundModeBox_.setEnabled(controlsEnabled);
    bitDepthBox_.setEnabled(controlsEnabled && !isMp3);
    bitRateBox_.setEnabled(controlsEnabled && isMp3);
    mp3Button_.setTooltip(surround51
        ? "MP3 is stereo-only. Select Stereo or Virtual Surround for MP3 export."
        : "Encode local audio with the Windows Media Foundation MP3 encoder. YouTube links are never downloaded.");
}

void RenderExportPage::timerCallback()
{
    if (previewIsActive_ && transportSource_.hasStreamFinished())
    {
        auto next = currentTrackIndex_ + 1;
        bool startedNextTrack = false;
        while (next < static_cast<int>(files_.size()))
        {
            playTrack(next, true);
            if (previewIsActive_)
            {
                startedNextTrack = true;
                break;
            }
            next = currentTrackIndex_ + 1;
        }
        if (!startedNextTrack)
        {
            stopPreview();
            setWorkerStatus("Finished playing the file queue.");
        }
    }

    if (!isSeeking_ && previewReaderSource_ != nullptr)
    {
        const auto position = transportSource_.getCurrentPosition();
        timeline_.setValue(position, juce::dontSendNotification);
        trackTimeLabel_.setText(formatTime(position) + " / "
                                 + formatTime(transportSource_.getLengthInSeconds()),
                                 juce::dontSendNotification);
    }
    if (!transportSource_.isPlaying())
        playPauseButton_.setButtonText("PLAY");

    if (renderWorker_ != nullptr)
    {
        progressValue_ = workerProgress_.load(std::memory_order_relaxed);
        const auto message = getWorkerStatus();
        if (message.isNotEmpty())
            statusLabel_.setText(message, juce::dontSendNotification);

        if (!renderWorker_->isThreadRunning())
        {
            renderWorker_->waitForThreadToExit(1000);
            const auto completionMessage = getWorkerStatus();
            renderWorker_.reset();
            setRenderControlsEnabled(true);
            progressValue_ = workerProgress_.load(std::memory_order_relaxed);
            statusLabel_.setText(completionMessage, juce::dontSendNotification);

            if (completionMessage.startsWith("Render complete:"))
            {
                const auto hadFailures = completionMessage.containsIgnoreCase(" failed");
                const auto popupMessage = completionMessage + "\n\nOutput folder:\n"
                                        + outputFolder_.getFullPathName();
                juce::AlertWindow::showMessageBoxAsync(
                    hadFailures ? juce::MessageBoxIconType::WarningIcon
                                : juce::MessageBoxIconType::InfoIcon,
                    hadFailures ? "Rendering finished with errors" : "Rendering completed",
                    popupMessage, "OK", this);
            }
        }
    }
    else
    {
        const auto message = getWorkerStatus();
        if (message.isNotEmpty())
            statusLabel_.setText(message, juce::dontSendNotification);
    }
    progressBar_.repaint();
}

void RenderExportPage::setWorkerProgress(double progress) noexcept
{
    workerProgress_.store(juce::jlimit(0.0, 1.0, progress), std::memory_order_relaxed);
}

void RenderExportPage::setWorkerStatus(const juce::String& status)
{
    const juce::ScopedLock lock(workerStatusLock_);
    workerStatus_ = status;
}

juce::String RenderExportPage::getWorkerStatus() const
{
    const juce::ScopedLock lock(workerStatusLock_);
    return workerStatus_;
}

void RenderExportPage::setRenderControlsEnabled(bool enabled)
{
    renderAllButton_.setEnabled(enabled);
    addFilesButton_.setEnabled(enabled);
    clearFilesButton_.setEnabled(enabled);
    chooseFolderButton_.setEnabled(enabled);
    wavButton_.setEnabled(enabled);
    flacButton_.setEnabled(enabled);
    mp3Button_.setEnabled(enabled);
    sampleRateBox_.setEnabled(enabled);
    surroundModeBox_.setEnabled(enabled);
    const auto isMp3 = mp3Button_.getToggleState();
    bitDepthBox_.setEnabled(enabled && !isMp3);
    bitRateBox_.setEnabled(enabled && isMp3);
    normalizeToggle_.setEnabled(enabled);
    effectsToggle_.setEnabled(enabled);
    updateFormatControls();
}

juce::String RenderExportPage::formatTime(double seconds) const
{
    if (!std::isfinite(seconds) || seconds < 0.0)
        return "--:--";
    const auto totalSeconds = juce::jmax(0, juce::roundToInt(seconds));
    const auto minutes = totalSeconds / 60;
    const auto remainder = totalSeconds % 60;
    return juce::String(minutes).paddedLeft('0', 2) + ":"
         + juce::String(remainder).paddedLeft('0', 2);
}

void RenderExportPage::renderBatch(RenderWorker& worker, const BatchSettings& settings)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    int rendered = 0;
    int failed = 0;
    juce::StringArray errors;

    for (int i = 0; i < static_cast<int>(settings.files.size()); ++i)
    {
        if (worker.threadShouldExit())
            break;

        const auto& file = settings.files[static_cast<std::size_t>(i)];
        setWorkerStatus("Rendering " + juce::String(i + 1) + "/"
                        + juce::String(settings.files.size()) + ": " + file.getFileName());
        juce::String error;
        if (renderOneFile(worker, settings, formats, i, file, error))
            ++rendered;
        else if (!worker.threadShouldExit())
        {
            ++failed;
            errors.add(file.getFileName() + ": " + error);
        }
        setWorkerProgress(static_cast<double>(i + 1) / static_cast<double>(settings.files.size()));
    }

    if (worker.threadShouldExit())
        setWorkerStatus("Render cancelled. Completed " + juce::String(rendered) + " file(s).");
    else
    {
        auto summary = "Render complete: " + juce::String(rendered) + " exported";
        if (failed > 0)
            summary += ", " + juce::String(failed) + " failed";
        if (errors.size() > 0)
            summary += ". " + errors[0];
        setWorkerStatus(summary);
        setWorkerProgress(1.0);
    }
}

bool RenderExportPage::renderOneFile(RenderWorker& worker, const BatchSettings& settings,
                                     juce::AudioFormatManager& formats, int fileIndex,
                                     const juce::File& inputFile, juce::String& error)
{
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(inputFile));
    if (reader == nullptr || reader->sampleRate <= 0.0 || reader->lengthInSamples <= 0)
    {
        error = "unsupported, unreadable, or empty audio file";
        return false;
    }

    const auto surround51 = settings.surroundMode == SurroundOutputMode::surround51;
    const auto virtualStereo = settings.surroundMode == SurroundOutputMode::virtualStereo;
    if (surround51 && settings.mp3)
    {
        error = "5.1 surround requires WAV or FLAC; MP3 export is stereo-only";
        return false;
    }
    const auto outputChannels = surround51 ? 6 : 2;
    const auto surroundLayout = juce::AudioChannelSet::create5point1();
    const std::array<int, 6> surroundChannelIndices
    {{
        surroundLayout.getChannelIndexForType(juce::AudioChannelSet::left),
        surroundLayout.getChannelIndexForType(juce::AudioChannelSet::right),
        surroundLayout.getChannelIndexForType(juce::AudioChannelSet::centre),
        surroundLayout.getChannelIndexForType(juce::AudioChannelSet::LFE),
        surroundLayout.getChannelIndexForType(juce::AudioChannelSet::leftSurround),
        surroundLayout.getChannelIndexForType(juce::AudioChannelSet::rightSurround)
    }};
    if (surround51 && std::find(surroundChannelIndices.begin(), surroundChannelIndices.end(), -1)
                          != surroundChannelIndices.end())
    {
        error = "JUCE could not resolve the 5.1 speaker channel layout";
        return false;
    }

    const auto sourceRate = reader->sampleRate;
    const auto sourceChannels = reader->numChannels;
    const auto totalOutputFrames = static_cast<std::int64_t>(std::llround(
        static_cast<double>(reader->lengthInSamples) * static_cast<double>(settings.sampleRate) / sourceRate));
    if (totalOutputFrames <= 0)
    {
        error = "audio duration is too short to render";
        return false;
    }

    if (settings.effectsEnabled)
    {
        if (!offlineEngine_.isLoaded())
        {
            error = "Neon effects DLL is unavailable; disable FX for a dry export";
            return false;
        }
        offlineEngine_.release();
        for (int id = 0; id < NEON_PARAMETER_COUNT; ++id)
            offlineEngine_.setParameter(id, settings.parameters[static_cast<std::size_t>(id)]);
        if (!offlineEngine_.prepare(static_cast<double>(settings.sampleRate), renderBlockSize))
        {
            error = "could not prepare the offline effects engine";
            return false;
        }
    }

    juce::AudioFormatReaderSource readerSource(reader.release(), true);
    readerSource.setLooping(false);
    juce::ResamplingAudioSource resampler(&readerSource, false, 2);
    resampler.setResamplingRatio(sourceRate / static_cast<double>(settings.sampleRate));
    resampler.prepareToPlay(renderBlockSize, static_cast<double>(settings.sampleRate));

    VirtualSurroundStereoProcessor virtualSurroundProcessor;
    StereoTo5point1Upmixer surroundUpmixer;
    if (virtualStereo)
        virtualSurroundProcessor.prepare(static_cast<double>(settings.sampleRate));
    if (surround51)
        surroundUpmixer.prepare(static_cast<double>(settings.sampleRate));

    const auto tempFile = settings.outputFolder.getNonexistentChildFile(".NeonRenderTemp", ".f32", false);
    auto tempOutput = tempFile.createOutputStream();
    if (tempOutput == nullptr)
    {
        resampler.releaseResources();
        error = "could not create a temporary render file";
        return false;
    }

    juce::AudioBuffer<float> inputBuffer(2, renderBlockSize);
    juce::AudioBuffer<float> processedBuffer(2, renderBlockSize);
    std::vector<float> interleaved(static_cast<std::size_t>(renderBlockSize)
                                   * static_cast<std::size_t>(outputChannels));
    const auto meteringLayout = surround51 ? surroundLayout : juce::AudioChannelSet::stereo();
    IntegratedLoudnessMeter loudnessMeter(static_cast<double>(settings.sampleRate), meteringLayout);
    juce::AudioBuffer<float> loudnessBuffer(outputChannels, renderBlockSize);
    auto peak = 0.0f;

    std::int64_t position = 0;
    while (position < totalOutputFrames)
    {
        if (worker.threadShouldExit())
        {
            tempOutput.reset();
            tempFile.deleteFile();
            resampler.releaseResources();
            error = "cancelled";
            return false;
        }

        const auto frames = static_cast<int>(juce::jmin<std::int64_t>(renderBlockSize,
                                                                      totalOutputFrames - position));
        inputBuffer.clear(0, frames);
        juce::AudioSourceChannelInfo inputInfo(&inputBuffer, 0, frames);
        resampler.getNextAudioBlock(inputInfo);
        if (sourceChannels == 1)
            inputBuffer.copyFrom(1, 0, inputBuffer, 0, 0, frames);

        processedBuffer.clear(0, frames);
        if (settings.effectsEnabled)
        {
            const float* inputPointers[2] { inputBuffer.getReadPointer(0), inputBuffer.getReadPointer(1) };
            float* outputPointers[2] { processedBuffer.getWritePointer(0), processedBuffer.getWritePointer(1) };
            offlineEngine_.process(inputPointers, 2, outputPointers, 2, frames);
        }
        else
        {
            processedBuffer.copyFrom(0, 0, inputBuffer, 0, 0, frames);
            processedBuffer.copyFrom(1, 0, inputBuffer, 1, 0, frames);
        }

        for (int sample = 0; sample < frames; ++sample)
        {
            auto left = processedBuffer.getSample(0, sample);
            auto right = processedBuffer.getSample(1, sample);
            if (!std::isfinite(left))
                left = 0.0f;
            if (!std::isfinite(right))
                right = 0.0f;

            if (virtualStereo)
                virtualSurroundProcessor.processSample(left, right, left, right);

            processedBuffer.setSample(0, sample, left);
            processedBuffer.setSample(1, sample, right);
            const auto index = static_cast<std::size_t>(sample) * static_cast<std::size_t>(outputChannels);
            if (surround51)
            {
                const auto frame = surroundUpmixer.processSample(left, right);
                const std::array<float, 6> channelSamples
                {{ frame.frontLeft, frame.frontRight, frame.centre, frame.lfe,
                   frame.surroundLeft, frame.surroundRight }};
                for (std::size_t channel = 0; channel < channelSamples.size(); ++channel)
                {
                    const auto channelIndex = static_cast<std::size_t>(surroundChannelIndices[channel]);
                    const auto value = channelSamples[channel];
                    interleaved[index + channelIndex] = value;
                    loudnessBuffer.setSample(static_cast<int>(channelIndex), sample, value);
                    peak = juce::jmax(peak, std::abs(value));
                }
            }
            else
            {
                interleaved[index] = left;
                interleaved[index + 1u] = right;
                loudnessBuffer.setSample(0, sample, left);
                loudnessBuffer.setSample(1, sample, right);
                peak = juce::jmax(peak, juce::jmax(std::abs(left), std::abs(right)));
            }
        }
        if (settings.normalize)
            loudnessMeter.process(loudnessBuffer, frames);
        const auto bytes = static_cast<std::size_t>(frames)
                         * static_cast<std::size_t>(outputChannels) * sizeof(float);
        if (!tempOutput->write(interleaved.data(), bytes))
        {
            tempOutput.reset();
            tempFile.deleteFile();
            resampler.releaseResources();
            error = "failed while writing temporary audio data";
            return false;
        }

        position += frames;
        const auto fraction = static_cast<double>(position) / static_cast<double>(totalOutputFrames);
        const auto fileProgress = 0.5 * fraction;
        setWorkerProgress((static_cast<double>(fileIndex) + fileProgress)
                          / static_cast<double>(settings.files.size()));
    }

    tempOutput->flush();
    tempOutput.reset();
    resampler.releaseResources();

    auto gain = 1.0f;
    auto measuredLufs = -70.0;
    if (settings.normalize)
    {
        measuredLufs = loudnessMeter.getIntegratedLufs();
        if (measuredLufs > -70.0)
        {
            const auto targetGain = static_cast<float>(std::pow(10.0, (-14.0 - measuredLufs) / 20.0));
            const auto peakCeiling = juce::Decibels::decibelsToGain(-1.0f);
            const auto peakLimitedGain = peak > 1.0e-9f ? peakCeiling / peak : targetGain;
            gain = juce::jmin(targetGain, peakLimitedGain, juce::Decibels::decibelsToGain(24.0f));
        }
    }

    const auto extension = settings.mp3 ? ".mp3" : (settings.flac ? ".flac" : ".wav");
    const auto renderSuffix = surround51 ? "_Neon_51" : (virtualStereo ? "_Neon_Virtual" : "_Neon");
    const auto outputBaseName = inputFile.getFileNameWithoutExtension() + renderSuffix;
    const auto outputFile = settings.outputFolder.getNonexistentChildFile(outputBaseName, extension, false);

    std::unique_ptr<juce::AudioFormatWriter> writer;
    std::unique_ptr<juce::OutputStream> outputStream;
    std::unique_ptr<MediaFoundationMp3Writer> mp3Writer;
    if (settings.mp3)
    {
#if defined(_WIN32)
        mp3Writer = std::make_unique<MediaFoundationMp3Writer>();
        if (!mp3Writer->open(outputFile, settings.sampleRate, settings.bitRateKbps, error))
        {
            mp3Writer.reset();
            outputFile.deleteFile();
            tempFile.deleteFile();
            return false;
        }
#else
        error = "MP3 export is only available on Windows through Media Foundation.";
        tempFile.deleteFile();
        return false;
#endif
    }
    else
    {
        outputStream = outputFile.createOutputStream();
        if (outputStream == nullptr)
        {
            tempFile.deleteFile();
            error = "could not create the output file";
            return false;
        }

        juce::WavAudioFormat wavFormat;
        juce::FlacAudioFormat flacFormat;
        juce::AudioFormat* outputFormat = settings.flac ? static_cast<juce::AudioFormat*>(&flacFormat)
                                                        : static_cast<juce::AudioFormat*>(&wavFormat);
        const auto outputLayout = surround51 ? surroundLayout : juce::AudioChannelSet::stereo();
        const auto writerOptions = juce::AudioFormatWriterOptions()
                                       .withSampleRate(static_cast<double>(settings.sampleRate))
                                       .withChannelLayout(outputLayout)
                                       .withBitsPerSample(settings.bitDepth)
                                       .withQualityOptionIndex(0);
        writer = outputFormat->createWriterFor(outputStream, writerOptions);
        if (writer == nullptr)
        {
            outputStream.reset();
            outputFile.deleteFile();
            tempFile.deleteFile();
            error = surround51
                  ? "selected WAV/FLAC writer rejected the 5.1 channel layout, sample rate, or bit depth"
                  : "selected output format rejected the sample rate or bit depth";
            return false;
        }
    }

    auto tempInput = std::make_unique<juce::FileInputStream>(tempFile);
    if (tempInput->failedToOpen())
    {
        tempInput.reset();
        mp3Writer.reset();
        writer.reset();
        outputFile.deleteFile();
        tempFile.deleteFile();
        error = "could not reopen the temporary audio data";
        return false;
    }

    juce::AudioBuffer<float> finalBuffer(outputChannels, renderBlockSize);
    std::vector<std::int16_t> mp3Interleaved;
    if (settings.mp3)
        mp3Interleaved.resize(static_cast<std::size_t>(renderBlockSize) * 2u);
    position = 0;
    while (position < totalOutputFrames)
    {
        if (worker.threadShouldExit())
        {
            tempInput.reset();
            mp3Writer.reset();
            writer.reset();
            outputFile.deleteFile();
            tempFile.deleteFile();
            error = "cancelled";
            return false;
        }

        const auto frames = static_cast<int>(juce::jmin<std::int64_t>(renderBlockSize,
                                                                      totalOutputFrames - position));
        const auto bytes = static_cast<int>(static_cast<std::size_t>(frames)
                                         * static_cast<std::size_t>(outputChannels) * sizeof(float));
        const auto bytesRead = tempInput->read(interleaved.data(), bytes);
        if (bytesRead != bytes)
        {
            tempInput.reset();
            mp3Writer.reset();
            writer.reset();
            outputFile.deleteFile();
            tempFile.deleteFile();
            error = "temporary audio data was incomplete";
            return false;
        }

        for (int sample = 0; sample < frames; ++sample)
        {
            const auto index = static_cast<std::size_t>(sample)
                             * static_cast<std::size_t>(outputChannels);
            if (settings.mp3)
            {
                const auto left = juce::jlimit(-1.0f, 1.0f, interleaved[index] * gain);
                const auto right = juce::jlimit(-1.0f, 1.0f, interleaved[index + 1u] * gain);
                mp3Interleaved[index] = static_cast<std::int16_t>(std::lround(left * 32767.0f));
                mp3Interleaved[index + 1u] = static_cast<std::int16_t>(std::lround(right * 32767.0f));
            }
            else
            {
                for (int channel = 0; channel < outputChannels; ++channel)
                    finalBuffer.setSample(channel, sample,
                        interleaved[index + static_cast<std::size_t>(channel)] * gain);
            }
        }

        const auto writeSucceeded = mp3Writer != nullptr
            ? mp3Writer->write(mp3Interleaved.data(), frames, error)
            : writer != nullptr && writer->writeFromAudioSampleBuffer(finalBuffer, 0, frames);
        if (!writeSucceeded)
        {
            tempInput.reset();
            mp3Writer.reset();
            writer.reset();
            outputFile.deleteFile();
            tempFile.deleteFile();
            if (error.isEmpty())
                error = "audio writer failed while encoding the output";
            return false;
        }

        position += frames;
        const auto fraction = static_cast<double>(position) / static_cast<double>(totalOutputFrames);
        const auto fileProgress = 0.5 + 0.5 * fraction;
        setWorkerProgress((static_cast<double>(fileIndex) + fileProgress)
                          / static_cast<double>(settings.files.size()));
    }

    tempInput.reset();
    if (mp3Writer != nullptr)
    {
        if (!mp3Writer->finish(error))
        {
            mp3Writer.reset();
            outputFile.deleteFile();
            tempFile.deleteFile();
            return false;
        }
        mp3Writer.reset();
    }
    writer.reset();
    tempFile.deleteFile();
    const auto modeDescription = surround51 ? " (synthetic 5.1 upmix)"
                               : (virtualStereo ? " (virtual-surround stereo)" : "");
    if (settings.normalize && measuredLufs > -70.0)
        setWorkerStatus("Rendered " + outputFile.getFileName() + modeDescription + "  /  "
                        + formatLoudness(measuredLufs) + " → -14 LUFS target");
    else
        setWorkerStatus("Rendered " + outputFile.getFileName() + modeDescription);
    return true;
}
