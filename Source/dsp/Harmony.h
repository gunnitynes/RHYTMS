#pragma once

#include <juce_core/juce_core.h>
#include <array>
#include <cmath>
#include <vector>

namespace rhytms
{

// Keys, scales and slowly turning chords. Every struck memory can be pulled
// onto the scale and voiced as a tone of the chord that is current at the
// moment it sounds, so rhythm made from any recording becomes harmony.
class Harmony
{
public:
    struct Scale { const char* name; std::vector<int> steps; };
    struct Progression { const char* name; std::vector<int> degrees; };

    static const std::vector<Scale>& scales()
    {
        static const std::vector<Scale> s = {
            { "chromatic",      { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 } },
            { "major",          { 0, 2, 4, 5, 7, 9, 11 } },
            { "minor",          { 0, 2, 3, 5, 7, 8, 10 } },
            { "dorian",         { 0, 2, 3, 5, 7, 9, 10 } },
            { "phrygian",       { 0, 1, 3, 5, 7, 8, 10 } },
            { "lydian",         { 0, 2, 4, 6, 7, 9, 11 } },
            { "mixolydian",     { 0, 2, 4, 5, 7, 9, 10 } },
            { "harmonic minor", { 0, 2, 3, 5, 7, 8, 11 } },
            { "pentatonic",     { 0, 2, 4, 7, 9 } },
            { "minor penta",    { 0, 3, 5, 7, 10 } },
            { "whole tone",     { 0, 2, 4, 6, 8, 10 } },
            { "hirajoshi",      { 0, 2, 3, 7, 8 } },
            { "pelog",          { 0, 1, 3, 7, 8 } },
            { "in sen",         { 0, 1, 5, 7, 10 } },
        };
        return s;
    }

    // Degrees are 0-based scale steps; "wander" is a seeded walk.
    static const std::vector<Progression>& progressions()
    {
        static const std::vector<Progression> p = {
            { "still",        { 0 } },
            { "I V vi IV",    { 0, 4, 5, 3 } },
            { "i VI III VII", { 0, 5, 2, 6 } },
            { "ii V I",       { 1, 4, 0, 0 } },
            { "pendulum",     { 0, 3 } },
            { "fifths",       { 0, 4, 1, 5, 2, 6, 3 } },
            { "wander",       {} },
        };
        return p;
    }

    static juce::StringArray scaleNames()       { juce::StringArray a; for (auto& s : scales()) a.add (s.name); return a; }
    static juce::StringArray progressionNames() { juce::StringArray a; for (auto& p : progressions()) a.add (p.name); return a; }
    static juce::StringArray keyNames()         { return { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }; }
    static juce::StringArray chordLengthNames() { return { "1/2 bar", "1 bar", "2 bars", "4 bars", "8 bars" }; }
    static double chordLengthBars (int i)       { static constexpr double b[] = { 0.5, 1, 2, 4, 8 }; return b[juce::jlimit (0, 4, i)]; }

    void configure (int newRoot, int scaleIndex, int progressionIndex) noexcept
    {
        root = juce::jlimit (0, 11, newRoot);
        scale = &scales()[(size_t) juce::jlimit (0, (int) scales().size() - 1, scaleIndex)].steps;
        progression = juce::jlimit (0, (int) progressions().size() - 1, progressionIndex);
    }

    int getRoot() const noexcept { return root; }
    int scaleSize() const noexcept { return (int) scale->size(); }

    // Chord degree in effect at a timeline position (stateless, so it
    // follows the DAW's transport exactly).
    int chordDegreeAt (double ppq, double barBeats, double chordBars) const noexcept
    {
        const auto index = (int64_t) std::floor (ppq / juce::jmax (0.25, barBeats * chordBars));
        const auto& degrees = progressions()[(size_t) progression].degrees;
        if (degrees.empty())
        {
            // wander: each chord a small step from the last, seeded by position
            int d = 0;
            for (int64_t i = juce::jmax<int64_t> (0, index - 16); i <= index; ++i)
            {
                const auto h = (uint32_t) (i * 2654435761u) >> 29;   // 0..7
                static constexpr int moves[] = { 0, 1, -1, 2, -2, 3, 4, -3 };
                d += moves[h];
            }
            return ((d % 7) + 7) % 7;
        }
        return degrees[(size_t) (((index % (int64_t) degrees.size()) + (int64_t) degrees.size()) % (int64_t) degrees.size())];
    }

