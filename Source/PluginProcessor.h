#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <atomic>

#include "dsp/Capture.h"
#include "dsp/Halo.h"
#include "dsp/Orbit.h"
#include "dsp/Voice.h"
#include "state/MorphEngine.h"
#include "state/PresetManager.h"

namespace rhytms
{
static constexpr int numOrbits = 3;
static constexpr int numVoices = 32;

inline juce::String orbitId (int orbit, const char* name) { return "o" + juce::String (orbit + 1) + "_" + name; }

// Lock-free values the editor reads to draw the living state.
struct VisualState
{
    struct OrbitView
    {
        std::atomic<uint32_t> pattern { 0 };
        std::atomic<int> steps { 16 };
        std::atomic<float> phase { 0.0f };   // 0..steps, fractional playhead
        std::atomic<int> hits { 0 };          // increments on every strike
        std::atomic<int> lastHitStep { -1 };
    };

    std::array<OrbitView, numOrbits> orbits;
    std::array<std::atomic<float>, Capture::fingerprintCells> fingerprint {};
    std::array<std::atomic<float>, Capture::overviewSize> overview {};
    std::array<std::atomic<float>, Capture::maxSlices> sliceAges {};
    std::atomic<int> sliceCount { 0 };
    std::array<std::atomic<float>, numVoices> voiceAges {};   // <0 when silent
    std::array<std::atomic<float>, numVoices> voiceLevels {};
    std::array<std::atomic<int>, numVoices> voiceOrbits {};
    std::atomic<double> writeFraction { 0.0 };
    std::atomic<double> bpm { 120.0 };
    std::atomic<bool> hostSynced { false };
    std::atomic<float> inputLevel { 0.0f };
};
} // namespace rhytms

class RhytmsProcessor : public juce::AudioProcessor,
                        private juce::AudioProcessorValueTreeState::Listener,
                        private juce::AsyncUpdater
{
public:
    RhytmsProcessor();
    ~RhytmsProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 8.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    static const juce::StringArray& rateNames();
    static double rateBeats (int index);

    // pin per step: 0 = free, 1 = always strike, 2 = never strike
    int getPin (int orbit, int step) const { return pins[(size_t) orbit][(size_t) step].load(); }
    void setPin (int orbit, int step, int value) { pins[(size_t) orbit][(size_t) step].store (value); }
    void clearPins (int orbit) { for (auto& p : pins[(size_t) orbit]) p.store (0); }

    void requestRegrow() { regrowRequested.store (true); }

    // ---- state, presets, randomize, history (message thread) ----
    juce::ValueTree captureState();
    // keepPerformance leaves sync, hold, morph and output level untouched
    // (used for presets and undo, not for restoring a session).
    void applyState (const juce::ValueTree& state, bool keepPerformance);

    void loadPreset (const rhytms::state::PresetManager::Entry& entry);
    bool saveUserPreset (const juce::File& file);
    juce::String getCurrentPresetName() const;
    std::vector<std::pair<juce::String, juce::ValueTree>> makeFactoryPresets();

    void randomize();
    void throwShapes();
    void pushHistory();
    void undo();
    void redo();
    bool canUndo() const { return ! undoStack.empty(); }
    bool canRedo() const { return ! redoStack.empty(); }

    float getUiScale() const { return uiScale; }
    void setUiScale (float s) { uiScale = s; }

    juce::AudioProcessorValueTreeState apvts;
    rhytms::VisualState visual;
    rhytms::state::PresetManager presetManager;
    rhytms::state::MorphEngine morphEngine;

private:
    struct OrbitParams
    {
        std::atomic<float>* on {};
        std::atomic<float>* steps {};
        std::atomic<float>* pulses {};
        std::atomic<float>* rotate {};
        std::atomic<float>* rate {};
        std::atomic<float>* pitch {};
        std::atomic<float>* reach {};
        std::atomic<float>* length {};
        std::atomic<float>* colour {};
        std::atomic<float>* pan {};
        std::atomic<float>* level {};
        std::atomic<float>* reverse {};
    };

    struct Pending
    {
        bool used = false;
        int orbit = 0;
        double time = 0.0;  // ppq
        float velocity = 1.0f;
        int step = 0;
    };

    struct Event
    {
        int offset = 0;
        int orbit = 0;
        int step = 0;
        float velocity = 1.0f;
    };

    void parameterChanged (const juce::String& id, float value) override;
    void handleAsyncUpdate() override;
    void setCurrentPresetName (const juce::String& name);

    void scheduleOrbit (int o, double ppqStart, double ppqEnd, double beatsPerSample, double barBeats);
    void strike (int orbit, int step, float velocity);
    void addEvent (int offset, int orbit, int step, float velocity);
    void publishVisuals (double ppqEnd);
    float param (std::atomic<float>* p) const { return p->load (std::memory_order_relaxed); }

    std::atomic<float>* pSync {};
    std::atomic<float>* pBpm {};
    std::atomic<float>* pHold {};
    std::atomic<float>* pSense {};
    std::atomic<float>* pMemory {};
    std::atomic<float>* pDensity {};
    std::atomic<float>* pOrder {};
    std::atomic<float>* pBloom {};
    std::atomic<float>* pEvolve {};
    std::atomic<float>* pMirror {};
    std::atomic<float>* pSwing {};
    std::atomic<float>* pHalo {};
    std::atomic<float>* pDust {};
    std::atomic<float>* pDry {};
    std::atomic<float>* pWet {};
    std::atomic<float>* pOut {};
    std::array<OrbitParams, rhytms::numOrbits> op;

    std::array<std::array<std::atomic<int>, rhytms::Orbit::maxSteps>, rhytms::numOrbits> pins {};
    std::atomic<bool> regrowRequested { false };

    rhytms::Capture capture;
    std::array<rhytms::Orbit, rhytms::numOrbits> orbits;
    std::array<rhytms::Voice, rhytms::numVoices> voices;
    rhytms::Dust dust;
    rhytms::Halo halo;
    juce::Random rng;

    std::array<Pending, 64> pending {};
    std::array<Event, 256> events {};
    int numEvents = 0;

    juce::AudioBuffer<float> wetBuffer;
    double sr = 44100.0;
    double internalPpq = 0.0;
    double currentBpm = 120.0, currentBarBeats = 4.0;
    double currentPpqStart = 0.0, currentBeatsPerSample = 0.0;
    int64_t sampleCounter = 0;
    uint32_t voiceSeed = 1;
    juce::SmoothedValue<float> dryGain, wetGain, outGain;

    std::vector<juce::ValueTree> undoStack, redoStack;
    juce::String currentPresetName { "Init" };
    juce::CriticalSection presetNameLock;
    float uiScale = 1.0f;
    juce::Random throwDice;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RhytmsProcessor)
};
