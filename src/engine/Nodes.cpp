#include "Nodes.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace neon
{
namespace
{
constexpr float pi = 3.14159265358979323846f;
constexpr float minimumDb = -72.0f;
constexpr std::array<float, 10> graphicEqFrequencies
{
    31.0f, 62.0f, 125.0f, 250.0f, 500.0f,
    1000.0f, 2000.0f, 4000.0f, 8000.0f, 16000.0f
};
constexpr std::array<int, 10> graphicEqParameters
{
    NEON_EQ_LOW_DB, NEON_EQ_62_DB, NEON_EQ_125_DB, NEON_EQ_250_DB, NEON_EQ_500_DB,
    NEON_EQ_MID_DB, NEON_EQ_2K_DB, NEON_EQ_4K_DB, NEON_EQ_8K_DB, NEON_EQ_HIGH_DB
};

float clampFrequency(float frequency, double sampleRate) noexcept
{
    const auto upper = static_cast<float>(std::max(20.0, sampleRate * 0.45));
    return juce::jlimit(10.0f, upper, frequency);
}

float readFractionalDelay(const std::vector<float>& line,
                          std::size_t writeIndex,
                          float delaySamples) noexcept
{
    if (line.empty())
        return 0.0f;

    const auto lineLength = static_cast<float>(line.size());
    auto position = static_cast<float>(writeIndex) - delaySamples;
    while (position < 0.0f)
        position += lineLength;
    while (position >= lineLength)
        position -= lineLength;

    const auto indexA = static_cast<std::size_t>(position);
    const auto indexB = (indexA + 1u) % line.size();
    const auto fraction = position - static_cast<float>(indexA);
    return line[indexA] + fraction * (line[indexB] - line[indexA]);
}
} // namespace

ParameterBank::ParameterBank() noexcept
{
    values_[NEON_EQ_LOW_DB].store(0.0f);
    values_[NEON_EQ_MID_DB].store(0.0f);
    values_[NEON_EQ_HIGH_DB].store(0.0f);
    values_[NEON_REVERB_MIX].store(0.35f);
    values_[NEON_REVERB_ROOM].store(0.55f);
    values_[NEON_SURROUND_SPREAD].store(1.40f);
    values_[NEON_DELAY_TIME_MS].store(325.0f);
    values_[NEON_DELAY_FEEDBACK].store(0.28f);
    values_[NEON_DELAY_MIX].store(0.22f);
    values_[NEON_DIMENSION_AMOUNT].store(0.28f);
    values_[NEON_MASTER_VOLUME].store(0.82f);
    values_[NEON_UTILITY_ENABLED].store(1.0f);
    values_[NEON_EQ_62_DB].store(0.0f);
    values_[NEON_EQ_125_DB].store(0.0f);
    values_[NEON_EQ_250_DB].store(0.0f);
    values_[NEON_EQ_500_DB].store(0.0f);
    values_[NEON_EQ_2K_DB].store(0.0f);
    values_[NEON_EQ_4K_DB].store(0.0f);
    values_[NEON_EQ_8K_DB].store(0.0f);
    values_[NEON_PREAMP_DB].store(0.0f);
    values_[NEON_EQ_BAND_Q].store(0.79f);
    values_[NEON_SPECTRUM_ENABLED].store(1.0f);
    values_[NEON_LIMITER_ENABLED].store(1.0f);
    values_[NEON_FIELD_ENABLED].store(1.0f);
    values_[NEON_DIMENSION_ENABLED].store(1.0f);
    values_[NEON_REVERB_ENABLED].store(1.0f);
    values_[NEON_DELAY_ENABLED].store(1.0f);
    values_[NEON_REVERB_DECAY_SECONDS].store(2.5f);
    values_[NEON_REVERB_LOW_CROSSOVER_HZ].store(250.0f);
    values_[NEON_REVERB_HIGH_CROSSOVER_HZ].store(4000.0f);
    values_[NEON_REVERB_LOW_DECAY_MULTIPLIER].store(1.0f);
    values_[NEON_REVERB_HIGH_DECAY_MULTIPLIER].store(1.0f);
    values_[NEON_REVERB_PRE_DELAY_MS].store(18.0f);
    values_[NEON_REVERB_LOW_DAMPING].store(0.18f);
    values_[NEON_REVERB_HIGH_DAMPING].store(0.45f);
    values_[NEON_REVERB_STEREO_WIDTH].store(1.0f);
    values_[NEON_REVERB_ASPECT_RATIO].store(1.0f);
    values_[NEON_REVERB_DIFFUSION].store(0.72f);
    values_[NEON_DELAY_LOW_CROSSOVER_HZ].store(250.0f);
    values_[NEON_DELAY_HIGH_CROSSOVER_HZ].store(4000.0f);
    values_[NEON_LOUDNESS_ENABLED].store(0.0f);
    values_[NEON_LOUDNESS_AMOUNT].store(0.55f);
    values_[NEON_FIELD_AMOUNT].store(0.15f);
    values_[NEON_SURROUND_BASS].store(0.50f);
    values_[NEON_SURROUND_CENTER_DB].store(0.0f);

    for (auto& band : spectrum_)
        band.store(0.0f);
}

float ParameterBank::get(int parameterId) const noexcept
{
    if (parameterId < 0 || parameterId >= NEON_PARAMETER_COUNT)
        return 0.0f;

    return values_[static_cast<std::size_t>(parameterId)].load(std::memory_order_relaxed);
}

void ParameterBank::set(int parameterId, float value) noexcept
{
    if (parameterId < 0 || parameterId >= NEON_PARAMETER_COUNT)
        return;

    values_[static_cast<std::size_t>(parameterId)].store(clampParameter(parameterId, value),
                                                      std::memory_order_relaxed);
}

float ParameterBank::getSpectrum(int band) const noexcept
{
    if (band < 0 || band >= NEON_SPECTRUM_BANDS)
        return 0.0f;

    return spectrum_[static_cast<std::size_t>(band)].load(std::memory_order_relaxed);
}

float ParameterBank::clampParameter(int parameterId, float value) noexcept
{
    if (!std::isfinite(value))
        return 0.0f;

    switch (parameterId)
    {
        case NEON_EQ_LOW_DB:
        case NEON_EQ_MID_DB:
        case NEON_EQ_HIGH_DB:
        case NEON_EQ_62_DB:
        case NEON_EQ_125_DB:
        case NEON_EQ_250_DB:
        case NEON_EQ_500_DB:
        case NEON_EQ_2K_DB:
        case NEON_EQ_4K_DB:
        case NEON_EQ_8K_DB:         return juce::jlimit(-12.0f, 12.0f, value);
        case NEON_REVERB_MIX:       return juce::jlimit(0.0f, 1.0f, value);
        case NEON_REVERB_ROOM:      return juce::jlimit(0.0f, 1.0f, value);
        case NEON_REVERB_DECAY_SECONDS:         return juce::jlimit(0.3f, 12.0f, value);
        case NEON_REVERB_LOW_CROSSOVER_HZ:      return juce::jlimit(40.0f, 1000.0f, value);
        case NEON_REVERB_HIGH_CROSSOVER_HZ:     return juce::jlimit(300.0f, 16000.0f, value);
        case NEON_REVERB_LOW_DECAY_MULTIPLIER:
        case NEON_REVERB_HIGH_DECAY_MULTIPLIER: return juce::jlimit(0.25f, 2.5f, value);
        case NEON_REVERB_PRE_DELAY_MS:          return juce::jlimit(0.0f, 250.0f, value);
        case NEON_REVERB_LOW_DAMPING:
        case NEON_REVERB_HIGH_DAMPING:
        case NEON_REVERB_DIFFUSION:             return juce::jlimit(0.0f, 1.0f, value);
        case NEON_REVERB_STEREO_WIDTH:          return juce::jlimit(0.0f, 2.0f, value);
        case NEON_REVERB_ASPECT_RATIO:          return juce::jlimit(0.5f, 2.0f, value);
        case NEON_SURROUND_SPREAD:              return juce::jlimit(0.5f, 2.5f, value);
        case NEON_FIELD_AMOUNT:
        case NEON_SURROUND_BASS:                return juce::jlimit(0.0f, 1.0f, value);
        case NEON_SURROUND_CENTER_DB:           return juce::jlimit(-12.0f, 6.0f, value);
        case NEON_DELAY_TIME_MS:    return juce::jlimit(20.0f, 1000.0f, value);
        case NEON_DELAY_FEEDBACK:   return juce::jlimit(0.0f, 0.85f, value);
        case NEON_DELAY_MIX:        return juce::jlimit(0.0f, 1.0f, value);
        case NEON_DELAY_LOW_CROSSOVER_HZ:  return juce::jlimit(40.0f, 1000.0f, value);
        case NEON_DELAY_HIGH_CROSSOVER_HZ: return juce::jlimit(300.0f, 16000.0f, value);
        case NEON_LOUDNESS_AMOUNT:   return juce::jlimit(0.0f, 1.0f, value);
        case NEON_DIMENSION_AMOUNT: return juce::jlimit(0.0f, 1.0f, value);
        case NEON_MASTER_VOLUME:    return juce::jlimit(0.0f, 1.5f, value);
        case NEON_UTILITY_ENABLED:
        case NEON_SPECTRUM_ENABLED:
        case NEON_LIMITER_ENABLED:
        case NEON_FIELD_ENABLED:
        case NEON_DIMENSION_ENABLED:
        case NEON_REVERB_ENABLED:
        case NEON_DELAY_ENABLED:
        case NEON_LOUDNESS_ENABLED: return value >= 0.5f ? 1.0f : 0.0f;
        case NEON_PREAMP_DB:        return juce::jlimit(-24.0f, 12.0f, value);
        case NEON_EQ_BAND_Q:        return juce::jlimit(0.3f, 4.0f, value);
        default:                    return 0.0f;
    }
}

StereoNode::StereoNode(const juce::String& processorName, ParameterBank& parameters)
    : juce::AudioProcessor(BusesProperties()
          .withInput("Input", juce::AudioChannelSet::stereo(), true)
          .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters_(parameters),
      name_(processorName)
{
}

void StereoNode::prepareToPlay(double sampleRate, int)
{
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
}

bool StereoNode::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    return layouts.getMainInputChannelSet() == juce::AudioChannelSet::stereo()
        && layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

InputNode::InputNode()
    : juce::AudioProcessorGraph::AudioGraphIOProcessor(
          juce::AudioProcessorGraph::AudioGraphIOProcessor::audioInputNode)
{
}

OutputNode::OutputNode()
    : juce::AudioProcessorGraph::AudioGraphIOProcessor(
          juce::AudioProcessorGraph::AudioGraphIOProcessor::audioOutputNode)
{
}

EqNode::EqNode(ParameterBank& parameters)
    : StereoNode("EQ", parameters)
{
}

void EqNode::prepareToPlay(double sampleRate, int maximumBlockSize)
{
    StereoNode::prepareToPlay(sampleRate, maximumBlockSize);
    for (auto& channel : filters_)
        for (auto& filter : channel)
        {
            filter.z1 = 0.0f;
            filter.z2 = 0.0f;
        }
}

void EqNode::Biquad::setNormalised(float nb0, float nb1, float nb2,
                                   float na0, float na1, float na2) noexcept
{
    const auto reciprocal = (std::abs(na0) > 1.0e-12f ? 1.0f / na0 : 1.0f);
    b0 = nb0 * reciprocal;
    b1 = nb1 * reciprocal;
    b2 = nb2 * reciprocal;
    a1 = na1 * reciprocal;
    a2 = na2 * reciprocal;
}

void EqNode::Biquad::setLowShelf(float sr, float frequency, float gainDb) noexcept
{
    const auto f = clampFrequency(frequency, sr);
    const auto a = std::pow(10.0f, gainDb / 40.0f);
    const auto w = 2.0f * pi * f / static_cast<float>(sr);
    const auto c = std::cos(w);
    const auto s = std::sin(w);
    constexpr float slope = 1.0f;
    const auto alpha = 0.5f * s * std::sqrt((a + 1.0f / a) * (1.0f / slope - 1.0f) + 2.0f);
    const auto beta = 2.0f * std::sqrt(a) * alpha;

    setNormalised(a * ((a + 1.0f) - (a - 1.0f) * c + beta),
                  2.0f * a * ((a - 1.0f) - (a + 1.0f) * c),
                  a * ((a + 1.0f) - (a - 1.0f) * c - beta),
                  (a + 1.0f) + (a - 1.0f) * c + beta,
                  -2.0f * ((a - 1.0f) + (a + 1.0f) * c),
                  (a + 1.0f) + (a - 1.0f) * c - beta);
}

void EqNode::Biquad::setPeak(float sr, float frequency, float q, float gainDb) noexcept
{
    const auto f = clampFrequency(frequency, sr);
    const auto a = std::pow(10.0f, gainDb / 40.0f);
    const auto w = 2.0f * pi * f / static_cast<float>(sr);
    const auto c = std::cos(w);
    const auto alpha = std::sin(w) / (2.0f * juce::jmax(0.1f, q));

    setNormalised(1.0f + alpha * a,
                  -2.0f * c,
                  1.0f - alpha * a,
                  1.0f + alpha / a,
                  -2.0f * c,
                  1.0f - alpha / a);
}

void EqNode::Biquad::setHighShelf(float sr, float frequency, float gainDb) noexcept
{
    const auto f = clampFrequency(frequency, sr);
    const auto a = std::pow(10.0f, gainDb / 40.0f);
    const auto w = 2.0f * pi * f / static_cast<float>(sr);
    const auto c = std::cos(w);
    const auto s = std::sin(w);
    constexpr float slope = 1.0f;
    const auto alpha = 0.5f * s * std::sqrt((a + 1.0f / a) * (1.0f / slope - 1.0f) + 2.0f);
    const auto beta = 2.0f * std::sqrt(a) * alpha;

    setNormalised(a * ((a + 1.0f) + (a - 1.0f) * c + beta),
                  -2.0f * a * ((a - 1.0f) + (a + 1.0f) * c),
                  a * ((a + 1.0f) + (a - 1.0f) * c - beta),
                  (a + 1.0f) - (a - 1.0f) * c + beta,
                  2.0f * ((a - 1.0f) - (a + 1.0f) * c),
                  (a + 1.0f) - (a - 1.0f) * c - beta);
}

float EqNode::Biquad::process(float input) noexcept
{
    const auto output = b0 * input + z1;
    z1 = b1 * input - a1 * output + z2;
    z2 = b2 * input - a2 * output;
    return output;
}

void EqNode::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    const auto q = parameter(NEON_EQ_BAND_Q);

    for (auto channel = 0; channel < juce::jmin(2, buffer.getNumChannels()); ++channel)
    {
        auto& channelFilters = filters_[static_cast<std::size_t>(channel)];
        for (std::size_t band = 0; band < graphicEqFrequencies.size(); ++band)
            channelFilters[band].setPeak(static_cast<float>(sampleRate_),
                                         graphicEqFrequencies[band], q,
                                         parameter(graphicEqParameters[band]));

        auto* samples = buffer.getWritePointer(channel);
        for (auto i = 0; i < buffer.getNumSamples(); ++i)
        {
            auto value = samples[i];
            for (auto& filter : channelFilters)
                value = filter.process(value);
            samples[i] = value;
        }
    }
}

SpectrumAnalyzerNode::SpectrumAnalyzerNode(ParameterBank& parameters)
    : StereoNode("Spectrum Analyzer", parameters)
{
    for (int i = 0; i < fftSize; ++i)
        window_[static_cast<std::size_t>(i)] = 0.5f - 0.5f * std::cos(2.0f * pi * static_cast<float>(i) / static_cast<float>(fftSize - 1));
}

void SpectrumAnalyzerNode::prepareToPlay(double sampleRate, int)
{
    StereoNode::prepareToPlay(sampleRate, 0);
    fifo_.fill(0.0f);
    fftWork_.fill(0.0f);
    fifoIndex_ = 0;
}

void SpectrumAnalyzerNode::publishFrame() noexcept
{
    fftWork_.fill(0.0f);
    for (int i = 0; i < fftSize; ++i)
        fftWork_[static_cast<std::size_t>(i)] = fifo_[static_cast<std::size_t>(i)] * window_[static_cast<std::size_t>(i)];

    fft_.performFrequencyOnlyForwardTransform(fftWork_.data());

    const auto binHz = static_cast<float>(sampleRate_) / static_cast<float>(fftSize);
    const auto maximumHz = juce::jmin(20000.0f, static_cast<float>(sampleRate_ * 0.48));
    constexpr float minimumHz = 24.0f;

    for (int band = 0; band < NEON_SPECTRUM_BANDS; ++band)
    {
        const auto fractionA = static_cast<float>(band) / static_cast<float>(NEON_SPECTRUM_BANDS);
        const auto fractionB = static_cast<float>(band + 1) / static_cast<float>(NEON_SPECTRUM_BANDS);
        const auto hzA = minimumHz * std::pow(maximumHz / minimumHz, fractionA);
        const auto hzB = minimumHz * std::pow(maximumHz / minimumHz, fractionB);
        auto firstBin = juce::jlimit(1, fftSize / 2 - 1, static_cast<int>(hzA / binHz));
        auto lastBin = juce::jlimit(firstBin + 1, fftSize / 2, static_cast<int>(hzB / binHz) + 1);
        auto magnitude = 0.0f;

        for (auto bin = firstBin; bin < lastBin; ++bin)
            magnitude = juce::jmax(magnitude, fftWork_[static_cast<std::size_t>(bin)] * (2.0f / static_cast<float>(fftSize)));

        const auto db = magnitude > 1.0e-6f ? 20.0f * std::log10(magnitude) : minimumDb;
        const auto normalised = juce::jlimit(0.0f, 1.0f, (db - minimumDb) / -minimumDb);
        parameters_.spectrum_[static_cast<std::size_t>(band)].store(normalised, std::memory_order_relaxed);
    }
}

void SpectrumAnalyzerNode::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    if (parameter(NEON_SPECTRUM_ENABLED) < 0.5f)
    {
        fifoIndex_ = 0;
        return;
    }

    const auto channelCount = buffer.getNumChannels();
    if (channelCount == 0)
        return;

    const auto* left = buffer.getReadPointer(0);
    const auto* right = channelCount > 1 ? buffer.getReadPointer(1) : left;

    for (auto i = 0; i < buffer.getNumSamples(); ++i)
    {
        fifo_[static_cast<std::size_t>(fifoIndex_++)] = 0.5f * (left[i] + right[i]);
        if (fifoIndex_ == fftSize)
        {
            publishFrame();
            fifoIndex_ = 0;
        }
    }
}

