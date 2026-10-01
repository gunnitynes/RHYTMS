#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "PluginVersion.h"

using namespace rhytms;

namespace
{
uint32_t hash32 (uint32_t x) noexcept
{
    x ^= x >> 16; x *= 0x7feb352du;
    x ^= x >> 15; x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

float hash01 (int orbit, int64_t k) noexcept
{
    const auto h = hash32 ((uint32_t) k * 0x9e3779b9u ^ hash32 ((uint32_t) (k >> 32) + (uint32_t) orbit * 0x85ebca6bu));
    return (float) (h & 0xffffff) / (float) 0x1000000;
}

double positiveMod (double a, double b) noexcept
{
    const double r = std::fmod (a, b);
    return r < 0.0 ? r + b : r;
}

struct OrbitDefaults
{
    int steps, pulses, rotate, rate;
    float pitch, reach, length, colour, pan, level, reverse;
};

// I: the pulse that stays close to what you played.
// II: a slower, darker triplet orbit an octave down.
// III: a quick, thin, odd-length orbit that drifts against the bar.
constexpr OrbitDefaults orbitDefaults[numOrbits] = {
    { 16, 5, 0, 4,   0.0f, 0.0f, 0.30f,  0.0f, -0.30f, 0.80f, 0.0f },
    { 12, 4, 1, 2, -12.0f, 0.4f, 0.60f, -0.45f, 0.35f, 0.60f, 0.2f },
    {  7, 3, 0, 6,   7.0f, 0.8f, 0.15f,  0.5f,  0.0f, 0.50f, 0.1f },
};
} // namespace

const juce::StringArray& RhytmsProcessor::rateNames()
{
    static const juce::StringArray names { "1/4", "1/8", "1/8T", "1/8.", "1/16", "1/16T", "1/16Q", "1/32" };
    return names;
}

double RhytmsProcessor::rateBeats (int index)
{
    static constexpr double beats[] = { 1.0, 0.5, 1.0 / 3.0, 0.75, 0.25, 1.0 / 6.0, 0.2, 0.125 };
    return beats[juce::jlimit (0, 7, index)];
}

juce::AudioProcessorValueTreeState::ParameterLayout RhytmsProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    auto unit = [] (const String& id, const String& name, float def)
    {
        return std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, NormalisableRange<float> (0.0f, 1.0f), def);
    };
    auto bipolar = [] (const String& id, const String& name, float def)
    {
        return std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, NormalisableRange<float> (-1.0f, 1.0f), def);
    };

    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "sync", 1 }, "Sync to DAW", true));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "bpm", 1 }, "Free Tempo", NormalisableRange<float> (30.0f, 240.0f, 0.1f), 96.0f));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "hold", 1 }, "Sostenuto Hold", false));
    layout.add (unit ("sense", "Sense", 0.55f));
    layout.add (std::make_unique<AudioParameterInt> (ParameterID { "memory", 1 }, "Memory", 1, Capture::maxSlices, 8));
    layout.add (unit ("density", "Density", 0.5f));
    layout.add (unit ("order", "Weather", 0.2f));
    layout.add (unit ("bloom", "Bloom", 0.3f));
    layout.add (unit ("evolve", "Evolve", 0.2f));
    layout.add (unit ("mirror", "Mirror", 0.35f));
    layout.add (unit ("swing", "Swing", 0.0f));
    layout.add (unit ("halo", "Halo", 0.25f));
    layout.add (unit ("dust", "Dust", 0.12f));
    layout.add (unit ("dry", "Dry", 0.5f));
    layout.add (unit ("wet", "Wet", 0.8f));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "out", 1 }, "Output", NormalisableRange<float> (-30.0f, 6.0f, 0.1f), 0.0f));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "dissolve", 1 }, "Dissolve", false));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "dissolveTime", 1 }, "Dissolve Time",
                                                       NormalisableRange<float> (0.25f, 16.0f, 0.25f, 0.5f), 2.0f,
                                                       AudioParameterFloatAttributes().withLabel ("bars")));

    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "musical", 1 }, "Musical", false));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "key", 1 }, "Key", Harmony::keyNames(), 2));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "scale", 1 }, "Scale", Harmony::scaleNames(), 3));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "chords", 1 }, "Chords", Harmony::progressionNames(), 0));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "chordLen", 1 }, "Chord Length", Harmony::chordLengthNames(), 2));
    layout.add (unit ("harmony", "Harmony", 0.6f));
    layout.add (unit ("snap", "Snap", 1.0f));
    layout.add (unit ("ring", "Ring", 0.3f));

    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "morph", 1 }, "Morph", false));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "morphTime", 1 }, "Morph Time",
                                                       NormalisableRange<float> (0.5f, 60.0f, 0.1f, 0.4f), 8.0f,
                                                       AudioParameterFloatAttributes().withLabel ("s")));

    for (int o = 0; o < numOrbits; ++o)
    {
        const auto& d = orbitDefaults[o];
        const String n = "Orbit " + String (o + 1) + " ";
        layout.add (std::make_unique<AudioParameterBool> (ParameterID { orbitId (o, "on"), 1 }, n + "On", true));
        layout.add (std::make_unique<AudioParameterInt> (ParameterID { orbitId (o, "steps"), 1 }, n + "Steps", 2, Orbit::maxSteps, d.steps));
        layout.add (std::make_unique<AudioParameterInt> (ParameterID { orbitId (o, "pulses"), 1 }, n + "Pulses", 0, Orbit::maxSteps, d.pulses));
        layout.add (std::make_unique<AudioParameterInt> (ParameterID { orbitId (o, "rotate"), 1 }, n + "Rotate", 0, Orbit::maxSteps - 1, d.rotate));
        layout.add (std::make_unique<AudioParameterChoice> (ParameterID { orbitId (o, "rate"), 1 }, n + "Rate", rateNames(), d.rate));
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { orbitId (o, "pitch"), 1 }, n + "Pitch", NormalisableRange<float> (-24.0f, 24.0f, 1.0f), d.pitch));
        layout.add (unit (orbitId (o, "reach"), n + "Reach", d.reach));
        layout.add (unit (orbitId (o, "length"), n + "Length", d.length));
        layout.add (bipolar (orbitId (o, "colour"), n + "Colour", d.colour));
        layout.add (bipolar (orbitId (o, "pan"), n + "Pan", d.pan));
        layout.add (unit (orbitId (o, "level"), n + "Level", d.level));
        layout.add (unit (orbitId (o, "reverse"), n + "Reverse", d.reverse));
    }

    return layout;
}

