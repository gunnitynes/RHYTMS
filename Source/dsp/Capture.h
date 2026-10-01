#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>

#include "Harmony.h"

namespace rhytms
{

// One remembered strike: where it begins in the capture memory and how long
// it rings before the next strike arrives.
struct Slice
{
    int64_t start = 0;
    int length = 0;
    float energy = 0.0f;
    float pitch = -1.0f;  // MIDI note, -1 = no clear pitch
    bool analysed = false;
};

// Continuous memory of the input with an onset detector that cuts it into
// slices, plus a "mirror": a one-bar fingerprint of where the input tends to
// strike. While held (the sostenuto pedal), the memory stops writing, so the
// slices caught at that moment are kept, while the mirror keeps listening.
class Capture
{
public:
    static constexpr int maxSlices = 32;
    static constexpr int fingerprintCells = 32;
    static constexpr int overviewSize = 256;

    void prepare (double newSampleRate)
    {
        sampleRate = newSampleRate;
        length = (int) (sampleRate * 20.0);
        buffer.setSize (2, length + 4, false, true, false);
        buffer.clear();
        writeCount = 0;
        sliceCount = 0;
        newest = -1;
        lastPitch = -1.0f;
        envFast = envSlow = 0.0f;
        sinceOnset = 1 << 30;
        lastBarPhase = 0.0;
        fingerprint.fill (0.0f);
        bucketPeak = 0.0f;
        currentBucket = 0;
        for (auto& p : overview) p.store (0.0f);

        fastAttack = coef (0.0005);
        fastRelease = coef (0.015);
        slowCoef = coef (0.12);
    }

    void setHold (bool shouldHold) noexcept { held = shouldHold; }
    bool isHeld() const noexcept { return held; }

    void setSense (float s) noexcept
    {
        ratio = juce::jmap (s, 3.2f, 1.25f);
        floorLevel = juce::Decibels::decibelsToGain (juce::jmap (s, -34.0f, -62.0f));
        refractory = (int) (sampleRate * juce::jmap ((double) s, 0.085, 0.035));
    }

    // barPhase is 0..1 position inside the current bar.
    void push (float l, float r, double barPhase) noexcept
    {
        if (barPhase < lastBarPhase - 0.5)
            for (auto& w : fingerprint)
                w *= 0.82f;
        lastBarPhase = barPhase;

        const float level = juce::jmax (std::abs (l), std::abs (r));
        envFast += (level > envFast ? fastAttack : fastRelease) * (level - envFast);
        envSlow += slowCoef * (level - envSlow);
        ++sinceOnset;

        if (envFast > floorLevel && envFast > envSlow * ratio && sinceOnset > refractory)
        {
            sinceOnset = 0;
            onOnset (barPhase, envFast);
        }

        if (! held)
        {
            const int idx = (int) (writeCount % length);
            buffer.setSample (0, idx, l);
            buffer.setSample (1, idx, r);
            ++writeCount;

            bucketPeak = juce::jmax (bucketPeak, level);
            const int bucket = (int) ((int64_t) idx * overviewSize / length);
            if (bucket != currentBucket)
            {
                overview[(size_t) currentBucket].store (bucketPeak, std::memory_order_relaxed);
                bucketPeak = 0.0f;
                currentBucket = bucket;
            }
        }
    }

