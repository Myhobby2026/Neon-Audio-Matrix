#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

class NeonLookAndFeel final : public juce::LookAndFeel_V2
{
public:
    NeonLookAndFeel();

    void drawRotarySlider(juce::Graphics& graphics,
                          int x, int y, int width, int height,
                          float sliderPosition,
                          float rotaryStartAngle,
                          float rotaryEndAngle,
                          juce::Slider& slider) override;

    void drawComboBox(juce::Graphics& graphics, int width, int height, bool isButtonDown,
                      int buttonX, int buttonY, int buttonW, int buttonH,
                      juce::ComboBox& box) override;
    void positionComboBoxText(juce::ComboBox& box, juce::Label& label) override;
    void drawPopupMenuBackground(juce::Graphics& graphics, int width, int height) override;
    void drawPopupMenuItem(juce::Graphics& graphics, const juce::Rectangle<int>& area,
                           bool isSeparator, bool isActive, bool isHighlighted, bool isTicked,
                           bool hasSubMenu, const juce::String& text, const juce::String& shortcutKeyText,
                           const juce::Drawable* icon, const juce::Colour* textColour) override;

    static juce::Colour background() noexcept { return juce::Colour(0xff08090e); }
    static juce::Colour panel() noexcept { return juce::Colour(0xff17191f); }
    static juce::Colour panelRaised() noexcept { return juce::Colour(0xff10131b); }
    static juce::Colour cyan() noexcept { return juce::Colour(0xff12d9ed); }
    static juce::Colour magenta() noexcept { return juce::Colour(0xffed2db8); }
    static juce::Colour violet() noexcept { return juce::Colour(0xff9c79f5); }
    static juce::Colour lime() noexcept { return juce::Colour(0xffbaff58); }
    static juce::Colour textPrimary() noexcept { return juce::Colour(0xffe7e9f0); }
    static juce::Colour textMuted() noexcept { return juce::Colour(0xff858b9b); }
};
