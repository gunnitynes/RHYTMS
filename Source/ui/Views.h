#pragma once

#include "Look.h"
#include "../PluginProcessor.h"

namespace rhytms::ui
{

// ---------------------------------------------------------------------------
// The orbits: concentric circles, each step a dot, the active steps joined
// into the polygon they draw. Around them, the mirror ring shows where your
// input tends to strike. Click a dot to pin it (always / never / free);
// scroll over a circle to rotate it.
class RingView : public juce::Component
{
public:
    explicit RingView (RhytmsProcessor& p) : proc (p) {}

    void paint (juce::Graphics& g) override
    {
        const auto c = centre();
        const float R = radiusFor (-1);

        // mirror ring
        g.setColour (colours::faint);
        g.drawEllipse (c.x - R, c.y - R, R * 2.0f, R * 2.0f, 0.8f);
        float mx = 0.0f;
        for (auto& f : proc.visual.fingerprint) mx = juce::jmax (mx, f.load());
        for (int i = 0; i < Capture::fingerprintCells; ++i)
        {
            const float w = mx > 1.0e-3f ? proc.visual.fingerprint[(size_t) i].load() / mx : 0.0f;
            const float a = angleFor ((float) i / Capture::fingerprintCells);
            const float len = 3.0f + w * 22.0f;
            g.setColour (colours::ochre.withAlpha (0.25f + 0.7f * w));
            g.drawLine ({ c.getPointOnCircumference (R + 3.0f, a), c.getPointOnCircumference (R + 3.0f + len, a) }, i % 8 == 0 ? 2.0f : 1.3f);
        }
        g.setColour (colours::ink.withAlpha (0.45f));
        g.setFont (serif (11.5f, true));
        g.drawText ("mirror", juce::Rectangle<float> (c.x + R * 0.72f, c.y - R - 18.0f, 80.0f, 14.0f), juce::Justification::left);

        const double now = juce::Time::getMillisecondCounterHiRes();
        const float dissolve = proc.visual.dissolve.load();

        // while dissolving, every dot drifts away from its circle
        auto place = [&] (int o, int i, float r, float frac)
        {
            auto pt = c.getPointOnCircumference (r, angleFor (frac));
            if (dissolve > 0.001f)
            {
                const auto h = (uint32_t) (o * 131 + i * 977 + 17) * 2654435761u;
                const float a = (float) (h & 0xffff) / 65535.0f * juce::MathConstants<float>::twoPi;
                const float dist = dissolve * (8.0f + 30.0f * (float) ((h >> 16) & 0xff) / 255.0f);
                pt += juce::Point<float> (std::cos (a), std::sin (a)) * dist;
            }
            return pt;
        };

        for (int o = 0; o < numOrbits; ++o)
        {
            auto& view = proc.visual.orbits[(size_t) o];
            const int steps = juce::jmax (1, view.steps.load());
            const uint32_t pattern = view.pattern.load();
            const bool on = proc.apvts.getRawParameterValue (orbitId (o, "on"))->load() > 0.5f;
            const float r = radiusFor (o);
            const auto col = colours::orbit (o).withMultipliedAlpha ((on ? 1.0f : 0.25f) * (1.0f - 0.55f * dissolve));

            const int hits = view.hits.load();
            if (hits != lastHits[o]) { lastHits[o] = hits; flashTime[o] = now; flashStep[o] = view.lastHitStep.load(); }
            const float flash = (float) juce::jmax (0.0, 1.0 - (now - flashTime[o]) / 260.0);

            g.setColour (colours::ghost);
            g.drawEllipse (c.x - r, c.y - r, r * 2.0f, r * 2.0f, 1.0f);

            // the figure drawn by the active steps
            juce::Path figure;
            bool first = true;
            for (int i = 0; i < steps; ++i)
                if ((pattern >> i) & 1u)
                {
                    const auto pt = place (o, i, r, (float) i / steps);
                    if (first) figure.startNewSubPath (pt), first = false;
                    else figure.lineTo (pt);
                }
            if (! first)
            {
                figure.closeSubPath();
                g.setColour (col.withMultipliedAlpha (0.06f + 0.08f * flash));
                g.fillPath (figure);
                g.setColour (col.withMultipliedAlpha (0.35f));
                g.strokePath (figure, juce::PathStrokeType (0.9f));
            }

            // playhead: a short stroke crossing the circle
            const float ph = view.phase.load();
            const float pa = angleFor (ph / (float) steps);
            g.setColour (col.withMultipliedAlpha (0.8f));
            g.drawLine ({ c.getPointOnCircumference (r - 9.0f, pa), c.getPointOnCircumference (r + 9.0f, pa) }, 1.4f);

            for (int i = 0; i < steps; ++i)
            {
                const auto pt = place (o, i, r, (float) i / steps);
                const bool active = ((pattern >> i) & 1u) != 0;
                const int pin = proc.getPin (o, i);
                const float f = (i == flashStep[o]) ? flash : 0.0f;

                if (f > 0.0f)
                {
                    const float hr = 6.0f + 12.0f * (1.0f - f);
                    g.setColour (col.withMultipliedAlpha (0.35f * f));
                    g.drawEllipse (pt.x - hr, pt.y - hr, hr * 2.0f, hr * 2.0f, 1.2f);
                }

                if (active || pin == 1)
                {
                    const float d = 4.5f + 2.0f * f;
                    g.setColour (col);
                    g.fillEllipse (pt.x - d, pt.y - d, d * 2.0f, d * 2.0f);
                }
                else
                {
                    g.setColour (col.withMultipliedAlpha (0.55f));
                    g.drawEllipse (pt.x - 2.2f, pt.y - 2.2f, 4.4f, 4.4f, 0.9f);
                }

                if (pin == 1)
                {
                    g.setColour (col);
                    g.drawEllipse (pt.x - 8.0f, pt.y - 8.0f, 16.0f, 16.0f, 1.0f);
                }
                else if (pin == 2)
                {
                    g.setColour (col);
                    g.drawLine (pt.x - 5.0f, pt.y - 5.0f, pt.x + 5.0f, pt.y + 5.0f, 1.2f);
                    g.drawLine (pt.x - 5.0f, pt.y + 5.0f, pt.x + 5.0f, pt.y - 5.0f, 1.2f);
                }
            }

            g.setColour (col);
            g.setFont (serif (13.0f, true));
            static const char* numerals[] = { "i", "ii", "iii" };
            const auto lp = c.getPointOnCircumference (r, juce::MathConstants<float>::pi * -0.06f);
            g.drawText (numerals[o], juce::Rectangle<float> (lp.x - 26.0f, lp.y - 7.0f, 18.0f, 14.0f), juce::Justification::centredRight);
        }

        // the still centre
        const bool held = proc.apvts.getRawParameterValue ("hold")->load() > 0.5f;
        g.setColour (held || dissolve > 0.01f ? colours::red : colours::ink.withAlpha (0.55f));
        g.setFont (serif (12.0f, true));
        g.drawText (dissolve > 0.01f ? "dissolving" : held ? "held" : juce::String (proc.visual.bpm.load(), 1),
                    juce::Rectangle<float> (c.x - 40.0f, c.y - 8.0f, 80.0f, 16.0f), juce::Justification::centred);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const auto c = centre();
        float best = 11.0f;
        int bo = -1, bs = -1;
        for (int o = 0; o < numOrbits; ++o)
        {
            const int steps = proc.visual.orbits[(size_t) o].steps.load();
            for (int i = 0; i < steps; ++i)
            {
                const float d = e.position.getDistanceFrom (c.getPointOnCircumference (radiusFor (o), angleFor ((float) i / steps)));
                if (d < best) { best = d; bo = o; bs = i; }
            }
        }
        if (bo < 0) return;

        if (e.mods.isRightButtonDown() || e.mods.isAltDown())
            proc.setPin (bo, bs, 0);
        else
            proc.setPin (bo, bs, (proc.getPin (bo, bs) + 1) % 3);
        repaint();
    }

    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        const float d = e.position.getDistanceFrom (centre());
        int o = -1;
        float best = 1.0e9f;
        for (int i = 0; i < numOrbits; ++i)
            if (std::abs (d - radiusFor (i)) < best) { best = std::abs (d - radiusFor (i)); o = i; }
        if (o < 0 || best > 20.0f) return;

