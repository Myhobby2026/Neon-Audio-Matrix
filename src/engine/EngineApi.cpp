#include "AudioEngine.h"
#include <Neon/EngineApi.h>

#include <algorithm>
#include <memory>

namespace
{
neon::AudioEngine* asEngine(NeonEngineHandle handle) noexcept
{
    return static_cast<neon::AudioEngine*>(handle);
}
}

extern "C"
{
uint32_t NEON_CALL neon_engine_abi_version(void)
{
    return NEON_ENGINE_ABI_VERSION;
}

NeonEngineHandle NEON_CALL neon_create_engine(void)
{
    try
    {
        return new neon::AudioEngine();
    }
    catch (...)
    {
        return nullptr;
    }
}

void NEON_CALL neon_destroy_engine(NeonEngineHandle engine)
{
    delete asEngine(engine);
}

int32_t NEON_CALL neon_prepare_engine(NeonEngineHandle engine,
                                     double sampleRate,
                                     int32_t maximumBlockSize)
{
    if (auto* processor = asEngine(engine); processor != nullptr
        && sampleRate > 0.0 && maximumBlockSize > 0)
    {
        processor->prepareToPlay(sampleRate, maximumBlockSize);
        return 1;
    }
    return 0;
}

void NEON_CALL neon_release_engine(NeonEngineHandle engine)
{
    if (auto* processor = asEngine(engine); processor != nullptr)
        processor->releaseResources();
}

void NEON_CALL neon_process_engine(NeonEngineHandle engine,
                                  const float* const* inputChannels,
                                  int32_t inputChannelCount,
                                  float* const* outputChannels,
                                  int32_t outputChannelCount,
                                  int32_t frameCount)
{
    if (auto* processor = asEngine(engine); processor != nullptr)
    {
        processor->processExternal(inputChannels, inputChannelCount,
                                   outputChannels, outputChannelCount, frameCount);
        return;
    }

    if (outputChannels == nullptr)
        return;

    for (int channel = 0; channel < outputChannelCount; ++channel)
        if (outputChannels[channel] != nullptr)
            std::fill_n(outputChannels[channel], juce::jmax(0, frameCount), 0.0f);
}

int32_t NEON_CALL neon_set_parameter(NeonEngineHandle engine,
                                     int32_t parameterId,
                                     float value)
{
    if (auto* processor = asEngine(engine); processor != nullptr)
        return processor->setEngineParameter(parameterId, value) ? 1 : 0;
    return 0;
}

float NEON_CALL neon_get_parameter(NeonEngineHandle engine, int32_t parameterId)
{
    if (auto* processor = asEngine(engine); processor != nullptr)
        return processor->getEngineParameter(parameterId);
    return 0.0f;
}

int32_t NEON_CALL neon_copy_spectrum(NeonEngineHandle engine,
                                     float* destination,
                                     int32_t capacity)
{
    if (auto* processor = asEngine(engine); processor != nullptr)
        return processor->copySpectrum(destination, capacity);
    return 0;
}

int32_t NEON_CALL neon_save_preset_json(NeonEngineHandle engine, const char* utf8Path)
{
    if (auto* processor = asEngine(engine); processor != nullptr && utf8Path != nullptr)
        return processor->savePreset(juce::File(juce::String::fromUTF8(utf8Path))) ? 1 : 0;
    return 0;
}

int32_t NEON_CALL neon_load_preset_json(NeonEngineHandle engine, const char* utf8Path)
{
    if (auto* processor = asEngine(engine); processor != nullptr && utf8Path != nullptr)
        return processor->loadPreset(juce::File(juce::String::fromUTF8(utf8Path))) ? 1 : 0;
    return 0;
}
} // extern "C"
