#include "PluginProcessor.h"
#include "PluginEditor.h"

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
      apvts (*this, nullptr, "RHYTMS", createLayout())
{
    auto get = [this] (const juce::String& id) { return apvts.getRawParameterValue (id); };
    pSync = get ("sync");     pBpm = get ("bpm");       pHold = get ("hold");
    pSense = get ("sense");   pMemory = get ("memory"); pDensity = get ("density");
    pOrder = get ("order");   pBloom = get ("bloom");   pEvolve = get ("evolve");
    pMirror = get ("mirror"); pSwing = get ("swing");   pHalo = get ("halo");
    pDust = get ("dust");     pDry = get ("dry");       pWet = get ("wet");
    pOut = get ("out");

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

void RhytmsProcessor::addEvent (int offset, int orbit, int step, float velocity)
{
    if (numEvents < (int) events.size())
        events[(size_t) numEvents++] = { offset, orbit, step, velocity };
}

void RhytmsProcessor::scheduleOrbit (int o, double ppqStart, double ppqEnd, double beatsPerSample, double barBeats)
{
    auto& p = op[(size_t) o];
    auto& orbit = orbits[(size_t) o];

    const int steps = (int) param (p.steps);
    const float density = param (pDensity);
    const float order = param (pOrder);
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

        const int pin = pins[(size_t) o][(size_t) pos].load (std::memory_order_relaxed);
        if (pin == 1) prob = 1.0f;
        if (pin == 2) prob = 0.0f;

        if (rng.nextFloat() >= prob)
            continue;

        float vel = (pos == orbit.firstHit()) ? 1.0f : 0.8f;
        if (! on && pin != 1) vel *= 0.6f;
        vel *= 1.0f - rng.nextFloat() * 0.45f * order;

        addEvent (offset, o, pos, vel);

        // rolls: a strike can stutter into the gap after it
        const float rollChance = order * (0.15f + 0.45f * density) * 0.6f;
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

void RhytmsProcessor::strike (int o, int step, float velocity)
{
    auto& p = op[(size_t) o];
    const float order = param (pOrder);
    const float bloom = param (pBloom);

    const int avail = juce::jmax (1, juce::jmin (capture.numSlices(), (int) param (pMemory)));
    int idx = (int) std::round (param (p.reach) * (float) (avail - 1));
    if (rng.nextFloat() < order * 0.7f)
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
    semis += (rng.nextFloat() * 2.0f - 1.0f) * order * 0.12f;
    v.rate = std::pow (2.0, semis / 12.0);

    v.reverse = rng.nextFloat() < param (p.reverse);
    v.gain = velocity * param (p.level);
    v.pan = juce::jlimit (-1.0f, 1.0f, param (p.pan) + (rng.nextFloat() * 2.0f - 1.0f) * order * 0.5f);
    v.colour = juce::jlimit (-1.0f, 1.0f, param (p.colour) + (rng.nextFloat() * 2.0f - 1.0f) * order * 0.15f);
    v.bloom = bloom;
    v.texture = order;
    v.orbit = o;

    const double samplesPerBeat = sr * 60.0 / currentBpm;
    const double stepSamples = rateBeats ((int) param (p.rate)) * samplesPerBeat;
    const double barSamples = currentBarBeats * samplesPerBeat;
    const float length = param (p.length);
    v.gateSamples = stepSamples * (0.25 + length * 3.75);
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
            addEvent (juce::jlimit (0, n - 1, (int) ((pe.time - ppqStart) / beatsPerSample)), pe.orbit, pe.step, pe.velocity);
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
        strike (events[(size_t) e].orbit, events[(size_t) e].step, events[(size_t) e].velocity);
    }
    renderVoices (cursor, n);

    wetBuffer.applyGain (0, n, 0.7f);
    dust.process (wl, wr, n, param (pDust), rng);
    halo.process (wl, wr, n, param (pHalo), sr * 60.0 / bpm);

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

void RhytmsProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    juce::ValueTree pinTree ("PINS");
    for (int o = 0; o < numOrbits; ++o)
    {
        juce::String s;
        for (auto& p : pins[(size_t) o]) s << p.load();
        pinTree.setProperty ("o" + juce::String (o), s, nullptr);
    }
    state.removeChild (state.getChildWithName ("PINS"), nullptr);
    state.appendChild (pinTree, nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void RhytmsProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return;

    auto state = juce::ValueTree::fromXml (*xml);
    auto pinTree = state.getChildWithName ("PINS");
    for (int o = 0; o < numOrbits; ++o)
    {
        const auto s = pinTree.getProperty ("o" + juce::String (o)).toString();
        for (int i = 0; i < Orbit::maxSteps; ++i)
            pins[(size_t) o][(size_t) i].store (i < s.length() ? juce::jlimit (0, 2, (int) (s[i] - '0')) : 0);
    }
    apvts.replaceState (state);
}

juce::AudioProcessorEditor* RhytmsProcessor::createEditor()
{
    return new RhytmsEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new RhytmsProcessor();
}
