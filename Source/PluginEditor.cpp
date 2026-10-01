#include "PluginEditor.h"
#include "PluginVersion.h"

using namespace rhytms;
using namespace rhytms::ui;

namespace
{
struct OrbitKnobSpec { const char* id; const char* label; };
constexpr OrbitKnobSpec orbitKnobs[] = {
    { "steps", "steps" }, { "pulses", "pulses" }, { "rotate", "rotate" }, { "rate", "rate" },
    { "pitch", "pitch" }, { "reach", "reach" },   { "length", "length" }, { "colour", "colour" },
    { "pan", "pan" },     { "level", "level" },   { "reverse", "reverse" },
};

constexpr int saveAsId = 1;
constexpr int firstPresetId = 100;
}

// ===========================================================================

RhytmsPanel::RhytmsPanel (RhytmsProcessor& p)
    : proc (p), rings (p), weather (p), memoryView (p)
{
    addAndMakeVisible (rings);
    addAndMakeVisible (weather);
    addAndMakeVisible (memoryView);

    // ---- header ----
    presetBox.setTextWhenNothingSelected ("Init");
    presetBox.setTooltip ("Presets. Choose \"save as...\" to keep the current state as a user preset.");
    presetBox.onChange = [this]
    {
        const int id = presetBox.getSelectedId();
        if (id == saveAsId)
        {
            showSelectedPreset();
            savePresetAs();
            return;
        }
        const int index = id - firstPresetId;
        if (juce::isPositiveAndBelow (index, (int) presetEntries.size()))
        {
            proc.loadPreset (presetEntries[(size_t) index]);
            shownPresetName = proc.getCurrentPresetName();
        }
    };
    addAndMakeVisible (presetBox);

    prevButton.setTooltip ("Previous preset");
    nextButton.setTooltip ("Next preset");
    prevButton.onClick = [this] { stepPreset (-1); };
    nextButton.onClick = [this] { stepPreset (1); };
    addAndMakeVisible (prevButton);
    addAndMakeVisible (nextButton);

    randomButton.setTooltip ("Randomize every unlocked knob. Alt-click a knob to lock it (ochre dot), "
                             "ctrl/cmd-drag a knob to limit its range (red arc). Mix and sense start locked.");
    randomButton.onClick = [this] { proc.randomize(); };
    randomButton.setColour (juce::TextButton::textColourOffId, colours::red);
    addAndMakeVisible (randomButton);

    categoryButton.setTooltip ("Steer randomize and morph towards a kind of rhythm");
    categoryButton.setColour (juce::TextButton::textColourOffId, colours::red);
    categoryButton.onClick = [this] { showCategoryMenu(); };
    addAndMakeVisible (categoryButton);

    morphButton.setColour (juce::ToggleButton::tickColourId, colours::red);
    morphButton.setTooltip ("Let every unlocked knob drift continuously between random states over the morph time");
    addAndMakeVisible (morphButton);
    buttonAttachments.push_back (std::make_unique<ButtonAttachment> (proc.apvts, "morph", morphButton));
    addKnob (morphTimeKnob, "morphTime", "time", colours::red);
    morphTimeKnob->setLockable (false);

    undoButton.setTooltip ("Step back through randomize, throw, morph and preset changes");
    redoButton.setTooltip ("Step forward again");
    undoButton.onClick = [this] { proc.undo(); };
    redoButton.onClick = [this] { proc.redo(); };
    addAndMakeVisible (undoButton);
    addAndMakeVisible (redoButton);

    // ---- i. listen ----
    syncButton.setColour (juce::ToggleButton::tickColourId, colours::red);
    holdButton.setColour (juce::ToggleButton::tickColourId, colours::red);
    syncButton.setTooltip ("Follow the DAW's tempo, meter and bar position");
    holdButton.setTooltip ("Freeze the memory: keep playing what was just caught");
    addAndMakeVisible (syncButton);
    addAndMakeVisible (holdButton);
    buttonAttachments.push_back (std::make_unique<ButtonAttachment> (proc.apvts, "sync", syncButton));
    buttonAttachments.push_back (std::make_unique<ButtonAttachment> (proc.apvts, "hold", holdButton));

    addKnob (senseKnob, "sense", "sense", colours::ochre);
    addKnob (memoryKnob, "memory", "memory", colours::ochre);
    addKnob (bpmKnob, "bpm", "free tempo", colours::ink);
    bpmKnob->setLockable (false);

    // ---- iii. bloom & space ----
    addKnob (bloomKnob, "bloom", "bloom", colours::red);
    addKnob (evolveKnob, "evolve", "evolve", colours::red);
    addKnob (mirrorKnob, "mirror", "mirror", colours::ochre);
    addKnob (swingKnob, "swing", "swing", colours::ink);
    addKnob (haloKnob, "halo", "halo", colours::blue);
    addKnob (dustKnob, "dust", "dust", colours::blue);
    addKnob (dryKnob, "dry", "dry", colours::ink);
    addKnob (wetKnob, "wet", "wet", colours::ink);
    addKnob (outKnob, "out", "out", colours::ink);
    outKnob->setLockable (false);

    // ---- iv. voices ----
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
    throwButton.setTooltip ("Throw new shapes (steps, pulses, rotation, rate) for all three orbits");
    clearButton.setTooltip ("Release every pinned step");
    regrowButton.onClick = [this] { proc.requestRegrow(); };
    throwButton.onClick = [this] { proc.throwShapes(); };
    clearButton.onClick = [this] { for (int o = 0; o < numOrbits; ++o) proc.clearPins (o); };
    addAndMakeVisible (regrowButton);
    addAndMakeVisible (throwButton);
    addAndMakeVisible (clearButton);

    populatePresets();
    refreshLocks();
    updateRandomizeText();

    setSize (panelWidth, panelHeight);
    startTimerHz (30);
}

