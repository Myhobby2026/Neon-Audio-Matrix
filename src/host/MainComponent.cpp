#include "MainComponent.h"
#include "RenderExportPage.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

#include <juce_audio_formats/juce_audio_formats.h>

namespace
{
constexpr int headerHeight = 78;

void styleLabel(juce::Label& label, float fontSize, juce::Colour colour, bool bold = false)
{
    label.setColour(juce::Label::textColourId, colour);
    label.setFont(juce::Font(juce::FontOptions(fontSize, bold ? juce::Font::bold : juce::Font::plain)));
}

void styleButton(juce::TextButton& button)
{
    button.setColour(juce::TextButton::buttonColourId, NeonLookAndFeel::panelRaised());
    button.setColour(juce::TextButton::buttonOnColourId, NeonLookAndFeel::cyan().withAlpha(0.20f));
    button.setColour(juce::TextButton::textColourOffId, NeonLookAndFeel::textPrimary());
    button.setColour(juce::TextButton::textColourOnId, NeonLookAndFeel::cyan());
}

int reverbKnobRowHeight(int componentHeight) noexcept
{
    constexpr int verticalInset = 91;
    return juce::jmax(1, (componentHeight - verticalInset * 2) / 3);
}

class NeonKnob final : public juce::Component
{
public:
    NeonKnob(EngineLibrary& engine, int parameterId, juce::String title,
             float minimum, float maximum, float interval, juce::String unit,
             juce::Colour tint = NeonLookAndFeel::cyan(),
             std::function<void()> onEdited = {}, bool valueAbove = false)
        : engine_(engine), parameterId_(parameterId), title_(std::move(title)),
          unit_(std::move(unit)), tint_(tint), onEdited_(std::move(onEdited)),
          valueAbove_(valueAbove)
    {
        slider_.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        slider_.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        slider_.setRange(minimum, maximum, interval);
        if (unit_ == "Hz" && minimum > 0.0f && maximum > minimum)
            slider_.setSkewFactorFromMidPoint(std::sqrt(minimum * maximum));
        slider_.setValue(engine_.getParameter(parameterId_), juce::dontSendNotification);
        slider_.setColour(juce::Slider::rotarySliderFillColourId, tint_);
        slider_.setColour(juce::Slider::rotarySliderOutlineColourId, tint_.withAlpha(0.25f));
        slider_.setDoubleClickReturnValue(true, engine_.getParameter(parameterId_));
        slider_.onValueChange = [this]
        {
            engine_.setParameter(parameterId_, static_cast<float>(slider_.getValue()));
            repaint();
            if (onEdited_)
                onEdited_();
        };
        addAndMakeVisible(slider_);
    }

    void refreshFromEngine()
    {
        const auto current = engine_.getParameter(parameterId_);
        if (std::abs(current - static_cast<float>(slider_.getValue())) > 0.0001f)
            slider_.setValue(current, juce::dontSendNotification);
        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        const auto value = slider_.getValue();
        juce::String text;
        if (unit_ == "%")
            text = juce::String(juce::roundToInt(value * 100.0)) + " " + unit_;
        else if (unit_ == "dB")
            text = juce::String(value, 1) + " " + unit_;
        else if (unit_ == "ms")
            text = juce::String(juce::roundToInt(value)) + " " + unit_;
        else if (unit_ == "s")
            text = juce::String(value, 1) + " s";
        else if (unit_ == "Hz")
            text = value >= 1000.0 ? juce::String(value / 1000.0, 1) + " kHz"
                                   : juce::String(juce::roundToInt(value)) + " Hz";
        else if (unit_ == "x")
            text = juce::String(value, 2) + "x";
        else if (unit_ == "Q")
            text = juce::String(value, 2);
        else
            text = juce::String(value, 2);

        g.setColour(valueAbove_ ? NeonLookAndFeel::textPrimary() : NeonLookAndFeel::textMuted());
        g.setFont(juce::Font(juce::FontOptions(valueAbove_ ? 12.5f : 11.5f, juce::Font::bold)));
        g.drawFittedText(valueAbove_ ? text : title_.toUpperCase(), getLocalBounds().removeFromTop(22),
                         juce::Justification::centred, 1);
        g.setColour(valueAbove_ ? NeonLookAndFeel::textMuted() : NeonLookAndFeel::textPrimary());
        g.setFont(juce::Font(juce::FontOptions(valueAbove_ ? 11.5f : 12.5f, juce::Font::bold)));
        g.drawFittedText(valueAbove_ ? title_.toUpperCase() : text,
                         getLocalBounds().removeFromBottom(21), juce::Justification::centred, 1);
    }

    void resized() override
    {
        auto bounds = getLocalBounds();
        bounds.removeFromTop(valueAbove_ ? 25 : 20);
        bounds.removeFromBottom(valueAbove_ ? 23 : 20);
        slider_.setBounds(bounds.reduced(3));
    }

private:
    EngineLibrary& engine_;
    int parameterId_;
    juce::String title_;
    juce::String unit_;
    juce::Colour tint_;
    std::function<void()> onEdited_;
    bool valueAbove_ = false;
    juce::Slider slider_;
};

const std::array<int, 10> graphicEqParameterIds
{{
    NEON_EQ_LOW_DB, NEON_EQ_62_DB, NEON_EQ_125_DB, NEON_EQ_250_DB, NEON_EQ_500_DB,
    NEON_EQ_MID_DB, NEON_EQ_2K_DB, NEON_EQ_4K_DB, NEON_EQ_8K_DB, NEON_EQ_HIGH_DB
}};

const std::array<float, 10> graphicEqFrequencies
{{ 31.0f, 62.0f, 125.0f, 250.0f, 500.0f, 1000.0f, 2000.0f, 4000.0f, 8000.0f, 16000.0f }};

const std::array<const char*, 10> graphicEqLabels
{{ "31", "62", "125", "250", "500", "1k", "2k", "4k", "8k", "16k" }};

const std::array<juce::Colour, 10> graphicEqColours
{{
    juce::Colour(0xff44f5ce), juce::Colour(0xff35e8d0), juce::Colour(0xff25dce5),
    juce::Colour(0xff26d1f2), juce::Colour(0xff25c5ff), juce::Colour(0xff38c7ff),
    juce::Colour(0xff62c5ff), juce::Colour(0xffa27dff), juce::Colour(0xffda55e8),
    juce::Colour(0xffff49c9)
}};

constexpr std::array<int, 3> otherSpatialEffectEnableParameterIds
{{ NEON_DIMENSION_ENABLED, NEON_DELAY_ENABLED, NEON_LOUDNESS_ENABLED }};

constexpr std::array<const char*, 3> otherSpatialEffectEnableLabels
{{ "DIMENSION", "STEREO DELAY", "LOUDNESS" }};

class NeonSwitch final : public juce::Component
{
public:
    NeonSwitch(juce::String label, bool initialValue, std::function<void(bool)> onChanged,
               bool showStateText = false)
        : label_(std::move(label)), value_(initialValue), onChanged_(std::move(onChanged)),
          showStateText_(showStateText)
    {
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
    }

    bool getValue() const noexcept { return value_; }

    void setValue(bool newValue, bool notify = false)
    {
        if (value_ == newValue)
            return;

        value_ = newValue;
        repaint();
        if (notify && onChanged_)
            onChanged_(value_);
    }

    void paint(juce::Graphics& g) override
    {
        const auto track = juce::Rectangle<float>(2.0f, (static_cast<float>(getHeight()) - 23.0f) * 0.5f,
                                                  48.0f, 23.0f);
        g.setColour(value_ ? NeonLookAndFeel::cyan().withAlpha(0.42f)
                           : juce::Colour(0xff263247));
        g.fillRoundedRectangle(track, 12.0f);
        g.setColour(value_ ? NeonLookAndFeel::cyan().withAlpha(0.92f)
                           : NeonLookAndFeel::textMuted().withAlpha(0.45f));
        g.drawRoundedRectangle(track.reduced(0.5f), 12.0f, 1.0f);

        const auto knobX = value_ ? track.getRight() - 20.0f : track.getX() + 3.0f;
        g.setColour(value_ ? NeonLookAndFeel::cyan() : NeonLookAndFeel::textMuted());
        g.fillEllipse(knobX, track.getY() + 3.0f, 17.0f, 17.0f);
        g.setColour(NeonLookAndFeel::textPrimary());
        g.setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::bold)));
        const auto stateWidth = showStateText_ ? 38 : 0;
        g.drawFittedText(label_, 57, 0, juce::jmax(0, getWidth() - 61 - stateWidth), getHeight(),
                         juce::Justification::centredLeft, 1);
        if (showStateText_)
        {
            g.setColour(value_ ? NeonLookAndFeel::lime() : NeonLookAndFeel::textMuted());
            g.setFont(juce::Font(juce::FontOptions(9.0f, juce::Font::bold)));
            g.drawText(value_ ? "ON" : "OFF", getWidth() - 38, 0, 36, getHeight(),
                       juce::Justification::centredRight);
        }
    }

    void mouseUp(const juce::MouseEvent&) override { setValue(!value_, true); }

private:
    juce::String label_;
    bool value_ = false;
    std::function<void(bool)> onChanged_;
    bool showStateText_ = false;
};