RhytmsProcessor::RhytmsProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "RHYTMS", createLayout()),
      morphEngine (apvts)
{
    auto get = [this] (const juce::String& id) { return apvts.getRawParameterValue (id); };
    pSync = get ("sync");     pBpm = get ("bpm");       pHold = get ("hold");
    pSense = get ("sense");   pMemory = get ("memory"); pDensity = get ("density");
    pOrder = get ("order");   pBloom = get ("bloom");   pEvolve = get ("evolve");
    pMirror = get ("mirror"); pSwing = get ("swing");   pHalo = get ("halo");
    pDust = get ("dust");     pDry = get ("dry");       pWet = get ("wet");
    pOut = get ("out");
    pMusical = get ("musical"); pKey = get ("key");         pScale = get ("scale");
    pChords = get ("chords");   pChordLen = get ("chordLen"); pHarmony = get ("harmony");
    pSnap = get ("snap");       pRing = get ("ring");
    pDissolve = get ("dissolve"); pDissolveTime = get ("dissolveTime");

    for (int o = 0; o < numOrbits; ++o)
    {
        auto& p = op[(size_t) o];
        p.on = get (orbitId (o, "on"));         p.steps = get (orbitId (o, "steps"));
        p.pulses = get (orbitId (o, "pulses")); p.rotate = get (orbitId (o, "rotate"));
        p.rate = get (orbitId (o, "rate"));     p.pitch = get (orbitId (o, "pitch"));
        p.reach = get (orbitId (o, "reach"));   p.length = get (orbitId (o, "length"));
        p.colour = get (orbitId (o, "colour")); p.pan = get (orbitId (o, "pan"));
        p.level = get (orbitId (o, "level"));   p.reverse = get (orbitId (o, "reverse"));
    }

    for (auto& v : visual.voiceAges) v.store (-1.0f);

    apvts.addParameterListener ("morph", this);
    presetManager.seedFactory (makeFactoryPresets());
}

RhytmsProcessor::~RhytmsProcessor()
{
    apvts.removeParameterListener ("morph", this);
    cancelPendingUpdate();
}

void RhytmsProcessor::parameterChanged (const juce::String&, float)
{
    // may arrive on the audio thread; the morph timer lives on the message thread
    triggerAsyncUpdate();
}

void RhytmsProcessor::handleAsyncUpdate()
{
    const bool on = param (apvts.getRawParameterValue ("morph")) > 0.5f;
    if (on && ! morphEngine.isMorphing())
        pushHistory();
    morphEngine.setMorphEnabled (on);
}

bool RhytmsProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    const auto in = layouts.getMainInputChannelSet();
    if (out != juce::AudioChannelSet::stereo())
        return false;
    return in == juce::AudioChannelSet::stereo() || in == juce::AudioChannelSet::mono();
}

void RhytmsProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sr = sampleRate;
    capture.prepare (sr);
    dust.prepare (sr);
    halo.prepare (sr);
    for (auto& v : voices) v.prepare (sr);
    dissolveAmount = dis = 0.0f;
    wetBuffer.setSize (2, juce::jmax (1, samplesPerBlock), false, true, false);
    for (auto& p : pending) p.used = false;
    for (auto& o : orbits) o.reset();

    dryGain.reset (sr, 0.03);
    wetGain.reset (sr, 0.03);
    outGain.reset (sr, 0.03);
    dryGain.setCurrentAndTargetValue (param (pDry));
    wetGain.setCurrentAndTargetValue (param (pWet));
    outGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (param (pOut)));
}

