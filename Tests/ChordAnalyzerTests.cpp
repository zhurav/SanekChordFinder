#include "ChordAnalyzer.h"

#include <iostream>
#include <stdexcept>

namespace
{
constexpr double sampleRate = 48000.0;
constexpr int blockSize = 256;

void require(bool okay, const char* message)
{
    if (!okay)
        throw std::runtime_error(message);
}

double midiFrequency(int midi)
{
    return 440.0 * std::pow(2.0, (midi - 69) / 12.0);
}

int analyseChord(int chord, int inversion)
{
    ChordAnalyzer analyzer;
    analyzer.prepare(sampleRate);
    juce::AudioBuffer<float> buffer(2, blockSize);
    int detected = -1;
    const int rootMidi = 48 + chord % 12;
    std::array<int, 3> notes { rootMidi,
                               rootMidi + (chord < 12 ? 4 : 3),
                               rootMidi + 7 };
    for (int i = 0; i < inversion; ++i)
        notes[static_cast<size_t>(i)] += 12;

    const int totalSamples = static_cast<int>(sampleRate * 1.0);
    for (int start = 0; start < totalSamples; start += blockSize)
    {
        buffer.clear();
        for (int sample = 0; sample < blockSize; ++sample)
        {
            const double time = static_cast<double>(start + sample) / sampleRate;
            float value = 0.0f;
            for (size_t tone = 0; tone < notes.size(); ++tone)
            {
                const double fundamental = midiFrequency(notes[tone]);
                for (int harmonic = 1; harmonic <= 5; ++harmonic)
                    value += 0.09f * (1.0f - 0.12f * static_cast<float>(tone))
                           * std::sin(2.0 * juce::MathConstants<double>::pi
                                      * fundamental * harmonic * time)
                           / static_cast<float>(harmonic);
            }
            buffer.setSample(0, sample, value);
            buffer.setSample(1, sample, value);
        }
        AnalysisTiming timing;
        timing.seconds = static_cast<double>(start) / sampleRate;
        analyzer.process(buffer, 65.0f, timing,
                         [&detected](const ChordFrame& frame) { detected = frame.chord; });
    }
    return detected;
}
}

int main()
{
    try
    {
        for (int chord = 0; chord < ChordMatcher::chordCount; ++chord)
            for (int inversion = 0; inversion < 3; ++inversion)
            {
                const int result = analyseChord(chord, inversion);
                if (result != chord)
                    std::cerr << "Expected " << ChordMatcher::name(chord) << " inversion "
                              << inversion << ", got " << ChordMatcher::name(result) << '\n';
                require(result == chord, "Time-domain analyzer failed a chord or inversion");
            }
        std::cout << "PASS: real FFT path for 24 chords and three inversions\n";

        ChordAnalyzer analyzer;
        analyzer.prepare(sampleRate);
        juce::AudioBuffer<float> silence(2, blockSize);
        silence.clear();
        int detected = -1;
        for (int block = 0; block < 80; ++block)
            analyzer.process(silence, 100.0f, {},
                             [&detected](const ChordFrame& frame) { detected = frame.chord; });
        require(detected == -1, "Silence must not become a chord in the real FFT path");
        std::cout << "PASS: real FFT silence rejection\n";
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