class FieldSurroundPanel final : public juce::Component,
                                private juce::Timer
{
public:
    explicit FieldSurroundPanel(EngineLibrary& engine)
        : engine_(engine)
    {
        addKnob(0, NEON_FIELD_AMOUNT, "FIELD", 0.0f, 1.0f, 0.01f, "%",
                NeonLookAndFeel::cyan());
        addKnob(1, NEON_SURROUND_SPREAD, "SPREAD", 0.5f, 2.5f, 0.01f, "x",
                NeonLookAndFeel::cyan());
        addKnob(2, NEON_SURROUND_BASS, "BASS", 0.0f, 1.0f, 0.01f, "%",
                juce::Colour(0xffff35ae));
        addKnob(3, NEON_SURROUND_CENTER_DB, "CENTER", -12.0f, 6.0f, 0.1f, "dB",
                NeonLookAndFeel::cyan());
        masterKnob_ = std::make_unique<NeonKnob>(engine_, NEON_MASTER_VOLUME,
                                                 "MASTER VOLUME", 0.0f, 1.5f, 0.01f, "%",
                                                 NeonLookAndFeel::lime(), std::function<void()> {}, true);
        addAndMakeVisible(*masterKnob_);
        startTimerHz(8);
    }

    void setKnobLayout(int cellWidth, int cellHeight)
    {
        knobCellWidth_ = juce::jmax(1, cellWidth);
        knobCellHeight_ = juce::jmax(1, cellHeight);
        resized();
    }

    void paint(juce::Graphics& g) override
    {
        const auto bounds = getLocalBounds().toFloat().reduced(0.5f);
        g.setColour(NeonLookAndFeel::panel());
        g.fillRoundedRectangle(bounds, 16.0f);
        g.setColour(NeonLookAndFeel::cyan().withAlpha(0.18f));
        g.drawRoundedRectangle(bounds, 16.0f, 1.0f);

        const auto normalKnobY = getHeight() - knobCellHeight_ - 8;
        const auto masterHeight = knobCellHeight_ + 60;
        const auto masterKnobY = getHeight() - masterHeight - 8;
        const auto cellWidth = juce::jmin(knobCellWidth_, juce::jmax(1, getWidth() / 5));
        const std::array<juce::Colour, 4> accents
        {{ NeonLookAndFeel::cyan(), NeonLookAndFeel::cyan(),
           juce::Colour(0xffff35ae), NeonLookAndFeel::cyan() }};
        for (std::size_t i = 0; i < knobs_.size(); ++i)
        {
            const auto x = static_cast<float>(i) * static_cast<float>(cellWidth);
            const auto card = juce::Rectangle<float>(x + 3.0f,
                                                       static_cast<float>(normalKnobY) + 3.0f,
                                                       juce::jmax(1.0f, static_cast<float>(cellWidth) - 6.0f),
                                                       juce::jmax(1.0f, static_cast<float>(knobCellHeight_) - 6.0f));
            drawKnobCard(g, card, accents[i]);
        }
        const auto masterX = static_cast<float>(cellWidth * 4);
        const auto masterCard = juce::Rectangle<float>(masterX + 3.0f,
                                                         static_cast<float>(masterKnobY) + 3.0f,
                                                         juce::jmax(1.0f, static_cast<float>(getWidth())
                                                                             - masterX - 6.0f),
                                                         juce::jmax(1.0f, static_cast<float>(masterHeight) - 6.0f));
        drawKnobCard(g, masterCard, NeonLookAndFeel::lime());

        const auto displayTop = 20.0f;
        const auto displayBottom = juce::jmax(displayTop + 80.0f,
                                              static_cast<float>(masterKnobY) - 13.0f);
        const auto display = juce::Rectangle<float>(24.0f, displayTop,
                                                     juce::jmax(40.0f, static_cast<float>(getWidth()) - 48.0f),
                                                     displayBottom - displayTop);
        const auto centerX = display.getCentreX();
        const auto centerY = display.getCentreY();
        const auto field = engine_.getParameter(NEON_FIELD_AMOUNT);
        const auto spread = engine_.getParameter(NEON_SURROUND_SPREAD);
        const auto bass = engine_.getParameter(NEON_SURROUND_BASS);
        const auto centerDb = engine_.getParameter(NEON_SURROUND_CENTER_DB);
        const auto spreadNorm = juce::jlimit(0.0f, 1.0f, (spread - 0.5f) / 2.0f);
        const auto ellipseWidth = display.getWidth() * (0.62f + 0.31f * spreadNorm);
        const auto ellipseHeight = juce::jlimit(42.0f, display.getHeight() * 0.52f,
                                                 display.getHeight() * (0.27f + 0.07f * field));
        const auto ellipse = juce::Rectangle<float>(centerX - ellipseWidth * 0.5f,
                                                      centerY - ellipseHeight * 0.5f,
                                                      ellipseWidth, ellipseHeight);

        g.setColour(NeonLookAndFeel::cyan().withAlpha(0.025f));
        g.fillRoundedRectangle(display, 14.0f);
        g.setColour(NeonLookAndFeel::cyan().withAlpha(0.07f));
        g.drawRoundedRectangle(display, 14.0f, 1.0f);
        g.setColour(NeonLookAndFeel::cyan().withAlpha(0.12f));
        g.drawLine(display.getX() + 24.0f, centerY, display.getRight() - 24.0f, centerY, 1.0f);
        g.drawLine(centerX, display.getY() + 8.0f, centerX, display.getBottom() - 8.0f, 1.0f);

        for (int ring = 4; ring >= 1; --ring)
        {
            const auto expansion = static_cast<float>(ring) * 13.0f;
            g.setColour(NeonLookAndFeel::cyan().withAlpha(0.025f + 0.008f * static_cast<float>(5 - ring)));
            g.drawEllipse(ellipse.expanded(expansion, expansion * 0.28f), 1.0f);
        }
        g.setColour(NeonLookAndFeel::cyan().withAlpha(0.07f));
        g.fillEllipse(ellipse);
        g.setColour(NeonLookAndFeel::cyan().withAlpha(0.32f));
        g.drawEllipse(ellipse, 1.5f);
        g.setColour(NeonLookAndFeel::cyan().withAlpha(0.56f));
        g.drawEllipse(ellipse.reduced(6.0f, 4.0f), 1.0f);

        const auto speakerRadius = 7.0f;
        const std::array<float, 2> speakerXs { ellipse.getX(), ellipse.getRight() };
        for (const auto x : speakerXs)
        {
            for (int ring = 3; ring >= 1; --ring)
            {
                const auto radius = speakerRadius + static_cast<float>(ring) * 10.0f;
                g.setColour(NeonLookAndFeel::cyan().withAlpha(0.045f + 0.02f * static_cast<float>(4 - ring)));
                g.drawEllipse(x - radius, centerY - radius, radius * 2.0f, radius * 2.0f, 1.0f);
            }
            g.setColour(NeonLookAndFeel::cyan().withAlpha(0.30f));
            g.fillEllipse(x - speakerRadius - 3.0f, centerY - speakerRadius - 3.0f,
                          (speakerRadius + 3.0f) * 2.0f, (speakerRadius + 3.0f) * 2.0f);
            g.setColour(NeonLookAndFeel::cyan());
            g.fillEllipse(x - speakerRadius, centerY - speakerRadius,
                          speakerRadius * 2.0f, speakerRadius * 2.0f);
        }

        g.setColour(NeonLookAndFeel::textMuted());
        g.setFont(juce::Font(juce::FontOptions(10.0f, juce::Font::bold)));
        g.drawText("L", juce::roundToInt(display.getX() + 9.0f),
                   juce::roundToInt(centerY - 10.0f), 20, 20, juce::Justification::centredLeft);
        g.drawText("R", juce::roundToInt(display.getRight() - 29.0f),
                   juce::roundToInt(centerY - 10.0f), 20, 20, juce::Justification::centredRight);

        const auto centerOffset = juce::jlimit(-1.0f, 1.0f, centerDb / 12.0f) * display.getWidth() * 0.045f;
        const auto centerDotX = centerX + centerOffset;
        const auto glowRadius = 11.0f + 15.0f * juce::jlimit(0.0f, 1.0f, bass);
        g.setColour(juce::Colour(0xfff72b9f).withAlpha(0.24f + 0.26f * bass));
        g.fillEllipse(centerDotX - glowRadius, centerY - glowRadius,
                      glowRadius * 2.0f, glowRadius * 2.0f);
        g.setColour(NeonLookAndFeel::lime().withAlpha(0.94f));
        g.fillEllipse(centerDotX - 6.0f, centerY - 6.0f, 12.0f, 12.0f);
    }

    void resized() override
    {
        const auto cellWidth = juce::jmin(knobCellWidth_, juce::jmax(1, getWidth() / 5));
        const auto normalKnobY = getHeight() - knobCellHeight_ - 8;
        const auto masterHeight = knobCellHeight_ + 60;
        const auto masterKnobY = getHeight() - masterHeight - 8;
        for (std::size_t i = 0; i < knobs_.size(); ++i)
            knobs_[i]->setBounds(static_cast<int>(i) * cellWidth, normalKnobY,
                                 cellWidth, knobCellHeight_);
        masterKnob_->setBounds(cellWidth * 4, masterKnobY,
                               juce::jmax(1, getWidth() - cellWidth * 4), masterHeight);
    }

private:
    static void drawKnobCard(juce::Graphics& g, juce::Rectangle<float> card, juce::Colour accent)
    {
        g.setColour(NeonLookAndFeel::panelRaised().withAlpha(0.72f));
        g.fillRoundedRectangle(card, 14.0f);
        g.setColour(accent.withAlpha(0.18f));
        g.drawRoundedRectangle(card.reduced(0.5f), 14.0f, 1.0f);
        g.setColour(accent.withAlpha(0.72f));
        g.drawLine(card.getX() + 22.0f, card.getBottom() - 1.5f,
                   card.getRight() - 22.0f, card.getBottom() - 1.5f, 2.0f);
    }

    void addKnob(std::size_t index, int parameterId, const char* title,
                 float minimum, float maximum, float interval, const char* unit,
                 juce::Colour tint)
    {
        auto knob = std::make_unique<NeonKnob>(engine_, parameterId, title,
                                               minimum, maximum, interval, unit, tint,
                                               std::function<void()> {}, true);
        addAndMakeVisible(*knob);
        knobs_[index] = std::move(knob);
    }

    void timerCallback() override
    {
        for (auto& knob : knobs_)
            knob->refreshFromEngine();
        masterKnob_->refreshFromEngine();
        repaint();
    }

    EngineLibrary& engine_;
    std::array<std::unique_ptr<NeonKnob>, 4> knobs_ {};
    std::unique_ptr<NeonKnob> masterKnob_;
    int knobCellWidth_ = 180;
    int knobCellHeight_ = 150;
};

class GraphicBandControl final : public juce::Component
{
public:
    GraphicBandControl(EngineLibrary& engine, int parameterId, juce::String frequencyLabel,
                       juce::Colour tint, std::function<void()> onEdited)
        : engine_(engine), parameterId_(parameterId), frequencyLabel_(std::move(frequencyLabel)),
          tint_(tint), onEdited_(std::move(onEdited))
    {
        slider_.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        slider_.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        slider_.setRange(-12.0, 12.0, 0.1);
        slider_.setValue(engine_.getParameter(parameterId_), juce::dontSendNotification);
        slider_.setDoubleClickReturnValue(true, 0.0);
        slider_.setColour(juce::Slider::rotarySliderFillColourId, tint_);
        slider_.setColour(juce::Slider::rotarySliderOutlineColourId, tint_.withAlpha(0.24f));
        slider_.onValueChange = [this]
        {
            engine_.setParameter(parameterId_, static_cast<float>(slider_.getValue()));
            repaint();
            if (onEdited_)
                onEdited_();
        };
        addAndMakeVisible(slider_);
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
    }

    void refreshFromEngine()
    {
        const auto value = engine_.getParameter(parameterId_);
        if (std::abs(value - static_cast<float>(slider_.getValue())) > 0.0001f)
            slider_.setValue(value, juce::dontSendNotification);
        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        const auto value = static_cast<float>(slider_.getValue());
        g.setColour(value < -0.05f ? tint_.brighter(0.10f) : tint_);
        g.setFont(juce::Font(juce::FontOptions(11.5f, juce::Font::bold)));
        g.drawFittedText(juce::String(value, 1), 0, 0, getWidth(), 19,
                         juce::Justification::centred, 1);
        g.setColour(NeonLookAndFeel::textMuted());
        g.setFont(juce::Font(juce::FontOptions(10.5f, juce::Font::bold)));
        g.drawFittedText(frequencyLabel_, 0, getHeight() - 20, getWidth(), 18,
                         juce::Justification::centred, 1);
    }

    void resized() override
    {
        auto area = getLocalBounds();
        area.removeFromTop(18);
        area.removeFromBottom(20);
        slider_.setBounds(area.reduced(1, 0));
    }

private:
    EngineLibrary& engine_;
    int parameterId_;
    juce::String frequencyLabel_;
    juce::Colour tint_;
    std::function<void()> onEdited_;
    juce::Slider slider_;
};