void RhytmsProcessor::addEvent (int offset, int orbit, int step, float velocity, double ppq)
{
    if (numEvents < (int) events.size())
        events[(size_t) numEvents++] = { offset, orbit, step, velocity, ppq };
}

void RhytmsProcessor::scheduleOrbit (int o, double ppqStart, double ppqEnd, double beatsPerSample, double barBeats)
{
    auto& p = op[(size_t) o];
    auto& orbit = orbits[(size_t) o];

    const int steps = (int) param (p.steps);
    const float density = param (pDensity);
    const float order = param (pOrder) + (1.0f - param (pOrder)) * 0.7f * dis;
    const int pulses = (int) param (p.pulses);
    const int effPulses = juce::jlimit (0, steps, (int) std::round ((float) pulses * std::pow (2.0f, (density - 0.5f) * 3.2f)));
    orbit.setShape (steps, effPulses, (int) param (p.rotate) % steps);

    if (param (p.on) < 0.5f)
        return;

    const double sb = rateBeats ((int) param (p.rate));
    const double swing = param (pSwing);
    const float mirrorAmt = param (pMirror) * capture.mirrorConfidence();
    const float evolve = param (pEvolve);

    const auto kStart = (int64_t) std::floor (ppqStart / sb) - 1;
    const auto kEnd = (int64_t) std::floor (ppqEnd / sb);

    for (int64_t k = kStart; k <= kEnd; ++k)
    {
        double t = (double) k * sb;
        if ((k & 1) != 0)
            t += swing * 0.5 * sb;
        t += order * 0.16 * sb * hash01 (o, k);

        if (t < ppqStart || t >= ppqEnd)
            continue;

        const int offset = juce::jlimit (0, 1 << 20, (int) ((t - ppqStart) / beatsPerSample));
        const int pos = (int) (((k % steps) + steps) % steps);

        if (pos == 0)
            orbit.onCycle (evolve, order, rng);

        const bool on = orbit.isOn (pos);
        float prob = on ? 1.0f : 0.0f;

        if (mirrorAmt > 0.001f)
        {
            double phase = positiveMod (t, barBeats) / barBeats;
            if (o == 1)
                phase = positiveMod (1.0 - phase - 1.0 / Capture::fingerprintCells, 1.0);
            float m = capture.mirrorAt (phase);
            if (o == 2)
                m = (1.0f - m) * 0.55f;
            prob += (m - prob) * mirrorAmt;
        }

        prob = prob * (1.0f - 0.45f * order) + (1.0f - prob) * 0.22f * order * (0.5f + density);
        prob *= 1.0f - 0.8f * dis;   // dissolving: the rhythm thins out

        const int pin = pins[(size_t) o][(size_t) pos].load (std::memory_order_relaxed);
        if (pin == 1) prob = 1.0f;
        if (pin == 2) prob = 0.0f;

        if (rng.nextFloat() >= prob)
            continue;

        float vel = (pos == orbit.firstHit()) ? 1.0f : 0.8f;
        if (! on && pin != 1) vel *= 0.6f;
        vel *= 1.0f - rng.nextFloat() * 0.45f * order;

        addEvent (offset, o, pos, vel, t);

        // rolls: a strike can stutter into the gap after it
        const float rollChance = order * (0.15f + 0.45f * density) * 0.6f * (1.0f - dis);
        if (rng.nextFloat() < rollChance)
        {
            const int count = 2 + rng.nextInt (3);
            for (int j = 1; j < count; ++j)
                for (auto& pe : pending)
                    if (! pe.used)
                    {
                        pe = { true, o, t + sb * j / count, vel * std::pow (0.72f, (float) j), pos };
                        break;
                    }
        }
    }
}

float RhytmsProcessor::harmonise (const Slice& slice, float semis, double ppq, float& ringHz)
{
    // Unpitched strikes (drums, breath, noise) are heard as the key's root,
    // so their transpositions land on the scale too.
    const float detected = capture.pitchOf (slice);
    const bool pitched = detected > 20.0f && detected < 110.0f;
    const float reference = pitched ? detected : 60.0f + (float) harmony.getRoot();
    const float desired = reference + semis;

    const int degree = harmony.chordDegreeAt (ppq, currentBarBeats, Harmony::chordLengthBars ((int) param (pChordLen)));
    visual.chordDegree.store (degree, std::memory_order_relaxed);

    float target;
    if (rng.nextFloat() < param (pHarmony))
    {
        // mostly root, third and fifth; now and then the seventh
        const float r = rng.nextFloat();
        const int tone = r < 0.38f ? 0 : r < 0.64f ? 1 : r < 0.9f ? 2 : 3;
        target = harmony.chordToneNear (desired, degree, harmony.scaleSize() >= 5 ? tone : tone % 3);
    }
    else
    {
        target = harmony.snapToScale (desired);
    }

    target = desired + (target - desired) * param (pSnap);
    ringHz = Harmony::midiToHz (target);
    const int pc = ((int) std::lround (target) % 12 + 12) % 12;
    visual.pitchClassHits[(size_t) pc].fetch_add (1, std::memory_order_relaxed);
    return juce::jlimit (-36.0f, 36.0f, target - reference);
}

