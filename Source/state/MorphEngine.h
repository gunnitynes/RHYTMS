#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>
#include <vector>

namespace rhytms::state
{

/** Randomize + continuous free-running morph (message thread only).

    - Randomize: one-shot random values for every unlocked parameter.
    - Morph: while enabled, every unlocked parameter glides between fresh random
      targets over Morph Time, cycle after cycle, until switched off.

    Per-knob metadata travels with the state (and with presets):
      * locked knobs keep their value on Randomize / Morph
      * a custom range limits Randomize / Morph to normalised [rmin, rmax]
    A sound-type category further steers where values land.
*/
class MorphEngine : private juce::Timer
{
public:
    struct KnobMeta
    {
        bool locked = false;
        bool customRange = false;
        float rmin = 0.0f;
        float rmax = 1.0f;
    };

    explicit MorphEngine (juce::AudioProcessorValueTreeState& apvts);
    ~MorphEngine() override;

    static bool isRandomizable (const juce::RangedAudioParameter& p);

    void randomizeAll();
    void setMorphEnabled (bool shouldMorph);
    bool isMorphing() const noexcept { return morphActive; }
    // After a preset or undo jumps the state, glide on from where it landed.
    void restartFromCurrent() { if (morphActive) beginMorphCycle(); }

    void setCategory (int c) noexcept { category = c; ++metaVersion; }
    int getCategory() const noexcept { return category; }
    static juce::StringArray categoryNames();

    KnobMeta getMeta (const juce::String& id) const;
    void setMeta (const juce::String& id, const KnobMeta& m);
    void applyDefaultMeta();

    juce::ValueTree toTree() const;
    void fromTree (const juce::ValueTree& locks);

    // Bumped whenever lock/range metadata or the category changes, so an
    // editor can notice without callbacks across threads.
    int getMetaVersion() const noexcept { return metaVersion.load(); }

private:
    void timerCallback() override;
    void beginMorphCycle();
    float randomTargetNormFor (size_t index) const;
    int indexOf (const juce::String& id) const;

    juce::AudioProcessorValueTreeState& apvts;
    mutable juce::Random rng;

    std::vector<juce::RangedAudioParameter*> params;
    std::vector<juce::String> ids;
    std::vector<KnobMeta> meta;
    std::vector<float> startNorm, targetNorm;
    float progress = 0.0f;
    bool morphActive = false;
    int category = 0;
    std::atomic<int> metaVersion { 0 };
};

} // namespace rhytms::state