ReverbNode::ReverbNode(ParameterBank& parameters)
    : StereoNode("Advanced Reverb", parameters)
{
}

void ReverbNode::prepareToPlay(double sampleRate, int maximumBlockSize)
{
    StereoNode::prepareToPlay(sampleRate, maximumBlockSize);

    const auto combCapacity = static_cast<std::size_t>(juce::jmax(512.0, sampleRate_ * 0.16 + 16.0));
    for (auto& group : combLines_)
    {
        for (auto& line : group)
        {
            line.samples.assign(combCapacity, 0.0f);
            line.writeIndex = 0;
            line.dampingState = 0.0f;
        }
    }

    const auto predelayCapacity = static_cast<std::size_t>(juce::jmax(64.0, sampleRate_ * 0.26 + 8.0));
    for (auto& line : preDelayLines_)
        line.assign(predelayCapacity, 0.0f);
    preDelayWriteIndex_ = 0;

    const auto diffuserCapacity = static_cast<std::size_t>(juce::jmax(64.0, sampleRate_ * 0.05 + 8.0));
    for (auto& channel : diffuserLines_)
    {
        for (auto& line : channel)
        {
            line.samples.assign(diffuserCapacity, 0.0f);
            line.writeIndex = 0;
        }
    }

    lowCrossoverState_.fill(0.0f);
    highCrossoverState_.fill(0.0f);
    mixSmoother_.reset(sampleRate_, 0.02);
    lastMixTarget_ = parameter(NEON_REVERB_ENABLED) >= 0.5f
                   ? parameter(NEON_REVERB_MIX) : 0.0f;
    mixSmoother_.setCurrentAndTargetValue(lastMixTarget_);
}