void RhytmsProcessor::strike (int o, int step, float velocity, double ppq)
{
    auto& p = op[(size_t) o];
    const float baseOrder = param (pOrder);
    const float order = baseOrder + (1.0f - baseOrder) * 0.7f * dis;
    const float bloom = param (pBloom) + (1.0f - param (pBloom)) * dis;

    const int avail = juce::jmax (1, juce::jmin (capture.numSlices(), (int) param (pMemory)));
    int idx = (int) std::round (param (p.reach) * (float) (avail - 1));
    if (rng.nextFloat() < order * 0.7f + dis * 0.3f)
        idx = rng.nextInt (avail);

    Slice slice;
    if (! capture.getSlice (idx, slice) && ! capture.getSlice (0, slice))
        return;

    VoiceStart v;
    v.slice = slice;

    float semis = param (p.pitch);
    if (rng.nextFloat() < order * 0.45f)
    {
        static constexpr int intervals[] = { 12, -12, 7, -5, 19, 24, -7, 5 };
        semis += (float) intervals[rng.nextInt (8)];
    }

    if (param (pMusical) > 0.5f)
    {
        float ringHz = 0.0f;
        semis = harmonise (slice, semis, ppq, ringHz);
        v.ringHz = ringHz;
        v.ring = param (pRing);
    }

    semis += (rng.nextFloat() * 2.0f - 1.0f) * (order * 0.12f + dis * 0.3f);
    v.rate = std::pow (2.0, semis / 12.0);

    v.reverse = rng.nextFloat() < param (p.reverse) + 0.3f * dis;
    v.gain = velocity * param (p.level);
    v.pan = juce::jlimit (-1.0f, 1.0f, param (p.pan) + (rng.nextFloat() * 2.0f - 1.0f) * order * 0.5f);
    v.colour = juce::jlimit (-1.0f, 1.0f, param (p.colour) + (rng.nextFloat() * 2.0f - 1.0f) * order * 0.15f - 0.45f * dis);
    v.bloom = bloom;
    v.texture = juce::jmin (1.0f, order + 0.4f * dis);
    v.orbit = o;

    const double samplesPerBeat = sr * 60.0 / currentBpm;
    const double stepSamples = rateBeats ((int) param (p.rate)) * samplesPerBeat;
    const double barSamples = currentBarBeats * samplesPerBeat;
    const float length = param (p.length);
    v.gateSamples = stepSamples * (0.25 + length * 3.75) * (1.0 + 3.0 * dis);
    v.releaseSamples = 0.004 * sr + (double) (bloom * bloom) * barSamples * 1.2 + length * stepSamples * 0.5;

    int chosen = -1;
    float quietest = 1.0e9f;
    for (int i = 0; i < numVoices; ++i)
    {
        if (! voices[(size_t) i].isActive()) { chosen = i; break; }
        const float score = voices[(size_t) i].getLevel() - (float) voices[(size_t) i].age() * 1.0e-7f;
        if (score < quietest) { quietest = score; chosen = i; }
    }

    voiceSeed = voiceSeed * 1664525u + 1013904223u;
    voices[(size_t) chosen].start (v, sr, voiceSeed);

    auto& view = visual.orbits[(size_t) o];
    view.hits.fetch_add (1, std::memory_order_relaxed);
    view.lastHitStep.store (step, std::memory_order_relaxed);
}

void RhytmsProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    if (n == 0)
        return;

    if (wetBuffer.getNumSamples() < n)
        wetBuffer.setSize (2, n, false, false, true);

    // ---- clock ----
    const bool sync = param (pSync) > 0.5f;
    double bpm = param (pBpm);
    bool hostPlaying = false;
    double hostPpq = 0.0;
    int num = 4, den = 4;

    if (auto* ph = getPlayHead())
    {
        if (auto pos = ph->getPosition())
        {
            if (sync)
            {
                if (auto b = pos->getBpm()) bpm = *b;
                if (auto ts = pos->getTimeSignature()) { num = ts->numerator; den = ts->denominator; }
                if (auto q = pos->getPpqPosition()) { hostPpq = *q; hostPlaying = pos->getIsPlaying(); }
            }
        }
    }

    bpm = juce::jlimit (20.0, 400.0, bpm);
    const double barBeats = juce::jlimit (0.5, 32.0, num * 4.0 / juce::jmax (1, den));
    const double beatsPerSample = bpm / 60.0 / sr;
    // Locked to the DAW's timeline while it plays; otherwise run on our own
    // clock (at the DAW tempo when synced) so live input still blooms.
    const double ppqStart = (sync && hostPlaying) ? hostPpq : internalPpq;
    const double ppqEnd = ppqStart + n * beatsPerSample;
    internalPpq = ppqEnd;
    currentBpm = bpm;
    currentBarBeats = barBeats;

    // dissolve ramps over its time (in bars) in either direction
    {
        const double rampSamples = juce::jmax (1.0, param (pDissolveTime) * barBeats * sr * 60.0 / bpm);
        const float step = (float) (n / rampSamples);
        dissolveAmount = juce::jlimit (0.0f, 1.0f, dissolveAmount + (param (pDissolve) > 0.5f ? step : -step));
        dis = dissolveAmount * dissolveAmount * (3.0f - 2.0f * dissolveAmount);
        visual.dissolve.store (dis, std::memory_order_relaxed);
    }
    harmony.configure ((int) param (pKey), (int) param (pScale), (int) param (pChords));

    if (regrowRequested.exchange (false))
        for (auto& o : orbits) o.reset();

    // ---- listen ----
    capture.setHold (param (pHold) > 0.5f);
    capture.setSense (param (pSense));

    const int inChannels = getTotalNumInputChannels();
    const float* inL = buffer.getReadPointer (0);
    const float* inR = buffer.getReadPointer (inChannels > 1 ? 1 : 0);
    float peak = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        const double phase = positiveMod (ppqStart + i * beatsPerSample, barBeats) / barBeats;
        capture.push (inL[i], inR[i], phase);
        peak = juce::jmax (peak, std::abs (inL[i]), std::abs (inR[i]));
    }
    visual.inputLevel.store (juce::jmax (peak, visual.inputLevel.load() * 0.8f));

    // ---- schedule strikes ----
    numEvents = 0;
    for (auto& pe : pending)
    {
        if (! pe.used) continue;
        if (pe.time < ppqStart - 1.0e-6 || pe.time > ppqEnd + 64.0) { pe.used = false; continue; }
        if (pe.time < ppqEnd)
        {
            addEvent (juce::jlimit (0, n - 1, (int) ((pe.time - ppqStart) / beatsPerSample)), pe.orbit, pe.step, pe.velocity, pe.time);
            pe.used = false;
        }
    }
    for (int o = 0; o < numOrbits; ++o)
        scheduleOrbit (o, ppqStart, ppqEnd, beatsPerSample, barBeats);

    std::sort (events.begin(), events.begin() + numEvents, [] (const Event& a, const Event& b) { return a.offset < b.offset; });

    // ---- play ----
    wetBuffer.clear();
    float* wl = wetBuffer.getWritePointer (0);
    float* wr = wetBuffer.getWritePointer (1);

    auto renderVoices = [&] (int from, int to)
    {
        if (to <= from) return;
        for (auto& v : voices)
            if (v.isActive())
                v.render (capture, wl + from, wr + from, to - from);
    };

    int cursor = 0;
    for (int e = 0; e < numEvents; ++e)
    {
        const int at = juce::jlimit (0, n, events[(size_t) e].offset);
        renderVoices (cursor, at);
        cursor = juce::jmax (cursor, at);
        strike (events[(size_t) e].orbit, events[(size_t) e].step, events[(size_t) e].velocity, events[(size_t) e].ppq);
    }
    renderVoices (cursor, n);

    wetBuffer.applyGain (0, n, 0.7f);
    dust.process (wl, wr, n, param (pDust), rng);
    const float haloAmount = param (pHalo);
    halo.process (wl, wr, n, haloAmount + juce::jmax (0.0f, 0.85f - haloAmount) * dis, sr * 60.0 / bpm);

    // ---- mix ----
    dryGain.setTargetValue (param (pDry));
    wetGain.setTargetValue (param (pWet));
    outGain.setTargetValue (juce::Decibels::decibelsToGain (param (pOut)));

    float* outL = buffer.getWritePointer (0);
    float* outR = buffer.getWritePointer (1);
    for (int i = 0; i < n; ++i)
    {
        const float d = dryGain.getNextValue();
        const float w = wetGain.getNextValue();
        const float g = outGain.getNextValue();
        const float dl = inL[i], dr = inR[i];
        outL[i] = std::tanh ((dl * d + wl[i] * w) * g * 0.9f) / 0.9f;
        outR[i] = std::tanh ((dr * d + wr[i] * w) * g * 0.9f) / 0.9f;
    }

    sampleCounter += n;
    publishVisuals (ppqEnd);
}