        if (auto* p = proc.apvts.getParameter (orbitId (o, "rotate")))
        {
            const int steps = proc.visual.orbits[(size_t) o].steps.load();
            const int current = (int) proc.apvts.getRawParameterValue (orbitId (o, "rotate"))->load();
            const int next = ((current + (w.deltaY > 0 ? 1 : -1)) % steps + steps) % steps;
            p->beginChangeGesture();
            p->setValueNotifyingHost (p->convertTo0to1 ((float) next));
            p->endChangeGesture();
        }
    }

private:
    juce::Point<float> centre() const { return getLocalBounds().toFloat().getCentre(); }
    float radiusFor (int o) const
    {
        const float R = juce::jmin (getWidth(), getHeight()) * 0.5f - 34.0f;
        if (o < 0) return R;
        return R * (0.84f - 0.22f * (float) o);
    }
    static float angleFor (float frac) { return juce::MathConstants<float>::twoPi * frac; }

    RhytmsProcessor& proc;
    int lastHits[numOrbits] {};
    double flashTime[numOrbits] {};
    int flashStep[numOrbits] { -1, -1, -1 };
};

// ---------------------------------------------------------------------------
// Weather: one gesture for the shape of everything. Left to right goes from
// sparse to dense; bottom to top from geometry (exact, euclidean, repeating)
// to weather (loose, scattered, rolling, never quite the same twice).
class WeatherPad : public juce::Component
{
public:
    explicit WeatherPad (RhytmsProcessor& p)
        : proc (p),
          density (p.apvts.getParameter ("density")),
          order (p.apvts.getParameter ("order"))
    {
        juce::Random r (7);
        for (auto& d : field) d = { r.nextFloat(), r.nextFloat(), r.nextFloat(), r.nextFloat() };
    }

