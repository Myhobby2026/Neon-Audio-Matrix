#include "AudioEngine.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <utility>

namespace neon
{
namespace
{
const juce::StringArray parameterNames
{
    "eq_low_db",
    "eq_mid_db",
    "eq_high_db",
    "reverb_mix",
    "reverb_room",
    "surround_spread",
    "delay_time_ms",
    "delay_feedback",
    "delay_mix",
    "dimension_amount",
    "master_volume",
    "utility_enabled",
    "eq_62_db",
    "eq_125_db",
    "eq_250_db",
    "eq_500_db",
    "eq_2k_db",
    "eq_4k_db",
    "eq_8k_db",
    "preamp_db",
    "eq_band_q",
    "spectrum_enabled",
    "limiter_enabled",
    "field_enabled",
    "dimension_enabled",
    "reverb_enabled",
    "delay_enabled",
    "reverb_decay_seconds",
    "reverb_low_crossover_hz",
    "reverb_high_crossover_hz",
    "reverb_low_decay_multiplier",
    "reverb_high_decay_multiplier",
    "reverb_pre_delay_ms",
    "reverb_low_damping",
    "reverb_high_damping",
    "reverb_stereo_width",
    "reverb_aspect_ratio",
    "reverb_diffusion",
    "delay_low_crossover_hz",
    "delay_high_crossover_hz",
    "loudness_enabled",
    "loudness_amount",
    "field_amount",
    "surround_bass",
    "surround_center_db"
};

juce::var valueTreePropertiesToJson(const juce::ValueTree& tree)
{
    juce::var result(new juce::DynamicObject());
    auto* object = result.getDynamicObject();
    if (object == nullptr)
        return {};

    object->setProperty("type", tree.getType().toString());
    for (int i = 0; i < tree.getNumProperties(); ++i)
    {
        const auto propertyName = tree.getPropertyName(i);
        object->setProperty(propertyName, tree.getProperty(propertyName));
    }

    return result;
}

juce::ValueTree jsonPropertiesToValueTree(const juce::var& json, const juce::Identifier& type)
{
    juce::ValueTree tree(type);
    const auto* object = json.getDynamicObject();
    if (object == nullptr)
        return {};

    const auto& properties = object->getProperties();
    for (int i = 0; i < properties.size(); ++i)
    {
        const auto propertyName = properties.getName(i);
        if (propertyName.toString() == "type")
            continue;
        tree.setProperty(propertyName, properties.getValueAt(i), nullptr);
    }

    return tree;
}
} // namespace

AudioEngine::AudioEngine()
    : juce::AudioProcessor(BusesProperties()
          .withInput("Input", juce::AudioChannelSet::stereo(), true)
          .withOutput("Output", juce::AudioChannelSet::stereo(), true))
{
    buildGraph();
}

AudioEngine::~AudioEngine()
{
    releaseResources();
}

bool AudioEngine::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    return layouts.getMainInputChannelSet() == juce::AudioChannelSet::stereo()
        && layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void AudioEngine::buildGraph()
{
    using Graph = juce::AudioProcessorGraph;

    // AudioGraphIOProcessor nodes inherit their channel counts from the graph
    // when they are added. Configure the graph's stereo buses first so the
    // input/output pins have two channels when addConnection() validates them.
    graph_.setPlayConfigDetails(2, 2, sampleRate_, maximumBlockSize_);

    const auto sync = Graph::UpdateKind::sync;

    const auto add = [this, sync](uint32_t id, std::unique_ptr<juce::AudioProcessor> processor)
    {
        const auto node = graph_.addNode(std::move(processor), Graph::NodeID { id }, sync);
        jassert(node != nullptr);
    };

    add(inputNodeId,       std::make_unique<InputNode>());
    add(eqNodeId,          std::make_unique<EqNode>(parameters_));
    add(spectrumNodeId,    std::make_unique<SpectrumAnalyzerNode>(parameters_));
    add(reverbNodeId,      std::make_unique<ReverbNode>(parameters_));
    add(surroundNodeId,    std::make_unique<FieldSurroundNode>(parameters_));
    add(delayNodeId,       std::make_unique<StereoDelayNode>(parameters_));
    add(dimensionNodeId,   std::make_unique<DimensionNode>(parameters_));
    add(utilityNodeId,     std::make_unique<UtilityNode>(parameters_));
    add(masterNodeId,      std::make_unique<MasterVolumeNode>(parameters_));
    add(outputNodeId,      std::make_unique<OutputNode>());

    connectStereo(Graph::NodeID { inputNodeId },     Graph::NodeID { eqNodeId });
    connectStereo(Graph::NodeID { eqNodeId },        Graph::NodeID { spectrumNodeId });
    connectStereo(Graph::NodeID { spectrumNodeId },  Graph::NodeID { reverbNodeId });
    connectStereo(Graph::NodeID { reverbNodeId },    Graph::NodeID { surroundNodeId });
    connectStereo(Graph::NodeID { surroundNodeId },  Graph::NodeID { delayNodeId });
    connectStereo(Graph::NodeID { delayNodeId },     Graph::NodeID { dimensionNodeId });
    connectStereo(Graph::NodeID { dimensionNodeId }, Graph::NodeID { utilityNodeId });
    connectStereo(Graph::NodeID { utilityNodeId },   Graph::NodeID { masterNodeId });
    connectStereo(Graph::NodeID { masterNodeId },    Graph::NodeID { outputNodeId });

    jassert(graph_.getNumNodes() == 10);
}

void AudioEngine::connectStereo(juce::AudioProcessorGraph::NodeID source,
                                juce::AudioProcessorGraph::NodeID destination)
{
    for (int channel = 0; channel < 2; ++channel)
    {
        const juce::AudioProcessorGraph::Connection connection
        {
            { source, channel },
            { destination, channel }
        };
        const auto added = graph_.addConnection(connection, juce::AudioProcessorGraph::UpdateKind::sync);
        jassert(added);
        juce::ignoreUnused(added);
    }
}

void AudioEngine::prepareToPlay(double sampleRate, int maximumBlockSize)
{
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
    maximumBlockSize_ = juce::jmax(1, maximumBlockSize);
    blockBuffer_.setSize(2, maximumBlockSize_, false, false, true);
    graph_.setPlayConfigDetails(2, 2, sampleRate_, maximumBlockSize_);
    graph_.prepareToPlay(sampleRate_, maximumBlockSize_);
}

void AudioEngine::releaseResources()
{
    graph_.releaseResources();
    blockBuffer_.setSize(0, 0);
}

void AudioEngine::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    graph_.processBlock(buffer, midi);
}