class EQGraphView final : public juce::Component,
                          private juce::Timer
{
public:
    explicit EQGraphView(EngineLibrary& engine) : engine_(engine)
    {
        startTimerHz(24);
    }

    void paint(juce::Graphics& g) override
    {
        const auto bounds = getLocalBounds().toFloat();
        g.setColour(NeonLookAndFeel::panel());
        g.fillRoundedRectangle(bounds, 15.0f);
        g.setColour(NeonLookAndFeel::cyan().withAlpha(0.16f));
        g.drawRoundedRectangle(bounds.reduced(0.5f), 15.0f, 1.0f);

        g.setColour(NeonLookAndFeel::textMuted());
        g.setFont(juce::Font(juce::FontOptions(10.0f, juce::Font::bold)));
        g.drawText("LIVE SPECTRUM   /   FREQUENCY RESPONSE", 18, 7, getWidth() - 36, 17,
                   juce::Justification::centredLeft);

        auto plot = bounds.reduced(42.0f, 28.0f);
        plot.removeFromTop(12.0f);
        plot.removeFromBottom(19.0f);
        plot.removeFromLeft(8.0f);
        plot.removeFromRight(4.0f);
        if (plot.getWidth() < 40.0f || plot.getHeight() < 40.0f)
            return;

        for (int tick = 0; tick <= 4; ++tick)
        {
            const auto db = 12.0f - static_cast<float>(tick) * 6.0f;
            const auto y = plot.getY() + (12.0f - db) / 24.0f * plot.getHeight();
            g.setColour(NeonLookAndFeel::textMuted().withAlpha(tick == 2 ? 0.24f : 0.11f));
            g.drawHorizontalLine(juce::roundToInt(y), plot.getX(), plot.getRight());
            g.setColour(NeonLookAndFeel::textMuted().withAlpha(0.75f));
            g.setFont(juce::Font(juce::FontOptions(9.5f)));
            g.drawText(db > 0.0f ? "+" + juce::String(juce::roundToInt(db))
                                 : juce::String(juce::roundToInt(db)),
                       5, juce::roundToInt(y) - 7, 31, 14, juce::Justification::centredRight);
        }

        const auto minLog = std::log(20.0f);
        const auto maxLog = std::log(20000.0f);
        const auto xForFrequency = [&plot, minLog, maxLog](float frequency)
        {
            const auto fraction = (std::log(juce::jlimit(20.0f, 20000.0f, frequency)) - minLog)
                                / (maxLog - minLog);
            return plot.getX() + fraction * plot.getWidth();
        };

        const std::array<float, 10> tickFrequencies
        {{ 20.0f, 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f, 20000.0f }};
        const std::array<const char*, 10> tickLabels
        {{ "20", "50", "100", "200", "500", "1k", "2k", "5k", "10k", "20k" }};
        for (std::size_t i = 0; i < tickFrequencies.size(); ++i)
        {
            const auto x = xForFrequency(tickFrequencies[i]);
            g.setColour(NeonLookAndFeel::textMuted().withAlpha(0.10f));
            g.drawVerticalLine(juce::roundToInt(x), plot.getY(), plot.getBottom());
            g.setColour(NeonLookAndFeel::textMuted().withAlpha(0.75f));
            g.setFont(juce::Font(juce::FontOptions(9.5f)));
            g.drawText(tickLabels[i], juce::roundToInt(x) - 19, juce::roundToInt(plot.getBottom()) + 2,
                       38, 14, juce::Justification::centred);
        }

        if (engine_.getParameter(NEON_SPECTRUM_ENABLED) >= 0.5f)
        {
            std::array<float, NEON_SPECTRUM_BANDS> spectrum {};
            engine_.copySpectrum(spectrum.data(), static_cast<int>(spectrum.size()));
            juce::Path area;
            juce::Path trace;
            area.startNewSubPath(plot.getX(), plot.getBottom());
            for (int i = 0; i < NEON_SPECTRUM_BANDS; ++i)
            {
                const auto fraction = static_cast<float>(i) / static_cast<float>(NEON_SPECTRUM_BANDS - 1);
                const auto x = plot.getX() + fraction * plot.getWidth();
                const auto level = juce::jlimit(0.0f, 1.0f, spectrum[static_cast<std::size_t>(i)]);
                const auto y = plot.getBottom() - level * plot.getHeight() * 0.90f;
                if (i == 0)
                    trace.startNewSubPath(x, y);
                else
                    trace.lineTo(x, y);
                area.lineTo(x, y);
            }
            area.lineTo(plot.getRight(), plot.getBottom());
            area.closeSubPath();

            g.setGradientFill(juce::ColourGradient(juce::Colour(0xff935dff).withAlpha(0.30f),
                                                    plot.getCentreX(), plot.getY(),
                                                    juce::Colour(0xff5530aa).withAlpha(0.02f),
                                                    plot.getCentreX(), plot.getBottom(), false));
            g.fillPath(area);
            g.setColour(juce::Colour(0xffa274ff).withAlpha(0.44f));
            g.strokePath(trace, juce::PathStrokeType(1.2f, juce::PathStrokeType::curved,
                                                      juce::PathStrokeType::rounded));
        }

        std::array<float, 10> gains {};
        for (std::size_t i = 0; i < gains.size(); ++i)
            gains[i] = engine_.getParameter(graphicEqParameterIds[i]);
        const auto q = engine_.getParameter(NEON_EQ_BAND_Q);
        const auto preamp = engine_.getParameter(NEON_PREAMP_DB);
        const auto responseAt = [&gains, q, preamp](float frequency)
        {
            auto response = preamp;
            const auto widthOctaves = 0.72f / juce::jmax(0.3f, q);
            for (std::size_t band = 0; band < gains.size(); ++band)
            {
                const auto distance = std::log2(frequency / graphicEqFrequencies[band]);
                const auto ratio = distance / widthOctaves;
                response += gains[band] * std::exp(-0.5f * ratio * ratio);
            }
            return juce::jlimit(-12.0f, 12.0f, response);
        };
        const auto yForDb = [&plot](float db)
        {
            const auto bounded = juce::jlimit(-12.0f, 12.0f, db);
            return plot.getY() + (12.0f - bounded) / 24.0f * plot.getHeight();
        };

        juce::Path response;
        constexpr int curveSamples = 260;
        for (int i = 0; i < curveSamples; ++i)
        {
            const auto fraction = static_cast<float>(i) / static_cast<float>(curveSamples - 1);
            const auto frequency = 20.0f * std::pow(1000.0f, fraction);
            const auto x = plot.getX() + fraction * plot.getWidth();
            const auto y = yForDb(responseAt(frequency));
            if (i == 0)
                response.startNewSubPath(x, y);
            else
                response.lineTo(x, y);
        }
        g.setColour(NeonLookAndFeel::cyan().withAlpha(0.13f));
        g.strokePath(response, juce::PathStrokeType(8.0f, juce::PathStrokeType::curved,
                                                     juce::PathStrokeType::rounded));
        g.setColour(NeonLookAndFeel::cyan());
        g.strokePath(response, juce::PathStrokeType(2.4f, juce::PathStrokeType::curved,
                                                     juce::PathStrokeType::rounded));

        for (std::size_t band = 0; band < graphicEqFrequencies.size(); ++band)
        {
            const auto x = xForFrequency(graphicEqFrequencies[band]);
            const auto y = yForDb(responseAt(graphicEqFrequencies[band]));
            const auto tint = graphicEqColours[band];
            g.setColour(tint.withAlpha(0.22f));
            g.fillEllipse(x - 11.0f, y - 11.0f, 22.0f, 22.0f);
            g.setColour(tint);
            g.fillEllipse(x - 5.5f, y - 5.5f, 11.0f, 11.0f);
            g.setColour(NeonLookAndFeel::background());
            g.fillEllipse(x - 2.1f, y - 2.1f, 4.2f, 4.2f);
        }
    }

private:
    void timerCallback() override { repaint(); }
    EngineLibrary& engine_;
};

class InOutMeters final : public juce::Component,
                          private juce::Timer
{
public:
    explicit InOutMeters(HostAudioCallback& callback) : callback_(callback)
    {
        startTimerHz(30);
    }

    void paint(juce::Graphics& g) override
    {
        auto area = getLocalBounds().toFloat();
        const auto labelHeight = 16.0f;
        const auto meterArea = area.withTrimmedBottom(labelHeight).reduced(2.0f, 2.0f);
        const auto meterWidth = juce::jmax(9.0f, (meterArea.getWidth() - 8.0f) * 0.5f);
        const auto gap = 8.0f;

        const auto drawMeter = [&g, &meterArea, meterWidth](float x, float peak)
        {
            const auto track = juce::Rectangle<float>(x, meterArea.getY(), meterWidth, meterArea.getHeight());
            g.setColour(juce::Colour(0xff111a28));
            g.fillRoundedRectangle(track, 4.0f);
            g.setColour(NeonLookAndFeel::cyan().withAlpha(0.18f));
            g.drawRoundedRectangle(track.reduced(0.5f), 4.0f, 1.0f);

            const auto db = peak > 0.000001f ? 20.0f * std::log10(peak) : -72.0f;
            const auto level = juce::jlimit(0.0f, 1.0f, (db + 60.0f) / 60.0f);
            const auto fillHeight = level * (track.getHeight() - 4.0f);
            auto fill = juce::Rectangle<float>(track.getX() + 2.0f,
                                                track.getBottom() - 2.0f - fillHeight,
                                                track.getWidth() - 4.0f, fillHeight);
            juce::ColourGradient gradient(juce::Colour(0xff34f59a), fill.getX(), track.getBottom(),
                                           juce::Colour(0xffff547c), fill.getX(), track.getY(), false);
            gradient.addColour(0.60, juce::Colour(0xffffd45b));
            gradient.addColour(0.32, juce::Colour(0xff20d8ff));
            g.setGradientFill(gradient);
            g.fillRoundedRectangle(fill, 3.0f);

            g.setColour(juce::Colour(0x88ffffff));
            for (int tick = 1; tick < 8; ++tick)
            {
                const auto y = track.getY() + track.getHeight() * static_cast<float>(tick) / 8.0f;
                g.drawHorizontalLine(juce::roundToInt(y), track.getX() + 1.0f, track.getRight() - 1.0f);
            }
        };

        const auto totalWidth = 2.0f * meterWidth + gap;
        const auto leftX = meterArea.getX() + (meterArea.getWidth() - totalWidth) * 0.5f;
        drawMeter(leftX, displayedInput_);
        drawMeter(leftX + meterWidth + gap, displayedOutput_);

        g.setColour(NeonLookAndFeel::textMuted());
        g.setFont(juce::Font(juce::FontOptions(9.0f, juce::Font::bold)));
        const auto halfWidth = getWidth() / 2;
        g.drawText("IN", 0, getHeight() - 15, halfWidth, 13, juce::Justification::centred);
        g.drawText("OUT", halfWidth, getHeight() - 15, getWidth() - halfWidth, 13,
                   juce::Justification::centred);
    }

private:
    void timerCallback() override
    {
        displayedInput_ = juce::jmax(callback_.getInputPeak(), displayedInput_ * 0.88f);
        displayedOutput_ = juce::jmax(callback_.getOutputPeak(), displayedOutput_ * 0.88f);
        repaint();
    }

    HostAudioCallback& callback_;
    float displayedInput_ = 0.0f;
    float displayedOutput_ = 0.0f;
};

