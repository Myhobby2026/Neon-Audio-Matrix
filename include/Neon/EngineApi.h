#pragma once

#include <stdint.h>

#if defined(_WIN32)
    #if defined(NEON_ENGINE_BUILD)
        #define NEON_API __declspec(dllexport)
    #else
        #define NEON_API
    #endif
    #define NEON_CALL __cdecl
#else
    #define NEON_API
    #define NEON_CALL
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef void* NeonEngineHandle;

enum NeonParameterId
{
    // Existing IDs stay stable; the three legacy EQ IDs now provide the 31 Hz,
    // 1 kHz, and 16 kHz graphic bands respectively. ABI 4 added advanced reverb,
    // ABI 5 added delay crossover/Loudness, and ABI 6 adds Field Surround controls.
    NEON_EQ_LOW_DB = 0,
    NEON_EQ_MID_DB,
    NEON_EQ_HIGH_DB,
    NEON_REVERB_MIX,
    NEON_REVERB_ROOM,
    NEON_SURROUND_WIDTH,
    NEON_SURROUND_SPREAD = NEON_SURROUND_WIDTH, // legacy ID, new spread scale
    NEON_DELAY_TIME_MS,
    NEON_DELAY_FEEDBACK,
    NEON_DELAY_MIX,
    NEON_DIMENSION_AMOUNT,
    NEON_MASTER_VOLUME,
    NEON_UTILITY_ENABLED,
    NEON_EQ_62_DB,
    NEON_EQ_125_DB,
    NEON_EQ_250_DB,
    NEON_EQ_500_DB,
    NEON_EQ_2K_DB,
    NEON_EQ_4K_DB,
    NEON_EQ_8K_DB,
    NEON_PREAMP_DB,
    NEON_EQ_BAND_Q,
    NEON_SPECTRUM_ENABLED,
    NEON_LIMITER_ENABLED,
    NEON_FIELD_ENABLED,
    NEON_DIMENSION_ENABLED,
    NEON_REVERB_ENABLED,
    NEON_DELAY_ENABLED,
    NEON_REVERB_DECAY_SECONDS,
    NEON_REVERB_LOW_CROSSOVER_HZ,
    NEON_REVERB_HIGH_CROSSOVER_HZ,
    NEON_REVERB_LOW_DECAY_MULTIPLIER,
    NEON_REVERB_HIGH_DECAY_MULTIPLIER,
    NEON_REVERB_PRE_DELAY_MS,
    NEON_REVERB_LOW_DAMPING,
    NEON_REVERB_HIGH_DAMPING,
    NEON_REVERB_STEREO_WIDTH,
    NEON_REVERB_ASPECT_RATIO,
    NEON_REVERB_DIFFUSION,
    NEON_DELAY_LOW_CROSSOVER_HZ,
    NEON_DELAY_HIGH_CROSSOVER_HZ,
    NEON_LOUDNESS_ENABLED,
    NEON_LOUDNESS_AMOUNT,
    NEON_FIELD_AMOUNT,
    NEON_SURROUND_BASS,
    NEON_SURROUND_CENTER_DB,
    NEON_PARAMETER_COUNT
};

enum
{
    NEON_SPECTRUM_BANDS = 64,
    NEON_ENGINE_ABI_VERSION = 6
};

NEON_API uint32_t NEON_CALL neon_engine_abi_version(void);
NEON_API NeonEngineHandle NEON_CALL neon_create_engine(void);
NEON_API void NEON_CALL neon_destroy_engine(NeonEngineHandle engine);
NEON_API int32_t NEON_CALL neon_prepare_engine(NeonEngineHandle engine,
                                               double sampleRate,
                                               int32_t maximumBlockSize);
NEON_API void NEON_CALL neon_release_engine(NeonEngineHandle engine);
NEON_API void NEON_CALL neon_process_engine(NeonEngineHandle engine,
                                             const float* const* inputChannels,
                                             int32_t inputChannelCount,
                                             float* const* outputChannels,
                                             int32_t outputChannelCount,
                                             int32_t frameCount);
NEON_API int32_t NEON_CALL neon_set_parameter(NeonEngineHandle engine,
                                               int32_t parameterId,
                                               float value);
NEON_API float NEON_CALL neon_get_parameter(NeonEngineHandle engine,
                                             int32_t parameterId);
NEON_API int32_t NEON_CALL neon_copy_spectrum(NeonEngineHandle engine,
                                               float* destination,
                                               int32_t capacity);
NEON_API int32_t NEON_CALL neon_save_preset_json(NeonEngineHandle engine,
                                                  const char* utf8Path);
NEON_API int32_t NEON_CALL neon_load_preset_json(NeonEngineHandle engine,
                                                  const char* utf8Path);

#ifdef __cplusplus
} // extern "C"
#endif
