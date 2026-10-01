#include "MorphEngine.h"

namespace rhytms::state
{

namespace
{
constexpr int morphRateHz = 30;

// Mix, output and the performance controls stay where the player put them.
const juce::StringArray neverRandomized { "bpm", "out", "morphTime" };

// Knobs locked on first run: the mix and the input sensitivity.
const juce::StringArray lockedByDefault { "dry", "wet", "sense" };

// Sound-type profiles, as normalised ranges. Orbit parameters are matched by
// their suffix ("length" covers o1_length, o2_length and o3_length).
struct CatRange { const char* id; float lo, hi; };
struct Category { const char* name; std::vector<CatRange> ranges; };

const std::vector<Category>& categories()
{
    static const std::vector<Category> cats = {
        { "Any", {} },

        { "Pulse",
          { { "order", 0.0f, 0.25f }, { "density", 0.35f, 0.65f }, { "bloom", 0.0f, 0.25f },
            { "evolve", 0.0f, 0.3f }, { "swing", 0.0f, 0.4f }, { "mirror", 0.2f, 0.6f },
            { "halo", 0.0f, 0.3f }, { "length", 0.08f, 0.4f }, { "reverse", 0.0f, 0.1f },
            { "pitch", 0.375f, 0.625f } } },

        { "Bloom",
          { { "bloom", 0.65f, 1.0f }, { "order", 0.1f, 0.5f }, { "density", 0.15f, 0.5f },
            { "evolve", 0.0f, 0.4f }, { "halo", 0.3f, 0.7f }, { "dust", 0.0f, 0.4f },
            { "length", 0.5f, 1.0f }, { "colour", 0.15f, 0.5f }, { "pitch", 0.25f, 0.75f } } },

        { "Storm",
          { { "order", 0.65f, 1.0f }, { "density", 0.6f, 1.0f }, { "evolve", 0.4f, 1.0f },
            { "bloom", 0.0f, 0.5f }, { "reverse", 0.2f, 0.6f }, { "length", 0.0f, 0.35f },
            { "dust", 0.2f, 0.7f } } },

        { "Sparse",
          { { "density", 0.0f, 0.35f }, { "order", 0.0f, 0.4f }, { "bloom", 0.3f, 0.8f },
            { "halo", 0.3f, 0.8f }, { "length", 0.3f, 0.8f }, { "evolve", 0.0f, 0.3f } } },

        { "Mirror",
          { { "mirror", 0.6f, 1.0f }, { "order", 0.0f, 0.3f }, { "evolve", 0.0f, 0.2f },
            { "reach", 0.0f, 0.3f }, { "bloom", 0.0f, 0.5f }, { "pitch", 0.375f, 0.625f } } },
    };
    return cats;
}

juce::String suffixOf (const juce::String& id)
{
    return id.startsWithChar ('o') && id.containsChar ('_') ? id.fromFirstOccurrenceOf ("_", false, false) : id;
}

const CatRange* categoryRangeFor (int category, const juce::String& id)
{
    const auto& cats = categories();
    if (! juce::isPositiveAndBelow (category, (int) cats.size()))
        return nullptr;
    const auto key = suffixOf (id);
    for (const auto& r : cats[(size_t) category].ranges)
        if (key == r.id)
            return &r;
    return nullptr;
}

float smoothStep (float t) noexcept
{
    t = juce::jlimit (0.0f, 1.0f, t);
    return t * t * (3.0f - 2.0f * t);
}
} // namespace

juce::StringArray MorphEngine::categoryNames()
{
    juce::StringArray names;
    for (const auto& c : categories())
        names.add (c.name);
    return names;
}

bool MorphEngine::isRandomizable (const juce::RangedAudioParameter& p)
{
    return dynamic_cast<const juce::AudioParameterBool*> (&p) == nullptr
        && ! neverRandomized.contains (p.getParameterID());
}

MorphEngine::MorphEngine (juce::AudioProcessorValueTreeState& vts) : apvts (vts)
{
    for (auto* p : apvts.processor.getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (p))
            if (isRandomizable (*ranged))
            {
                params.push_back (ranged);
                ids.push_back (ranged->getParameterID());
            }

    meta.resize (params.size());
    startNorm.resize (params.size());
    targetNorm.resize (params.size());
    applyDefaultMeta();
}

MorphEngine::~MorphEngine()
{
    stopTimer();
}

int MorphEngine::indexOf (const juce::String& id) const
{
    for (size_t i = 0; i < ids.size(); ++i)
        if (ids[i] == id)
            return (int) i;
    return -1;
}

MorphEngine::KnobMeta MorphEngine::getMeta (const juce::String& id) const
{
    const int i = indexOf (id);
    return i >= 0 ? meta[(size_t) i] : KnobMeta {};
}

void MorphEngine::setMeta (const juce::String& id, const KnobMeta& m)
{
    const int i = indexOf (id);
    if (i < 0)
        return;
    auto& dst = meta[(size_t) i];
    dst = m;
    dst.rmin = juce::jlimit (0.0f, 1.0f, m.rmin);
    dst.rmax = juce::jlimit (0.0f, 1.0f, m.rmax);
    ++metaVersion;
}

void MorphEngine::applyDefaultMeta()
{
    for (size_t i = 0; i < ids.size(); ++i)
    {
        meta[i] = {};
        meta[i].locked = lockedByDefault.contains (ids[i]);
    }

    // Keep free randomizing musical: pitch within an octave either way.
    for (int o = 1; o <= 3; ++o)
    {
        const int i = indexOf ("o" + juce::String (o) + "_pitch");
        if (i >= 0)
            meta[(size_t) i] = { false, true, 0.25f, 0.75f };
    }

    ++metaVersion;
}

juce::ValueTree MorphEngine::toTree() const
{
    juce::ValueTree locks ("UILOCKS");
    for (size_t i = 0; i < ids.size(); ++i)
    {
        juce::ValueTree node ("KNOB");
        node.setProperty ("id", ids[i], nullptr);
        node.setProperty ("lock", meta[i].locked, nullptr);
        node.setProperty ("crange", meta[i].customRange, nullptr);
        node.setProperty ("rmin", meta[i].rmin, nullptr);
        node.setProperty ("rmax", meta[i].rmax, nullptr);
        locks.appendChild (node, nullptr);
    }
    locks.setProperty ("category", category, nullptr);
    return locks;
}

void MorphEngine::fromTree (const juce::ValueTree& locks)
{
    if (! locks.isValid())
    {
        applyDefaultMeta();
        return;
    }

    for (size_t i = 0; i < ids.size(); ++i)
    {
        const auto node = locks.getChildWithProperty ("id", ids[i]);
        if (! node.isValid())
            continue;
        meta[i].locked = (bool) node.getProperty ("lock", false);
        meta[i].customRange = (bool) node.getProperty ("crange", false);
        meta[i].rmin = (float) (double) node.getProperty ("rmin", 0.0);
        meta[i].rmax = (float) (double) node.getProperty ("rmax", 1.0);
    }
    category = juce::jlimit (0, categoryNames().size() - 1, (int) locks.getProperty ("category", 0));

    ++metaVersion;
}

float MorphEngine::randomTargetNormFor (size_t index) const
{
    auto* param = params[index];
    const auto& m = meta[index];
    const auto& id = ids[index];

    if (m.locked)
        return param->getValue();

    // The category steers the range; a user range narrows it further when the
    // two overlap, otherwise the category wins.
    if (const auto* cat = categoryRangeFor (category, id))
    {
        float lo = cat->lo, hi = cat->hi;
        if (m.customRange)
        {
            const float iLo = juce::jmax (lo, juce::jmin (m.rmin, m.rmax));
            const float iHi = juce::jmin (hi, juce::jmax (m.rmin, m.rmax));
            if (iHi >= iLo) { lo = iLo; hi = iHi; }
        }
        return lo + rng.nextFloat() * juce::jmax (0.0f, hi - lo);
    }

    if (m.customRange)
    {
        const float lo = juce::jmin (m.rmin, m.rmax);
        const float hi = juce::jmax (m.rmin, m.rmax);
        return lo + rng.nextFloat() * (hi - lo);
    }

    float norm = rng.nextFloat();
    const auto key = suffixOf (id);
    if (key == "dust" || key == "evolve" || key == "reverse")
        norm *= 0.55f;
    else if (key == "halo")
        norm *= 0.75f;
    return norm;
}

void MorphEngine::randomizeAll()
{
    for (size_t i = 0; i < params.size(); ++i)
    {
        if (meta[i].locked)
            continue;
        params[i]->beginChangeGesture();
        params[i]->setValueNotifyingHost (randomTargetNormFor (i));
        params[i]->endChangeGesture();
    }

    if (morphActive)
        beginMorphCycle();
}

void MorphEngine::setMorphEnabled (bool shouldMorph)
{
    if (shouldMorph == morphActive)
        return;

    morphActive = shouldMorph;
    if (morphActive)
    {
        beginMorphCycle();
        startTimerHz (morphRateHz);
    }
    else
    {
        stopTimer();
    }
}

void MorphEngine::beginMorphCycle()
{
    for (size_t i = 0; i < params.size(); ++i)
    {
        startNorm[i] = params[i]->getValue();
        targetNorm[i] = randomTargetNormFor (i);
    }
    progress = 0.0f;
}

void MorphEngine::timerCallback()
{
    if (auto* on = apvts.getRawParameterValue ("morph"); on != nullptr && on->load() <= 0.5f)
    {
        setMorphEnabled (false);
        return;
    }

    float duration = 8.0f;
    if (auto* t = apvts.getRawParameterValue ("morphTime"))
        duration = juce::jmax (0.2f, t->load());

    progress += 1.0f / (duration * (float) morphRateHz);

    if (progress >= 1.0f)
    {
        for (size_t i = 0; i < params.size(); ++i)
            if (! meta[i].locked)
                params[i]->setValueNotifyingHost (targetNorm[i]);
        beginMorphCycle();
        return;
    }

    const float t = smoothStep (progress);
    for (size_t i = 0; i < params.size(); ++i)
        if (! meta[i].locked)
            params[i]->setValueNotifyingHost (startNorm[i] + (targetNorm[i] - startNorm[i]) * t);
}

} // namespace rhytms::state