void AudioEngine::processExternal(const float* const* inputChannels,
                                  int inputChannelCount,
                                  float* const* outputChannels,
                                  int outputChannelCount,
                                  int frameCount) noexcept
{
    if (frameCount <= 0)
        return;

    if (blockBuffer_.getNumChannels() != 2 || blockBuffer_.getNumSamples() <= 0)
        prepareToPlay(sampleRate_, maximumBlockSize_);

    const auto preampGain = std::pow(10.0f, parameters_.get(NEON_PREAMP_DB) * 0.05f);
    auto offset = 0;
    while (offset < frameCount)
    {
        const auto frames = juce::jmin(maximumBlockSize_, frameCount - offset);

        for (int channel = 0; channel < 2; ++channel)
        {
            const auto sourceChannel = inputChannelCount == 1 ? 0 : channel;
            if (inputChannels != nullptr
                && sourceChannel < inputChannelCount
                && inputChannels[sourceChannel] != nullptr)
            {
                blockBuffer_.copyFrom(channel, 0, inputChannels[sourceChannel] + offset, frames);
                if (preampGain != 1.0f)
                    blockBuffer_.applyGain(channel, 0, frames, preampGain);
            }
            else
            {
                blockBuffer_.clear(channel, 0, frames);
            }
        }

        midiScratch_.clear();

        // Process an exact-length view of the preallocated scratch buffer. The
        // graph must see `frames`, not maximumBlockSize_, especially for a
        // short final chunk or hosts that vary callback sizes.
        std::array<float*, 2> blockPointers
        {
            blockBuffer_.getWritePointer(0),
            blockBuffer_.getWritePointer(1)
        };
        juce::AudioBuffer<float> activeBlock(blockPointers.data(), 2, frames);
        processBlock(activeBlock, midiScratch_);

        for (int channel = 0; channel < juce::jmin(2, outputChannelCount); ++channel)
        {
            if (outputChannels != nullptr && outputChannels[channel] != nullptr)
                juce::FloatVectorOperations::copy(outputChannels[channel] + offset,
                                                   activeBlock.getReadPointer(channel), frames);
        }

        offset += frames;
    }
}

