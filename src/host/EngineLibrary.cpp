#include "EngineLibrary.h"

namespace
{
template <typename FunctionType>
FunctionType loadFunction(juce::DynamicLibrary& library, const char* name)
{
    return reinterpret_cast<FunctionType>(library.getFunction(name));
}
}

EngineLibrary::~EngineLibrary()
{
    close();
}

bool EngineLibrary::open(const juce::File& dllFile)
{
    close();
    error_.clear();

    if (!library_.open(dllFile.getFullPathName()))
    {
        error_ = "Could not load " + dllFile.getFullPathName();
        return false;
    }

    const auto create = loadFunction<CreateFn>(library_, "neon_create_engine");
    destroy_ = loadFunction<DestroyFn>(library_, "neon_destroy_engine");
    version_ = loadFunction<VersionFn>(library_, "neon_engine_abi_version");
    prepare_ = loadFunction<PrepareFn>(library_, "neon_prepare_engine");
    release_ = loadFunction<ReleaseFn>(library_, "neon_release_engine");
    process_ = loadFunction<ProcessFn>(library_, "neon_process_engine");
    setParameter_ = loadFunction<SetParamFn>(library_, "neon_set_parameter");
    getParameter_ = loadFunction<GetParamFn>(library_, "neon_get_parameter");
    copySpectrum_ = loadFunction<SpectrumFn>(library_, "neon_copy_spectrum");
    savePreset_ = loadFunction<PresetFn>(library_, "neon_save_preset_json");
    loadPreset_ = loadFunction<PresetFn>(library_, "neon_load_preset_json");

    if (create == nullptr || destroy_ == nullptr || version_ == nullptr || prepare_ == nullptr
        || release_ == nullptr || process_ == nullptr || setParameter_ == nullptr || getParameter_ == nullptr
        || copySpectrum_ == nullptr || savePreset_ == nullptr || loadPreset_ == nullptr)
    {
        error_ = "NeonEngine.dll is missing one or more required C-ABI exports.";
        close();
        return false;
    }

    if (version_() != NEON_ENGINE_ABI_VERSION)
    {
        error_ = "NeonEngine.dll ABI version does not match this host.";
        close();
        return false;
    }

    engine_ = create();
    if (engine_ == nullptr)
    {
        error_ = "NeonEngine.dll could not create an audio engine instance.";
        close();
        return false;
    }

    return true;
}

void EngineLibrary::close()
{
    if (engine_ != nullptr && destroy_ != nullptr)
        destroy_(engine_);

    engine_ = nullptr;
    version_ = nullptr;
    destroy_ = nullptr;
    prepare_ = nullptr;
    release_ = nullptr;
    process_ = nullptr;
    setParameter_ = nullptr;
    getParameter_ = nullptr;
    copySpectrum_ = nullptr;
    savePreset_ = nullptr;
    loadPreset_ = nullptr;
    library_.close();
}

bool EngineLibrary::prepare(double sampleRate, int maximumBlockSize) noexcept
{
    return engine_ != nullptr && prepare_ != nullptr
        && prepare_(engine_, sampleRate, maximumBlockSize) != 0;
}

void EngineLibrary::release() noexcept
{
    if (engine_ != nullptr && release_ != nullptr)
        release_(engine_);
}

void EngineLibrary::process(const float* const* inputChannels,
                            int inputChannelCount,
                            float* const* outputChannels,
                            int outputChannelCount,
                            int frameCount) noexcept
{
    if (engine_ != nullptr && process_ != nullptr)
        process_(engine_, inputChannels, inputChannelCount,
                 outputChannels, outputChannelCount, frameCount);
}

bool EngineLibrary::setParameter(int parameterId, float value) noexcept
{
    return engine_ != nullptr && setParameter_ != nullptr
        && setParameter_(engine_, parameterId, value) != 0;
}

float EngineLibrary::getParameter(int parameterId) const noexcept
{
    return engine_ != nullptr && getParameter_ != nullptr
        ? getParameter_(engine_, parameterId) : 0.0f;
}

int EngineLibrary::copySpectrum(float* destination, int capacity) const noexcept
{
    return engine_ != nullptr && copySpectrum_ != nullptr
        ? copySpectrum_(engine_, destination, capacity) : 0;
}

bool EngineLibrary::savePreset(const juce::File& file) const
{
    if (engine_ == nullptr || savePreset_ == nullptr)
        return false;

    const auto fullPath = file.getFullPathName();
    return savePreset_(engine_, fullPath.toRawUTF8()) != 0;
}

bool EngineLibrary::loadPreset(const juce::File& file) const
{
    if (engine_ == nullptr || loadPreset_ == nullptr)
        return false;

    const auto fullPath = file.getFullPathName();
    return loadPreset_(engine_, fullPath.toRawUTF8()) != 0;
}