void RhytmsProcessor::publishVisuals (double ppqEnd)
{
    for (int o = 0; o < numOrbits; ++o)
    {
        auto& view = visual.orbits[(size_t) o];
        const int steps = orbits[(size_t) o].getSteps();
        view.steps.store (steps, std::memory_order_relaxed);
        view.pattern.store (orbits[(size_t) o].getPattern(), std::memory_order_relaxed);
        const double sb = rateBeats ((int) param (op[(size_t) o].rate));
        view.phase.store ((float) positiveMod (ppqEnd / sb, (double) steps), std::memory_order_relaxed);
    }

    const auto& fp = capture.getFingerprint();
    for (size_t i = 0; i < fp.size(); ++i)
        visual.fingerprint[i].store (fp[i], std::memory_order_relaxed);

    for (size_t i = 0; i < capture.overview.size(); ++i)
        visual.overview[i].store (capture.overview[i].load (std::memory_order_relaxed), std::memory_order_relaxed);

    const int count = capture.numSlices();
    visual.sliceCount.store (count, std::memory_order_relaxed);
    for (int i = 0; i < Capture::maxSlices; ++i)
    {
        Slice s;
        float age = -1.0f;
        if (i < count && capture.getSlice (i, s))
            age = capture.ageOf (s.start);
        visual.sliceAges[(size_t) i].store (age, std::memory_order_relaxed);
    }

    for (int i = 0; i < numVoices; ++i)
    {
        const auto& v = voices[(size_t) i];
        visual.voiceAges[(size_t) i].store (v.isActive() ? capture.ageOf ((int64_t) v.playPosition()) : -1.0f, std::memory_order_relaxed);
        visual.voiceLevels[(size_t) i].store (v.getLevel(), std::memory_order_relaxed);
        visual.voiceOrbits[(size_t) i].store (v.getOrbit(), std::memory_order_relaxed);
    }

    visual.writeFraction.store (capture.writeFraction(), std::memory_order_relaxed);
    visual.bpm.store (currentBpm, std::memory_order_relaxed);
    visual.hostSynced.store (param (pSync) > 0.5f, std::memory_order_relaxed);
}

// ---------------------------------------------------------------------------
// state, presets, randomize, history