class EqualiserPage final : public juce::Component,
                            private juce::Timer
{
public:
    explicit EqualiserPage(MainComponent& owner)
        : owner_(owner), engine_(owner.getEngine()), graph_(engine_), meters_(owner.getAudioCallback()),
          spectrumSwitch_("Spectrum", engine_.getParameter(NEON_SPECTRUM_ENABLED) >= 0.5f,
              [this](bool enabled) { engine_.setParameter(NEON_SPECTRUM_ENABLED, enabled ? 1.0f : 0.0f); }),
          limiterSwitch_("Limiter", engine_.getParameter(NEON_LIMITER_ENABLED) >= 0.5f,
              [this](bool enabled) { engine_.setParameter(NEON_LIMITER_ENABLED, enabled ? 1.0f : 0.0f); })
    {
        addAndMakeVisible(graph_);
        addAndMakeVisible(presetLabel_);
        addAndMakeVisible(sourceLabel_);
        addAndMakeVisible(preset_);
        addAndMakeVisible(captureSource_);
        addAndMakeVisible(spectrumSwitch_);
        addAndMakeVisible(limiterSwitch_);
        addAndMakeVisible(meters_);

        presetLabel_.setText("PRESET", juce::dontSendNotification);
        styleLabel(presetLabel_, 10.0f, NeonLookAndFeel::textMuted(), true);
        sourceLabel_.setText("CAPTURE SOURCE", juce::dontSendNotification);
        styleLabel(sourceLabel_, 10.0f, NeonLookAndFeel::textMuted(), true);

        preset_.addItem("Flat", 1);
        preset_.addItem("Warm", 2);
        preset_.addItem("Bass Lift", 3);
        preset_.addItem("Vocal", 4);
        preset_.addItem("Clarity", 5);
        preset_.addItem("Custom", 6);
        preset_.setSelectedId(1, juce::dontSendNotification);
        preset_.setTooltip("Quick graphic-EQ curves. Use the Presets tab for JSON files.");
        preset_.onChange = [this] { applyQuickPreset(preset_.getSelectedId()); };

        populateCaptureSources();
        captureSource_.onChange = [this] { captureSourceChanged(); };
        captureSource_.setTooltip("Choose default-render loopback or a WASAPI input such as VB-CABLE.");

        preamp_ = std::make_unique<NeonKnob>(engine_, NEON_PREAMP_DB, "PREAMP",
                                             -24.0f, 12.0f, 0.1f, "dB",
                                             NeonLookAndFeel::magenta(), [this] { markCustom(); });
        bandQ_ = std::make_unique<NeonKnob>(engine_, NEON_EQ_BAND_Q, "BAND Q",
                                            0.3f, 4.0f, 0.01f, "Q",
                                            NeonLookAndFeel::violet(), [this] { markCustom(); });
        addAndMakeVisible(*preamp_);
        addAndMakeVisible(*bandQ_);

        for (std::size_t i = 0; i < graphicEqParameterIds.size(); ++i)
        {
            auto band = std::make_unique<GraphicBandControl>(
                engine_, graphicEqParameterIds[i], graphicEqLabels[i], graphicEqColours[i],
                [this] { markCustom(); });
            addAndMakeVisible(*band);
            bands_.push_back(std::move(band));
        }

        startTimerHz(8);
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(NeonLookAndFeel::background());
        auto area = getLocalBounds().reduced(18, 12);
        auto top = area.removeFromTop(84);
        area.removeFromTop(12);
        auto bands = area.removeFromBottom(176);
        area.removeFromBottom(12);
        drawPanel(g, top.toFloat());
        drawPanel(g, bands.toFloat());

        g.setColour(NeonLookAndFeel::textMuted());
        g.setFont(juce::Font(juce::FontOptions(9.5f, juce::Font::bold)));
        g.drawText("GRAPHIC BANDS   /   dB", bands.getX() + 12, bands.getY() + 4, 220, 14,
                   juce::Justification::centredLeft);

        const auto bandCenterY = static_cast<float>(bands.getY()) + 104.0f;
        juce::Path guide;
        guide.startNewSubPath(static_cast<float>(bands.getX()) + 20.0f, bandCenterY);
        guide.lineTo(static_cast<float>(bands.getRight()) - 76.0f, bandCenterY);
        g.setColour(NeonLookAndFeel::cyan().withAlpha(0.18f));
        g.strokePath(guide, juce::PathStrokeType(1.2f));
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(18, 12);
        const auto top = area.removeFromTop(84);
        area.removeFromTop(12);
        const auto bandArea = area.removeFromBottom(176);
        area.removeFromBottom(12);
        graph_.setBounds(area);

        auto x = top.getX() + 14;
        const auto labelY = top.getY() + 8;
        const auto comboY = top.getY() + 29;
        const auto gap = juce::jlimit(8, 12, top.getWidth() / 100);
        const auto presetWidth = juce::jmin(220, juce::jmax(155, top.getWidth() / 5));
        presetLabel_.setBounds(x, labelY, presetWidth, 15);
        preset_.setBounds(x, comboY, presetWidth, 34);
        x += presetWidth + gap;

        const auto sourceWidth = juce::jmin(420, juce::jmax(270, top.getWidth() / 3));
        sourceLabel_.setBounds(x, labelY, sourceWidth, 15);
        captureSource_.setBounds(x, comboY, sourceWidth, 34);
        x += sourceWidth + gap;

        const auto knobWidth = juce::jmin(82, juce::jmax(68, top.getWidth() / 14));
        preamp_->setBounds(x, top.getY() + 1, knobWidth, top.getHeight() - 2);
        x += knobWidth + gap / 2;
        bandQ_->setBounds(x, top.getY() + 1, knobWidth, top.getHeight() - 2);
        x += knobWidth + gap;

        spectrumSwitch_.setBounds(x, top.getY() + 20, 108, 36);
        x += 108 + gap;
        limiterSwitch_.setBounds(x, top.getY() + 20, 98, 36);

        auto controls = bandArea.reduced(10, 20);
        auto meterBounds = controls.removeFromRight(58);
        meters_.setBounds(meterBounds);
        const auto cellWidth = controls.getWidth() / static_cast<int>(bands_.size());
        for (std::size_t i = 0; i < bands_.size(); ++i)
        {
            const auto cellX = controls.getX() + static_cast<int>(i) * cellWidth;
            bands_[i]->setBounds(cellX + 2, controls.getY(), cellWidth - 4, controls.getHeight());
        }
    }

private:
    static void drawPanel(juce::Graphics& g, juce::Rectangle<float> bounds)
    {
        g.setColour(NeonLookAndFeel::panel());
        g.fillRoundedRectangle(bounds, 15.0f);
        g.setColour(NeonLookAndFeel::cyan().withAlpha(0.13f));
        g.drawRoundedRectangle(bounds.reduced(0.5f), 15.0f, 1.0f);
    }

    void markCustom()
    {
        preset_.setSelectedId(6, juce::dontSendNotification);
    }

    void applyQuickPreset(int id)
    {
        std::array<float, 10> values {};
        float preamp = 0.0f;
        switch (id)
        {
            case 2: values = {{ 2.0f, 1.2f, 0.4f, -0.5f, -0.6f, 0.0f, 0.5f, 0.4f, -0.8f, -1.2f }}; break;
            case 3: values = {{ 4.0f, 3.2f, 2.0f, 1.0f, 0.3f, 0.0f, 0.0f, 0.0f, -0.5f, -1.0f }}; preamp = -2.0f; break;
            case 4: values = {{ -1.5f, -0.5f, 0.5f, 1.5f, 2.0f, 2.4f, 1.7f, 0.7f, 0.0f, -0.5f }}; break;
            case 5: values = {{ -1.5f, -1.0f, 0.0f, 1.0f, 2.0f, 2.5f, 2.4f, 1.8f, 1.0f, 0.3f }}; break;
            case 1: values.fill(0.0f); break;
            default: return;
        }

        for (std::size_t i = 0; i < values.size(); ++i)
            engine_.setParameter(graphicEqParameterIds[i], values[i]);
        engine_.setParameter(NEON_PREAMP_DB, preamp);
        if (preamp_ != nullptr)
            preamp_->refreshFromEngine();
        for (auto& band : bands_)
            band->refreshFromEngine();
        graph_.repaint();
    }

    void populateCaptureSources()
    {
        captureSource_.clear(juce::dontSendNotification);
        inputDeviceNames_.clear();
        captureSource_.addItem("WASAPI LOOPBACK  /  DEFAULT RENDER", 1);

        if (auto* type = owner_.getDeviceManager().getCurrentDeviceTypeObject())
        {
            const auto names = type->getDeviceNames(true);
            for (const auto& name : names)
            {
                if (name.isEmpty())
                    continue;
                inputDeviceNames_.add(name);
                captureSource_.addItem(name, inputDeviceNames_.size() + 1);
            }
        }

        refreshCaptureSelection();
    }

    void refreshCaptureSelection()
    {
        if (owner_.getCaptureMode() == 1)
        {
            captureSource_.setSelectedId(1, juce::dontSendNotification);
            return;
        }

        const auto setup = owner_.getDeviceManager().getAudioDeviceSetup();
        const auto index = inputDeviceNames_.indexOf(setup.inputDeviceName);
        if (index >= 0)
            captureSource_.setSelectedId(index + 2, juce::dontSendNotification);
    }

    void captureSourceChanged()
    {
        const auto id = captureSource_.getSelectedId();
        if (id == 1)
        {
            owner_.setCaptureMode(1);
            return;
        }

        const auto index = id - 2;
        if (index < 0 || index >= inputDeviceNames_.size())
            return;

        auto& manager = owner_.getDeviceManager();
        auto setup = manager.getAudioDeviceSetup();
        setup.inputDeviceName = inputDeviceNames_[index];
        setup.useDefaultInputChannels = true;
        setup.inputChannels.clear();
        const auto error = manager.setAudioDeviceSetup(setup, true);
        owner_.setCaptureMode(2);
        if (error.isNotEmpty())
            captureSource_.setTooltip(error);
        else
            captureSource_.setTooltip("Capturing from " + inputDeviceNames_[index]);
    }

    void timerCallback() override
    {
        preamp_->refreshFromEngine();
        bandQ_->refreshFromEngine();
        for (auto& band : bands_)
            band->refreshFromEngine();
        spectrumSwitch_.setValue(engine_.getParameter(NEON_SPECTRUM_ENABLED) >= 0.5f);
        limiterSwitch_.setValue(engine_.getParameter(NEON_LIMITER_ENABLED) >= 0.5f);
        refreshCaptureSelection();
    }

    MainComponent& owner_;
    EngineLibrary& engine_;
    EQGraphView graph_;
    InOutMeters meters_;
    juce::Label presetLabel_;
    juce::Label sourceLabel_;
    juce::ComboBox preset_;
    juce::ComboBox captureSource_;
    juce::StringArray inputDeviceNames_;
    std::unique_ptr<NeonKnob> preamp_;
    std::unique_ptr<NeonKnob> bandQ_;
    NeonSwitch spectrumSwitch_;
    NeonSwitch limiterSwitch_;
    std::vector<std::unique_ptr<GraphicBandControl>> bands_;
};