bool AudioEngine::setEngineParameter(int parameterId, float value) noexcept
{
    if (parameterId < 0 || parameterId >= NEON_PARAMETER_COUNT)
        return false;

    parameters_.set(parameterId, value);
    return true;
}

float AudioEngine::getEngineParameter(int parameterId) const noexcept
{
    return parameters_.get(parameterId);
}

int AudioEngine::copySpectrum(float* destination, int capacity) const noexcept
{
    if (destination == nullptr || capacity <= 0)
        return 0;

    const auto count = juce::jmin(capacity, static_cast<int>(NEON_SPECTRUM_BANDS));
    for (int band = 0; band < count; ++band)
        destination[band] = parameters_.getSpectrum(band);
    return count;
}

juce::ValueTree AudioEngine::createPresetTree() const
{
    juce::ValueTree state("NEON_PRESET");
    state.setProperty("schema", 6, nullptr);
    for (int i = 0; i < NEON_PARAMETER_COUNT; ++i)
        state.setProperty(parameterNames[i], parameters_.get(i), nullptr);
    return state;
}

bool AudioEngine::applyPresetTree(const juce::ValueTree& tree)
{
    if (!tree.isValid() || tree.getType() != juce::Identifier("NEON_PRESET"))
        return false;

    const auto schema = static_cast<int>(tree.getProperty("schema", 0));
    if (schema < 1)
        return false;

    // Schema 6 replaces the old 0..1 width scale with an explicit spread
    // multiplier while accepting the legacy JSON property name and scale.
    for (int i = 0; i < NEON_PARAMETER_COUNT; ++i)
    {
        auto property = tree.getProperty(parameterNames[i], juce::var());
        auto legacyWidthProperty = false;
        if (i == NEON_SURROUND_SPREAD && property.isVoid())
        {
            property = tree.getProperty("surround_width", juce::var());
            legacyWidthProperty = !property.isVoid();
        }

        if (!property.isVoid())
        {
            auto value = static_cast<float>(property);
            if (i == NEON_SURROUND_SPREAD && (schema < 6 || legacyWidthProperty))
                value = 1.0f + 1.35f * juce::jlimit(0.0f, 1.0f, value);
            parameters_.set(i, value);
        }
        else if (i == NEON_SURROUND_SPREAD && schema < 6)
        {
            parameters_.set(i, 1.40f);
        }
    }

    const auto reverbLowCrossover = parameters_.get(NEON_REVERB_LOW_CROSSOVER_HZ);
    if (parameters_.get(NEON_REVERB_HIGH_CROSSOVER_HZ) < reverbLowCrossover + 50.0f)
        parameters_.set(NEON_REVERB_HIGH_CROSSOVER_HZ, reverbLowCrossover + 50.0f);

    const auto delayLowCrossover = parameters_.get(NEON_DELAY_LOW_CROSSOVER_HZ);
    if (parameters_.get(NEON_DELAY_HIGH_CROSSOVER_HZ) < delayLowCrossover + 50.0f)
        parameters_.set(NEON_DELAY_HIGH_CROSSOVER_HZ, delayLowCrossover + 50.0f);

    // Earlier preset schemas predate effect bypasses; retain their former
    // always-on behaviour instead of inheriting the host's current switch state.
    if (schema < 3)
    {
        parameters_.set(NEON_FIELD_ENABLED, 1.0f);
        parameters_.set(NEON_DIMENSION_ENABLED, 1.0f);
        parameters_.set(NEON_REVERB_ENABLED, 1.0f);
        parameters_.set(NEON_DELAY_ENABLED, 1.0f);
    }

    if (schema < 4)
    {
        parameters_.set(NEON_REVERB_DECAY_SECONDS, 2.5f);
        parameters_.set(NEON_REVERB_LOW_CROSSOVER_HZ, 250.0f);
        parameters_.set(NEON_REVERB_HIGH_CROSSOVER_HZ, 4000.0f);
        parameters_.set(NEON_REVERB_LOW_DECAY_MULTIPLIER, 1.0f);
        parameters_.set(NEON_REVERB_HIGH_DECAY_MULTIPLIER, 1.0f);
        parameters_.set(NEON_REVERB_PRE_DELAY_MS, 18.0f);
        parameters_.set(NEON_REVERB_LOW_DAMPING, 0.18f);
        parameters_.set(NEON_REVERB_HIGH_DAMPING, 0.45f);
        parameters_.set(NEON_REVERB_STEREO_WIDTH, 1.0f);
        parameters_.set(NEON_REVERB_ASPECT_RATIO, 1.0f);
        parameters_.set(NEON_REVERB_DIFFUSION, 0.72f);
    }

    if (schema < 5)
    {
        parameters_.set(NEON_DELAY_LOW_CROSSOVER_HZ, 250.0f);
        parameters_.set(NEON_DELAY_HIGH_CROSSOVER_HZ, 4000.0f);
        parameters_.set(NEON_LOUDNESS_ENABLED, 0.0f);
        parameters_.set(NEON_LOUDNESS_AMOUNT, 0.55f);
    }

    if (schema < 6)
    {
        parameters_.set(NEON_FIELD_AMOUNT, 0.15f);
        parameters_.set(NEON_SURROUND_BASS, 0.50f);
        parameters_.set(NEON_SURROUND_CENTER_DB, 0.0f);
    }

    return true;
}