void ReverbNode::releaseResources()
{
    for (auto& group : combLines_)
        for (auto& line : group)
        {
            line.samples.clear();
            line.writeIndex = 0;
            line.dampingState = 0.0f;
        }

    for (auto& channel : diffuserLines_)
        for (auto& line : channel)
        {
            line.samples.clear();
            line.writeIndex = 0;
        }

    for (auto& line : preDelayLines_)
        line.clear();
    preDelayWriteIndex_ = 0;
    lowCrossoverState_.fill(0.0f);
    highCrossoverState_.fill(0.0f);
}

void ReverbNode::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    if (buffer.getNumChannels() < 2 || preDelayLines_[0].empty()
        || combLines_[0][0].samples.empty())
        return;

    const auto sampleRate = static_cast<float>(sampleRate_);
    const auto maximumCrossoverHz = juce::jmax(300.0f, sampleRate * 0.45f);
    const auto lowCrossoverUpper = juce::jmax(40.0f, juce::jmin(1000.0f, maximumCrossoverHz - 60.0f));
    const auto lowCrossover = juce::jlimit(40.0f, lowCrossoverUpper,
                                            parameter(NEON_REVERB_LOW_CROSSOVER_HZ));
    const auto highCrossoverFloor = juce::jmin(maximumCrossoverHz, lowCrossover + 50.0f);
    const auto highCrossover = juce::jlimit(highCrossoverFloor, maximumCrossoverHz,
                                             parameter(NEON_REVERB_HIGH_CROSSOVER_HZ));
    const auto lowAlpha = 1.0f - std::exp(-2.0f * pi * lowCrossover / sampleRate);
    const auto highAlpha = 1.0f - std::exp(-2.0f * pi * highCrossover / sampleRate);

    const auto roomSize = parameter(NEON_REVERB_ROOM);
    const auto roomScale = 0.65f + 0.70f * roomSize;
    const auto aspect = parameter(NEON_REVERB_ASPECT_RATIO);
    const std::array<float, combsPerChannel> aspectFactors
    {{ 1.0f / aspect, 1.0f / std::sqrt(aspect), std::sqrt(aspect), aspect }};
    const std::array<float, combsPerChannel> combBaseMilliseconds
    {{ 29.7f, 37.1f, 41.1f, 43.7f }};
    const std::array<float, diffuserStages> diffuserBaseMilliseconds
    {{ 5.1f, 7.3f, 11.7f, 13.9f }};

    const auto mainDecay = parameter(NEON_REVERB_DECAY_SECONDS);
    const std::array<float, bandCount> bandDecaySeconds
    {{
        mainDecay * parameter(NEON_REVERB_LOW_DECAY_MULTIPLIER),
        mainDecay,
        mainDecay * parameter(NEON_REVERB_HIGH_DECAY_MULTIPLIER)
    }};
    const auto lowDamping = parameter(NEON_REVERB_LOW_DAMPING);
    const auto highDamping = parameter(NEON_REVERB_HIGH_DAMPING);
    const std::array<float, bandCount> bandDamping
    {{ lowDamping, 0.5f * (lowDamping + highDamping), highDamping }};
    std::array<float, bandCount> dampingAlpha {};
    for (int band = 0; band < bandCount; ++band)
        dampingAlpha[static_cast<std::size_t>(band)] = 1.0f - 0.88f * bandDamping[static_cast<std::size_t>(band)];

    std::array<std::array<float, combsPerChannel>, bandCount * 2> combDelaySamples {};
    std::array<std::array<float, combsPerChannel>, bandCount * 2> combFeedback {};
    for (int band = 0; band < bandCount; ++band)
    {
        const auto decay = juce::jmax(0.1f, bandDecaySeconds[static_cast<std::size_t>(band)]);
        for (int channel = 0; channel < 2; ++channel)
        {
            const auto group = band * 2 + channel;
            const auto channelScale = channel == 0 ? 0.997f : 1.003f;
            for (int comb = 0; comb < combsPerChannel; ++comb)
            {
                const auto combJitter = 1.0f + (static_cast<float>(comb) - 1.5f) * 0.0007f;
                const auto delaySeconds = 0.001f * combBaseMilliseconds[static_cast<std::size_t>(comb)]
                                        * roomScale * aspectFactors[static_cast<std::size_t>(comb)]
                                        * channelScale * combJitter;
                const auto delayCapacity = static_cast<float>(combLines_[static_cast<std::size_t>(group)]
                                                               [static_cast<std::size_t>(comb)].samples.size() - 2);
                const auto delaySamples = juce::jlimit(1.0f, delayCapacity, delaySeconds * sampleRate);
                combDelaySamples[static_cast<std::size_t>(group)][static_cast<std::size_t>(comb)] = delaySamples;
                combFeedback[static_cast<std::size_t>(group)][static_cast<std::size_t>(comb)]
                    = std::pow(0.001f, (delaySamples / sampleRate) / decay);
            }
        }
    }

    // Pre-delay lives only on the wet path; the dry samples are kept untouched.
    const auto predelaySamples = parameter(NEON_REVERB_PRE_DELAY_MS) * 0.001f * sampleRate;
    const auto diffusion = parameter(NEON_REVERB_DIFFUSION);
    const auto diffuserCoefficient = 0.25f + 0.60f * diffusion;
    std::array<std::array<float, diffuserStages>, 2> diffuserDelaySamples {};
    for (int channel = 0; channel < 2; ++channel)
    {
        const auto channelScale = channel == 0 ? 0.997f : 1.003f;
        for (int stage = 0; stage < diffuserStages; ++stage)
        {
            const auto shape = aspectFactors[static_cast<std::size_t>((stage + 1) % combsPerChannel)];
            const auto delay = 0.001f * diffuserBaseMilliseconds[static_cast<std::size_t>(stage)]
                             * roomScale * shape * channelScale * sampleRate;
            const auto capacity = static_cast<float>(diffuserLines_[static_cast<std::size_t>(channel)]
                                                      [static_cast<std::size_t>(stage)].samples.size() - 2);
            diffuserDelaySamples[static_cast<std::size_t>(channel)][static_cast<std::size_t>(stage)]
                = juce::jlimit(1.0f, capacity, delay);
        }
    }

    const auto effectEnabled = parameter(NEON_REVERB_ENABLED) >= 0.5f;
    // Linear Dry/Wet blend: mix=0 is dry-only and mix=1 is fully wet.
    const auto mixTarget = effectEnabled ? parameter(NEON_REVERB_MIX) : 0.0f;
    if (std::abs(mixTarget - lastMixTarget_) > 0.0001f)
    {
        lastMixTarget_ = mixTarget;
        mixSmoother_.setTargetValue(mixTarget);
    }
    const auto wetWidth = parameter(NEON_REVERB_STEREO_WIDTH);
    auto* left = buffer.getWritePointer(0);
    auto* right = buffer.getWritePointer(1);

    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
    {
        const std::array<float, 2> dry {{ left[sample], right[sample] }};
        std::array<float, 2> delayedInput {};
        for (int channel = 0; channel < 2; ++channel)
        {
            delayedInput[static_cast<std::size_t>(channel)] = predelaySamples < 0.5f
                ? dry[static_cast<std::size_t>(channel)]
                : readFractionalDelay(preDelayLines_[static_cast<std::size_t>(channel)],
                                      preDelayWriteIndex_, predelaySamples);
            preDelayLines_[static_cast<std::size_t>(channel)][preDelayWriteIndex_]
                = dry[static_cast<std::size_t>(channel)];
        }
        preDelayWriteIndex_ = (preDelayWriteIndex_ + 1u) % preDelayLines_[0].size();

        std::array<std::array<float, 2>, bandCount> bandInput {};
        for (int channel = 0; channel < 2; ++channel)
        {
            auto& lowState = lowCrossoverState_[static_cast<std::size_t>(channel)];
            auto& highState = highCrossoverState_[static_cast<std::size_t>(channel)];
            const auto input = delayedInput[static_cast<std::size_t>(channel)];
            lowState += lowAlpha * (input - lowState);
            highState += highAlpha * (input - highState);
            bandInput[0][static_cast<std::size_t>(channel)] = lowState;
            bandInput[1][static_cast<std::size_t>(channel)] = highState - lowState;
            bandInput[2][static_cast<std::size_t>(channel)] = input - highState;
        }

        std::array<float, 2> wet {{ 0.0f, 0.0f }};
        for (int band = 0; band < bandCount; ++band)
        {
            for (int channel = 0; channel < 2; ++channel)
            {
                const auto group = band * 2 + channel;
                const auto input = 0.82f * bandInput[static_cast<std::size_t>(band)][static_cast<std::size_t>(channel)]
                                 + 0.18f * bandInput[static_cast<std::size_t>(band)][static_cast<std::size_t>(1 - channel)];
                auto combSum = 0.0f;
                for (int comb = 0; comb < combsPerChannel; ++comb)
                {
                    auto& line = combLines_[static_cast<std::size_t>(group)][static_cast<std::size_t>(comb)];
                    const auto delayed = readFractionalDelay(
                        line.samples, line.writeIndex,
                        combDelaySamples[static_cast<std::size_t>(group)][static_cast<std::size_t>(comb)]);
                    line.dampingState += dampingAlpha[static_cast<std::size_t>(band)]
                                       * (delayed - line.dampingState);
                    line.samples[line.writeIndex] = 0.82f * input
                        + line.dampingState * combFeedback[static_cast<std::size_t>(group)][static_cast<std::size_t>(comb)];
                    line.writeIndex = (line.writeIndex + 1u) % line.samples.size();
                    combSum += delayed;
                }
                wet[static_cast<std::size_t>(channel)] += 0.65f * combSum;
            }
        }

        // Keep some raw comb energy in parallel with the allpass chain. Crossfading
        // each allpass stage against its input can cancel its initial response.
        for (int channel = 0; channel < 2; ++channel)
        {
            const auto rawWet = wet[static_cast<std::size_t>(channel)];
            auto diffused = rawWet;
            for (int stage = 0; stage < diffuserStages; ++stage)
            {
                auto& line = diffuserLines_[static_cast<std::size_t>(channel)][static_cast<std::size_t>(stage)];
                const auto delayed = readFractionalDelay(
                    line.samples, line.writeIndex,
                    diffuserDelaySamples[static_cast<std::size_t>(channel)][static_cast<std::size_t>(stage)]);
                const auto allpass = delayed - diffuserCoefficient * diffused;
                line.samples[line.writeIndex] = diffused + diffuserCoefficient * allpass;
                line.writeIndex = (line.writeIndex + 1u) % line.samples.size();
                diffused = allpass;
            }
            wet[static_cast<std::size_t>(channel)] = rawWet * (1.0f - diffusion)
                                                   + diffused * diffusion;
        }

        const auto mid = 0.5f * (wet[0] + wet[1]);
        const auto side = 0.5f * (wet[0] - wet[1]) * wetWidth;
        wet[0] = mid + side;
        wet[1] = mid - side;

        const auto sampleMix = mixSmoother_.getNextValue();
        left[sample] = dry[0] * (1.0f - sampleMix) + wet[0] * sampleMix;
        right[sample] = dry[1] * (1.0f - sampleMix) + wet[1] * sampleMix;
    }
}

