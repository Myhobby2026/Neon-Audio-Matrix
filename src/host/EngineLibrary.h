#pragma once

#include <atomic>

#include <juce_core/juce_core.h>
#include <Neon/EngineApi.h>

class EngineLibrary final
{
public:
    EngineLibrary() = default;
    ~EngineLibrary();

    EngineLibrary(const EngineLibrary&) = delete;
    EngineLibrary& operator=(const EngineLibrary&) = delete;

    bool open(const juce::File& dllFile);
    void close();
    bool isLoaded() const noexcept { return engine_ != nullptr; }
    const juce::String& getError() const noexcept { return error_; }

    bool prepare(double sampleRate, int maximumBlockSize) noexcept;
    void release() noexcept;
    void process(const float* const* inputChannels,
                 int inputChannelCount,
                 float* const* outputChannels,
                 int outputChannelCount,
                 int frameCount) noexcept;

    bool setParameter(int parameterId, float value) noexcept;
    float getParameter(int parameterId) const noexcept;
    int copySpectrum(float* destination, int capacity) const noexcept;
    bool savePreset(const juce::File& file) const;
    bool loadPreset(const juce::File& file) const;

private:
    using CreateFn   = NeonEngineHandle (NEON_CALL*)();
    using DestroyFn  = void (NEON_CALL*)(NeonEngineHandle);
    using VersionFn  = uint32_t (NEON_CALL*)();
    using PrepareFn  = int32_t (NEON_CALL*)(NeonEngineHandle, double, int32_t);
    using ReleaseFn  = void (NEON_CALL*)(NeonEngineHandle);
    using ProcessFn  = void (NEON_CALL*)(NeonEngineHandle, const float* const*, int32_t,
                                         float* const*, int32_t, int32_t);
    using SetParamFn = int32_t (NEON_CALL*)(NeonEngineHandle, int32_t, float);
    using GetParamFn = float (NEON_CALL*)(NeonEngineHandle, int32_t);
    using SpectrumFn = int32_t (NEON_CALL*)(NeonEngineHandle, float*, int32_t);
    using PresetFn   = int32_t (NEON_CALL*)(NeonEngineHandle, const char*);

    juce::DynamicLibrary library_;
    NeonEngineHandle engine_ = nullptr;
    VersionFn version_ = nullptr;
    DestroyFn destroy_ = nullptr;
    PrepareFn prepare_ = nullptr;
    ReleaseFn release_ = nullptr;
    ProcessFn process_ = nullptr;
    SetParamFn setParameter_ = nullptr;
    GetParamFn getParameter_ = nullptr;
    SpectrumFn copySpectrum_ = nullptr;
    PresetFn savePreset_ = nullptr;
    PresetFn loadPreset_ = nullptr;
    juce::String error_;
};
