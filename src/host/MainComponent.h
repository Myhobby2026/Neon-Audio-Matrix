#pragma once

#include <memory>

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_extra/juce_gui_extra.h>

#include "EngineLibrary.h"
#include "HostAudioCallback.h"
#include "NeonLookAndFeel.h"
#include "WasapiLoopback.h"

class WasapiSharedDeviceManager final : public juce::AudioDeviceManager
{
public:
    void createAudioDeviceTypes(juce::OwnedArray<juce::AudioIODeviceType>& types) override;
};

class MainComponent final : public juce::Component,
                            private juce::Timer
{
public:
    MainComponent();
    ~MainComponent() override;

    EngineLibrary& getEngine() noexcept { return engine_; }
    juce::AudioDeviceManager& getDeviceManager() noexcept { return deviceManager_; }
    HostAudioCallback& getAudioCallback() noexcept { return audioCallback_; }
    void setCaptureMode(int modeId);
    int getCaptureMode() const noexcept { return captureModeId_; }
    juce::String getAudioStatus() const;
    juce::String getEngineStatus() const;

    void paint(juce::Graphics& graphics) override;
    void resized() override;

private:
    void initialiseAudio();
    void timerCallback() override;

    // Keep the DLL loaded until after the audio callback is removed and stopped.
    EngineLibrary engine_;
    SpscStereoFifo loopbackFifo_;
    WasapiLoopbackCapture loopback_;
    WasapiSharedDeviceManager deviceManager_;
    HostAudioCallback audioCallback_;
    NeonLookAndFeel lookAndFeel_;
    juce::TabbedComponent tabs_ { juce::TabbedButtonBar::TabsAtTop };
    juce::TextButton listenOriginalButton_ { "LISTEN ORIGINAL" };

    juce::String audioInitError_;
    juce::String liveStatus_ { "STARTING AUDIO ENGINE" };
    int captureModeId_ = 1; // 1 = WASAPI loopback, 2 = selected WASAPI input
};