class SurroundPage final : public juce::Component,
                           private juce::Timer
{
public:
    explicit SurroundPage(EngineLibrary& engine)
        : engine_(engine),
          fieldEnabledSwitch_("", engine_.getParameter(NEON_FIELD_ENABLED) >= 0.5f,
              [this](bool enabled)
              {
                  engine_.setParameter(NEON_FIELD_ENABLED, enabled ? 1.0f : 0.0f);
              }, true),
          fieldPanel_(engine)
    {
        addAndMakeVisible(fieldEnabledSwitch_);
        viewport_.setViewedComponent(&fieldPanel_, false);
        viewport_.setScrollBarsShown(true, false);
        addAndMakeVisible(viewport_);
        startTimerHz(8);
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(NeonLookAndFeel::background());
        const auto card = getLocalBounds().reduced(12, 12).toFloat();
        g.setColour(NeonLookAndFeel::panel());
        g.fillRoundedRectangle(card, 17.0f);
        g.setColour(NeonLookAndFeel::cyan().withAlpha(0.14f));
        g.drawRoundedRectangle(card.reduced(0.5f), 17.0f, 1.0f);

        const auto icon = juce::Rectangle<float>(27.0f, 20.0f, 48.0f, 48.0f);
        g.setColour(NeonLookAndFeel::cyan().withAlpha(0.10f));
        g.fillEllipse(icon);
        g.setColour(NeonLookAndFeel::cyan().withAlpha(0.84f));
        g.drawEllipse(icon, 1.5f);
        juce::Path waveform;
        waveform.startNewSubPath(35.0f, 44.0f);
        waveform.lineTo(41.0f, 44.0f);
        waveform.lineTo(45.0f, 36.0f);
        waveform.lineTo(50.0f, 52.0f);
        waveform.lineTo(55.0f, 38.0f);
        waveform.lineTo(60.0f, 47.0f);
        waveform.lineTo(67.0f, 47.0f);
        g.strokePath(waveform, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved,
                                                     juce::PathStrokeType::rounded));

        g.setFont(juce::Font(juce::FontOptions(20.0f, juce::Font::bold)));
        g.setColour(NeonLookAndFeel::textPrimary());
        g.drawText("FIELD", 88, 22, 66, 28, juce::Justification::centredLeft);
        g.setColour(NeonLookAndFeel::cyan());
        g.drawText("SURROUND", 155, 22, 220, 28, juce::Justification::centredLeft);
        g.setColour(NeonLookAndFeel::textMuted());
        g.setFont(juce::Font(juce::FontOptions(11.5f)));
        g.drawText("Field, Spread, Bass and Center controls with a larger master output knob.",
                   88, 53, juce::jmax(220, getWidth() - 540), 20,
                   juce::Justification::centredLeft);

        const auto virtualPill = juce::Rectangle<float>(static_cast<float>(getWidth() - 426),
                                                          19.0f, 226.0f, 46.0f);
        g.setColour(NeonLookAndFeel::panelRaised().withAlpha(0.70f));
        g.fillRoundedRectangle(virtualPill, 15.0f);
        g.setColour(NeonLookAndFeel::cyan().withAlpha(0.18f));
        g.drawRoundedRectangle(virtualPill, 15.0f, 1.0f);
        g.setColour(NeonLookAndFeel::textMuted());
        g.setFont(juce::Font(juce::FontOptions(10.0f, juce::Font::bold)));
        g.drawText("VIRTUAL", getWidth() - 419, 31, 71, 20,
                   juce::Justification::centredRight);

        const auto paintIconButton = [&g](juce::Rectangle<float> button)
        {
            g.setColour(NeonLookAndFeel::panelRaised().withAlpha(0.70f));
            g.fillRoundedRectangle(button, 13.0f);
            g.setColour(NeonLookAndFeel::cyan().withAlpha(0.15f));
            g.drawRoundedRectangle(button, 13.0f, 1.0f);
        };
        const auto gearButton = juce::Rectangle<float>(static_cast<float>(getWidth() - 178),
                                                         19.0f, 58.0f, 46.0f);
        paintIconButton(gearButton);
        const auto gearCentreX = gearButton.getCentreX();
        const auto gearCentreY = gearButton.getCentreY();
        g.setColour(NeonLookAndFeel::textMuted());
        g.drawEllipse(gearCentreX - 5.0f, gearCentreY - 5.0f, 10.0f, 10.0f, 1.8f);
        g.drawEllipse(gearCentreX - 1.8f, gearCentreY - 1.8f, 3.6f, 3.6f, 1.3f);
        for (int tooth = 0; tooth < 8; ++tooth)
        {
            const auto angle = static_cast<float>(tooth) * 0.7853981634f;
            g.drawLine(gearCentreX + std::cos(angle) * 6.0f,
                       gearCentreY + std::sin(angle) * 6.0f,
                       gearCentreX + std::cos(angle) * 9.5f,
                       gearCentreY + std::sin(angle) * 9.5f, 1.8f);
        }

        const auto moreButton = juce::Rectangle<float>(static_cast<float>(getWidth() - 108),
                                                         19.0f, 58.0f, 46.0f);
        paintIconButton(moreButton);
        g.setColour(NeonLookAndFeel::textMuted());
        const auto moreCentreX = moreButton.getCentreX();
        const auto moreCentreY = moreButton.getCentreY();
        for (int dot = -1; dot <= 1; ++dot)
            g.fillEllipse(moreCentreX + static_cast<float>(dot) * 7.0f - 1.5f,
                          moreCentreY - 1.5f, 3.0f, 3.0f);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(18, 14);
        area.removeFromTop(78);
        viewport_.setBounds(area);
        const auto contentWidth = juce::jmax(1, viewport_.getWidth() - viewport_.getScrollBarThickness());
        const auto reverbCellWidth = juce::jmax(1, (getWidth() - 36) / 5 - 4);
        fieldPanel_.setKnobLayout(reverbCellWidth, reverbKnobRowHeight(getHeight()) + 8);
        fieldPanel_.setSize(contentWidth, area.getHeight());
        fieldEnabledSwitch_.setBounds(getWidth() - 338, 27, 105, 30);
    }

private:
    void timerCallback() override
    {
        fieldEnabledSwitch_.setValue(engine_.getParameter(NEON_FIELD_ENABLED) >= 0.5f);
    }

    EngineLibrary& engine_;
    NeonSwitch fieldEnabledSwitch_;
    FieldSurroundPanel fieldPanel_;
    juce::Viewport viewport_;
};

class OtherSpatialEffectsControlsContent final : public juce::Component,
                                                 private juce::Timer
{
public:
    explicit OtherSpatialEffectsControlsContent(EngineLibrary& engine)
        : engine_(engine)
    {
        const std::array<Spec, 7> specs
        {{
            { NEON_DIMENSION_AMOUNT, "DIMENSION", 0.0f, 1.0f, 0.01f, "%" },
            { NEON_DELAY_TIME_MS, "STEREO / TIME", 20.0f, 1000.0f, 1.0f, "ms" },
            { NEON_DELAY_FEEDBACK, "STEREO / FEED", 0.0f, 0.85f, 0.01f, "%" },
            { NEON_DELAY_MIX, "STEREO / MIX", 0.0f, 1.0f, 0.01f, "%" },
            { NEON_DELAY_LOW_CROSSOVER_HZ, "DELAY / LOW XOVER", 40.0f, 1000.0f, 1.0f, "Hz" },
            { NEON_DELAY_HIGH_CROSSOVER_HZ, "DELAY / HIGH XOVER", 300.0f, 16000.0f, 1.0f, "Hz" },
            { NEON_LOUDNESS_AMOUNT, "LOUDNESS / AMOUNT", 0.0f, 1.0f, 0.01f, "%" }
        }};

        for (const auto& spec : specs)
        {
            std::function<void()> crossoverOrder;
            if (spec.id == NEON_DELAY_LOW_CROSSOVER_HZ)
            {
                crossoverOrder = [this]
                {
                    const auto low = engine_.getParameter(NEON_DELAY_LOW_CROSSOVER_HZ);
                    if (engine_.getParameter(NEON_DELAY_HIGH_CROSSOVER_HZ) < low + 50.0f)
                        engine_.setParameter(NEON_DELAY_HIGH_CROSSOVER_HZ,
                                             juce::jmin(16000.0f, low + 50.0f));
                };
            }
            else if (spec.id == NEON_DELAY_HIGH_CROSSOVER_HZ)
            {
                crossoverOrder = [this]
                {
                    const auto high = engine_.getParameter(NEON_DELAY_HIGH_CROSSOVER_HZ);
                    if (engine_.getParameter(NEON_DELAY_LOW_CROSSOVER_HZ) > high - 50.0f)
                        engine_.setParameter(NEON_DELAY_LOW_CROSSOVER_HZ,
                                             juce::jmax(40.0f, high - 50.0f));
                };
            }

            auto knob = std::make_unique<NeonKnob>(engine_, spec.id, spec.name,
                                                   spec.minimum, spec.maximum,
                                                   spec.interval, spec.unit,
                                                   NeonLookAndFeel::cyan(), std::move(crossoverOrder));
            addAndMakeVisible(*knob);
            knobs_.push_back(std::move(knob));
        }
        startTimerHz(8);
    }

    void setKnobCellSize(int width, int height)
    {
        knobCellWidth_ = juce::jmax(1, width);
        knobCellHeight_ = juce::jmax(1, height);
        resized();
    }

    static int preferredHeight(int knobCellHeight)
    {
        return 68 + 2 * juce::jmax(1, knobCellHeight);
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(NeonLookAndFeel::background());
        const auto card = getLocalBounds().reduced(1, 1).toFloat();
        g.setColour(NeonLookAndFeel::panel());
        g.fillRoundedRectangle(card, 16.0f);
        g.setColour(NeonLookAndFeel::cyan().withAlpha(0.16f));
        g.drawRoundedRectangle(card.reduced(0.5f), 16.0f, 1.0f);

        g.setColour(NeonLookAndFeel::textMuted());
        g.setFont(juce::Font(juce::FontOptions(12.0f, juce::Font::bold)));
        g.drawText("DIMENSION  /  STEREO DELAY  /  LOUDNESS", 20, 12,
                   getWidth() - 40, 22, juce::Justification::centredLeft);
        g.setColour(NeonLookAndFeel::cyan().withAlpha(0.15f));
        g.drawHorizontalLine(42, 18.0f, static_cast<float>(getWidth()) - 18.0f);
    }

    void resized() override
    {
        constexpr int gridY = 50;
        const auto firstRowX = (getWidth() - knobCellWidth_ * 4) / 2;
        const auto secondRowX = (getWidth() - knobCellWidth_ * 3) / 2;
        for (std::size_t i = 0; i < knobs_.size(); ++i)
        {
            const auto row = i < 4 ? 0 : 1;
            const auto column = row == 0 ? static_cast<int>(i) : static_cast<int>(i) - 4;
            const auto x = row == 0 ? firstRowX + column * knobCellWidth_
                                    : secondRowX + column * knobCellWidth_;
            knobs_[i]->setBounds(x, gridY + row * knobCellHeight_,
                                 knobCellWidth_, knobCellHeight_);
        }
    }

private:
    struct Spec
    {
        int id;
        const char* name;
        float minimum, maximum, interval;
        const char* unit;
    };

    void timerCallback() override
    {
        for (auto& knob : knobs_)
            knob->refreshFromEngine();
    }

    EngineLibrary& engine_;
    std::vector<std::unique_ptr<NeonKnob>> knobs_;
    int knobCellWidth_ = 180;
    int knobCellHeight_ = 150;
};

