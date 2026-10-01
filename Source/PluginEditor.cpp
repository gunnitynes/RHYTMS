#include "PluginEditor.h"

using namespace rhytms;
using namespace rhytms::ui;

namespace
{
constexpr int editorWidth = 1100;
constexpr int editorHeight = 744;

struct OrbitKnobSpec { const char* id; const char* label; };
constexpr OrbitKnobSpec orbitKnobs[] = {
    { "steps", "steps" }, { "pulses", "pulses" }, { "rotate", "rotate" }, { "rate", "rate" },
    { "pitch", "pitch" }, { "reach", "reach" },   { "length", "length" }, { "colour", "colour" },
    { "pan", "pan" },     { "level", "level" },   { "reverse", "reverse" },
};
}

RhytmsEditor::RhytmsEditor (RhytmsProcessor& p)
    : AudioProcessorEditor (&p), proc (p), rings (p), weather (p), memoryView (p)
{
    setLookAndFeel (&look);

    addAndMakeVisible (rings);
    addAndMakeVisible (weather);
    addAndMakeVisible (memoryView);

    syncButton.setColour (juce::ToggleButton::tickColourId, colours::red);
    holdButton.setColour (juce::ToggleButton::tickColourId, colours::red);
    addAndMakeVisible (syncButton);
    addAndMakeVisible (holdButton);
    buttonAttachments.push_back (std::make_unique<ButtonAttachment> (proc.apvts, "sync", syncButton));
    buttonAttachments.push_back (std::make_unique<ButtonAttachment> (proc.apvts, "hold", holdButton));

    addKnob (senseKnob, "sense", "sense", colours::ochre);
    addKnob (memoryKnob, "memory", "memory", colours::ochre);
    addKnob (bpmKnob, "bpm", "free tempo", colours::ink);

    addKnob (bloomKnob, "bloom", "bloom", colours::red);
    addKnob (evolveKnob, "evolve", "evolve", colours::red);
    addKnob (mirrorKnob, "mirror", "mirror", colours::ochre);
    addKnob (swingKnob, "swing", "swing", colours::ink);
    addKnob (haloKnob, "halo", "halo", colours::blue);
    addKnob (dustKnob, "dust", "dust", colours::blue);
    addKnob (dryKnob, "dry", "dry", colours::ink);
    addKnob (wetKnob, "wet", "wet", colours::ink);
    addKnob (outKnob, "out", "out", colours::ink);

    for (int o = 0; o < numOrbits; ++o)
    {
        auto& row = rows[(size_t) o];
        static const char* numerals[] = { "i", "ii", "iii" };
        row.on = std::make_unique<juce::ToggleButton> (numerals[o]);
        row.on->setColour (juce::ToggleButton::tickColourId, colours::orbit (o));
        addAndMakeVisible (*row.on);
        row.onAttachment = std::make_unique<ButtonAttachment> (proc.apvts, orbitId (o, "on"), *row.on);

        for (const auto& spec : orbitKnobs)
        {
            row.knobs.emplace_back();
            addKnob (row.knobs.back(), orbitId (o, spec.id), spec.label, colours::orbit (o));
        }
    }

    regrowButton.setTooltip ("Forget what the orbits have grown into and return to their skeletons");
    throwButton.setTooltip ("Throw new shapes for all three orbits");
    clearButton.setTooltip ("Release every pinned step");
    regrowButton.onClick = [this] { proc.requestRegrow(); };
    throwButton.onClick = [this] { throwDice(); };
    clearButton.onClick = [this] { for (int o = 0; o < numOrbits; ++o) proc.clearPins (o); };
    addAndMakeVisible (regrowButton);
    addAndMakeVisible (throwButton);
    addAndMakeVisible (clearButton);

    setSize (editorWidth, editorHeight);
    startTimerHz (30);
}

RhytmsEditor::~RhytmsEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void RhytmsEditor::addKnob (std::unique_ptr<Knob>& knob, const juce::String& id, const juce::String& label, juce::Colour accent)
{
    knob = std::make_unique<Knob> (label, accent);
    addAndMakeVisible (*knob);
    sliderAttachments.push_back (std::make_unique<SliderAttachment> (proc.apvts, id, knob->slider));
    knob->slider.setDoubleClickReturnValue (true, (double) proc.apvts.getParameterRange (id).convertFrom0to1 (
        proc.apvts.getParameter (id)->getDefaultValue()));
}

void RhytmsEditor::throwDice()
{
    auto set = [this] (const juce::String& id, float value)
    {
        if (auto* prm = proc.apvts.getParameter (id))
        {
            prm->beginChangeGesture();
            prm->setValueNotifyingHost (prm->convertTo0to1 (value));
            prm->endChangeGesture();
        }
    };

    static constexpr int stepChoices[] = { 5, 7, 8, 9, 10, 11, 12, 13, 16, 16, 24 };
    static constexpr float pitchChoices[] = { -12.0f, -7.0f, -5.0f, 0.0f, 0.0f, 0.0f, 5.0f, 7.0f, 12.0f };

    for (int o = 0; o < numOrbits; ++o)
    {
        const int steps = stepChoices[dice.nextInt ((int) std::size (stepChoices))];
        const int pulses = juce::jlimit (1, steps, 1 + dice.nextInt (juce::jmax (1, steps * 3 / 5)));
        set (orbitId (o, "steps"), (float) steps);
        set (orbitId (o, "pulses"), (float) pulses);
        set (orbitId (o, "rotate"), (float) dice.nextInt (steps));
        set (orbitId (o, "rate"), (float) dice.nextInt (RhytmsProcessor::rateNames().size()));
        set (orbitId (o, "pitch"), pitchChoices[dice.nextInt ((int) std::size (pitchChoices))]);
        set (orbitId (o, "reach"), dice.nextFloat());
        set (orbitId (o, "length"), 0.05f + 0.8f * dice.nextFloat() * dice.nextFloat());
        set (orbitId (o, "colour"), dice.nextFloat() * 1.6f - 0.8f);
        set (orbitId (o, "pan"), dice.nextFloat() * 1.4f - 0.7f);
        set (orbitId (o, "reverse"), dice.nextFloat() < 0.4f ? dice.nextFloat() * 0.6f : 0.0f);
    }
    proc.requestRegrow();
}

