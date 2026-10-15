#pragma once

#include <array>
#include <atomic>
#include <memory>
#include <vector>

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_extra/juce_gui_extra.h>

#include <Neon/EngineApi.h>

#include "EngineLibrary.h"
#include "HostAudioCallback.h"
#include "NeonLookAndFeel.h"

class RenderExportPage final : public juce::Component,
                               private juce::Timer,
                               private juce::ListBoxModel
{
public:
    RenderExportPage(EngineLibrary& liveEngine,
                     HostAudioCallback& audioCallback,
                     const juce::File& engineDll);
    ~RenderExportPage() override;

    void paint(juce::Graphics& graphics) override;
    void resized() override;

    int getNumRows() override;
    void paintListBoxItem(int rowNumber, juce::Graphics& graphics,
                          int width, int height, bool rowIsSelected) override;
    void selectedRowsChanged(int lastRowSelected) override;
    void listBoxItemDoubleClicked(int row, const juce::MouseEvent& event) override;

private:
    enum class SurroundOutputMode : int
    {
        stereo = 1,
        virtualStereo = 2,
        surround51 = 3
    };

    struct BatchSettings
    {
        std::vector<juce::File> files;
        juce::File outputFolder;
        std::array<float, NEON_PARAMETER_COUNT> parameters {};
        int sampleRate = 48000;
        int bitDepth = 24;
        bool flac = false;
        bool mp3 = false;
        int bitRateKbps = 192;
        SurroundOutputMode surroundMode = SurroundOutputMode::stereo;
        bool normalize = true;
        bool effectsEnabled = true;
    };

    class RenderWorker;
    class IntegratedLoudnessMeter;

    void timerCallback() override;
    void addFiles();
    void chooseOutputFolder();
    void clearFiles();
    void togglePlayback();
    void playTrack(int index, bool restartFromBeginning);
    bool loadPreviewTrack(int index);
    void pausePreview();
    void stopPreview();
    void playNextTrack();
    void playPreviousTrack();
    void fastForward();
    void renderAll();
    void openOutputFolder();
    void openYoutubeLink();
    void updateFormatControls();

    void renderBatch(RenderWorker& worker, const BatchSettings& settings);
    bool renderOneFile(RenderWorker& worker, const BatchSettings& settings,
                       juce::AudioFormatManager& formats, int fileIndex,
                       const juce::File& inputFile, juce::String& error);
    void setWorkerProgress(double progress) noexcept;
    void setWorkerStatus(const juce::String& status);
    juce::String getWorkerStatus() const;
    void setRenderControlsEnabled(bool enabled);
    juce::String formatTime(double seconds) const;

    EngineLibrary& liveEngine_;
    HostAudioCallback& audioCallback_;
    juce::File engineDll_;
    EngineLibrary offlineEngine_;

    juce::AudioFormatManager previewFormats_;
    juce::TimeSliceThread readAheadThread_ { "Neon Render Read Ahead" };
    juce::AudioTransportSource transportSource_;
    std::unique_ptr<juce::AudioFormatReaderSource> previewReaderSource_;

    std::vector<juce::File> files_;
    juce::File outputFolder_;
    std::unique_ptr<juce::FileChooser> activeChooser_;
    int currentTrackIndex_ = -1;
    bool previewIsActive_ = false;
    bool isSeeking_ = false;

    std::unique_ptr<RenderWorker> renderWorker_;
    std::atomic<double> workerProgress_ { 0.0 };
    mutable juce::CriticalSection workerStatusLock_;
    juce::String workerStatus_;
    double progressValue_ = 0.0;

    juce::ListBox filesList_;
    juce::TextButton addFilesButton_ { "+ ADD FILES" };
    juce::TextButton clearFilesButton_ { "CLEAR" };
    juce::TextButton previousButton_ { "<<" };
    juce::TextButton playPauseButton_ { "PLAY" };
    juce::TextButton stopButton_ { "STOP" };
    juce::TextButton nextButton_ { "NEXT >>" };
    juce::TextButton fastForwardButton_ { "FF +10s" };
    juce::TextButton wavButton_ { "WAV  -  LOSSLESS" };
    juce::TextButton flacButton_ { "FLAC  -  COMPRESSED" };
    juce::TextButton mp3Button_ { "MP3  -  COMPRESSED" };
    juce::TextButton openYoutubeButton_ { "OPEN YOUTUBE" };
    juce::TextButton chooseFolderButton_ { "CHOOSE FOLDER" };
    juce::TextEditor youtubeUrlEditor_;
    juce::TextButton renderAllButton_ { "RENDER ALL" };
    juce::TextButton openFolderButton_ { "OPEN FOLDER" };
    juce::ToggleButton effectsToggle_ { "FX" };
    juce::ToggleButton normalizeToggle_ { "NORMALIZE  -14 LUFS" };
    juce::ComboBox surroundModeBox_;
    juce::ComboBox sampleRateBox_;
    juce::ComboBox bitDepthBox_;
    juce::ComboBox bitRateBox_;
    juce::Label fileCountLabel_;
    juce::Label outputFolderLabel_;
    juce::Label trackTimeLabel_;
    juce::Label formatsLabel_;
    juce::Label statusLabel_;
    juce::Slider timeline_;
    juce::ProgressBar progressBar_;
};