class OtherSpatialEffectsPage final : public juce::Component,
                                      private juce::Timer
{
public:
    explicit OtherSpatialEffectsPage(EngineLibrary& engine)
        : engine_(engine),
          dcBlockSwitch_("DC BLOCK", engine_.getParameter(NEON_UTILITY_ENABLED) >= 0.5f,
              [this](bool enabled) { engine_.setParameter(NEON_UTILITY_ENABLED, enabled ? 1.0f : 0.0f); }),
          controlsContent_(engine)
    {
        addAndMakeVisible(dcBlockSwitch_);
        for (std::size_t i = 0; i < effectSwitches_.size(); ++i)
        {
            const auto parameterId = otherSpatialEffectEnableParameterIds[i];
            auto toggle = std::make_unique<NeonSwitch>(
                otherSpatialEffectEnableLabels[i], engine_.getParameter(parameterId) >= 0.5f,
                [this, parameterId](bool enabled)
                {
                    engine_.setParameter(parameterId, enabled ? 1.0f : 0.0f);
                }, true);
            addAndMakeVisible(*toggle);
            effectSwitches_[i] = std::move(toggle);
        }

        viewport_.setViewedComponent(&controlsContent_, false);
        viewport_.setScrollBarsShown(false, false);
        addAndMakeVisible(viewport_);
        startTimerHz(8);
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(NeonLookAndFeel::background());
        const auto card = getLocalBounds().reduced(18, 14).toFloat();
        g.setColour(NeonLookAndFeel::panel());
        g.fillRoundedRectangle(card, 16.0f);
        g.setColour(NeonLookAndFeel::cyan().withAlpha(0.16f));
        g.drawRoundedRectangle(card.reduced(0.5f), 16.0f, 1.0f);

        g.setColour(NeonLookAndFeel::textPrimary());
        g.setFont(juce::Font(juce::FontOptions(18.0f, juce::Font::bold)));
        g.drawText("OTHER SPATIAL EFFECTS", 36, 28, getWidth() - 72, 28,
                   juce::Justification::centredLeft);
        g.setColour(NeonLookAndFeel::textMuted());
        g.setFont(juce::Font(juce::FontOptions(11.5f)));
        g.drawText("Dimension, Stereo Delay and Loudness controls with independent bypasses.",
                   36, 58, getWidth() - 72, 20, juce::Justification::centredLeft);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(28, 88);
        const auto toggleRow = area.removeFromTop(38);
        area.removeFromTop(12);
        constexpr int toggleGap = 8;
        constexpr int toggleCount = 4;
        const auto toggleWidth = (toggleRow.getWidth() - toggleGap * (toggleCount - 1)) / toggleCount;
        const std::array<NeonSwitch*, 4> switches
        {{ effectSwitches_[0].get(), effectSwitches_[1].get(),
           effectSwitches_[2].get(), &dcBlockSwitch_ }};
        for (std::size_t i = 0; i < switches.size(); ++i)
        {
            const auto x = toggleRow.getX() + static_cast<int>(i) * (toggleWidth + toggleGap);
            switches[i]->setBounds(x, toggleRow.getY(),
                                   i + 1 == switches.size()
                                       ? toggleRow.getRight() - x : toggleWidth,
                                   toggleRow.getHeight());
        }

        viewport_.setBounds(area);
        const auto contentWidth = juce::jmax(1, viewport_.getWidth());
        const auto knobCellWidth = juce::jmax(1, (getWidth() - 36) / 5 - 4);
        const auto reverbCellHeight = reverbKnobRowHeight(getHeight());
        const auto maxCellHeight = juce::jmax(1, (area.getHeight() - 68) / 2);
        const auto knobCellHeight = juce::jmin(reverbCellHeight, maxCellHeight);
        controlsContent_.setKnobCellSize(knobCellWidth, knobCellHeight);
        controlsContent_.setSize(contentWidth,
            OtherSpatialEffectsControlsContent::preferredHeight(knobCellHeight));
    }

private:
    void timerCallback() override
    {
        for (std::size_t i = 0; i < effectSwitches_.size(); ++i)
            effectSwitches_[i]->setValue(
                engine_.getParameter(otherSpatialEffectEnableParameterIds[i]) >= 0.5f);
        dcBlockSwitch_.setValue(engine_.getParameter(NEON_UTILITY_ENABLED) >= 0.5f);
    }

    EngineLibrary& engine_;
    std::array<std::unique_ptr<NeonSwitch>, 3> effectSwitches_ {};
    NeonSwitch dcBlockSwitch_;
    OtherSpatialEffectsControlsContent controlsContent_;
    juce::Viewport viewport_;
};

class AdvancedReverbPage final : public juce::Component,
                                 private juce::Timer
{
public:
    explicit AdvancedReverbPage(EngineLibrary& engine)
        : engine_(engine),
          enabledSwitch_("REVERB", engine_.getParameter(NEON_REVERB_ENABLED) >= 0.5f,
              [this](bool enabled)
              {
                  engine_.setParameter(NEON_REVERB_ENABLED, enabled ? 1.0f : 0.0f);
              }, true)
    {
        addAndMakeVisible(enabledSwitch_);

        const std::array<Spec, 13> specs
        {{
            { NEON_REVERB_MIX, "WET / DRY", 0.0f, 1.0f, 0.01f, "%" },
            { NEON_REVERB_DECAY_SECONDS, "MAIN DECAY", 0.3f, 12.0f, 0.1f, "s" },
            { NEON_REVERB_ROOM, "ROOM SIZE", 0.0f, 1.0f, 0.01f, "%" },
            { NEON_REVERB_LOW_CROSSOVER_HZ, "LOW CROSSOVER", 40.0f, 1000.0f, 1.0f, "Hz" },
            { NEON_REVERB_HIGH_CROSSOVER_HZ, "HIGH CROSSOVER", 300.0f, 16000.0f, 10.0f, "Hz" },
            { NEON_REVERB_LOW_DECAY_MULTIPLIER, "LOW DECAY RATIO", 0.25f, 2.5f, 0.01f, "x" },
            { NEON_REVERB_HIGH_DECAY_MULTIPLIER, "HIGH DECAY RATIO", 0.25f, 2.5f, 0.01f, "x" },
            { NEON_REVERB_PRE_DELAY_MS, "PRE-DELAY", 0.0f, 250.0f, 1.0f, "ms" },
            { NEON_REVERB_LOW_DAMPING, "LOW DAMPING", 0.0f, 1.0f, 0.01f, "%" },
            { NEON_REVERB_HIGH_DAMPING, "HIGH DAMPING", 0.0f, 1.0f, 0.01f, "%" },
            { NEON_REVERB_STEREO_WIDTH, "STEREO WIDTH", 0.0f, 2.0f, 0.01f, "%" },
            { NEON_REVERB_ASPECT_RATIO, "ROOM ASPECT", 0.5f, 2.0f, 0.01f, "x" },
            { NEON_REVERB_DIFFUSION, "DIFFUSION", 0.0f, 1.0f, 0.01f, "%" }
        }};

        for (const auto& spec : specs)
        {
            std::function<void()> enforceCrossoverOrder;
            if (spec.id == NEON_REVERB_LOW_CROSSOVER_HZ)
            {
                enforceCrossoverOrder = [this]
                {
                    const auto low = engine_.getParameter(NEON_REVERB_LOW_CROSSOVER_HZ);
                    const auto high = engine_.getParameter(NEON_REVERB_HIGH_CROSSOVER_HZ);
                    if (low > high - 50.0f)
                        engine_.setParameter(NEON_REVERB_HIGH_CROSSOVER_HZ, low + 50.0f);
                };
            }
            else if (spec.id == NEON_REVERB_HIGH_CROSSOVER_HZ)
            {
                enforceCrossoverOrder = [this]
                {
                    const auto low = engine_.getParameter(NEON_REVERB_LOW_CROSSOVER_HZ);
                    const auto high = engine_.getParameter(NEON_REVERB_HIGH_CROSSOVER_HZ);
                    if (high < low + 50.0f)
                        engine_.setParameter(NEON_REVERB_LOW_CROSSOVER_HZ, high - 50.0f);
                };
            }

            auto knob = std::make_unique<NeonKnob>(engine_, spec.id, spec.name,
                                                   spec.minimum, spec.maximum,
                                                   spec.interval, spec.unit,
                                                   NeonLookAndFeel::cyan(), std::move(enforceCrossoverOrder));
            addAndMakeVisible(*knob);
            knobs_.push_back(std::move(knob));
        }
        startTimerHz(8);
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(NeonLookAndFeel::background());
        const auto card = getLocalBounds().reduced(18, 14).toFloat();
        g.setColour(NeonLookAndFeel::panel());
        g.fillRoundedRectangle(card, 16.0f);
        g.setColour(NeonLookAndFeel::cyan().withAlpha(0.16f));
        g.drawRoundedRectangle(card.reduced(0.5f), 16.0f, 1.0f);

        g.setColour(NeonLookAndFeel::textPrimary());
        g.setFont(juce::Font(juce::FontOptions(18.0f, juce::Font::bold)));
        g.drawText("ADVANCED REVERB", 36, 24, getWidth() - 300, 28,
                   juce::Justification::centredLeft);
        g.setColour(NeonLookAndFeel::textMuted());
        g.setFont(juce::Font(juce::FontOptions(11.5f)));
        g.drawText("0% MIX = DRY ONLY  /  100% = WET ONLY  •  PRE-DELAY AFFECTS WET ONLY  •  AUX SEND: 100% WET",
                   36, 55, getWidth() - 72, 20, juce::Justification::centredLeft);
    }

    void resized() override
    {
        enabledSwitch_.setBounds(getWidth() - 218, 22, 176, 34);
        auto area = getLocalBounds().reduced(18, 91);
        constexpr int columns = 5;
        const auto cellWidth = area.getWidth() / columns;
        const auto cellHeight = reverbKnobRowHeight(getHeight());
        for (std::size_t i = 0; i < knobs_.size(); ++i)
        {
            const auto row = static_cast<int>(i) / columns;
            const auto column = static_cast<int>(i) % columns;
            const auto rowCount = row < 2 ? columns : static_cast<int>(knobs_.size()) - 2 * columns;
            const auto columnOffset = (columns - rowCount) / 2;
            const auto x = area.getX() + (column + columnOffset) * cellWidth;
            knobs_[i]->setBounds(x + 2, area.getY() + row * cellHeight,
                                 cellWidth - 4, cellHeight);
        }
    }

private:
    struct Spec
    {
        int id;
        const char* name;
        float minimum, maximum, interval;
        const char* unit;
    };

    void timerCallback() override
    {
        for (auto& knob : knobs_)
            knob->refreshFromEngine();
        enabledSwitch_.setValue(engine_.getParameter(NEON_REVERB_ENABLED) >= 0.5f);
    }

    EngineLibrary& engine_;
    std::vector<std::unique_ptr<NeonKnob>> knobs_;
    NeonSwitch enabledSwitch_;
};

