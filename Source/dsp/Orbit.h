#pragma once

#include <juce_core/juce_core.h>
#include <cstdint>

namespace rhytms
{

// A circular rhythm. Its skeleton is euclidean (pulses spread as evenly as
// possible over the steps), but each time the circle closes it may grow:
// an elementary cellular automaton rewrites the ring, while a pull back
// towards the skeleton keeps it from forgetting where it came from.
class Orbit
{
public:
    static constexpr int maxSteps = 32;

    static uint32_t euclid (int steps, int pulses, int rotate) noexcept
    {
        uint32_t m = 0;
        if (steps <= 0 || pulses <= 0)
            return 0;
        pulses = juce::jmin (pulses, steps);
        for (int i = 0; i < steps; ++i)
        {
            const int j = ((i - rotate) % steps + steps) % steps;
            if ((j * pulses) % steps < pulses)
                m |= (1u << i);
        }
        return m;
    }

    // Rebuild the skeleton when the shape changes. Keeps the grown pattern
    // if nothing changed.
    void setShape (int steps, int pulses, int rotate) noexcept
    {
        if (steps == numSteps && pulses == numPulses && rotate == rot)
            return;
        numSteps = juce::jlimit (1, maxSteps, steps);
        numPulses = pulses;
        rot = rotate;
        skeleton = euclid (numSteps, numPulses, rot);
        pattern = skeleton;
    }

    void reset() noexcept { pattern = skeleton; generation = 0; }

    // Called when the circle closes.
    void onCycle (float evolve, float order, juce::Random& rng) noexcept
    {
        ++generation;
        const uint32_t full = numSteps >= 32 ? 0xffffffffu : ((1u << numSteps) - 1u);

        if (rng.nextFloat() < evolve)
        {
            // Calm weather favours symmetric rule 90, storms favour rule 30 / 110.
            int rule = 90;
            const float r = rng.nextFloat();
            if (r < order * 0.6f)       rule = 30;
            else if (r < order * 0.9f)  rule = 110;
            else if (r > 0.85f)         rule = 150;

            uint32_t next = 0;
            for (int i = 0; i < numSteps; ++i)
            {
                const int l = bit ((i - 1 + numSteps) % numSteps);
                const int c = bit (i);
                const int rr = bit ((i + 1) % numSteps);
                const int idx = (l << 2) | (c << 1) | rr;
                if ((rule >> idx) & 1)
                    next |= (1u << i);
            }

            // Anchor: each cell may fall back to the skeleton.
            const float anchor = 0.55f * (1.0f - evolve);
            for (int i = 0; i < numSteps; ++i)
                if (rng.nextFloat() < anchor)
                    next = (next & ~(1u << i)) | (skeleton & (1u << i));

            // Rings that fill up or empty out return home.
            const int count = juce::countNumberOfBits (next & full);
            if (count == 0 || count > juce::jmax (numPulses * 2 + 2, numSteps * 3 / 4))
                next = skeleton;

            pattern = next & full;
        }
        else if (rng.nextFloat() < 0.5f)
        {
            for (int i = 0; i < numSteps; ++i)
                if (rng.nextFloat() < 0.3f)
                    pattern = (pattern & ~(1u << i)) | (skeleton & (1u << i));
        }
    }

    bool isOn (int step) const noexcept { return bit (step) != 0; }
    uint32_t getPattern() const noexcept { return pattern; }
    int getSteps() const noexcept { return numSteps; }
    int firstHit() const noexcept
    {
        for (int i = 0; i < numSteps; ++i)
            if (isOn (i)) return i;
        return 0;
    }

private:
    int bit (int i) const noexcept { return (int) ((pattern >> i) & 1u); }

    int numSteps = 16, numPulses = 4, rot = 0;
    uint32_t skeleton = 0, pattern = 0;
    int64_t generation = 0;
};

} // namespace rhytms