    void paint (juce::Graphics& g) override
    {
        const auto b = pad();
        g.setColour (colours::paperDk.withAlpha (0.5f));
        g.fillRect (b);
        g.setColour (colours::faint);
        g.drawRect (b, 1.0f);

        // a field showing what each corner means
        const int cols = 15, rows = 12;
        for (int y = 0; y < rows; ++y)
            for (int x = 0; x < cols; ++x)
            {
                const auto& d = field[(size_t) (y * cols + x)];
                const float fx = (x + 0.5f) / cols, fy = (y + 0.5f) / rows;
                const float storm = 1.0f - fy;
                if (d[2] > fx * 1.05f + 0.08f) continue;
                const float jx = (d[0] - 0.5f) * storm * 1.6f / cols;
                const float jy = (d[1] - 0.5f) * storm * 1.6f / rows;
                const float px = b.getX() + (fx + jx) * b.getWidth();
                const float py = b.getY() + (fy + jy) * b.getHeight();
                const float rr = 1.1f + d[3] * storm * 1.6f;
                g.setColour (colours::ink.withAlpha (0.18f + 0.12f * storm));
                g.fillEllipse (px - rr, py - rr, rr * 2.0f, rr * 2.0f);
            }

        const float dx = proc.apvts.getRawParameterValue ("density")->load();
        const float oy = proc.apvts.getRawParameterValue ("order")->load();
        const juce::Point<float> h (b.getX() + dx * b.getWidth(), b.getBottom() - oy * b.getHeight());

        g.setColour (colours::red.withAlpha (0.35f));
        g.drawLine (b.getX(), h.y, b.getRight(), h.y, 0.7f);
        g.drawLine (h.x, b.getY(), h.x, b.getBottom(), 0.7f);
        g.setColour (colours::red);
        g.drawEllipse (h.x - 9.0f, h.y - 9.0f, 18.0f, 18.0f, 1.4f);
        g.fillEllipse (h.x - 3.0f, h.y - 3.0f, 6.0f, 6.0f);

        g.setColour (colours::ink.withAlpha (0.7f));
        g.setFont (serif (12.0f, true));
        auto full = getLocalBounds().toFloat();
        g.drawText ("sparse", full.removeFromBottom (16.0f).withTrimmedLeft (2.0f), juce::Justification::left);
        g.drawText ("dense", getLocalBounds().toFloat().removeFromBottom (16.0f).withTrimmedRight (2.0f), juce::Justification::right);
        g.drawText ("weather", getLocalBounds().toFloat().removeFromTop (16.0f).withTrimmedLeft (2.0f), juce::Justification::left);
        g.drawText ("geometry", juce::Rectangle<float> (b.getX() + 4.0f, b.getBottom() - 16.0f, 90.0f, 14.0f), juce::Justification::left);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        density->beginChangeGesture();
        order->beginChangeGesture();
        mouseDrag (e);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        const auto b = pad();
        density->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, (e.position.x - b.getX()) / b.getWidth()));
        order->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, (b.getBottom() - e.position.y) / b.getHeight()));
        repaint();
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        density->endChangeGesture();
        order->endChangeGesture();
    }