class RoutingPage final : public juce::Component
{
public:
    void paint(juce::Graphics& g) override
    {
        g.fillAll(NeonLookAndFeel::background());
        g.setColour(NeonLookAndFeel::textPrimary());
        g.setFont(juce::Font(juce::FontOptions(19.0f, juce::Font::bold)));
        g.drawText("AUDIO PROCESSOR GRAPH", 22, 18, getWidth() - 44, 30,
                   juce::Justification::centredLeft);
        g.setColour(NeonLookAndFeel::textMuted());
        g.setFont(juce::Font(juce::FontOptions(12.0f)));
        g.drawText("10 AudioProcessor nodes  •  stereo buses  •  explicit channel-to-channel connections",
                   22, 50, getWidth() - 44, 22, juce::Justification::centredLeft);

        const std::array<juce::String, 10> names
        {{
            "INPUT", "EQ", "SPECTRUM", "ADV. REVERB", "FIELD SURROUND",
            "STEREO DELAY", "DIMENSION", "UTILITY", "MASTER VOLUME", "OUTPUT"
        }};
        const auto margin = 28.0f;
        const auto cardWidth = (static_cast<float>(getWidth()) - 2.0f * margin - 4.0f * 22.0f) / 5.0f;
        const auto cardHeight = 92.0f;
        const auto topY = 138.0f;
        const auto bottomY = static_cast<float>(getHeight()) - cardHeight - 76.0f;
        const auto gap = 22.0f;

        auto drawCard = [&g, &names, cardWidth, cardHeight, margin, gap, topY, bottomY](int index, bool lowerRow)
        {
            const auto column = lowerRow ? 4 - (index - 5) : index;
            const auto x = margin + static_cast<float>(column) * (cardWidth + gap);
            const auto y = lowerRow ? bottomY : topY;
            const auto card = juce::Rectangle<float>(x, y, cardWidth, cardHeight);

            g.setColour(NeonLookAndFeel::panel());
            g.fillRoundedRectangle(card, 13.0f);
            g.setColour((index == 0 || index == 9 ? NeonLookAndFeel::magenta() : NeonLookAndFeel::cyan()).withAlpha(0.50f));
            g.drawRoundedRectangle(card.reduced(0.5f), 13.0f, 1.2f);
            g.setColour(NeonLookAndFeel::textMuted());
            g.setFont(juce::Font(juce::FontOptions(9.5f, juce::Font::bold)));
            g.drawText("NODE  " + juce::String(index + 1).paddedLeft('0', 2),
                       card.toNearestInt().reduced(9).removeFromTop(22), juce::Justification::centredLeft);
            g.setColour(NeonLookAndFeel::textPrimary());
            g.setFont(juce::Font(juce::FontOptions(12.0f, juce::Font::bold)));
            g.drawFittedText(names[static_cast<std::size_t>(index)], card.toNearestInt().reduced(9).withTrimmedTop(22),
                             juce::Justification::centred, 2);
        };

        for (int i = 0; i < 5; ++i)
            drawCard(i, false);
        for (int i = 5; i < 10; ++i)
            drawCard(i, true);

        auto drawArrow = [&g](juce::Point<float> from, juce::Point<float> to, bool leftward)
        {
            g.setColour(NeonLookAndFeel::cyan().withAlpha(0.72f));
            g.drawLine(from.x, from.y, to.x, to.y, 2.0f);
            const auto direction = leftward ? -1.0f : 1.0f;
            juce::Path head;
            head.startNewSubPath(to.x - direction * 7.0f, to.y - 5.0f);
            head.lineTo(to.x, to.y);
            head.lineTo(to.x - direction * 7.0f, to.y + 5.0f);
            g.strokePath(head, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved,
                                                    juce::PathStrokeType::rounded));
        };

        const auto centerYTop = topY + cardHeight * 0.5f;
        const auto centerYBottom = bottomY + cardHeight * 0.5f;
        for (int i = 0; i < 4; ++i)
        {
            const auto x1 = margin + static_cast<float>(i) * (cardWidth + gap) + cardWidth;
            const auto x2 = x1 + gap;
            drawArrow({ x1 + 2.0f, centerYTop }, { x2 - 2.0f, centerYTop }, false);
        }
        // Serpentine connection from Field Surround to Stereo Delay, then back across row two.
        const auto rightEdge = margin + 5.0f * cardWidth + 4.0f * gap - 10.0f;
        const auto leftEdge = margin + 10.0f;
        drawArrow({ rightEdge, topY + cardHeight }, { rightEdge, bottomY }, false);
        for (int i = 0; i < 4; ++i)
        {
            const auto column = 4 - i;
            const auto x1 = margin + static_cast<float>(column) * (cardWidth + gap);
            const auto x2 = x1 - gap;
            drawArrow({ x1 - 2.0f, centerYBottom }, { x2 + cardWidth + 2.0f, centerYBottom }, true);
        }
        drawArrow({ leftEdge, bottomY + cardHeight }, { leftEdge, bottomY + cardHeight + 28.0f }, false);
        g.setColour(NeonLookAndFeel::textMuted());
        g.setFont(juce::Font(juce::FontOptions(10.5f)));
        g.drawText("Graph connections are built with AudioProcessorGraph::addConnection(). Each processor declares a stereo input and output bus.",
                   22, getHeight() - 36, getWidth() - 44, 18, juce::Justification::centredLeft);
    }
};

class DevicePage final : public juce::Component,
                         private juce::Timer
{
public:
    explicit DevicePage(MainComponent& owner)
        : owner_(owner),
          selector_(std::make_unique<juce::AudioDeviceSelectorComponent>(
              owner.getDeviceManager(), 0, 2, 2, 2, false, false, true, false))
    {
        addAndMakeVisible(modeLabel_);
        addAndMakeVisible(mode_);
        addAndMakeVisible(status_);
        addAndMakeVisible(note_);
        addAndMakeVisible(*selector_);

        modeLabel_.setText("CAPTURE SOURCE", juce::dontSendNotification);
        styleLabel(modeLabel_, 11.0f, NeonLookAndFeel::textMuted(), true);
        mode_.addItem("WASAPI LOOPBACK  /  DEFAULT RENDER", 1);
        mode_.addItem("WASAPI INPUT  /  VB-CABLE OR SELECTED INPUT", 2);
        mode_.setSelectedId(owner_.getCaptureMode(), juce::dontSendNotification);
        mode_.onChange = [this]
        {
            owner_.setCaptureMode(mode_.getSelectedId());
        };

        status_.setText({}, juce::dontSendNotification);
        status_.setJustificationType(juce::Justification::topLeft);
        styleLabel(status_, 12.0f, NeonLookAndFeel::cyan());
        note_.setText("WASAPI shared mode is used. In input mode, choose “CABLE Output (VB-Audio Virtual Cable)” below as the capture endpoint.\n\nLoopback captures the Windows default render endpoint. If the capture endpoint and output endpoint are the same, the host mutes its output to prevent a feedback loop. Select a different output device to monitor processed audio.",
                       juce::dontSendNotification);
        note_.setJustificationType(juce::Justification::topLeft);
        note_.setColour(juce::Label::textColourId, NeonLookAndFeel::textMuted());
        note_.setFont(juce::Font(juce::FontOptions(12.0f)));
        startTimerHz(3);
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(NeonLookAndFeel::background());
        g.setColour(NeonLookAndFeel::textPrimary());
        g.setFont(juce::Font(juce::FontOptions(19.0f, juce::Font::bold)));
        g.drawText("WASAPI DEVICE MATRIX", 22, 14, getWidth() - 44, 28, juce::Justification::centredLeft);
        g.setColour(NeonLookAndFeel::textMuted());
        g.setFont(juce::Font(juce::FontOptions(11.5f)));
        g.drawText("Windows 10 / 11  •  shared-mode capture and playback  •  live callback buffer size follows the active device",
                   22, 44, getWidth() - 44, 20, juce::Justification::centredLeft);
    }

    void resized() override
    {
        const auto margin = 22;
        const auto contentTop = 78;
        modeLabel_.setBounds(margin, contentTop, 170, 22);
        mode_.setBounds(margin, contentTop + 24, juce::jmin(540, getWidth() - 2 * margin), 34);
        status_.setBounds(margin, contentTop + 66, juce::jmin(540, getWidth() - 2 * margin), 36);

        const auto splitY = contentTop + 112;
        const auto leftWidth = juce::jmax(430, getWidth() * 56 / 100);
        selector_->setBounds(margin, splitY, leftWidth - margin, getHeight() - splitY - 18);
        note_.setBounds(leftWidth + 8, splitY + 6, getWidth() - leftWidth - margin - 8,
                        getHeight() - splitY - 22);
    }

private:
    void timerCallback() override
    {
        status_.setText(owner_.getAudioStatus(), juce::dontSendNotification);
        status_.repaint();
    }

    MainComponent& owner_;
    juce::Label modeLabel_;
    juce::ComboBox mode_;
    juce::Label status_;
    juce::Label note_;
    std::unique_ptr<juce::AudioDeviceSelectorComponent> selector_;
};

class PresetPage final : public juce::Component
{
public:
    explicit PresetPage(EngineLibrary& engine) : engine_(engine)
    {
        addAndMakeVisible(title_);
        addAndMakeVisible(description_);
        addAndMakeVisible(saveButton_);
        addAndMakeVisible(loadButton_);
        addAndMakeVisible(status_);

        title_.setText("PRESET VAULT", juce::dontSendNotification);
        styleLabel(title_, 19.0f, NeonLookAndFeel::textPrimary(), true);
        description_.setText("Presets are stored as readable JSON generated from a JUCE ValueTree. They contain the full parameter set and can be versioned or edited outside the host.",
                             juce::dontSendNotification);
        description_.setColour(juce::Label::textColourId, NeonLookAndFeel::textMuted());
        description_.setFont(juce::Font(juce::FontOptions(13.0f)));
        description_.setJustificationType(juce::Justification::topLeft);

        saveButton_.setButtonText("SAVE JSON PRESET");
        loadButton_.setButtonText("LOAD JSON PRESET");
        styleButton(saveButton_);
        styleButton(loadButton_);
        saveButton_.onClick = [this] { savePreset(); };
        loadButton_.onClick = [this] { loadPreset(); };
        status_.setColour(juce::Label::textColourId, NeonLookAndFeel::cyan());
        status_.setFont(juce::Font(juce::FontOptions(12.0f)));
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(NeonLookAndFeel::background());
        auto card = getLocalBounds().reduced(22).toFloat();
        g.setColour(NeonLookAndFeel::panel());
        g.fillRoundedRectangle(card, 16.0f);
        g.setColour(NeonLookAndFeel::cyan().withAlpha(0.18f));
        g.drawRoundedRectangle(card.reduced(0.5f), 16.0f, 1.0f);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(44);
        title_.setBounds(area.removeFromTop(36));
        area.removeFromTop(10);
        description_.setBounds(area.removeFromTop(64));
        area.removeFromTop(18);
        saveButton_.setBounds(area.removeFromTop(42).removeFromLeft(240));
        area.removeFromTop(12);
        loadButton_.setBounds(area.removeFromTop(42).removeFromLeft(240));
        area.removeFromTop(20);
        status_.setBounds(area.removeFromTop(34));
    }

private:
    void savePreset()
    {
        activeChooser_ = std::make_unique<juce::FileChooser>(
            "Save Neon preset as JSON",
            juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                .getChildFile("Neon Audio Presets"),
            "*.json", true, false, this);

        const juce::Component::SafePointer<PresetPage> safeThis(this);
        activeChooser_->launchAsync(juce::FileBrowserComponent::saveMode
                                        | juce::FileBrowserComponent::canSelectFiles
                                        | juce::FileBrowserComponent::warnAboutOverwriting,
            [safeThis](const juce::FileChooser& chooser)
            {
                if (safeThis == nullptr)
                    return;

                auto file = chooser.getResult();
                if (file.getFullPathName().isEmpty())
                    return;
                if (file.getFileExtension().isEmpty())
                    file = file.withFileExtension(".json");

                const auto saved = safeThis->engine_.savePreset(file);
                safeThis->status_.setText(saved ? "Saved: " + file.getFullPathName()
                                                : "Preset save failed. Check the file path and permissions.",
                                          juce::dontSendNotification);
            });
    }