FieldSurroundNode::FieldSurroundNode(ParameterBank& parameters)
    : StereoNode("Field Surround", parameters)
{
}

void FieldSurroundNode::prepareToPlay(double sampleRate, int maximumBlockSize)
{
    StereoNode::prepareToPlay(sampleRate, maximumBlockSize);
    const auto capacity = static_cast<std::size_t>(juce::jmax(2048.0, sampleRate_ * 0.05 + 8.0));
    fieldDelayLine_.assign(capacity, 0.0f);
    fieldWriteIndex_ = 0;
    validFrames_ = 0;
    bassLowState_.fill(0.0f);
}

void FieldSurroundNode::releaseResources()
{
    fieldDelayLine_.clear();
    fieldWriteIndex_ = 0;
    validFrames_ = 0;
    bassLowState_.fill(0.0f);
}

void FieldSurroundNode::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    if (buffer.getNumChannels() < 2 || fieldDelayLine_.empty())
        return;

    const auto enabled = parameter(NEON_FIELD_ENABLED) >= 0.5f;
    const auto field = parameter(NEON_FIELD_AMOUNT);
    const auto spread = parameter(NEON_SURROUND_SPREAD);
    const auto bass = parameter(NEON_SURROUND_BASS);
    const auto centerDb = parameter(NEON_SURROUND_CENTER_DB);
    const auto sampleRate = static_cast<float>(sampleRate_);
    const auto bassAlpha = 1.0f - std::exp(-2.0f * pi * 150.0f / sampleRate);
    const auto bassGain = std::pow(10.0f, ((bass - 0.5f) * 12.0f) / 20.0f);
    const auto centerGain = std::pow(10.0f, centerDb / 20.0f);
    const auto delayLeft = juce::jmin(static_cast<float>(fieldDelayLine_.size() - 2), sampleRate * 0.011f);
    const auto delayRight = juce::jmin(static_cast<float>(fieldDelayLine_.size() - 2), sampleRate * 0.017f);
    const auto* inputLeft = buffer.getReadPointer(0);
    const auto* inputRight = buffer.getReadPointer(1);
    auto* left = buffer.getWritePointer(0);
    auto* right = buffer.getWritePointer(1);

    for (auto i = 0; i < buffer.getNumSamples(); ++i)
    {
        const auto dryL = inputLeft[i];
        const auto dryR = inputRight[i];
        const auto rawMid = 0.5f * (dryL + dryR);
        fieldDelayLine_[fieldWriteIndex_] = rawMid;

        auto& bassL = bassLowState_[0];
        auto& bassR = bassLowState_[1];
        bassL += bassAlpha * (dryL - bassL);
        bassR += bassAlpha * (dryR - bassR);

        if (enabled)
        {
            const auto shapedL = dryL + (bassGain - 1.0f) * bassL;
            const auto shapedR = dryR + (bassGain - 1.0f) * bassR;
            auto mid = 0.5f * (shapedL + shapedR) * centerGain;
            auto side = 0.5f * (shapedL - shapedR) * spread;

            if (static_cast<float>(validFrames_) >= delayRight)
            {
                const auto decorrelatedL = readFractionalDelay(fieldDelayLine_, fieldWriteIndex_, delayLeft);
                const auto decorrelatedR = readFractionalDelay(fieldDelayLine_, fieldWriteIndex_, delayRight);
                side += 0.20f * field * (decorrelatedL - decorrelatedR);
            }

            left[i] = mid + side;
            right[i] = mid - side;
        }
        else
        {
            left[i] = dryL;
            right[i] = dryR;
        }

        fieldWriteIndex_ = (fieldWriteIndex_ + 1u) % fieldDelayLine_.size();
        if (validFrames_ < fieldDelayLine_.size())
            ++validFrames_;
    }
}

