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

bool samePitchSet(int first, int second)
{
    if (first < 0 || second < 0)
        return false;
    for (int pitch = 0; pitch < 12; ++pitch)
        if (ChordMatcher::containsPitch(first, pitch)
            != ChordMatcher::containsPitch(second, pitch))
            return false;
    return true;
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
    const int toneCount = ChordMatcher::toneCount(chord);
    const std::array<float, 4> levels { 1.0f, 0.84f, 0.72f, 1.0f };
    for (int tone = 0; tone < toneCount; ++tone)
    {
        const int selected = (tone + inversion) % toneCount;
        const int octaveBase = toneCount == 4 ? 60 : 48;
        int midi = octaveBase + ChordMatcher::rootOf(chord)
                 + ChordMatcher::intervalAt(chord, selected);
        while (midi >= octaveBase + 12)
            midi -= 12;
        while (midi < octaveBase)
            midi += 12;
        const double fundamental = midiFrequency(midi);
        for (int harmonic = 1; harmonic <= 5; ++harmonic)
            addPeak(spectrum, fundamental * harmonic,
                    (toneCount == 4 ? 1.0f : levels[static_cast<size_t>(tone)])
                        / static_cast<float>(harmonic));
    }
    return spectrum;
}
}

int main()
{
    try
    {
        ChordMatcher matcher;
        int checked = 0;
        for (int chord = 0; chord < ChordMatcher::chordCount; ++chord)
        {
            const int quality = ChordMatcher::qualityOf(chord);
            const int inversions = quality == ChordMatcher::major
                                || quality == ChordMatcher::minor
                                 ? ChordMatcher::toneCount(chord) : 1;
            for (int inversion = 0; inversion < inversions; ++inversion)
            {
                const auto spectrum = chordSpectrum(chord, inversion);
                const auto chroma = SpectrumChroma::convert(spectrum.data(),
                    static_cast<int>(spectrum.size()), fftSize, sampleRate);
                const auto result = matcher.match(chroma, -18.0f, 65.0f, true);
                const bool rootAmbiguousQuality = quality == ChordMatcher::sus2
                                               || quality == ChordMatcher::sus4
                                               || quality == ChordMatcher::diminished;
                const bool accepted = result.chord == chord
                    || (rootAmbiguousQuality && samePitchSet(result.chord, chord));
                if (!accepted)
                {
                    std::cerr << "Expected " << ChordMatcher::name(chord) << " inversion "
                              << inversion << ", got " << ChordMatcher::name(result.chord)
                              << ", alternative " << ChordMatcher::name(result.alternative)
                              << ", score " << result.score << ", margin " << result.margin << '\n';
                    std::cerr << "Chroma:";
                    for (const float value : chroma)
                        std::cerr << ' ' << value;
                    std::cerr << '\n';
                }
                require(accepted, "Spectrum-to-chroma failed a chord or inversion");
                ++checked;
            }
        }
        std::cout << "PASS: harmonic spectra for " << checked
                  << " chord voicings across 96 chord names\n";

        std::vector<float> noise(fftSize / 2 + 1, 1.0f);
        const auto flat = SpectrumChroma::convert(noise.data(), static_cast<int>(noise.size()),
                                                  fftSize, sampleRate);
        require(matcher.match(flat, -18.0f, 65.0f, true).chord == -1,
                "Broadband spectrum must not become a chord");
        const auto invalid = SpectrumChroma::convert(nullptr, 0, fftSize, sampleRate);
        require(matcher.match(invalid, -18.0f, 100.0f, true).chord == -1,
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
