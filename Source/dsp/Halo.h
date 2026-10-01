#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>
#include <vector>

namespace rhytms
{

// Fractional delay line used by both the dust (wow) and the halo.
class DelayLine
{
public:
    void prepare (int maxSamples)
    {
        data.assign ((size_t) maxSamples + 4, 0.0f);
        w = 0;
    }
    void clear() { std::fill (data.begin(), data.end(), 0.0f); }

    void write (float x) noexcept
    {
        data[(size_t) w] = x;
        if (++w >= (int) data.size()) w = 0;
    }

    float read (double delay) const noexcept
    {
        const int n = (int) data.size();
        delay = juce::jlimit (1.0, (double) n - 3.0, delay);
        double rp = (double) w - delay;
        while (rp < 0.0) rp += n;
        const int i = (int) rp;
        const float t = (float) (rp - i);
        const float a = data[(size_t) i];
        const float b = data[(size_t) ((i + 1) % n)];
        return a + t * (b - a);
    }

private:
    std::vector<float> data;
    int w = 0;
};

// "Dust": tape-like wobble and soft saturation, for the struck memories.
class Dust
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        for (auto& d : lines) d.prepare ((int) (sr * 0.03));
        phase = 0.0;
        flutter = 0.0f;
        lp[0] = lp[1] = 0.0f;
    }

    void process (float* l, float* r, int n, float amount, juce::Random& rng) noexcept
    {
        if (amount <= 0.0005f)
        {
            for (int i = 0; i < n; ++i) { lines[0].write (l[i]); lines[1].write (r[i]); }
            return;
        }

        const float drive = 1.0f + amount * 7.0f;
        const float norm = 1.0f / std::tanh (drive);
        const double base = sr * 0.006;
        const double depth = sr * 0.0022 * amount;
        const float cutoff = 20000.0f * std::pow (0.3f, amount);
        const float a = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * cutoff / (float) sr);

        for (int i = 0; i < n; ++i)
        {
            phase += 0.55 / sr;
            if (phase >= 1.0) phase -= 1.0;
            flutter += 0.0007f * ((rng.nextFloat() * 2.0f - 1.0f) - flutter);
            const double mod = base + depth * (std::sin (phase * juce::MathConstants<double>::twoPi) + 2.5 * flutter);

            lines[0].write (l[i]);
            lines[1].write (r[i]);
            float x[2] = { lines[0].read (mod), lines[1].read (mod + depth * 0.2) };

            for (int c = 0; c < 2; ++c)
            {
                lp[c] += a * (x[c] - lp[c]);
                x[c] = std::tanh (lp[c] * drive) * norm;
            }

            l[i] = x[0];
            r[i] = x[1];
        }
    }

private:
    double sr = 44100.0, phase = 0.0;
    float flutter = 0.0f, lp[2] {};
    DelayLine lines[2];
};

// "Halo": a tempo-locked, diffused, darkening feedback space that lets the
// struck rhythm leave ghosts of itself between the beats.
class Halo
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        for (auto& d : delays) d.prepare ((int) (sr * 4.0));
        static constexpr double apTimes[4] = { 0.0047, 0.0071, 0.0113, 0.0159 };
        for (int i = 0; i < 4; ++i)
        {
            allpass[i].prepare ((int) (sr * 0.05));
            apLen[i] = sr * apTimes[i];
        }
        fbState[0] = fbState[1] = 0.0f;
        smoothedTime[0] = smoothedTime[1] = sr * 0.5;
        lfo = 0.0;
    }

    void process (float* l, float* r, int n, float amount, double beatSamples) noexcept
    {
        if (amount <= 0.0005f)
        {
            for (int i = 0; i < n; ++i) { delays[0].write (0.0f); delays[1].write (0.0f); }
            return;
        }

        const double targetL = juce::jlimit (sr * 0.02, sr * 3.9, beatSamples * 0.75); // dotted eighth
        const double targetR = juce::jlimit (sr * 0.02, sr * 3.9, beatSamples * 1.0);  // quarter
        const float fb = 0.25f + amount * 0.62f;
        const float damp = 0.35f + 0.4f * amount;
        const float g = 0.6f;

        for (int i = 0; i < n; ++i)
        {
            smoothedTime[0] += 0.0002 * (targetL - smoothedTime[0]);
            smoothedTime[1] += 0.0002 * (targetR - smoothedTime[1]);
            lfo += 0.21 / sr;
            if (lfo >= 1.0) lfo -= 1.0;
            const double wob = std::sin (lfo * juce::MathConstants<double>::twoPi) * sr * 0.0012;

            const float dl = delays[0].read (smoothedTime[0] + wob);
            const float dr = delays[1].read (smoothedTime[1] - wob);

            // darkening cross-feedback
            fbState[0] += (1.0f - damp) * (dr - fbState[0]);
            fbState[1] += (1.0f - damp) * (dl - fbState[1]);

            float inL = l[i] + fbState[0] * fb;
            float inR = r[i] + fbState[1] * fb;

            // diffusion: two allpasses per side
            inL = diffuse (0, inL, g);
            inL = diffuse (1, inL, g);
            inR = diffuse (2, inR, g);
            inR = diffuse (3, inR, g);

            delays[0].write (std::tanh (inL));
            delays[1].write (std::tanh (inR));

            l[i] += dl * amount;
            r[i] += dr * amount;
        }
    }

private:
    float diffuse (int i, float x, float g) noexcept
    {
        const float d = allpass[i].read (apLen[i]);
        const float v = x + g * d;
        allpass[i].write (v);
        return d - g * v;
    }

    double sr = 44100.0, lfo = 0.0;
    DelayLine delays[2];
    DelayLine allpass[4];
    double apLen[4] {};
    float fbState[2] {};
    double smoothedTime[2] {};
};

} // namespace rhytms