StereoDelayNode::StereoDelayNode(ParameterBank& parameters)
    : StereoNode("Stereo Delay", parameters)
{
}

void StereoDelayNode::prepareToPlay(double sampleRate, int)
{
    StereoNode::prepareToPlay(sampleRate, 0);
    const auto capacity = static_cast<std::size_t>(juce::jmax(4096.0, sampleRate * 2.0 + 8.0));
    for (auto& line : delayLines_)
        line.assign(capacity, 0.0f);
    writeIndex_ = 0;
    validFrames_ = 0;
    lowCrossoverState_.fill(0.0f);
    highCrossoverState_.fill(0.0f);
    wasEnabled_ = parameter(NEON_DELAY_ENABLED) >= 0.5f;
}

void StereoDelayNode::releaseResources()
{
    for (auto& line : delayLines_)
        line.clear();
    writeIndex_ = 0;
    validFrames_ = 0;
    lowCrossoverState_.fill(0.0f);
    highCrossoverState_.fill(0.0f);
    wasEnabled_ = false;
}

void StereoDelayNode::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    if (buffer.getNumChannels() < 2 || delayLines_[0].empty())
        return;

    if (parameter(NEON_DELAY_ENABLED) < 0.5f)
    {
        if (wasEnabled_)
        {
            validFrames_ = 0;
            lowCrossoverState_.fill(0.0f);
            highCrossoverState_.fill(0.0f);
        }
        wasEnabled_ = false;
        for (auto i = 0; i < buffer.getNumSamples(); ++i)
        {
            delayLines_[0][writeIndex_] = 0.0f;
            delayLines_[1][writeIndex_] = 0.0f;
            writeIndex_ = (writeIndex_ + 1u) % delayLines_[0].size();
        }
        return;
    }
    wasEnabled_ = true;

    const auto capacity = delayLines_[0].size();
    const auto sampleRate = static_cast<float>(sampleRate_);
    const auto delaySamples = juce::jlimit(1.0f, static_cast<float>(capacity - 2),
        parameter(NEON_DELAY_TIME_MS) * 0.001f * sampleRate);
    const auto maximumCrossoverHz = juce::jmax(300.0f, sampleRate * 0.45f);
    const auto lowCrossoverUpper = juce::jmax(40.0f, juce::jmin(1000.0f, maximumCrossoverHz - 60.0f));
    const auto lowCrossover = juce::jlimit(40.0f, lowCrossoverUpper,
                                            parameter(NEON_DELAY_LOW_CROSSOVER_HZ));
    const auto highCrossoverFloor = juce::jmin(maximumCrossoverHz, lowCrossover + 50.0f);
    const auto highCrossover = juce::jlimit(highCrossoverFloor, maximumCrossoverHz,
                                             parameter(NEON_DELAY_HIGH_CROSSOVER_HZ));
    const auto lowAlpha = 1.0f - std::exp(-2.0f * pi * lowCrossover / sampleRate);
    const auto highAlpha = 1.0f - std::exp(-2.0f * pi * highCrossover / sampleRate);
    const auto feedback = parameter(NEON_DELAY_FEEDBACK);
    const auto wet = parameter(NEON_DELAY_MIX);
    auto* left = buffer.getWritePointer(0);
    auto* right = buffer.getWritePointer(1);

    for (auto i = 0; i < buffer.getNumSamples(); ++i)
    {
        const auto dryL = left[i];
        const auto dryR = right[i];
        const auto hasDelayHistory = static_cast<float>(validFrames_) >= delaySamples;
        const auto delayedL = hasDelayHistory
                            ? readFractionalDelay(delayLines_[0], writeIndex_, delaySamples) : 0.0f;
        const auto delayedR = hasDelayHistory
                            ? readFractionalDelay(delayLines_[1], writeIndex_, delaySamples) : 0.0f;

        auto& lowLState = lowCrossoverState_[0];
        auto& lowRState = lowCrossoverState_[1];
        auto& highLState = highCrossoverState_[0];
        auto& highRState = highCrossoverState_[1];
        lowLState += lowAlpha * (delayedL - lowLState);
        lowRState += lowAlpha * (delayedR - lowRState);
        highLState += highAlpha * (delayedL - highLState);
        highRState += highAlpha * (delayedR - highRState);

        const auto lowBandL = lowLState;
        const auto lowBandR = lowRState;
        const auto midBandL = highLState - lowLState;
        const auto midBandR = highRState - lowRState;
        const auto highBandL = delayedL - highLState;
        const auto highBandR = delayedR - highRState;
        // Shape the repeats by band: keep bass/full mids, gently darken treble.
        const auto toneL = 0.85f * lowBandL + midBandL + 0.62f * highBandL;
        const auto toneR = 0.85f * lowBandR + midBandR + 0.62f * highBandR;

        delayLines_[0][writeIndex_] = dryL + toneR * feedback;
        delayLines_[1][writeIndex_] = dryR + toneL * feedback;
        left[i] = dryL * (1.0f - wet) + toneL * wet;
        right[i] = dryR * (1.0f - wet) + toneR * wet;

        writeIndex_ = (writeIndex_ + 1u) % capacity;
        if (validFrames_ < capacity)
            ++validFrames_;
    }
}