private:
    juce::Rectangle<float> pad() const { return getLocalBounds().toFloat().reduced (0.0f, 16.0f); }

    RhytmsProcessor& proc;
    juce::RangedAudioParameter* density;
    juce::RangedAudioParameter* order;
    std::array<std::array<float, 4>, 15 * 12> field {};
};

// ---------------------------------------------------------------------------
// Memory: the last twenty seconds of input, oldest at the left. Ticks mark
// the strikes it has cut out; coloured hairlines are the memories currently
// sounding, wandering as they play or hover while frozen.
class MemoryView : public juce::Component
{
public:
    explicit MemoryView (RhytmsProcessor& p) : proc (p) {}

    void paint (juce::Graphics& g) override
    {
        const auto b = getLocalBounds().toFloat();
        const bool held = proc.apvts.getRawParameterValue ("hold")->load() > 0.5f;
        g.setColour (held ? colours::red.withAlpha (0.07f) : colours::paperDk.withAlpha (0.5f));
        g.fillRect (b);
        g.setColour (held ? colours::red.withAlpha (0.6f) : colours::faint);
        g.drawRect (b, 1.0f);

        const double wf = proc.visual.writeFraction.load();
        const int N = Capture::overviewSize;
        const float mid = b.getCentreY();
        const int W = (int) b.getWidth();
        g.setColour (colours::ink.withAlpha (0.55f));
        for (int x = 0; x < W; x += 2)
        {
            double f = wf - (1.0 - (double) x / W);
            f -= std::floor (f);
            const float v = juce::jmin (1.0f, proc.visual.overview[(size_t) ((int) (f * N) % N)].load() * 1.6f);
            const float h = v * b.getHeight() * 0.45f;
            g.drawVerticalLine (x, mid - h, mid + h + 1.0f);
        }

        const int count = proc.visual.sliceCount.load();
        for (int i = 0; i < juce::jmin (count, Capture::maxSlices); ++i)
        {
            const float age = proc.visual.sliceAges[(size_t) i].load();
            if (age < 0.0f || age > 1.0f) continue;
            const float x = b.getRight() - age * b.getWidth();
            g.setColour (colours::ochre.withAlpha (i < memory() ? 0.95f : 0.3f));
            g.fillRect (x - 0.5f, b.getY(), 1.5f, 7.0f);
            g.fillRect (x - 0.5f, b.getBottom() - 7.0f, 1.5f, 7.0f);
        }

        for (int v = 0; v < numVoices; ++v)
        {
            const float age = proc.visual.voiceAges[(size_t) v].load();
            if (age < 0.0f || age > 1.0f) continue;
            const float lvl = juce::jlimit (0.0f, 1.0f, proc.visual.voiceLevels[(size_t) v].load());
            const float x = b.getRight() - age * b.getWidth();
            g.setColour (colours::orbit (proc.visual.voiceOrbits[(size_t) v].load()).withAlpha (0.25f + 0.7f * lvl));
            const float h = b.getHeight() * (0.3f + 0.7f * lvl) * 0.5f;
            g.drawLine (x, mid - h, x, mid + h, 1.3f);
        }

        g.setColour (held ? colours::red : colours::ink.withAlpha (0.6f));
        g.setFont (serif (12.0f, true));
        g.drawText (held ? "memory - held" : "memory", b.reduced (5.0f, 3.0f), juce::Justification::topLeft);
    }

private:
    int memory() const { return (int) proc.apvts.getRawParameterValue ("memory")->load(); }
    RhytmsProcessor& proc;
};

