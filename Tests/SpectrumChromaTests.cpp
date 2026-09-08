#include "ChordMatcher.h"
#include "SpectrumChroma.h"
#include <iostream>
#include <stdexcept>
#include <vector>

namespace
{
constexpr int fftSize = 8192;
constexpr double sampleRate = 48000.0;

void require(bool okay, const char* message)
{
    if (!okay)
        throw std::runtime_error(message);
}

double midiFrequency(int midi)
{
    return 440.0 * std::pow(2.0, (midi - 69) / 12.0);
}

void addPeak(std::vector<float>& spectrum, double frequency, float magnitude)
{
    const double exactBin = frequency * fftSize / sampleRate;
    const int centre = static_cast<int>(std::floor(exactBin + 0.5));
    for (int delta = -2; delta <= 2; ++delta)
    {
        const int bin = centre + delta;
        if (bin > 0 && bin < static_cast<int>(spectrum.size()))
        {
            const float distance = static_cast<float>(std::abs(exactBin - bin));
            spectrum[static_cast<size_t>(bin)] += magnitude * std::max(0.0f, 1.0f - 0.34f * distance);
        }
    }
}

std::vector<float> chordSpectrum(int chord, int inversion)
{
    std::vector<float> spectrum(fftSize / 2 + 1, 0.001f);
    const int root = chord % 12;
    const int thirdInterval = chord < 12 ? 4 : 3;
    const std::array<int, 3> pitchClasses { root, (root + thirdInterval) % 12, (root + 7) % 12 };
    const std::array<float, 3> levels { 1.0f, 0.82f, 0.68f };
    for (int tone = 0; tone < 3; ++tone)
    {
        const int selected = (tone + inversion) % 3;
        int midi = 48 + pitchClasses[static_cast<size_t>(selected)];
        while (midi < 48)
            midi += 12;
        const double fundamental = midiFrequency(midi);
        for (int harmonic = 1; harmonic <= 5; ++harmonic)
            addPeak(spectrum, fundamental * harmonic,
                    levels[static_cast<size_t>(tone)] / static_cast<float>(harmonic));
    }
    return spectrum;
}
}

int main()
{
    try
    {
        ChordMatcher matcher;
        for (int chord = 0; chord < ChordMatcher::chordCount; ++chord)
            for (int inversion = 0; inversion < 3; ++inversion)
            {
                const auto spectrum = chordSpectrum(chord, inversion);
                const auto chroma = SpectrumChroma::convert(spectrum.data(),
                    static_cast<int>(spectrum.size()), fftSize, sampleRate);
                const auto result = matcher.match(chroma, -18.0f, 65.0f);
                if (result.chord != chord)
                    std::cerr << "Expected " << ChordMatcher::name(chord) << " inversion "
                              << inversion << ", got " << ChordMatcher::name(result.chord)
                              << ", score " << result.score << ", margin " << result.margin << '\n';
                require(result.chord == chord, "Spectrum-to-chroma failed a chord or inversion");
            }
        std::cout << "PASS: harmonic spectra for 24 chords and three inversions\n";

        std::vector<float> noise(fftSize / 2 + 1, 1.0f);
        const auto flat = SpectrumChroma::convert(noise.data(), static_cast<int>(noise.size()),
                                                  fftSize, sampleRate);
        require(matcher.match(flat, -18.0f, 65.0f).chord == -1,
                "Broadband spectrum must not become a chord");
        const auto invalid = SpectrumChroma::convert(nullptr, 0, fftSize, sampleRate);
        require(matcher.match(invalid, -18.0f, 100.0f).chord == -1,
                "Invalid spectrum must not become a chord");
        std::cout << "PASS: broadband and invalid spectrum rejection\n";
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