DimensionNode::DimensionNode(ParameterBank& parameters)
    : StereoNode("Dimension", parameters)
{
}

void DimensionNode::prepareToPlay(double sampleRate, int)
{
    StereoNode::prepareToPlay(sampleRate, 0);
    const auto capacity = static_cast<std::size_t>(juce::jmax(1024.0, sampleRate * 0.04 + 8.0));
    for (auto& line : delayLines_)
        line.assign(capacity, 0.0f);
    writeIndex_ = 0;
    validFrames_ = 0;
    phase_ = 0.0f;
    wasEnabled_ = parameter(NEON_DIMENSION_ENABLED) >= 0.5f
               && parameter(NEON_DIMENSION_AMOUNT) > 0.0001f;
}

void DimensionNode::releaseResources()
{
    for (auto& line : delayLines_)
        line.clear();
    writeIndex_ = 0;
    validFrames_ = 0;
    phase_ = 0.0f;
    wasEnabled_ = false;
}

void DimensionNode::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    if (buffer.getNumChannels() < 2 || delayLines_[0].empty())
        return;

    const auto amount = parameter(NEON_DIMENSION_AMOUNT);
    const auto enabled = parameter(NEON_DIMENSION_ENABLED) >= 0.5f && amount > 0.0001f;
    if (!enabled)
    {
        if (wasEnabled_)
        {
            validFrames_ = 0;
            phase_ = 0.0f;
        }
        wasEnabled_ = false;
        for (auto i = 0; i < buffer.getNumSamples(); ++i)
        {
            delayLines_[0][writeIndex_] = 0.0f;
            delayLines_[1][writeIndex_] = 0.0f;
            writeIndex_ = (writeIndex_ + 1u) % delayLines_[0].size();
        }
        return;
    }
    wasEnabled_ = true;

    const auto capacity = delayLines_[0].size();
    auto* left = buffer.getWritePointer(0);
    auto* right = buffer.getWritePointer(1);
    const auto phaseIncrement = 2.0f * pi * 0.23f / static_cast<float>(sampleRate_);

    for (auto i = 0; i < buffer.getNumSamples(); ++i)
    {
        const auto dryL = left[i];
        const auto dryR = right[i];
        const auto modulation = std::sin(phase_);
        const auto baseDelay = static_cast<float>(sampleRate_) * 0.0065f;
        const auto depth = static_cast<float>(sampleRate_) * 0.0028f * amount;
        const auto delayL = juce::jlimit(1.0f, static_cast<float>(capacity - 2), baseDelay + depth * modulation);
        const auto delayR = juce::jlimit(1.0f, static_cast<float>(capacity - 2), baseDelay - depth * modulation);
        const auto hasDelayHistory = static_cast<float>(validFrames_) >= juce::jmax(delayL, delayR);
        const auto wetL = hasDelayHistory
                        ? readFractionalDelay(delayLines_[0], writeIndex_, delayL) : 0.0f;
        const auto wetR = hasDelayHistory
                        ? readFractionalDelay(delayLines_[1], writeIndex_, delayR) : 0.0f;

        delayLines_[0][writeIndex_] = dryL;
        delayLines_[1][writeIndex_] = dryR;
        left[i] = dryL * (1.0f - 0.35f * amount) + wetL * (0.35f * amount);
        right[i] = dryR * (1.0f - 0.35f * amount) + wetR * (0.35f * amount);

        writeIndex_ = (writeIndex_ + 1u) % capacity;
        if (validFrames_ < capacity)
            ++validFrames_;
        phase_ += phaseIncrement;
        if (phase_ >= 2.0f * pi)
            phase_ -= 2.0f * pi;
    }
}