    // Read with cubic interpolation at an absolute (ever-increasing) position.
    float read (int channel, double absPos) const noexcept
    {
        const double lo = (double) (writeCount - length + 8);
        const double hi = (double) (writeCount - 2);
        if (hi <= 0.0)
            return 0.0f;
        absPos = juce::jlimit (juce::jmax (0.0, lo), hi, absPos);

        const auto i = (int64_t) absPos;
        const float t = (float) (absPos - (double) i);
        const float* d = buffer.getReadPointer (channel);
        int k = (int) ((i - 1) % length);
        if (k < 0) k += length;
        auto next = [&] { const float v = d[k]; if (++k >= length) k = 0; return v; };

        const float y0 = next(), y1 = next(), y2 = next(), y3 = next();
        const float c1 = 0.5f * (y2 - y0);
        const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * t + c2) * t + c1) * t + y1;
    }

    int numSlices() const noexcept { return sliceCount; }

    // 0 = most recent strike. Returns false when there is nothing usable,
    // in which case the last fraction of a second is offered instead.
    bool getSlice (int fromNewest, Slice& out) const noexcept
    {
        const int maxLen = (int) (sampleRate * 2.5);

        if (sliceCount == 0)
        {
            const int len = (int) (sampleRate * 0.3);
            if (writeCount < len + 16)
                return false;
            out = { writeCount - len, len, 0.5f };
            return true;
        }

        fromNewest = juce::jlimit (0, sliceCount - 1, fromNewest);
        const Slice& s = slices[(size_t) (((newest - fromNewest) % maxSlices + maxSlices) % maxSlices)];
        out = s;

        if (fromNewest == 0)
            out.length = (int) juce::jmin<int64_t> (writeCount - s.start, maxLen);

        const int minLen = (int) (sampleRate * 0.02);
        if (out.length < minLen)
        {
            if (fromNewest + 1 < sliceCount)
                return getSlice (fromNewest + 1, out);
            out.length = (int) juce::jmin<int64_t> (writeCount - s.start, minLen);
            if (out.length < 8)
                return false;
        }

        return writeCount - out.start < (int64_t) (length - sampleRate * 9.0);
    }

    // Fundamental of a strike as a MIDI note, or -1 when it has no clear
    // pitch. Measured once, just after the attack, then remembered.
    float analysePitch (int fromNewest) noexcept
    {
        if (sliceCount == 0)
            return -1.0f;
        fromNewest = juce::jlimit (0, sliceCount - 1, fromNewest);
        auto& s = slices[(size_t) (((newest - fromNewest) % maxSlices + maxSlices) % maxSlices)];
        if (s.analysed)
            return s.pitch;

        constexpr int window = 768, maxTau = 600, decimate = 2;
        const int skip = (int) (sampleRate * 0.012);
        const int need = skip + (window + maxTau + 2) * decimate;
        const auto available = fromNewest == 0 ? writeCount - s.start : (int64_t) s.length;
        if (available < need)
        {
            if (fromNewest != 0 || held)
                s.analysed = true;   // too short to ever tell
            return -1.0f;
        }

        std::array<float, window + maxTau + 4> x {};
        float peak = 0.0f;
        for (size_t i = 0; i < x.size(); ++i)
        {
            const double pos = (double) (s.start + skip) + (double) i * decimate;
            x[i] = 0.25f * (read (0, pos) + read (1, pos) + read (0, pos + 1.0) + read (1, pos + 1.0));
            peak = juce::jmax (peak, std::abs (x[i]));
        }
        s.analysed = true;
        if (peak < 1.0e-4f)
            return s.pitch = -1.0f;

        const double rate = sampleRate / decimate;
        const int minTau = juce::jmax (2, (int) (rate / 1500.0));
        const int maxT = juce::jmin (maxTau, (int) (rate / 45.0));
        const float period = estimatePeriod (x.data(), window, maxT, minTau);
        s.pitch = period > 0.0f ? Harmony::hzToMidi ((float) (rate / period)) : -1.0f;
        return s.pitch;
    }

    // A strike played the instant it is caught is too fresh to measure, so
    // it borrows the last pitch heard (a melody's notes rarely jump far).
    float pitchOf (const Slice& slice) noexcept
    {
        for (int i = 0; i < sliceCount; ++i)
        {
            const auto& s = slices[(size_t) (((newest - i) % maxSlices + maxSlices) % maxSlices)];
            if (s.start != slice.start)
                continue;
            const float p = analysePitch (i);
            if (p > 0.0f)
                lastPitch = p;
            return s.analysed ? p : lastPitch;
        }
        return lastPitch;
    }

    // Normalised mirror value for a 0..1 bar position, plus overall confidence.
    float mirrorAt (double barPhase) const noexcept
    {
        const float mx = fingerprintMax();
        if (mx < 1.0e-3f)
            return 0.0f;
        const int cell = juce::jlimit (0, fingerprintCells - 1, (int) (barPhase * fingerprintCells));
        return fingerprint[(size_t) cell] / mx;
    }

    float mirrorConfidence() const noexcept { return juce::jlimit (0.0f, 1.0f, fingerprintMax() * 2.5f); }

    float fingerprintMax() const noexcept
    {
        float mx = 0.0f;
        for (auto w : fingerprint) mx = juce::jmax (mx, w);
        return mx;
    }

    const std::array<float, fingerprintCells>& getFingerprint() const noexcept { return fingerprint; }

    // ---- visual taps (read from the message thread) ----
    double writeFraction() const noexcept { return length > 0 ? (double) (writeCount % length) / length : 0.0; }
    float ageOf (int64_t absPos) const noexcept { return length > 0 ? (float) (writeCount - absPos) / (float) length : 1.0f; }
    std::array<std::atomic<float>, overviewSize> overview {};

private:
    float coef (double seconds) const { return (float) (1.0 - std::exp (-1.0 / (seconds * sampleRate))); }

    void onOnset (double barPhase, float strength) noexcept
    {
        const int cell = juce::jlimit (0, fingerprintCells - 1, (int) (barPhase * fingerprintCells));
        const float amount = juce::jlimit (0.2f, 1.0f, strength * 4.0f);
        fingerprint[(size_t) cell] += (1.0f - fingerprint[(size_t) cell]) * 0.55f * amount;

        if (held)
            return;

        const int preroll = (int) (sampleRate * 0.003);
        const int64_t start = juce::jmax<int64_t> (0, writeCount - preroll);

        if (newest >= 0)
        {
            auto& prev = slices[(size_t) newest];
            prev.length = (int) juce::jmin<int64_t> (start - prev.start, (int64_t) (sampleRate * 2.5));
        }

        newest = (newest + 1) % maxSlices;
        slices[(size_t) newest] = { start, 0, strength };
        sliceCount = juce::jmin (sliceCount + 1, maxSlices);
    }

    double sampleRate = 44100.0;
    int length = 1;
    juce::AudioBuffer<float> buffer;
    int64_t writeCount = 0;
    bool held = false;

    std::array<Slice, maxSlices> slices {};
    int sliceCount = 0, newest = -1;
    float lastPitch = -1.0f;

    float envFast = 0.0f, envSlow = 0.0f;
    float fastAttack = 0.1f, fastRelease = 0.01f, slowCoef = 0.001f;
    float ratio = 2.0f, floorLevel = 0.01f;
    int refractory = 2000, sinceOnset = 0;

    std::array<float, fingerprintCells> fingerprint {};
    double lastBarPhase = 0.0;

    float bucketPeak = 0.0f;
    int currentBucket = 0;
};

} // namespace rhytms
