#pragma once

#include "Capture.h"
#include <juce_core/juce_core.h>

namespace rhytms
{

struct VoiceStart
{
    Slice slice;
    double rate = 1.0;      // playback speed (pitch)
    bool reverse = false;
    float gain = 1.0f;
    float pan = 0.0f;       // -1..1
    float colour = 0.0f;    // -1 dark .. +1 thin
    float bloom = 0.0f;     // 0 chopped .. 1 frozen sostenuto
    float texture = 0.0f;   // grain restlessness while frozen
    double gateSamples = 4000.0;
    double releaseSamples = 200.0;
    int orbit = 0;
};

// A single struck memory. It plays its slice, and depending on bloom it
// "catches" the sound partway through and holds it with two overlapping
// grains, like a sostenuto pedal sustaining just the notes that were down.
class Voice
{
public:
    bool isActive() const noexcept { return active; }
    int getOrbit() const noexcept { return params.orbit; }
    float getLevel() const noexcept { return amp; }
    int64_t age() const noexcept { return elapsed; }
    double playPosition() const noexcept { return lastReadPos; }

    void start (const VoiceStart& p, double sampleRate, uint32_t seed)
    {
        params = p;
        sr = sampleRate;
        rng.setSeed ((juce::int64) seed);
        active = true;
        frozen = false;
        elapsed = 0;
        amp = 0.0f;
        dir = p.reverse ? -1.0 : 1.0;
        head = p.reverse ? (double) (p.slice.start + p.slice.length - 1) : (double) p.slice.start;
        travelled = 0.0;
        lastReadPos = head;

        const double attackPart = juce::jmin ((double) p.slice.length, 0.035 * sr * p.rate);
        const double rest = juce::jmax (0.0, (double) p.slice.length - attackPart);
        const double b = 1.0 - (double) p.bloom;
        freezeAt = p.bloom > 0.001f ? attackPart + b * b * rest * 0.95 : -1.0;

        window = (int) (sr * juce::jmap ((double) p.bloom, 0.03, 0.14));
        attackSamples = juce::jmax (1, (int) (sr * 0.0015));
        releaseCoef = (float) std::exp (-1.0 / juce::jmax (1.0, p.releaseSamples));
        fadeOut = -1;

        const float angle = (juce::jlimit (-1.0f, 1.0f, p.pan) + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
        panL = std::cos (angle);
        panR = std::sin (angle);

        setupFilter (p.colour);
        s1[0] = s1[1] = s2[0] = s2[1] = 0.0f;
    }

    void render (const Capture& cap, float* outL, float* outR, int numSamples) noexcept
    {
        for (int i = 0; i < numSamples && active; ++i)
        {
            float l = 0.0f, r = 0.0f;

            if (! frozen)
            {
                l = cap.read (0, head);
                r = cap.read (1, head);
                lastReadPos = head;
                head += dir * params.rate;
                travelled += params.rate;

                if (freezeAt >= 0.0 && travelled >= freezeAt)
                    beginFreeze();
                else if (travelled >= (double) params.slice.length - 1.0 && fadeOut < 0)
                    fadeOut = (int) (sr * 0.006);
            }
            else
            {
                for (int g = 0; g < 2; ++g)
                {
                    auto& gr = grains[g];
                    const float w = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) gr.phase / (float) window);
                    const double pos = gr.centre + dir * gr.phase * params.rate;
                    l += w * cap.read (0, pos);
                    r += w * cap.read (1, pos);
                    if (g == 0) lastReadPos = pos;
                    if (++gr.phase >= window)
                        respawn (gr);
                }
            }

            // envelope: short attack, flat gate, then exponential release
            if (elapsed < attackSamples)
                amp = (float) elapsed / (float) attackSamples;
            else if ((double) elapsed < params.gateSamples)
                amp = 1.0f;
            else
                amp *= releaseCoef;

            if (fadeOut >= 0)
            {
                amp *= (float) fadeOut / (float) (sr * 0.006 + 1.0);
                if (--fadeOut <= 0)
                    active = false;
            }

            if ((double) elapsed > params.gateSamples && amp < 1.0e-4f)
                active = false;

            const float g = amp * params.gain;
            outL[i] += filter (0, l) * g * panL;
            outR[i] += filter (1, r) * g * panR;
            ++elapsed;
        }
    }

private:
    struct Grain { double centre = 0.0; int phase = 0; };

    void beginFreeze() noexcept
    {
        frozen = true;
        freezePos = head;
        // grain 0 sits at its window peak exactly where the playhead was,
        // grain 1 starts fresh, so the handover is seamless.
        grains[0].phase = window / 2;
        grains[0].centre = clampCentre (head - dir * (window / 2) * params.rate);
        grains[1].phase = 0;
        grains[1].centre = clampCentre (head + jitter());
    }

    void respawn (Grain& g) noexcept
    {
        g.phase = 0;
        g.centre = clampCentre (freezePos + jitter());
    }

    double jitter() noexcept
    {
        const double span = window * (0.15 + 1.6 * params.texture);
        return (rng.nextDouble() * 2.0 - 1.0) * span;
    }

    double clampCentre (double c) const noexcept
    {
        const double span = window * params.rate;
        const double lo = (double) params.slice.start;
        const double hi = (double) (params.slice.start + params.slice.length);
        if (hi - lo <= span)
            return dir > 0 ? lo : hi;
        return dir > 0 ? juce::jlimit (lo, hi - span, c) : juce::jlimit (lo + span, hi, c);
    }

    void setupFilter (float colour) noexcept
    {
        colour = juce::jlimit (-1.0f, 1.0f, colour);
        double cutoff;
        if (colour < 0.0f) { mode = 0; cutoff = 18000.0 * std::pow (0.012, (double) -colour); }
        else               { mode = 1; cutoff = 25.0 * std::pow (150.0, (double) colour); }
        cutoff = juce::jlimit (20.0, sr * 0.45, cutoff);
        const double q = 0.65 + std::abs (colour) * 2.2;
        fg = (float) std::tan (juce::MathConstants<double>::pi * cutoff / sr);
        fk = (float) (1.0 / q);
        fa1 = 1.0f / (1.0f + fg * (fg + fk));
        fa2 = fg * fa1;
        fa3 = fg * fa2;
    }

    float filter (int ch, float x) noexcept
    {
        const float v3 = x - s2[ch];
        const float v1 = fa1 * s1[ch] + fa2 * v3;
        const float v2 = s2[ch] + fa2 * s1[ch] + fa3 * v3;
        s1[ch] = 2.0f * v1 - s1[ch];
        s2[ch] = 2.0f * v2 - s2[ch];
        return mode == 0 ? v2 : x - fk * v1 - v2;
    }

    VoiceStart params;
    double sr = 44100.0;
    juce::Random rng;
    bool active = false, frozen = false;
    int64_t elapsed = 0;
    float amp = 0.0f, releaseCoef = 0.999f;
    int attackSamples = 64, fadeOut = -1;
    double dir = 1.0, head = 0.0, travelled = 0.0, freezeAt = -1.0, freezePos = 0.0, lastReadPos = 0.0;
    int window = 2000;
    Grain grains[2];
    float panL = 0.7f, panR = 0.7f;

    int mode = 0;
    float fg = 0.5f, fk = 1.0f, fa1 = 0.0f, fa2 = 0.0f, fa3 = 0.0f;
    float s1[2] {}, s2[2] {};
};

} // namespace rhytms