MasterVolumeNode::MasterVolumeNode(ParameterBank& parameters)
    : StereoNode("Master Volume", parameters)
{
}

void MasterVolumeNode::prepareToPlay(double sampleRate, int maximumBlockSize)
{
    StereoNode::prepareToPlay(sampleRate, maximumBlockSize);
    gain_.reset(sampleRate_, 0.025);
    lastTarget_ = parameter(NEON_MASTER_VOLUME);
    gain_.setCurrentAndTargetValue(lastTarget_);

    const juce::dsp::ProcessSpec spec
    {
        sampleRate_, static_cast<uint32_t>(juce::jmax(1, maximumBlockSize)), 2
    };
    limiter_.prepare(spec);
    limiter_.setThreshold(-1.0f);
    limiter_.setRelease(80.0f);
    limiter_.reset();
}

void MasterVolumeNode::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    const auto target = parameter(NEON_MASTER_VOLUME);
    if (std::abs(target - lastTarget_) > 0.0001f)
    {
        lastTarget_ = target;
        gain_.setTargetValue(target);
    }

    for (auto sample = 0; sample < buffer.getNumSamples(); ++sample)
    {
        const auto gain = gain_.getNextValue();
        for (auto channel = 0; channel < juce::jmin(2, buffer.getNumChannels()); ++channel)
            buffer.getWritePointer(channel)[sample] *= gain;
    }

    if (parameter(NEON_LIMITER_ENABLED) >= 0.5f)
    {
        juce::dsp::AudioBlock<float> block(buffer);
        const juce::dsp::ProcessContextReplacing<float> context(block);
        limiter_.process(context);
    }
}