    // Pitch class (0..11) of a scale degree, any integer degree.
    int pitchClassOf (int degree) const noexcept
    {
        const int n = scaleSize();
        const int d = ((degree % n) + n) % n;
        return (root + (*scale)[(size_t) d]) % 12;
    }

    // Up to four chord tones stacked in scale thirds on a degree.
    std::array<int, 4> chordPitchClasses (int degree) const noexcept
    {
        return { pitchClassOf (degree), pitchClassOf (degree + 2), pitchClassOf (degree + 4), pitchClassOf (degree + 6) };
    }

    static float nearestWithPitchClass (float midi, int pc) noexcept
    {
        float diff = std::fmod ((float) pc - midi, 12.0f);
        if (diff > 6.0f) diff -= 12.0f;
        if (diff < -6.0f) diff += 12.0f;
        return midi + diff;
    }

    float snapToScale (float midi) const noexcept
    {
        float best = midi, bestDist = 1.0e9f;
        for (int d = 0; d < scaleSize(); ++d)
        {
            const float cand = nearestWithPitchClass (midi, pitchClassOf (d));
            if (std::abs (cand - midi) < bestDist) { bestDist = std::abs (cand - midi); best = cand; }
        }
        return std::round (best);
    }

    float chordToneNear (float midi, int degree, int toneIndex) const noexcept
    {
        return std::round (nearestWithPitchClass (midi, chordPitchClasses (degree)[(size_t) (toneIndex & 3)]));
    }

    static float hzToMidi (float hz) noexcept { return 69.0f + 12.0f * std::log2 (hz / 440.0f); }
    static float midiToHz (float m) noexcept  { return 440.0f * std::pow (2.0f, (m - 69.0f) / 12.0f); }

private:
    int root = 0;
    const std::vector<int>* scale = &scales()[1].steps;
    int progression = 0;
};

// YIN pitch estimate on a mono block (already decimated). Returns the period
// in samples, or 0 when the sound has no clear pitch (a snare, a breath).
inline float estimatePeriod (const float* x, int window, int maxTau, int minTau) noexcept
{
    std::array<float, 1024> d {};
    maxTau = juce::jmin (maxTau, (int) d.size() - 2);
    float running = 0.0f;
    d[0] = 1.0f;
    int found = 0;

    for (int tau = 1; tau <= maxTau; ++tau)
    {
        float sum = 0.0f;
        for (int i = 0; i < window; ++i)
        {
            const float diff = x[i] - x[i + tau];
            sum += diff * diff;
        }
        running += sum;
        d[(size_t) tau] = running > 0.0f ? sum * (float) tau / running : 1.0f;

        if (found == 0 && tau > minTau && d[(size_t) tau] < 0.15f)
            found = tau;
        if (found != 0 && tau > found && d[(size_t) tau] > d[(size_t) (tau - 1)])
            break;
    }

    int best = found;
    if (best == 0)
    {
        float m = 1.0f;
        for (int tau = minTau; tau <= maxTau; ++tau)
            if (d[(size_t) tau] < m) { m = d[(size_t) tau]; best = tau; }
        if (m > 0.32f)
            return 0.0f;
    }
    else
    {
        while (best + 1 <= maxTau && d[(size_t) (best + 1)] < d[(size_t) best])
            ++best;
    }

    if (best <= 1 || best >= maxTau)
        return (float) best;
    const float a = d[(size_t) (best - 1)], b = d[(size_t) best], c = d[(size_t) (best + 1)];
    const float den = a - 2.0f * b + c;
    return (float) best + (std::abs (den) > 1.0e-9f ? 0.5f * (a - c) / den : 0.0f);
}

} // namespace rhytms