namespace
{
// Loading a preset or stepping through history never changes these: they
// belong to the performance, not to the sound.
const juce::StringArray performanceIds { "sync", "hold", "morph", "morphTime", "out", "dissolve", "dissolveTime" };

struct Setting { const char* id; float value; };
struct FactoryPreset { const char* name; std::vector<Setting> settings; };

const std::vector<FactoryPreset>& factoryPresets()
{
    static const std::vector<FactoryPreset> presets = {
        { "Init", {} },

        { "Tapped Gamelan",
          { { "bloom", 0.6f }, { "order", 0.15f }, { "halo", 0.4f }, { "o2_pitch", 12.0f },
            { "o3_pitch", 19.0f }, { "o1_colour", 0.2f }, { "o3_reverse", 0.0f }, { "o2_length", 0.45f } } },

        { "Ghost Choir",
          { { "bloom", 0.92f }, { "order", 0.3f }, { "density", 0.35f }, { "halo", 0.55f }, { "dry", 0.0f },
            { "o1_length", 0.9f }, { "o2_length", 1.0f }, { "o3_length", 0.8f },
            { "o1_colour", -0.5f }, { "o2_colour", -0.7f }, { "o3_colour", -0.3f } } },

        { "Stutter Poem",
          { { "bloom", 0.0f }, { "order", 0.8f }, { "density", 0.8f }, { "memory", 16.0f },
            { "o1_reach", 1.0f }, { "o2_reach", 0.8f }, { "o3_reach", 1.0f },
            { "o1_reverse", 0.4f }, { "o2_reverse", 0.4f }, { "o3_reverse", 0.5f } } },

        { "Polymeter Clock",
          { { "order", 0.0f }, { "density", 0.5f }, { "evolve", 0.0f }, { "bloom", 0.1f }, { "mirror", 0.0f },
            { "o1_steps", 16.0f }, { "o1_rate", 4.0f }, { "o2_steps", 12.0f }, { "o2_rate", 2.0f },
            { "o3_steps", 7.0f }, { "o3_rate", 6.0f } } },

        { "Rain on Tin",
          { { "density", 0.9f }, { "order", 0.55f }, { "bloom", 0.15f }, { "halo", 0.35f }, { "dust", 0.3f },
            { "o1_rate", 7.0f }, { "o1_steps", 24.0f }, { "o1_pulses", 9.0f }, { "o1_level", 0.55f },
            { "o1_colour", 0.6f }, { "o2_colour", 0.7f }, { "o3_colour", 0.4f } } },

        { "Slow Bells",
          { { "density", 0.2f }, { "order", 0.1f }, { "bloom", 0.75f }, { "halo", 0.6f },
            { "o1_rate", 0.0f }, { "o2_rate", 3.0f }, { "o3_rate", 1.0f },
            { "o1_pitch", 12.0f }, { "o2_pitch", 7.0f }, { "o3_pitch", 24.0f },
            { "o1_length", 0.7f }, { "o2_length", 0.7f }, { "o3_length", 0.7f } } },

        { "Broken Machine",
          { { "order", 0.95f }, { "density", 0.7f }, { "evolve", 0.9f }, { "dust", 0.7f }, { "bloom", 0.2f },
            { "swing", 0.3f }, { "o1_reverse", 0.3f } } },

        { "Mirror Mirror",
          { { "mirror", 1.0f }, { "order", 0.05f }, { "evolve", 0.05f }, { "bloom", 0.2f },
            { "o1_reach", 0.0f }, { "o2_reach", 0.2f }, { "o3_reach", 0.4f } } },

        { "Dust Waltz",
          { { "swing", 0.55f }, { "dust", 0.5f }, { "halo", 0.45f }, { "bloom", 0.35f },
            { "o1_steps", 12.0f }, { "o1_pulses", 4.0f }, { "o1_rate", 1.0f },
            { "o2_steps", 9.0f }, { "o2_pulses", 3.0f }, { "o2_rate", 1.0f }, { "o2_pitch", -12.0f } } },

        // ---- musical: key, scale, chords and the ring resonator ----
        { "Kalimba Rain",
          { { "musical", 1.0f }, { "key", 2.0f }, { "scale", 8.0f }, { "ring", 0.55f }, { "harmony", 0.7f },
            { "bloom", 0.2f }, { "density", 0.65f }, { "order", 0.2f }, { "o1_pitch", 12.0f }, { "o2_pitch", 0.0f },
            { "o3_pitch", 7.0f }, { "o1_colour", 0.3f }, { "halo", 0.35f } } },

        { "Drum Choir",
          { { "musical", 1.0f }, { "key", 9.0f }, { "scale", 2.0f }, { "chords", 2.0f }, { "chordLen", 2.0f },
            { "harmony", 0.9f }, { "ring", 0.35f }, { "bloom", 0.85f }, { "halo", 0.5f }, { "density", 0.4f },
            { "o2_pitch", -12.0f }, { "o1_length", 0.8f }, { "o2_length", 0.9f }, { "o1_colour", -0.3f } } },

        { "Modal Bells",
          { { "musical", 1.0f }, { "key", 2.0f }, { "scale", 3.0f }, { "chords", 5.0f }, { "chordLen", 1.0f },
            { "ring", 0.7f }, { "harmony", 0.75f }, { "bloom", 0.5f }, { "halo", 0.5f }, { "density", 0.4f },
            { "o1_rate", 1.0f }, { "o2_rate", 3.0f }, { "o3_rate", 0.0f }, { "o1_pitch", 12.0f }, { "o3_pitch", 24.0f } } },

        { "Pelog Garden",
          { { "musical", 1.0f }, { "key", 4.0f }, { "scale", 12.0f }, { "ring", 0.6f }, { "harmony", 0.5f },
            { "density", 0.6f }, { "order", 0.2f }, { "o3_pitch", 12.0f }, { "dust", 0.2f }, { "bloom", 0.25f } } },

        { "Glass Progression",
          { { "musical", 1.0f }, { "key", 0.0f }, { "scale", 1.0f }, { "chords", 1.0f }, { "chordLen", 1.0f },
            { "harmony", 1.0f }, { "ring", 0.45f }, { "bloom", 0.35f }, { "o1_colour", 0.4f }, { "o2_colour", 0.2f },
            { "o2_pitch", 0.0f } } },

        { "Dissolving Hymn",
          { { "musical", 1.0f }, { "key", 5.0f }, { "scale", 5.0f }, { "chords", 4.0f }, { "chordLen", 3.0f },
            { "harmony", 0.8f }, { "ring", 0.25f }, { "bloom", 0.7f }, { "halo", 0.6f }, { "density", 0.3f },
            { "o1_length", 0.7f }, { "o2_length", 0.8f } } },
    };
    return presets;
}
} // namespace

std::vector<std::pair<juce::String, juce::ValueTree>> RhytmsProcessor::makeFactoryPresets()
{
    std::vector<std::pair<juce::String, juce::ValueTree>> out;
    const auto base = captureState();

    for (const auto& preset : factoryPresets())
    {
        auto tree = base.createCopy();
        for (const auto& s : preset.settings)
        {
            auto child = tree.getChildWithProperty ("id", juce::String (s.id));
            jassert (child.isValid());
            child.setProperty ("value", s.value, nullptr);
        }
        tree.setProperty ("presetName", preset.name, nullptr);
        out.emplace_back (preset.name, tree);
    }
    return out;
}

juce::ValueTree RhytmsProcessor::captureState()
{
    auto state = apvts.copyState();
    for (auto* name : { "PINS", "UILOCKS" })
        state.removeChild (state.getChildWithName (name), nullptr);

    juce::ValueTree pinTree ("PINS");
    for (int o = 0; o < numOrbits; ++o)
    {
        juce::String s;
        for (auto& p : pins[(size_t) o]) s << p.load();
        pinTree.setProperty ("o" + juce::String (o), s, nullptr);
    }
    state.appendChild (pinTree, nullptr);
    state.appendChild (morphEngine.toTree(), nullptr);
    state.setProperty ("presetName", getCurrentPresetName(), nullptr);
    state.setProperty ("uiScale", uiScale, nullptr);
    state.setProperty ("version", RHYTMS_VERSION_STRING, nullptr);
    return state;
}

