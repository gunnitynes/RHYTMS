#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace rhytms::ui
{

// Paper, ink and three pigments: a graphic score more than a machine.
namespace colours
{
    const juce::Colour paper   { 0xffebe5d7 };
    const juce::Colour paperDk { 0xffe0d9c8 };
    const juce::Colour ink     { 0xff1c1b18 };
    const juce::Colour faint   { 0x2e1c1b18 };
    const juce::Colour ghost   { 0x141c1b18 };
    const juce::Colour red     { 0xffc5402a };
    const juce::Colour blue    { 0xff284a8e };
    const juce::Colour ochre   { 0xffb98a2f };

    inline juce::Colour orbit (int o)
    {
        switch (o)
        {
            case 0:  return ink;
            case 1:  return red;
            default: return blue;
        }
    }
}

// First installed face from a list of book serifs; never asks for a missing one.
inline const juce::String& serifName()
{
    static const juce::String name = []
    {
        const auto installed = juce::Font::findAllTypefaceNames();
        for (auto* candidate : { "Georgia", "Baskerville", "Palatino", "Times New Roman",
                                 "DejaVu Serif", "Liberation Serif", "FreeSerif" })
            if (installed.contains (candidate))
                return juce::String (candidate);
        return juce::Font::getDefaultSerifFontName();
    }();
    return name;
}

inline juce::Font serif (float size, bool italic = false)
{
    return juce::Font (juce::FontOptions (serifName(), size, italic ? juce::Font::italic : juce::Font::plain));
}

inline juce::Font mono (float size)
{
    return juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), size, juce::Font::plain));
}

class Look : public juce::LookAndFeel_V4
{
public:
    Look()
    {
        setColour (juce::ResizableWindow::backgroundColourId, colours::paper);
        setColour (juce::Slider::textBoxTextColourId, colours::ink);
        setColour (juce::TextButton::buttonColourId, colours::paper);
        setColour (juce::TextButton::textColourOffId, colours::ink);
        setColour (juce::PopupMenu::backgroundColourId, colours::paper);
        setColour (juce::PopupMenu::textColourId, colours::ink);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, colours::ink);
        setColour (juce::PopupMenu::highlightedTextColourId, colours::paper);
    }

    // A hand-drawn-feeling dial: thin ink circle, a red stroke for the value.
    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider& s) override
    {
        const auto area = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (3.0f);
        const float r = juce::jmin (area.getWidth(), area.getHeight()) * 0.5f;
        const auto c = area.getCentre();
        const auto accent = s.findColour (juce::Slider::rotarySliderFillColourId);

        g.setColour (colours::faint);
        g.drawEllipse (c.x - r, c.y - r, r * 2.0f, r * 2.0f, 1.0f);

        // tick marks like a clock face
        for (int i = 0; i <= 10; ++i)
        {
            const float a = startAngle + (endAngle - startAngle) * (float) i / 10.0f;
            const auto p1 = c.getPointOnCircumference (r + 1.5f, a);
            const auto p2 = c.getPointOnCircumference (r + (i % 5 == 0 ? 4.5f : 3.0f), a);
            g.setColour (colours::faint);
            g.drawLine ({ p1, p2 }, 0.8f);
        }

        const float angle = startAngle + pos * (endAngle - startAngle);
        juce::Path arc;
        const bool bipolar = s.getMinimum() < 0.0 && s.getMaximum() > 0.0;
        const float from = bipolar ? (startAngle + endAngle) * 0.5f : startAngle;
        arc.addCentredArc (c.x, c.y, r - 3.0f, r - 3.0f, 0.0f, from, angle, true);
        g.setColour (accent.withAlpha (0.85f));
        g.strokePath (arc, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        const auto tip = c.getPointOnCircumference (r - 3.0f, angle);
        g.setColour (colours::ink);
        g.drawLine ({ c.getPointOnCircumference (r * 0.25f, angle), tip }, 1.2f);
        g.fillEllipse (c.x - 1.5f, c.y - 1.5f, 3.0f, 3.0f);
    }

    void drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool over, bool) override
    {
        const auto bounds = b.getLocalBounds().toFloat();
        const float d = juce::jmin (12.0f, bounds.getHeight() - 4.0f);
        const auto box = juce::Rectangle<float> (2.0f, (bounds.getHeight() - d) * 0.5f, d, d);
        const auto accent = b.findColour (juce::ToggleButton::tickColourId);

        g.setColour (over ? colours::ink : colours::ink.withAlpha (0.7f));
        g.drawEllipse (box, 1.0f);
        if (b.getToggleState())
            g.setColour (accent), g.fillEllipse (box.reduced (2.5f));

        g.setColour (colours::ink);
        g.setFont (serif (13.0f, true));
        g.drawText (b.getButtonText(), bounds.withLeft (d + 8.0f), juce::Justification::centredLeft);
    }

    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down) override
    {
        auto r = b.getLocalBounds().toFloat().reduced (0.5f);
        if (down) g.setColour (colours::ink.withAlpha (0.12f)), g.fillRect (r);
        g.setColour (over ? colours::ink : colours::ink.withAlpha (0.45f));
        g.drawRect (r, 1.0f);
    }

    juce::Font getTextButtonFont (juce::TextButton&, int) override { return serif (13.0f, true); }
    juce::Font getPopupMenuFont() override { return serif (14.0f); }
};

// Rotary dial with its lowercase name underneath; while touched it shows
// its value instead.
class Knob : public juce::Component
{
public:
    Knob (const juce::String& labelText, juce::Colour accent = colours::red)
        : label (labelText)
    {
        slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
        slider.setColour (juce::Slider::rotarySliderFillColourId, accent);
        slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
        slider.setMouseDragSensitivity (180);
        slider.onValueChange = [this] { repaint(); };
        slider.addMouseListener (this, false);
        addAndMakeVisible (slider);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        r.removeFromBottom (15);
        const int d = juce::jmin (r.getWidth(), r.getHeight());
        slider.setBounds (r.withSizeKeepingCentre (d, d));
    }

    void paint (juce::Graphics& g) override
    {
        const bool active = slider.isMouseOverOrDragging();
        g.setColour (active ? colours::red : colours::ink.withAlpha (0.8f));
        g.setFont (active ? mono (10.5f) : serif (12.5f, true));
        const auto text = active ? slider.getTextFromValue (slider.getValue()) : label;
        g.drawFittedText (text, getLocalBounds().removeFromBottom (15), juce::Justification::centred, 1);
    }

    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }

    juce::Slider slider;

private:
    juce::String label;
};

} // namespace rhytms::ui