bool AudioEngine::savePreset(const juce::File& file) const
{
    if (file.getFullPathName().isEmpty())
        return false;

    const auto json = juce::JSON::toString(valueTreePropertiesToJson(createPresetTree()), false);
    return file.replaceWithText(json);
}

bool AudioEngine::loadPreset(const juce::File& file)
{
    if (!file.existsAsFile())
        return false;

    const auto parsed = juce::JSON::parse(file.loadFileAsString());
    const auto* object = parsed.getDynamicObject();
    const auto schema = object != nullptr ? static_cast<int>(object->getProperty("schema")) : 0;
    if (object == nullptr || object->getProperty("type").toString() != "NEON_PRESET"
        || schema < 1)
        return false;

    const auto tree = jsonPropertiesToValueTree(parsed, juce::Identifier("NEON_PRESET"));
    return applyPresetTree(tree);
}

void AudioEngine::getStateInformation(juce::MemoryBlock& destination)
{
    const auto json = juce::JSON::toString(valueTreePropertiesToJson(createPresetTree()), false);
    destination.replaceAll(json.toRawUTF8(), json.getNumBytesAsUTF8());
}

void AudioEngine::setStateInformation(const void* data, int sizeInBytes)
{
    if (data == nullptr || sizeInBytes <= 0)
        return;

    const auto text = juce::String::fromUTF8(static_cast<const char*>(data), sizeInBytes);
    const auto parsed = juce::JSON::parse(text);
    const auto tree = jsonPropertiesToValueTree(parsed, juce::Identifier("NEON_PRESET"));
    applyPresetTree(tree);
}
} // namespace neon
