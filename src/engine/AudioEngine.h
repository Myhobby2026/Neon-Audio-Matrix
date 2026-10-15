#pragma once

#include "Nodes.h"

namespace neon
{
class AudioEngine final : public juce::AudioProcessor
{
public:
    AudioEngine();
    ~AudioEngine() override;

    const juce::String getName() const override { return "Neon Audio Matrix Engine"; }
    double getTailLengthSeconds() const override { return 30.0; }
    void prepareToPlay(double sampleRate, int maximumBlockSize) override;
    void releaseResources() override;
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool hasEditor() const override { return false; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return "Default"; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock& destination) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    void processExternal(const float* const* inputChannels,
                         int inputChannelCount,
                         float* const* outputChannels,
                         int outputChannelCount,
                         int frameCount) noexcept;

    bool setEngineParameter(int parameterId, float value) noexcept;
    float getEngineParameter(int parameterId) const noexcept;
    int copySpectrum(float* destination, int capacity) const noexcept;
    bool savePreset(const juce::File& file) const;
    bool loadPreset(const juce::File& file);
    int getGraphNodeCount() const noexcept { return graph_.getNumNodes(); }

private:
    enum NodeIds : uint32_t
    {
        inputNodeId = 1,
        eqNodeId,
        spectrumNodeId,
        reverbNodeId,
        surroundNodeId,
        delayNodeId,
        dimensionNodeId,
        utilityNodeId,
        masterNodeId,
        outputNodeId
    };

    void buildGraph();
    void connectStereo(juce::AudioProcessorGraph::NodeID source,
                       juce::AudioProcessorGraph::NodeID destination);
    juce::ValueTree createPresetTree() const;
    bool applyPresetTree(const juce::ValueTree& tree);

    // Declaration order matters: graph nodes refer to parameters_, so graph_ is
    // destroyed first (members are destroyed in reverse declaration order).
    ParameterBank parameters_;
    juce::AudioProcessorGraph graph_;
    juce::AudioBuffer<float> blockBuffer_;
    juce::MidiBuffer midiScratch_;
    int maximumBlockSize_ = 512;
    double sampleRate_ = 48000.0;
};
} // namespace neon