RhytmsPanel::~RhytmsPanel()
{
    stopTimer();
}

void RhytmsPanel::addKnob (std::unique_ptr<Knob>& knob, const juce::String& id, const juce::String& label, juce::Colour accent)
{
    knob = std::make_unique<Knob> (label, accent);
    addAndMakeVisible (*knob);
    sliderAttachments.push_back (std::make_unique<SliderAttachment> (proc.apvts, id, knob->slider));
    auto* prm = proc.apvts.getParameter (id);
    knob->slider.setDoubleClickReturnValue (true, (double) prm->convertFrom0to1 (prm->getDefaultValue()));

    if (state::MorphEngine::isRandomizable (*prm))
    {
        lockableKnobs.emplace_back (id, knob.get());
        knob->onLockEdited = [this, id] (const KnobLock& l)
        {
            proc.morphEngine.setMeta (id, { l.locked, l.custom, l.lo, l.hi });
            seenMetaVersion = proc.morphEngine.getMetaVersion();
        };
    }
    else
    {
        knob->setLockable (false);
    }
}

void RhytmsPanel::refreshLocks()
{
    seenMetaVersion = proc.morphEngine.getMetaVersion();
    for (auto& [id, knob] : lockableKnobs)
    {
        const auto m = proc.morphEngine.getMeta (id);
        knob->setLock ({ m.locked, m.customRange, m.rmin, m.rmax });
    }
    updateRandomizeText();
}

// ---- presets ----

void RhytmsPanel::populatePresets()
{
    presetEntries = proc.presetManager.getEntries();
    presetBox.clear (juce::dontSendNotification);
    presetBox.addItem ("save as...", saveAsId);
    presetBox.addSeparator();

    bool factoryHeading = false, userHeading = false;
    for (size_t i = 0; i < presetEntries.size(); ++i)
    {
        const auto& e = presetEntries[i];
        if (e.factory && ! factoryHeading) { presetBox.addSectionHeading ("factory"); factoryHeading = true; }
        if (! e.factory && ! userHeading)  { presetBox.addSectionHeading ("user"); userHeading = true; }
        presetBox.addItem (e.name, firstPresetId + (int) i);
    }
    showSelectedPreset();
}

void RhytmsPanel::showSelectedPreset()
{
    shownPresetName = proc.getCurrentPresetName();
    const int index = proc.presetManager.indexOf (shownPresetName);
    if (index >= 0 && index < (int) presetEntries.size())
        presetBox.setSelectedId (firstPresetId + index, juce::dontSendNotification);
    else
    {
        presetBox.setSelectedId (0, juce::dontSendNotification);
        presetBox.setText (shownPresetName, juce::dontSendNotification);
    }
}

void RhytmsPanel::stepPreset (int delta)
{
    if (presetEntries.empty())
        return;
    const int n = (int) presetEntries.size();
    int index = proc.presetManager.indexOf (proc.getCurrentPresetName());
    index = index < 0 ? (delta > 0 ? 0 : n - 1) : ((index + delta) % n + n) % n;
    proc.loadPreset (presetEntries[(size_t) index]);
    showSelectedPreset();
}

void RhytmsPanel::savePresetAs()
{
    const auto userDir = proc.presetManager.getUserDirectory();
    chooser = std::make_shared<juce::FileChooser> ("save preset as...", userDir.getChildFile (proc.getCurrentPresetName() + ".xml"), "*.xml");
    const auto chooserFlags = juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                     | juce::FileBrowserComponent::warnAboutOverwriting;

    chooser->launchAsync (chooserFlags, [this, userDir, safe = juce::Component::SafePointer<RhytmsPanel> (this)] (const juce::FileChooser& fc)
    {
        if (safe == nullptr)
            return;
        auto target = fc.getResult();
        if (target == juce::File {})
            return;
        if (target.isDirectory())
            target = target.getChildFile ("preset.xml");
        target = target.withFileExtension (".xml");
        if (! target.isAChildOf (userDir))
            target = userDir.getChildFile (target.getFileName());

        proc.saveUserPreset (target);
        populatePresets();
    });
}

// ---- randomize category ----

