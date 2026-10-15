#include "MainComponent.h"

#include <array>

namespace
{
juce::Image createWindowIcon()
{
    juce::Image icon(juce::Image::ARGB, 64, 64, true);
    juce::Graphics g(icon);
    g.setColour(juce::Colour(0xff090e1b));
    g.fillRoundedRectangle(1.0f, 1.0f, 62.0f, 62.0f, 13.0f);
    g.setColour(juce::Colour(0xff35eaff).withAlpha(0.50f));
    g.drawRoundedRectangle(1.5f, 1.5f, 61.0f, 61.0f, 13.0f, 1.2f);

    const std::array<float, 5> heights {{ 22.0f, 34.0f, 44.0f, 34.0f, 22.0f }};
    const std::array<juce::Colour, 5> colours
    {{
        juce::Colour(0xff38edce), juce::Colour(0xff32dfff), juce::Colour(0xff7a8bff),
        juce::Colour(0xffba6aff), juce::Colour(0xfff34ccf)
    }};
    for (int i = 0; i < 5; ++i)
    {
        const auto x = 9.0f + static_cast<float>(i) * 9.5f;
        const auto height = heights[static_cast<std::size_t>(i)];
        g.setColour(colours[static_cast<std::size_t>(i)].withAlpha(0.85f));
        g.fillRoundedRectangle(x, 32.0f - height * 0.5f, 6.0f, height, 3.0f);
    }

    juce::Path wave;
    wave.startNewSubPath(8.0f, 42.0f);
    wave.lineTo(19.0f, 33.0f);
    wave.lineTo(29.0f, 39.0f);
    wave.lineTo(39.0f, 24.0f);
    wave.lineTo(49.0f, 32.0f);
    wave.lineTo(57.0f, 20.0f);
    g.setColour(juce::Colour(0xffeafaff).withAlpha(0.94f));
    g.strokePath(wave, juce::PathStrokeType(2.2f, juce::PathStrokeType::curved,
                                             juce::PathStrokeType::rounded));
    return icon;
}
}

class NeonAudioMatrixApplication final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "Neon Audio Matrix"; }
    const juce::String getApplicationVersion() override { return "1.0.0"; }
    bool moreThanOneInstanceAllowed() override { return true; }

    void initialise(const juce::String&) override
    {
        mainWindow_ = std::make_unique<MainWindow>(getApplicationName());
    }

    void shutdown() override
    {
        mainWindow_.reset();
    }

    void systemRequestedQuit() override
    {
        quit();
    }

private:
    class MainWindow final : public juce::DocumentWindow
    {
    public:
        explicit MainWindow(const juce::String& name)
            : juce::DocumentWindow(name,
                                   NeonLookAndFeel::background(),
                                   juce::DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar(false);
            setIcon(createWindowIcon());
            setResizable(true, true);

            const auto* primaryDisplay = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay();
            auto workArea = primaryDisplay != nullptr
                          ? primaryDisplay->userArea
                          : juce::Rectangle<int>(0, 0, 1280, 900);
            if (workArea.isEmpty())
                workArea = juce::Rectangle<int>(0, 0, 1280, 900);

            constexpr int screenMargin = 20;
            const auto availableWidth = juce::jmax(1, workArea.getWidth() - screenMargin * 2);
            const auto availableHeight = juce::jmax(1, workArea.getHeight() - screenMargin * 2);
            const auto initialWidth = juce::jmin(1240, availableWidth);
            const auto initialHeight = juce::jmin(820, availableHeight);
            setResizeLimits(juce::jmin(980, initialWidth), juce::jmin(680, initialHeight),
                            availableWidth, availableHeight);

            setContentOwned(new MainComponent(), true);
            setBounds(workArea.withSizeKeepingCentre(initialWidth, initialHeight));
            setVisible(true);
        }

        void closeButtonPressed() override
        {
            juce::JUCEApplication::getInstance()->systemRequestedQuit();
        }
    };

    std::unique_ptr<MainWindow> mainWindow_;
};

START_JUCE_APPLICATION(NeonAudioMatrixApplication)
