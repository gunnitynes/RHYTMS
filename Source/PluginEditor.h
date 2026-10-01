#pragma once

#include "PluginProcessor.h"
#include "ui/Look.h"
#include "ui/Views.h"

// Everything drawn at a fixed 1100 x 852; the editor scales it.
class RhytmsPanel : public juce::Component, private juce::Timer
{
public:
    static constexpr int panelWidth = 1100;
    static constexpr int panelHeight = 852;

    explicit RhytmsPanel (RhytmsProcessor&);
    ~RhytmsPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    void timerCallback() override;
    void addKnob (std::unique_ptr<rhytms::ui::Knob>& knob, const juce::String& id, const juce::String& label, juce::Colour accent);
    void refreshLocks();

    void populatePresets();
    void showSelectedPreset();
    void stepPreset (int delta);
    void savePresetAs();
    void showCategoryMenu();
    void updateRandomizeText();

    RhytmsProcessor& proc;

    rhytms::ui::RingView rings;
    rhytms::ui::WeatherPad weather;
    rhytms::ui::MemoryView memoryView;

    // header
    juce::ComboBox presetBox;
    juce::TextButton prevButton { juce::String::fromUTF8 ("\xe2\x80\xb9") }, nextButton { juce::String::fromUTF8 ("\xe2\x80\xba") };
    juce::TextButton randomButton { "randomize" }, categoryButton { juce::String::fromUTF8 ("\xe2\x96\xbe") };
    juce::ToggleButton morphButton { "morph" };
    std::unique_ptr<rhytms::ui::Knob> morphTimeKnob;
    juce::TextButton undoButton { "undo" }, redoButton { "redo" };
    std::vector<rhytms::state::PresetManager::Entry> presetEntries;
    juce::String shownPresetName;
    std::shared_ptr<juce::FileChooser> chooser;

    // i. listen
    juce::ToggleButton syncButton { "sync to daw" };
    juce::ToggleButton holdButton { "hold  (sostenuto)" };
    std::unique_ptr<rhytms::ui::Knob> bpmKnob, senseKnob, memoryKnob;

    // iii. bloom & space
    std::unique_ptr<rhytms::ui::Knob> bloomKnob, evolveKnob, mirrorKnob, swingKnob, haloKnob, dustKnob, dryKnob, wetKnob, outKnob;

    // iv. voices
    struct OrbitRow
    {
        std::unique_ptr<juce::ToggleButton> on;
        std::unique_ptr<ButtonAttachment> onAttachment;
        std::vector<std::unique_ptr<rhytms::ui::Knob>> knobs;
    };
    std::array<OrbitRow, rhytms::numOrbits> rows;

    juce::TextButton regrowButton { "regrow" }, throwButton { "throw" }, clearButton { "unpin" };

    // dissolve
    rhytms::ui::DissolveButton dissolveButton;
    std::unique_ptr<rhytms::ui::Knob> dissolveTimeKnob;

    // v. harmony
    juce::ToggleButton musicalButton { "musical" };
    rhytms::ui::HarmonyView harmonyView;
    std::vector<std::unique_ptr<rhytms::ui::Knob>> harmonyKnobs;

    std::vector<std::pair<juce::String, rhytms::ui::Knob*>> lockableKnobs;
    std::vector<std::unique_ptr<SliderAttachment>> sliderAttachments;
    std::vector<std::unique_ptr<ButtonAttachment>> buttonAttachments;
    int seenMetaVersion = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RhytmsPanel)
};

class RhytmsEditor : public juce::AudioProcessorEditor
{
public:
    explicit RhytmsEditor (RhytmsProcessor&);
    ~RhytmsEditor() override;

    void resized() override;

private:
    RhytmsProcessor& proc;
    rhytms::ui::Look look;
    RhytmsPanel panel;
    juce::TooltipWindow tooltips { this, 600 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RhytmsEditor)
};
