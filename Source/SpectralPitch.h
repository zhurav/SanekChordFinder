#pragma once

#include <algorithm>
#include <array>
#include <cmath>

// Fixed storage: called on the audio thread after channel spectra are combined.
class SpectralPitch
{
public:
    void reset() noexcept { cents = evidence = 0.0; ready = false; resetBass(); }
    void resetBass() noexcept { bass = candidate = -1; frames = missing = 0; }
    double a4() const noexcept { return 440.0 * std::exp2(cents / 1200.0); }
    double tuningCents() const noexcept { return cents; }
    bool tuningReady() const noexcept { return ready; }
    int bassNote() const noexcept { return bass; }

    void process(const float* spectrum, int size, int fftSize, double rate,
                 double elapsed, bool audible) noexcept
    {
        if (!audible) { resetBass(); return; }
        float maximum = 0.0f;
        for (int bin = 2; bin < size - 2 && bin * rate / fftSize <= 1800.0; ++bin)
            maximum = std::max(maximum, spectrum[bin]);
        double x = 0.0, y = 0.0, weight = 0.0;
        int peakCount = 0;
        constexpr double twoPi = 6.283185307179586;
        for (int bin = 2; bin < size - 2; ++bin)
        {
            const double nominal = bin * rate / fftSize;
            if (nominal > 1600.0) break;
            if (nominal < 150.0 || spectrum[bin] < maximum * 0.12f || !peak(spectrum, bin)) continue;
            const double frequency = interpolatedBin(spectrum, bin) * rate / fftSize;
            const double note = 69.0 + 12.0 * std::log2(frequency / 440.0);
            const double angle = twoPi * (note - std::round(note));
            const double w = std::sqrt(spectrum[bin]);
            x += w * std::cos(angle); y += w * std::sin(angle); weight += w; ++peakCount;
        }
        // Reject broadband/inharmonic spectra and mutually inconsistent tunings.
        if (peakCount > 0 && weight > 0.0 && std::hypot(x, y) / weight > 0.80)
        {
            const double estimate = 100.0 * std::atan2(y, x) / twoPi;
            evidence += elapsed;
            if (evidence >= 0.7)
            {
                const double difference = std::remainder(estimate - cents, 100.0);
                cents = std::remainder(cents + (1.0 - std::exp(-elapsed / 0.7)) * difference, 100.0);
                ready = true;
            }
        }
        float lowMaximum = 0.0f;
        for (int bin = 2; bin < size - 2 && bin * rate / fftSize <= 255.0; ++bin)
            if (bin * rate / fftSize >= 35.0) lowMaximum = std::max(lowMaximum, spectrum[bin]);
        int found = -1;
        for (int bin = 2; bin < size - 2 && bin * rate / fftSize <= 255.0; ++bin)
        {
            if (spectrum[bin] < std::max(maximum * 0.10f, lowMaximum * 0.22f)
                || !peak(spectrum, bin)) continue;
            const double frequency = interpolatedBin(spectrum, bin) * rate / fftSize;
            if (frequency < 40.0 || frequency > 250.0) continue;
            const double note = 69.0 + 12.0 * std::log2(frequency / a4());
            if (std::abs(note - std::round(note)) > 0.24) continue;
            found = static_cast<int>(std::lround(note));
            break; // Lowest supported tone, not the loudest overtone.
        }
        if (found < 0)
        {
            candidate = -1; frames = 0;
            if (++missing * elapsed > 0.3) bass = -1;
        }
        else
        {
            missing = 0;
            if (found != candidate) { candidate = found; frames = 0; }
            if (++frames * elapsed >= 0.12) bass = candidate;
        }
    }

private:
    static bool peak(const float* values, int bin) noexcept
    {
        return values[bin] > 0.0f && values[bin] > values[bin - 1]
            && values[bin] >= values[bin + 1]
            && values[bin] > 2.0f * std::min(values[bin - 2], values[bin + 2]);
    }
    static double interpolatedBin(const float* values, int bin) noexcept
    {
        const double left = std::log(std::max(1.0e-12f, values[bin - 1]));
        const double centre = std::log(std::max(1.0e-12f, values[bin]));
        const double right = std::log(std::max(1.0e-12f, values[bin + 1]));
        const double denominator = left - 2.0 * centre + right;
        return bin + (std::abs(denominator) > 1.0e-12
            ? std::clamp(0.5 * (left - right) / denominator, -0.5, 0.5) : 0.0);
    }
    double cents = 0.0, evidence = 0.0;
    bool ready = false;
    int bass = -1, candidate = -1, frames = 0, missing = 0;
};
