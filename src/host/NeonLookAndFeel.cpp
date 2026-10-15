#include "NeonLookAndFeel.h"

NeonLookAndFeel::NeonLookAndFeel()
{
    setColour(juce::ResizableWindow::backgroundColourId, background());
    setColour(juce::Label::textColourId, textPrimary());
    setColour(juce::TextButton::buttonColourId, panelRaised());
    setColour(juce::TextButton::buttonOnColourId, cyan().withAlpha(0.22f));
    setColour(juce::TextButton::textColourOffId, textPrimary());
    setColour(juce::TextButton::textColourOnId, cyan());
    setColour(juce::ComboBox::backgroundColourId, panelRaised());
    setColour(juce::ComboBox::textColourId, textPrimary());
    setColour(juce::ComboBox::outlineColourId, textMuted().withAlpha(0.35f));
    setColour(juce::Slider::textBoxTextColourId, textPrimary());
    setColour(juce::Slider::textBoxBackgroundColourId, panel());
    setColour(juce::Slider::textBoxOutlineColourId, textMuted().withAlpha(0.22f));
    setColour(juce::PopupMenu::backgroundColourId, panel());
    setColour(juce::PopupMenu::textColourId, textPrimary());
    setColour(juce::PopupMenu::highlightedBackgroundColourId, cyan().withAlpha(0.18f));
    setColour(juce::PopupMenu::highlightedTextColourId, cyan());
    setColour(juce::TabbedComponent::outlineColourId, cyan().withAlpha(0.20f));
    setColour(juce::TabbedComponent::backgroundColourId, background());
    setColour(juce::TabbedButtonBar::tabTextColourId, textMuted());
    setColour(juce::TabbedButtonBar::frontTextColourId, cyan());
    setColour(juce::TabbedButtonBar::tabOutlineColourId, textMuted().withAlpha(0.16f));
    setColour(juce::TabbedButtonBar::frontOutlineColourId, cyan().withAlpha(0.55f));
}

void NeonLookAndFeel::drawRotarySlider(juce::Graphics& g,
                                       int x, int y, int width, int height,
                                       float sliderPosition,
                                       float rotaryStartAngle,
                                       float rotaryEndAngle,
                                       juce::Slider& slider)
{
    const auto size = static_cast<float>(juce::jmin(width, height));
    const auto radius = size * 0.39f;
    const auto centreX = static_cast<float>(x) + static_cast<float>(width) * 0.5f;
    const auto centreY = static_cast<float>(y) + static_cast<float>(height) * 0.5f;
    const auto angle = rotaryStartAngle + sliderPosition * (rotaryEndAngle - rotaryStartAngle);

    const auto knobBounds = juce::Rectangle<float>(centreX - radius, centreY - radius, radius * 2.0f, radius * 2.0f);
    g.setColour(juce::Colour(0xff11141a));
    g.fillEllipse(knobBounds);
    g.setColour(textMuted().withAlpha(0.20f));
    g.drawEllipse(knobBounds, 1.3f);

    juce::Path track;
    track.addCentredArc(centreX, centreY, radius + 5.0f, radius + 5.0f, 0.0f,
                        rotaryStartAngle, rotaryEndAngle, true);
    g.setColour(juce::Colour(0xff30343d));
    g.strokePath(track, juce::PathStrokeType(4.2f, juce::PathStrokeType::curved,
                                              juce::PathStrokeType::rounded));

    juce::Path valueArc;
    valueArc.addCentredArc(centreX, centreY, radius + 5.0f, radius + 5.0f, 0.0f,
                           rotaryStartAngle, angle, true);
    const auto tint = slider.findColour(juce::Slider::rotarySliderFillColourId, true);
    g.setColour(tint.isTransparent() ? cyan() : tint);
    g.strokePath(valueArc, juce::PathStrokeType(4.2f, juce::PathStrokeType::curved,
                                                 juce::PathStrokeType::rounded));

    const auto pointerLength = radius * 0.68f;
    const auto pointerWidth = juce::jmax(2.0f, size * 0.035f);
    juce::Path pointer;
    pointer.addRoundedRectangle(-pointerWidth * 0.5f, -radius * 0.70f,
                                pointerWidth, pointerLength, pointerWidth * 0.5f);
    g.setColour(textPrimary());
    g.fillPath(pointer, juce::AffineTransform::rotation(angle).translated(centreX, centreY));

    g.setColour(juce::Colour(0xff08090e));
    g.fillEllipse(centreX - 3.0f, centreY - 3.0f, 6.0f, 6.0f);
}