void RhytmsProcessor::applyState (const juce::ValueTree& source, bool keepPerformance)
{
    if (! source.isValid() || ! source.hasType (apvts.state.getType()))
        return;

    auto state = source.createCopy();

    if (keepPerformance)
        for (const auto& id : performanceIds)
        {
            auto child = state.getChildWithProperty ("id", id);
            if (child.isValid())
                child.setProperty ("value", apvts.getRawParameterValue (id)->load(), nullptr);
        }

    const auto pinTree = state.getChildWithName ("PINS");
    for (int o = 0; o < numOrbits; ++o)
    {
        const auto s = pinTree.getProperty ("o" + juce::String (o)).toString();
        for (int i = 0; i < Orbit::maxSteps; ++i)
            pins[(size_t) o][(size_t) i].store (i < s.length() ? juce::jlimit (0, 2, (int) (s[i] - '0')) : 0);
    }

    morphEngine.fromTree (state.getChildWithName ("UILOCKS"));
    setCurrentPresetName (state.getProperty ("presetName", "Init").toString());
    if (! keepPerformance)
        uiScale = juce::jlimit (0.6f, 1.6f, (float) state.getProperty ("uiScale", 1.0f));

    for (auto* name : { "PINS", "UILOCKS" })
        state.removeChild (state.getChildWithName (name), nullptr);
    for (auto* prop : { "presetName", "uiScale", "version" })
        state.removeProperty (prop, nullptr);

    apvts.replaceState (state);
    requestRegrow();
    morphEngine.restartFromCurrent();
}

void RhytmsProcessor::setCurrentPresetName (const juce::String& name)
{
    const juce::ScopedLock sl (presetNameLock);
    currentPresetName = name;
}

juce::String RhytmsProcessor::getCurrentPresetName() const
{
    const juce::ScopedLock sl (presetNameLock);
    return currentPresetName;
}

void RhytmsProcessor::loadPreset (const rhytms::state::PresetManager::Entry& entry)
{
    const auto tree = presetManager.load (entry.file, apvts.state.getType());
    if (! tree.isValid())
        return;
    pushHistory();
    applyState (tree, true);
    setCurrentPresetName (entry.name);
}

bool RhytmsProcessor::saveUserPreset (const juce::File& file)
{
    const auto name = file.getFileNameWithoutExtension();
    const auto previous = getCurrentPresetName();
    setCurrentPresetName (name);
    if (presetManager.save (captureState(), file))
        return true;
    setCurrentPresetName (previous);
    return false;
}

void RhytmsProcessor::randomize()
{
    pushHistory();
    morphEngine.randomizeAll();
    requestRegrow();
}

void RhytmsProcessor::throwShapes()
{
    pushHistory();

    auto set = [this] (const juce::String& id, float value)
    {
        if (auto* prm = apvts.getParameter (id))
        {
            if (morphEngine.getMeta (id).locked)
                return;
            prm->beginChangeGesture();
            prm->setValueNotifyingHost (prm->convertTo0to1 (value));
            prm->endChangeGesture();
        }
    };

    static constexpr int stepChoices[] = { 5, 7, 8, 9, 10, 11, 12, 13, 16, 16, 24 };
    for (int o = 0; o < numOrbits; ++o)
    {
        const int steps = stepChoices[throwDice.nextInt ((int) std::size (stepChoices))];
        set (orbitId (o, "steps"), (float) steps);
        set (orbitId (o, "pulses"), (float) juce::jlimit (1, steps, 1 + throwDice.nextInt (juce::jmax (1, steps * 3 / 5))));
        set (orbitId (o, "rotate"), (float) throwDice.nextInt (steps));
        set (orbitId (o, "rate"), (float) throwDice.nextInt (rateNames().size()));
    }
    requestRegrow();
}

void RhytmsProcessor::pushHistory()
{
    undoStack.push_back (captureState());
    if (undoStack.size() > 64)
        undoStack.erase (undoStack.begin());
    redoStack.clear();
}

void RhytmsProcessor::undo()
{
    if (undoStack.empty())
        return;
    redoStack.push_back (captureState());
    const auto state = undoStack.back();
    undoStack.pop_back();
    applyState (state, true);
}

void RhytmsProcessor::redo()
{
    if (redoStack.empty())
        return;
    undoStack.push_back (captureState());
    const auto state = redoStack.back();
    redoStack.pop_back();
    applyState (state, true);
}

void RhytmsProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = captureState().createXml())
        copyXmlToBinary (*xml, destData);
}

void RhytmsProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        applyState (juce::ValueTree::fromXml (*xml), false);
}

juce::AudioProcessorEditor* RhytmsProcessor::createEditor()
{
    return new RhytmsEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new RhytmsProcessor();
}