// ---------------------------------------------------------------------------
// Dissolve: click to let the rhythm come apart into a held cloud over the
// dissolve time, click again to gather it back. Press and hold to dissolve
// only while held.
class DissolveButton : public juce::Button
{
public:
    explicit DissolveButton (RhytmsProcessor& p) : juce::Button ("dissolve"), proc (p)
    {
        setClickingTogglesState (false);
        setTooltip ("dissolve: click to let the rhythm come apart into a held cloud over the dissolve time, "
                    "click again to gather it. press and hold to dissolve only while held.");
    }

    void mouseDown (const juce::MouseEvent&) override
    {
        pressedOn = ! getToggleState();
        pressTime = juce::Time::getMillisecondCounter();
        setToggleState (pressedOn, juce::sendNotification);
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (pressedOn && juce::Time::getMillisecondCounter() - pressTime > 450)
            setToggleState (false, juce::sendNotification);
        pressedOn = false;
    }

    void paintButton (juce::Graphics& g, bool over, bool) override
    {
        const auto b = getLocalBounds().toFloat().reduced (1.0f);
        const float d = proc.visual.dissolve.load();

        g.setColour (colours::red.withAlpha (0.08f + 0.25f * d));
        g.fillRect (b);
        g.setColour (getToggleState() ? colours::red : (over ? colours::ink : colours::ink.withAlpha (0.5f)));
        g.drawRect (b, getToggleState() ? 1.6f : 1.0f);

        // the word comes apart into grains as it dissolves
        juce::Random r (5);
        for (int i = 0; i < 90; ++i)
        {
            const float x = b.getX() + r.nextFloat() * b.getWidth();
            const float y = b.getY() + r.nextFloat() * b.getHeight();
            const float drift = d * (r.nextFloat() - 0.3f) * 14.0f;
            g.setColour (colours::red.withAlpha (d * (0.25f + 0.5f * r.nextFloat())));
            g.fillEllipse (x + drift, y - drift * 0.4f, 1.6f, 1.6f);
        }

        g.setColour ((getToggleState() ? colours::red : colours::ink).withAlpha (1.0f - 0.6f * d));
        g.setFont (serif (19.0f, true));
        g.drawText ("dissolve", b, juce::Justification::centred);
    }

private:
    RhytmsProcessor& proc;
    bool pressedOn = false;
    juce::uint32 pressTime = 0;
};

// ---------------------------------------------------------------------------
// Harmony: the twelve pitch classes as a little score. Ink dots are the
// scale, red rings the chord sounding now, and each note flashes when a
// strike lands on it.
class HarmonyView : public juce::Component
{
public:
    explicit HarmonyView (RhytmsProcessor& p) : proc (p) {}