void RhytmsEditor::timerCallback()
{
    const bool synced = proc.apvts.getRawParameterValue ("sync")->load() > 0.5f;
    bpmKnob->setAlpha (synced ? 0.35f : 1.0f);
    rings.repaint();
    memoryView.repaint();
    weather.repaint();
    repaint (0, 0, getWidth(), 60);
}

void RhytmsEditor::paint (juce::Graphics& g)
{
    g.fillAll (colours::paper);

    // paper grain
    juce::Random grain (11);
    for (int i = 0; i < 1400; ++i)
    {
        g.setColour (colours::ink.withAlpha (0.018f + grain.nextFloat() * 0.03f));
        g.fillRect (grain.nextFloat() * getWidth(), grain.nextFloat() * getHeight(), 1.0f, 1.0f);
    }

    g.setColour (colours::ink);
    g.setFont (serif (34.0f, true));
    g.drawText ("rhytms", 24, 8, 160, 44, juce::Justification::left);
    g.setColour (colours::ink.withAlpha (0.6f));
    g.setFont (serif (13.0f, true));
    g.drawText ("a rhythmic sostenuto  /  it listens, remembers, and plays your sound back as rhythm",
                150, 22, 560, 22, juce::Justification::left);

    const bool synced = proc.apvts.getRawParameterValue ("sync")->load() > 0.5f;
    g.setFont (mono (11.0f));
    g.setColour (synced ? colours::red : colours::ink.withAlpha (0.6f));
    g.drawText (juce::String (proc.visual.bpm.load(), 1) + (synced ? " bpm  daw" : " bpm  free"),
                getWidth() - 150, 20, 126, 22, juce::Justification::right);

    g.setColour (colours::faint);
    g.drawHorizontalLine (58, 24.0f, (float) getWidth() - 24.0f);
    g.drawHorizontalLine (522, 24.0f, (float) getWidth() - 24.0f);
    g.drawVerticalLine (348, 70.0f, 510.0f);
    g.drawVerticalLine (770, 70.0f, 510.0f);

    auto caption = [&] (const juce::String& text, int x, int y)
    {
        g.setColour (colours::ink.withAlpha (0.5f));
        g.setFont (serif (12.0f, true));
        g.drawText (text, x, y, 300, 16, juce::Justification::left);
    };
    caption ("i.  listen", 24, 64);
    caption ("ii.  orbits", 362, 64);
    caption ("iii.  bloom & space", 790, 64);
    caption ("iv.  voices of the orbits", 24, 528);

    g.setColour (colours::ink.withAlpha (0.45f));
    g.setFont (serif (11.5f, true));
    g.drawFittedText ("click a step to pin it: always, then never, then free.\nscroll on a circle to turn it.",
                      790, 450, 290, 34, juce::Justification::topLeft, 2);
}

void RhytmsEditor::resized()
{
    syncButton.setBounds (getWidth() - 290, 18, 130, 26);

    // i. listen
    memoryView.setBounds (24, 86, 310, 82);
    holdButton.setBounds (24, 176, 200, 24);
    senseKnob->setBounds (24, 206, 80, 74);
    memoryKnob->setBounds (110, 206, 80, 74);
    bpmKnob->setBounds (196, 206, 80, 74);
    weather.setBounds (24, 288, 310, 226);

    // ii. orbits
    rings.setBounds (360, 80, 400, 400);
    const int bx = 360 + (400 - 3 * 86 + 6) / 2;
    regrowButton.setBounds (bx, 486, 80, 24);
    throwButton.setBounds (bx + 86, 486, 80, 24);
    clearButton.setBounds (bx + 172, 486, 80, 24);

    // iii. bloom & space
    rhytms::ui::Knob* grid[3][3] = {
        { bloomKnob.get(), evolveKnob.get(), mirrorKnob.get() },
        { swingKnob.get(), haloKnob.get(), dustKnob.get() },
        { dryKnob.get(), wetKnob.get(), outKnob.get() },
    };
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c)
            grid[r][c]->setBounds (790 + c * 98, 88 + r * 118, 92, 92);

    // iv. voices
    for (int o = 0; o < numOrbits; ++o)
    {
        const int y = 548 + o * 64;
        auto& row = rows[(size_t) o];
        row.on->setBounds (24, y + 18, 70, 24);
        for (size_t k = 0; k < row.knobs.size(); ++k)
            row.knobs[k]->setBounds (100 + (int) k * 88, y, 70, 62);
    }
}
