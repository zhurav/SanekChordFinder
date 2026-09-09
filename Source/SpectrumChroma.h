#pragma once

#include <algorithm>
#include <array>
#include <cmath>

class SpectrumChroma
{
public:
    static std::array<float, 12> convert(const float* magnitudes, int magnitudeCount,
                                         int fftSize, double sampleRate) noexcept
    {
        std::array<float, 12> chroma {};
        if (magnitudes == nullptr || magnitudeCount <= 1 || fftSize <= 0
            || !std::isfinite(sampleRate) || sampleRate <= 0.0)
            return chroma;

        const int firstBin = std::max(1, static_cast<int>(std::ceil(55.0 * fftSize / sampleRate)));
        const int lastBin = std::min(magnitudeCount - 1,
                                     static_cast<int>(std::floor(1800.0 * fftSize / sampleRate)));
        for (int bin = firstBin; bin <= lastBin; ++bin)
        {
            const double frequency = static_cast<double>(bin) * sampleRate / fftSize;
            constexpr double pi = 3.14159265358979323846;
            const float magnitude = std::max(0.0f,
                std::isfinite(magnitudes[bin]) ? magnitudes[bin] : 0.0f);
            // Real guitar overtones were the main source of false D#m/C#m/Cm
            // detections in v0.1. Map each peak only to the pitch it actually
            // represents; do not also treat it as five speculative fundamentals.
            const int left = std::max(0, bin - 2);
            const int right = std::min(magnitudeCount - 1, bin + 2);
            const bool localPeak = magnitude > magnitudes[left] && magnitude >= magnitudes[right];
            const float contrastWeight = localPeak ? 1.0f : 0.10f;
            const double midi = 69.0 + 12.0 * std::log2(frequency / 440.0);
            const int nearestMidi = static_cast<int>(std::floor(midi + 0.5));
            const double distance = std::clamp(midi - nearestMidi, -0.5, 0.5);
            const float tuningWeight = static_cast<float>(std::pow(std::cos(distance * pi), 2.0));
            const float frequencyWeight = static_cast<float>(1.0 / std::sqrt(std::max(1.0,
                                                                     frequency / 110.0)));
            const int pitchClass = (nearestMidi % 12 + 12) % 12;
            chroma[static_cast<size_t>(pitchClass)] += std::pow(magnitude, 0.70f)
                                                     * contrastWeight
                                                     * tuningWeight
                                                     * frequencyWeight;
        }

        // Reduce broadband energy from drums and noise without deleting quiet chord tones.
        auto sorted = chroma;
        std::sort(sorted.begin(), sorted.end());
        const float floor = 0.20f * 0.5f * (sorted[5] + sorted[6]);
        float maximum = 0.0f;
        for (auto& value : chroma)
        {
            value = std::max(0.0f, value - floor);
            maximum = std::max(maximum, value);
        }
        if (maximum > 0.0f)
            for (auto& value : chroma)
                value /= maximum;
        return chroma;
    }

};