UtilityNode::UtilityNode(ParameterBank& parameters)
    : StereoNode("Utility", parameters)
{
}

void UtilityNode::prepareToPlay(double sampleRate, int maximumBlockSize)
{
    StereoNode::prepareToPlay(sampleRate, maximumBlockSize);
    inputHistory_.fill(0.0f);
    outputHistory_.fill(0.0f);
    loudnessLowState_.fill(0.0f);
    loudnessHighState_.fill(0.0f);
}

void UtilityNode::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    const auto dcBlockEnabled = parameter(NEON_UTILITY_ENABLED) >= 0.5f;
    const auto loudnessEnabled = parameter(NEON_LOUDNESS_ENABLED) >= 0.5f;
    const auto amount = parameter(NEON_LOUDNESS_AMOUNT);
    const auto sampleRate = static_cast<float>(sampleRate_);
    const auto dcPole = std::exp(-2.0f * pi * 18.0f / sampleRate);
    const auto lowAlpha = 1.0f - std::exp(-2.0f * pi * 160.0f / sampleRate);
    const auto highAlpha = 1.0f - std::exp(-2.0f * pi * 4200.0f / sampleRate);
    const auto lowGain = std::pow(10.0f, 8.0f * amount * 0.05f);
    const auto highGain = std::pow(10.0f, 4.0f * amount * 0.05f);

    for (auto channel = 0; channel < juce::jmin(2, buffer.getNumChannels()); ++channel)
    {
        const auto index = static_cast<std::size_t>(channel);
        auto* samples = buffer.getWritePointer(channel);
        auto x1 = inputHistory_[index];
        auto y1 = outputHistory_[index];
        auto lowState = loudnessLowState_[index];
        auto highState = loudnessHighState_[index];

        for (auto i = 0; i < buffer.getNumSamples(); ++i)
        {
            const auto input = samples[i];
            lowState += lowAlpha * (input - lowState);
            highState += highAlpha * (input - highState);

            auto enhanced = input;
            if (loudnessEnabled)
            {
                const auto lowBand = lowState;
                const auto highBand = input - highState;
                enhanced += (lowGain - 1.0f) * lowBand
                          + (highGain - 1.0f) * highBand;
            }

            // Keep filter history warm while bypassed so switching either
            // utility on/off does not restore stale state or pop.
            const auto dcBlocked = enhanced - x1 + dcPole * y1;
            x1 = enhanced;
            y1 = dcBlocked;
            samples[i] = dcBlockEnabled ? dcBlocked : enhanced;
        }

        inputHistory_[index] = x1;
        outputHistory_[index] = y1;
        loudnessLowState_[index] = lowState;
        loudnessHighState_[index] = highState;
    }
}
} // namespace neon