void NeonLookAndFeel::drawComboBox(juce::Graphics& g, int width, int height, bool isButtonDown,
                                   int buttonX, int buttonY, int buttonW, int buttonH,
                                   juce::ComboBox& box)
{
    const auto bounds = juce::Rectangle<float>(0.5f, 0.5f,
                                                static_cast<float>(width) - 1.0f,
                                                static_cast<float>(height) - 1.0f);
    juce::ColourGradient fill(panelRaised().brighter(isButtonDown ? 0.12f : 0.04f), 0.0f, 0.0f,
                              panelRaised(), 0.0f, static_cast<float>(height), false);
    g.setGradientFill(fill);
    g.fillRoundedRectangle(bounds, 9.0f);

    if (isButtonDown)
    {
        g.setColour(cyan().withAlpha(0.06f));
        g.fillRoundedRectangle(bounds, 9.0f);
    }

    const auto focused = box.hasKeyboardFocus(false);
    const auto outline = focused ? cyan().withAlpha(0.78f)
                                 : box.findColour(juce::ComboBox::outlineColourId)
                                       .withMultipliedAlpha(box.isEnabled() ? 1.0f : 0.45f);
    g.setColour(outline);
    g.drawRoundedRectangle(bounds, 9.0f, focused ? 1.4f : 1.0f);

    const auto centreX = static_cast<float>(buttonX) + static_cast<float>(buttonW) * 0.5f;
    const auto centreY = static_cast<float>(buttonY) + static_cast<float>(buttonH) * 0.5f;
    juce::Path chevron;
    chevron.startNewSubPath(centreX - 4.5f, centreY - 1.5f);
    chevron.lineTo(centreX, centreY + 2.5f);
    chevron.lineTo(centreX + 4.5f, centreY - 1.5f);
    g.setColour(box.findColour(juce::ComboBox::arrowColourId)
                    .withMultipliedAlpha(box.isEnabled() ? 1.0f : 0.45f));
    g.strokePath(chevron, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved,
                                                juce::PathStrokeType::rounded));
}

void NeonLookAndFeel::positionComboBoxText(juce::ComboBox& box, juce::Label& label)
{
    label.setBounds(12, 1, juce::jmax(0, box.getWidth() - 44), box.getHeight() - 2);
    label.setFont(getComboBoxFont(box));
    label.setJustificationType(juce::Justification::centredLeft);
}

void NeonLookAndFeel::drawPopupMenuBackground(juce::Graphics& g, int width, int height)
{
    const auto bounds = juce::Rectangle<float>(0.5f, 0.5f,
                                                static_cast<float>(width) - 1.0f,
                                                static_cast<float>(height) - 1.0f);
    g.setColour(panelRaised());
    g.fillRoundedRectangle(bounds, 9.0f);
    g.setColour(textMuted().withAlpha(0.30f));
    g.drawRoundedRectangle(bounds, 9.0f, 1.0f);
}

void NeonLookAndFeel::drawPopupMenuItem(juce::Graphics& g, const juce::Rectangle<int>& area,
                                        bool isSeparator, bool isActive, bool isHighlighted,
                                        bool isTicked, bool hasSubMenu, const juce::String& text,
                                        const juce::String& shortcutKeyText, const juce::Drawable* icon,
                                        const juce::Colour* textColour)
{
    if (isSeparator)
    {
        g.setColour(textMuted().withAlpha(0.20f));
        g.drawHorizontalLine(area.getCentreY(), static_cast<float>(area.getX() + 12),
                             static_cast<float>(area.getRight() - 12));
        return;
    }

    if (isHighlighted && isActive)
    {
        g.setColour(cyan().withAlpha(0.12f));
        g.fillRoundedRectangle(area.toFloat().reduced(4.0f, 1.0f), 5.0f);
    }

    const auto foreground = isActive
                          ? (isHighlighted ? cyan() : (textColour != nullptr ? *textColour : textPrimary()))
                          : textMuted();
    g.setColour(foreground);
    g.setFont(getPopupMenuFont());

    if (icon != nullptr)
    {
        icon->drawWithin(g, juce::Rectangle<float>(area.getX() + 8.0f, area.getY() + 4.0f,
                                                   18.0f, static_cast<float>(area.getHeight()) - 8.0f),
                         juce::RectanglePlacement::centred, isActive ? 1.0f : 0.45f);
    }
    else if (isTicked)
    {
        juce::Path tick;
        tick.startNewSubPath(static_cast<float>(area.getX() + 10), static_cast<float>(area.getCentreY()));
        tick.lineTo(static_cast<float>(area.getX() + 14), static_cast<float>(area.getCentreY() + 4));
        tick.lineTo(static_cast<float>(area.getX() + 21), static_cast<float>(area.getCentreY() - 4));
        g.setColour(cyan());
        g.strokePath(tick, juce::PathStrokeType(1.8f, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));
    }

    const auto rightInset = hasSubMenu ? 26 : 12;
    auto textArea = area.withTrimmedLeft(30).withTrimmedRight(rightInset);
    if (shortcutKeyText.isNotEmpty())
    {
        g.drawFittedText(shortcutKeyText, area.getRight() - 92, area.getY(), 70,
                         area.getHeight(), juce::Justification::centredRight, 1);
        textArea = textArea.withTrimmedRight(82);
    }
    g.drawFittedText(text, textArea, juce::Justification::centredLeft, 1);

    if (hasSubMenu)
    {
        juce::Path arrow;
        arrow.addTriangle(static_cast<float>(area.getRight() - 16), static_cast<float>(area.getCentreY() - 4),
                          static_cast<float>(area.getRight() - 16), static_cast<float>(area.getCentreY() + 4),
                          static_cast<float>(area.getRight() - 11), static_cast<float>(area.getCentreY()));
        g.setColour(foreground);
        g.fillPath(arrow);
    }
}