    void loadPreset()
    {
        activeChooser_ = std::make_unique<juce::FileChooser>(
            "Load Neon JSON preset",
            juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                .getChildFile("Neon Audio Presets"),
            "*.json", true, false, this);

        const juce::Component::SafePointer<PresetPage> safeThis(this);
        activeChooser_->launchAsync(juce::FileBrowserComponent::openMode
                                        | juce::FileBrowserComponent::canSelectFiles,
            [safeThis](const juce::FileChooser& chooser)
            {
                if (safeThis == nullptr)
                    return;

                const auto file = chooser.getResult();
                if (file.getFullPathName().isEmpty())
                    return;

                const auto loaded = safeThis->engine_.loadPreset(file);
                safeThis->status_.setText(loaded ? "Loaded: " + file.getFullPathName()
                                                 : "Preset load failed. The JSON file is invalid or from an unsupported schema.",
                                          juce::dontSendNotification);
            });
    }

    EngineLibrary& engine_;
    std::unique_ptr<juce::FileChooser> activeChooser_;
    juce::Label title_;
    juce::Label description_;
    juce::TextButton saveButton_;
    juce::TextButton loadButton_;
    juce::Label status_;
};
} // namespace

void WasapiSharedDeviceManager::createAudioDeviceTypes(juce::OwnedArray<juce::AudioIODeviceType>& types)
{
    if (auto* sharedWasapi = juce::AudioIODeviceType::createAudioIODeviceType_WASAPI(
            juce::WASAPIDeviceMode::shared))
        types.add(sharedWasapi);
}

MainComponent::MainComponent()
    : audioCallback_(engine_, loopbackFifo_)
{
    setSize(1240, 820);
    setOpaque(true);
    setLookAndFeel(&lookAndFeel_);

    const auto executable = juce::File::getSpecialLocation(juce::File::currentExecutableFile);
    const auto dll = executable.getParentDirectory().getChildFile("NeonEngine.dll");
    if (!engine_.open(dll))
        liveStatus_ = "ENGINE DLL ERROR  /  " + engine_.getError();

    initialiseAudio();

    tabs_.setTabBarDepth(42);
    tabs_.setColour(juce::TabbedComponent::backgroundColourId, NeonLookAndFeel::background());
    tabs_.setColour(juce::TabbedComponent::outlineColourId, NeonLookAndFeel::cyan().withAlpha(0.18f));
    tabs_.addTab("EQUALISER", NeonLookAndFeel::background(), new EqualiserPage(*this), true);
    tabs_.addTab("SURROUND", NeonLookAndFeel::background(), new SurroundPage(engine_), true);
    tabs_.addTab("REVERB", NeonLookAndFeel::background(), new AdvancedReverbPage(engine_), true);
    tabs_.addTab("OTHER SPATIAL EFFECTS", NeonLookAndFeel::background(),
                  new OtherSpatialEffectsPage(engine_), true);
    tabs_.addTab("GRAPH", NeonLookAndFeel::background(), new RoutingPage(), true);
    tabs_.addTab("DEVICES", NeonLookAndFeel::background(), new DevicePage(*this), true);
    tabs_.addTab("PRESETS", NeonLookAndFeel::background(), new PresetPage(engine_), true);
    tabs_.addTab("RENDER", NeonLookAndFeel::background(), new RenderExportPage(engine_, audioCallback_, dll), true);
    addAndMakeVisible(tabs_);

    listenOriginalButton_.setClickingTogglesState(true);
    styleButton(listenOriginalButton_);
    listenOriginalButton_.setTooltip("On: monitor original unprocessed input. Off: monitor processed output.");
    listenOriginalButton_.onClick = [this]
    {
        audioCallback_.setListenOriginalOutput(listenOriginalButton_.getToggleState());
    };
    addAndMakeVisible(listenOriginalButton_);

    deviceManager_.addAudioCallback(&audioCallback_);
    setCaptureMode(captureModeId_);
    startTimerHz(3);
}

MainComponent::~MainComponent()
{
    stopTimer();
    deviceManager_.removeAudioCallback(&audioCallback_);
    loopback_.stop();
    deviceManager_.closeAudioDevice();
    setLookAndFeel(nullptr);
}

void MainComponent::initialiseAudio()
{
    audioInitError_ = deviceManager_.initialise(2, 2, nullptr, true);

    juce::String wasapiType;
    for (auto* type : deviceManager_.getAvailableDeviceTypes())
    {
        const auto name = type->getTypeName();
        if (name.containsIgnoreCase("WASAPI") || name.containsIgnoreCase("Windows Audio"))
        {
            wasapiType = name;
            break;
        }
    }

    if (wasapiType.isNotEmpty())
        deviceManager_.setCurrentAudioDeviceType(wasapiType, true);

    if (auto* type = deviceManager_.getCurrentDeviceTypeObject())
    {
        const auto inputNames = type->getDeviceNames(true);
        for (const auto& inputName : inputNames)
        {
            if (inputName.containsIgnoreCase("CABLE Output")
                || inputName.containsIgnoreCase("VB-Audio Virtual Cable"))
            {
                auto setup = deviceManager_.getAudioDeviceSetup();
                setup.inputDeviceName = inputName;
                setup.useDefaultInputChannels = false;
                setup.inputChannels.clear();
                setup.inputChannels.setRange(0, 2, true);
                const auto setupError = deviceManager_.setAudioDeviceSetup(setup, true);
                if (setupError.isNotEmpty())
                    audioInitError_ = setupError;
                break;
            }
        }
    }

}

void MainComponent::setCaptureMode(int modeId)
{
    captureModeId_ = modeId == 2 ? 2 : 1;
    if (captureModeId_ == 1)
    {
        audioCallback_.setCaptureMode(HostAudioCallback::CaptureMode::defaultRenderLoopback);
        audioCallback_.setFeedbackGuard(true); // stay silent until the source/output endpoints are checked
        juce::String error;
        if (!loopback_.start(loopbackFifo_, error))
            liveStatus_ = "LOOPBACK START ERROR  /  " + error;
    }
    else
    {
        audioCallback_.setCaptureMode(HostAudioCallback::CaptureMode::selectedWasapiInput);
        audioCallback_.setFeedbackGuard(false);
        loopback_.stop();
        loopbackFifo_.reset();
    }
}

juce::String MainComponent::getAudioStatus() const
{
    if (audioInitError_.isNotEmpty())
        return "AUDIO DEVICE NOTICE  /  " + audioInitError_;

    const auto* outputDevice = deviceManager_.getCurrentAudioDevice();
    const auto outputName = outputDevice != nullptr ? outputDevice->getName() : juce::String("No output device");
    const auto setup = deviceManager_.getAudioDeviceSetup();
    const auto inputName = setup.inputDeviceName.isNotEmpty() ? setup.inputDeviceName
                                                              : juce::String("No active input");

    if (captureModeId_ == 1)
    {
        const auto source = loopback_.getSourceDeviceName();
        if (loopback_.getLastError().isNotEmpty())
            return "WASAPI LOOPBACK ERROR  /  " + loopback_.getLastError();
        if (!loopback_.isConnected())
            return "WASAPI LOOPBACK STARTING  /  " + (source.isNotEmpty() ? source : juce::String("default render endpoint"));
        if (audioCallback_.isFeedbackGuardActive())
            return "FEEDBACK GUARD  /  output muted — choose a different playback endpoint";
        return "WASAPI LOOPBACK LIVE  /  " + source + "  →  " + outputName;
    }

    return "WASAPI INPUT LIVE  /  " + inputName + "  →  " + outputName;
}

juce::String MainComponent::getEngineStatus() const
{
    if (!engine_.isLoaded())
        return engine_.getError();
    return "NEONENGINE.DLL  /  ABI " + juce::String(NEON_ENGINE_ABI_VERSION)
         + "  /  10-NODE AUDIOPROCESSORGRAPH";
}

void MainComponent::timerCallback()
{
    const auto* output = deviceManager_.getCurrentAudioDevice();
    bool sameEndpoint = false;

    if (captureModeId_ == 1)
    {
        const auto sourceName = loopback_.getSourceDeviceName();
        const auto outputName = output != nullptr ? output->getName() : juce::String();
        if (!loopback_.isConnected() || sourceName.isEmpty() || outputName.isEmpty())
        {
            sameEndpoint = true;
        }
        else
        {
            sameEndpoint = sourceName.compareIgnoreCase(outputName) == 0
                || outputName.containsIgnoreCase(sourceName)
                || sourceName.containsIgnoreCase(outputName);
        }
    }

    audioCallback_.setFeedbackGuard(sameEndpoint);
    liveStatus_ = getAudioStatus();
    repaint();
}

void MainComponent::paint(juce::Graphics& g)
{
    g.fillAll(NeonLookAndFeel::background());
    g.setColour(NeonLookAndFeel::panel());
    g.fillRect(0, 0, getWidth(), headerHeight);
    g.setColour(NeonLookAndFeel::cyan().withAlpha(0.62f));
    g.drawLine(0.0f, static_cast<float>(headerHeight - 1), static_cast<float>(getWidth()),
               static_cast<float>(headerHeight - 1), 1.0f);

    g.setColour(NeonLookAndFeel::textPrimary());
    g.setFont(juce::Font(juce::FontOptions(23.0f, juce::Font::bold)));
    g.drawText("NEON", 24, 13, 93, 30, juce::Justification::centredLeft);
    g.setColour(NeonLookAndFeel::cyan());
    g.drawText("AUDIO MATRIX", 119, 13, 180, 30, juce::Justification::centredLeft);

    g.setColour(NeonLookAndFeel::textMuted());
    g.setFont(juce::Font(juce::FontOptions(10.5f, juce::Font::bold)));
    g.drawText(getEngineStatus(), 25, 47, getWidth() - 50, 16, juce::Justification::centredLeft);

    const auto statusColour = audioCallback_.isFeedbackGuardActive()
                            ? NeonLookAndFeel::magenta() : NeonLookAndFeel::lime();
    g.setColour(statusColour.withAlpha(0.14f));
    g.fillRoundedRectangle(static_cast<float>(getWidth() - 332), 20.0f, 306.0f, 28.0f, 12.0f);
    g.setColour(statusColour);
    g.setFont(juce::Font(juce::FontOptions(10.5f, juce::Font::bold)));
    g.drawFittedText(liveStatus_, getWidth() - 322, 24, 286, 20,
                     juce::Justification::centredRight, 1);
}

void MainComponent::resized()
{
    const auto buttonWidth = juce::jmax(0, juce::jmin(190, getWidth() - 674));
    const auto showOriginalButton = buttonWidth >= 120;
    listenOriginalButton_.setVisible(showOriginalButton);
    if (showOriginalButton)
        listenOriginalButton_.setBounds(320, 14, buttonWidth, 30);

    tabs_.setBounds(0, headerHeight, getWidth(), getHeight() - headerHeight);
}