    void paint (juce::Graphics& g) override
    {
        const auto b = getLocalBounds().toFloat();
        const bool on = proc.apvts.getRawParameterValue ("musical")->load() > 0.5f;
        const int key = (int) proc.apvts.getRawParameterValue ("key")->load();
        const int scale = (int) proc.apvts.getRawParameterValue ("scale")->load();
        Harmony h;
        h.configure (key, scale, (int) proc.apvts.getRawParameterValue ("chords")->load());
        const int degree = proc.visual.chordDegree.load();
        const auto chord = h.chordPitchClasses (degree);

        g.setColour (colours::paperDk.withAlpha (0.5f));
        g.fillRect (b);
        g.setColour (colours::faint);
        g.drawRect (b, 1.0f);

        // title: key, scale, roman numeral of the chord
        static const char* numerals[] = { "i", "ii", "iii", "iv", "v", "vi", "vii" };
        const int third = (chord[1] - chord[0] + 12) % 12, fifth = (chord[2] - chord[0] + 12) % 12;
        juce::String roman = numerals[((degree % 7) + 7) % 7];
        if (third == 4) roman = roman.toUpperCase();
        if (fifth == 6) roman << juce::String::fromUTF8 ("\xc2\xb0");
        g.setColour (on ? colours::ink : colours::ink.withAlpha (0.4f));
        g.setFont (serif (13.0f, true));
        g.drawText (Harmony::keyNames()[key] + " " + Harmony::scaleNames()[scale] + (on ? "   " + roman : "   (off)"),
                    b.reduced (8.0f, 4.0f).removeFromTop (16.0f), juce::Justification::left);

        const double now = juce::Time::getMillisecondCounterHiRes();
        auto cells = b.reduced (8.0f, 6.0f).withTrimmedTop (18.0f);
        const float w = cells.getWidth() / 12.0f;
        for (int pc = 0; pc < 12; ++pc)
        {
            const int hits = proc.visual.pitchClassHits[(size_t) pc].load();
            if (hits != lastHits[pc]) { lastHits[pc] = hits; flash[pc] = now; }
            const float f = on ? (float) juce::jmax (0.0, 1.0 - (now - flash[pc]) / 350.0) : 0.0f;

            bool inScale = false;
            for (int d = 0; d < h.scaleSize(); ++d) inScale |= h.pitchClassOf (d) == pc;
            const bool inChord = pc == chord[0] || pc == chord[1] || pc == chord[2];
            const float cx = cells.getX() + w * (pc + 0.5f);
            const float cy = cells.getY() + cells.getHeight() * 0.42f;
            const float alpha = on ? 1.0f : 0.35f;

            if (f > 0.0f)
            {
                g.setColour (colours::red.withAlpha (0.25f * f * alpha));
                g.fillEllipse (cx - 11.0f, cy - 11.0f, 22.0f, 22.0f);
            }
            if (inScale)
            {
                const float rad = pc == key ? 4.5f : 3.2f;
                g.setColour (colours::ink.withAlpha (alpha));
                g.fillEllipse (cx - rad, cy - rad, rad * 2.0f, rad * 2.0f);
            }
            else
            {
                g.setColour (colours::faint);
                g.fillEllipse (cx - 1.2f, cy - 1.2f, 2.4f, 2.4f);
            }
            if (inChord && on)
            {
                g.setColour (colours::red);
                g.drawEllipse (cx - 7.5f, cy - 7.5f, 15.0f, 15.0f, 1.3f);
            }

            g.setColour (colours::ink.withAlpha (0.5f * alpha));
            g.setFont (mono (9.0f));
            g.drawText (Harmony::keyNames()[pc], juce::Rectangle<float> (cx - w * 0.5f, cells.getBottom() - 12.0f, w, 12.0f),
                        juce::Justification::centred);
        }
    }

private:
    RhytmsProcessor& proc;
    int lastHits[12] {};
    double flash[12] {};
};

} // namespace rhytms::ui
