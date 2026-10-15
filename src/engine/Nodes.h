#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <memory>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

#include <Neon/EngineApi.h>

namespace neon
{
class ParameterBank
{
public:
    ParameterBank() noexcept;

    float get(int parameterId) const noexcept;
    void set(int parameterId, float value) noexcept;
    float getSpectrum(int band) const noexcept;

    static float clampParameter(int parameterId, float value) noexcept;

private:
    std::array<std::atomic<float>, NEON_PARAMETER_COUNT> values_ {};
    std::array<std::atomic<float>, NEON_SPECTRUM_BANDS> spectrum_ {};

    friend class SpectrumAnalyzerNode;
};

class StereoNode : public juce::AudioProcessor
{
public:
    StereoNode(const juce::String& processorName, ParameterBank& parameters);

    const juce::String getName() const override { return name_; }
    double getTailLengthSeconds() const override { return 0.0; }
    void prepareToPlay(double sampleRate, int maximumBlockSize) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool hasEditor() const override { return false; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock& destination) override { destination.reset(); }
    void setStateInformation(const void*, int) override {}

protected:
    float parameter(int parameterId) const noexcept { return parameters_.get(parameterId); }
    ParameterBank& parameters_;
    double sampleRate_ = 48000.0;

private:
    juce::String name_;
};

// These wrappers are AudioProcessor subclasses through AudioGraphIOProcessor,
// and are the graph's actual external input/output pins.
class InputNode final : public juce::AudioProcessorGraph::AudioGraphIOProcessor
{
public:
    InputNode();
};

class OutputNode final : public juce::AudioProcessorGraph::AudioGraphIOProcessor
{
public:
    OutputNode();
};

class EqNode final : public StereoNode
{
public:
    explicit EqNode(ParameterBank& parameters);
    void prepareToPlay(double sampleRate, int maximumBlockSize) override;
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override;

private:
    struct Biquad
    {
        float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f;
        float a1 = 0.0f, a2 = 0.0f;
        float z1 = 0.0f, z2 = 0.0f;

        void setLowShelf(float sampleRate, float frequency, float gainDb) noexcept;
        void setPeak(float sampleRate, float frequency, float q, float gainDb) noexcept;
        void setHighShelf(float sampleRate, float frequency, float gainDb) noexcept;
        float process(float input) noexcept;

    private:
        void setNormalised(float nb0, float nb1, float nb2,
                           float na0, float na1, float na2) noexcept;
    };

    std::array<std::array<Biquad, 10>, 2> filters_ {};
};

class SpectrumAnalyzerNode final : public StereoNode
{
public:
    explicit SpectrumAnalyzerNode(ParameterBank& parameters);
    void prepareToPlay(double sampleRate, int maximumBlockSize) override;
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override;

private:
    static constexpr int fftOrder = 10;
    static constexpr int fftSize = 1 << fftOrder;

    juce::dsp::FFT fft_ { fftOrder };
    std::array<float, fftSize> fifo_ {};
    std::array<float, fftSize> window_ {};
    std::array<float, fftSize * 2> fftWork_ {};
    int fifoIndex_ = 0;

    void publishFrame() noexcept;
};

class ReverbNode final : public StereoNode
{
public:
    explicit ReverbNode(ParameterBank& parameters);
    void prepareToPlay(double sampleRate, int maximumBlockSize) override;
    void releaseResources() override;
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override;

private:
    static constexpr int bandCount = 3;
    static constexpr int combsPerChannel = 4;
    static constexpr int diffuserStages = 4;

    struct DelayLine
    {
        std::vector<float> samples;
        std::size_t writeIndex = 0;
        float dampingState = 0.0f;
    };

    struct DiffuserLine
    {
        std::vector<float> samples;
        std::size_t writeIndex = 0;
    };

    std::array<std::array<DelayLine, combsPerChannel>, bandCount * 2> combLines_ {};
    std::array<std::array<DiffuserLine, diffuserStages>, 2> diffuserLines_ {};
    std::array<std::vector<float>, 2> preDelayLines_;
    std::size_t preDelayWriteIndex_ = 0;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> mixSmoother_;
    float lastMixTarget_ = -1.0f;
    std::array<float, 2> lowCrossoverState_ {};
    std::array<float, 2> highCrossoverState_ {};
};

class FieldSurroundNode final : public StereoNode
{
public:
    explicit FieldSurroundNode(ParameterBank& parameters);
    void prepareToPlay(double sampleRate, int maximumBlockSize) override;
    void releaseResources() override;
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override;

private:
    std::vector<float> fieldDelayLine_;
    std::size_t fieldWriteIndex_ = 0;
    std::size_t validFrames_ = 0;
    std::array<float, 2> bassLowState_ {};
};

class StereoDelayNode final : public StereoNode
{
public:
    explicit StereoDelayNode(ParameterBank& parameters);
    void prepareToPlay(double sampleRate, int maximumBlockSize) override;
    void releaseResources() override;
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override;

private:
    std::array<std::vector<float>, 2> delayLines_;
    std::size_t writeIndex_ = 0;
    std::size_t validFrames_ = 0;
    std::array<float, 2> lowCrossoverState_ {};
    std::array<float, 2> highCrossoverState_ {};
    bool wasEnabled_ = true;
};

class DimensionNode final : public StereoNode
{
public:
    explicit DimensionNode(ParameterBank& parameters);
    void prepareToPlay(double sampleRate, int maximumBlockSize) override;
    void releaseResources() override;
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override;

private:
    std::array<std::vector<float>, 2> delayLines_;
    std::size_t writeIndex_ = 0;
    std::size_t validFrames_ = 0;
    float phase_ = 0.0f;
    bool wasEnabled_ = true;
};

class MasterVolumeNode final : public StereoNode
{
public:
    explicit MasterVolumeNode(ParameterBank& parameters);
    void prepareToPlay(double sampleRate, int maximumBlockSize) override;
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override;

private:
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> gain_;
    juce::dsp::Limiter<float> limiter_;
    float lastTarget_ = -1.0f;
};

class UtilityNode final : public StereoNode
{
public:
    explicit UtilityNode(ParameterBank& parameters);
    void prepareToPlay(double sampleRate, int maximumBlockSize) override;
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override;

private:
    std::array<float, 2> inputHistory_ {};
    std::array<float, 2> outputHistory_ {};
    std::array<float, 2> loudnessLowState_ {};
    std::array<float, 2> loudnessHighState_ {};
};
} // namespace neon
