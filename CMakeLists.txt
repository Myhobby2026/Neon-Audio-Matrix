cmake_minimum_required(VERSION 4.2)
project(NeonAudioMatrix VERSION 1.0.0 LANGUAGES C CXX)

if (NOT WIN32)
    message(FATAL_ERROR "Neon Audio Matrix is a Windows 10/11 WASAPI application. Configure it with Visual Studio 2026 on Windows.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

enable_language(RC)

include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.15
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(JUCE)

# The engine is deliberately a plain C-ABI DLL, not a VST. The GUI host loads it
# by path through juce::DynamicLibrary, so no JUCE/C++ objects cross the DLL edge.
add_library(NeonEngine SHARED
    src/engine/AudioEngine.cpp
    src/engine/Nodes.cpp
    src/engine/EngineApi.cpp
)

target_include_directories(NeonEngine
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/engine
)

target_compile_definitions(NeonEngine PRIVATE
    NEON_ENGINE_BUILD=1
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(NeonEngine PRIVATE
    juce::juce_audio_processors
    juce::juce_dsp
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
)

juce_add_gui_app(NeonHost
    PRODUCT_NAME "Neon Audio Matrix"
    COMPANY_NAME "Neon Audio"
    BUNDLE_ID    "audio.neon.matrix"
)

# Embed the project icon in the Windows executable/resource metadata.
configure_file(resources/NeonAudioMatrix.rc.in
               "${CMAKE_CURRENT_BINARY_DIR}/NeonAudioMatrix.rc" @ONLY)
target_sources(NeonHost PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/NeonAudioMatrix.rc")

# Main application sources
target_sources(NeonHost PRIVATE
    src/host/Main.cpp
    src/host/MainComponent.cpp
    src/host/RenderExportPage.cpp
    src/host/EngineLibrary.cpp
    src/host/HostAudioCallback.cpp
    src/host/WasapiLoopback.cpp
    src/host/NeonLookAndFeel.cpp
)

target_include_directories(NeonHost PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src/host
)

target_compile_definitions(NeonHost PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_WASAPI=1
    JUCE_USE_MP3AUDIOFORMAT=1
    JUCE_USE_FLAC=1
    JUCE_USE_OGGVORBIS=1
)

target_link_libraries(NeonHost PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
)

# Native loopback uses Windows Core Audio; MP3 export uses the built-in Media Foundation encoder.
target_link_libraries(NeonHost PRIVATE ole32 uuid propsys mfplat mfreadwrite mfuuid)

# Make sure the DLL is built and placed next to the executable for DynamicLibrary.
add_dependencies(NeonHost NeonEngine)
add_custom_command(TARGET NeonHost POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:NeonEngine>
            $<TARGET_FILE_DIR:NeonHost>
    COMMENT "Copying NeonEngine.dll next to NeonHost"
)

set_target_properties(NeonEngine PROPERTIES
    OUTPUT_NAME "NeonEngine"
    WINDOWS_EXPORT_ALL_SYMBOLS OFF
)
