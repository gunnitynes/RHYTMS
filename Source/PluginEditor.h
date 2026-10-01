#pragma once

#include "PluginProcessor.h"
#include "ui/Look.h"
#include "ui/Views.h"

class RhytmsEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit RhytmsEditor (RhytmsProcessor&);
    ~RhytmsEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    void timerCallback() override;
    void addKnob (std::unique_ptr<rhytms::ui::Knob>& knob, const juce::String& id, const juce::String& label, juce::Colour accent);
    void throwDice();

    RhytmsProcessor& proc;
    rhytms::ui::Look look;

    rhytms::ui::RingView rings;
    rhytms::ui::WeatherPad weather;
    rhytms::ui::MemoryView memoryView;

    juce::ToggleButton syncButton { "sync to daw" };
    juce::ToggleButton holdButton { "hold  (sostenuto)" };
    std::unique_ptr<rhytms::ui::Knob> bpmKnob, senseKnob, memoryKnob;
    std::unique_ptr<rhytms::ui::Knob> bloomKnob, evolveKnob, mirrorKnob, swingKnob, haloKnob, dustKnob, dryKnob, wetKnob, outKnob;

    struct OrbitRow
    {
        std::unique_ptr<juce::ToggleButton> on;
        std::unique_ptr<ButtonAttachment> onAttachment;
        std::vector<std::unique_ptr<rhytms::ui::Knob>> knobs;
    };
    std::array<OrbitRow, rhytms::numOrbits> rows;

    juce::TextButton regrowButton { "regrow" }, throwButton { "throw" }, clearButton { "unpin" };

    std::vector<std::unique_ptr<SliderAttachment>> sliderAttachments;
    std::vector<std::unique_ptr<ButtonAttachment>> buttonAttachments;

    juce::Random dice;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RhytmsEditor)
};