void RhytmsPanel::showCategoryMenu()
{
    const auto names = state::MorphEngine::categoryNames();
    juce::PopupMenu menu;
    menu.addSectionHeader ("randomize towards");
    for (int i = 0; i < names.size(); ++i)
        menu.addItem (i + 1, names[i].toLowerCase(), true, i == proc.morphEngine.getCategory());

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&categoryButton),
                        [safe = juce::Component::SafePointer<RhytmsPanel> (this)] (int result)
                        {
                            if (safe == nullptr || result <= 0)
                                return;
                            safe->proc.morphEngine.setCategory (result - 1);
                            safe->updateRandomizeText();
                        });
}

void RhytmsPanel::updateRandomizeText()
{
    const int c = proc.morphEngine.getCategory();
    randomButton.setButtonText (c == 0 ? "randomize" : "randomize: " + state::MorphEngine::categoryNames()[c].toLowerCase());
}

// ---- live ----

void RhytmsPanel::timerCallback()
{
    const bool synced = proc.apvts.getRawParameterValue ("sync")->load() > 0.5f;
    bpmKnob->setAlpha (synced ? 0.35f : 1.0f);
    undoButton.setEnabled (proc.canUndo());
    redoButton.setEnabled (proc.canRedo());
    undoButton.setAlpha (proc.canUndo() ? 1.0f : 0.35f);
    redoButton.setAlpha (proc.canRedo() ? 1.0f : 0.35f);

    if (proc.morphEngine.getMetaVersion() != seenMetaVersion)
        refreshLocks();
    if (proc.getCurrentPresetName() != shownPresetName)
        showSelectedPreset();

    rings.repaint();
    memoryView.repaint();
    weather.repaint();
}

void RhytmsPanel::paint (juce::Graphics& g)
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
    g.drawText ("rhytms", 24, 8, 130, 44, juce::Justification::left);
    g.setColour (colours::ink.withAlpha (0.6f));
    g.setFont (serif (13.0f, true));
    g.drawText ("a rhythmic sostenuto", 140, 14, 220, 18, juce::Justification::left);
    g.setColour (colours::ink.withAlpha (0.4f));
    g.setFont (mono (10.0f));
    g.drawText ("v" RHYTMS_VERSION_STRING, 140, 32, 120, 14, juce::Justification::left);

    g.setColour (colours::faint);
    g.drawHorizontalLine (58, 24.0f, (float) getWidth() - 24.0f);
    g.drawHorizontalLine (522, 24.0f, (float) getWidth() - 24.0f);
    g.drawVerticalLine (348, 70.0f, 510.0f);
    g.drawVerticalLine (770, 70.0f, 510.0f);
    g.drawVerticalLine (530, 16.0f, 44.0f);
    g.drawVerticalLine (826, 16.0f, 44.0f);

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
    g.drawFittedText ("click a step to pin it: always, then never, then free.\n"
                      "scroll on a circle to turn it.\n"
                      "alt-click a knob to lock it, ctrl/cmd-drag to set its randomize range.",
                      790, 440, 290, 50, juce::Justification::topLeft, 3);
}

void RhytmsPanel::resized()
{
    // header
    undoButton.setBounds (420, 16, 48, 26);
    redoButton.setBounds (472, 16, 48, 26);
    morphButton.setBounds (540, 17, 74, 24);
    morphTimeKnob->setBounds (612, 4, 46, 52);
    randomButton.setBounds (668, 16, 128, 26);
    categoryButton.setBounds (798, 16, 22, 26);
    prevButton.setBounds (836, 16, 24, 26);
    presetBox.setBounds (862, 16, 188, 26);
    nextButton.setBounds (1052, 16, 24, 26);

    // i. listen
    memoryView.setBounds (24, 86, 310, 82);
    holdButton.setBounds (24, 176, 160, 24);
    syncButton.setBounds (192, 176, 142, 24);
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
    Knob* grid[3][3] = {
        { bloomKnob.get(), evolveKnob.get(), mirrorKnob.get() },
        { swingKnob.get(), haloKnob.get(), dustKnob.get() },
        { dryKnob.get(), wetKnob.get(), outKnob.get() },
    };
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c)
            grid[r][c]->setBounds (790 + c * 98, 84 + r * 116, 92, 92);

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

// ===========================================================================

RhytmsEditor::RhytmsEditor (RhytmsProcessor& p)
    : AudioProcessorEditor (&p), proc (p), panel (p)
{
    setLookAndFeel (&look);
    addAndMakeVisible (panel);

    // read before the limits below trigger a resize that would overwrite it
    const float scale = juce::jlimit (0.6f, 1.6f, proc.getUiScale());
    const double aspect = (double) RhytmsPanel::panelWidth / RhytmsPanel::panelHeight;
    setResizable (true, true);
    setResizeLimits (660, (int) (660 / aspect), 1760, (int) (1760 / aspect));
    if (auto* c = getConstrainer())
        c->setFixedAspectRatio (aspect);

    setSize (juce::roundToInt (RhytmsPanel::panelWidth * scale), juce::roundToInt (RhytmsPanel::panelHeight * scale));
}

RhytmsEditor::~RhytmsEditor()
{
    setLookAndFeel (nullptr);
}

void RhytmsEditor::resized()
{
    const float scale = (float) getWidth() / (float) RhytmsPanel::panelWidth;
    panel.setTransform (juce::AffineTransform::scale (scale));
    proc.setUiScale (scale);
}
