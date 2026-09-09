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

int analyseChord(int chord, int inversion, ChordFrame* finalFrame = nullptr)
{
    ChordAnalyzer analyzer;
    analyzer.prepare(sampleRate);
    analyzer.setExtendedChords(true);
    juce::AudioBuffer<float> buffer(2, blockSize);
    int detected = -1;
    const int tones = ChordMatcher::toneCount(chord);
    const int rootMidi = (tones == 4 ? 60 : 48) + ChordMatcher::rootOf(chord);
    std::array<int, 4> notes {};
    for (int tone = 0; tone < tones; ++tone)
        notes[static_cast<size_t>(tone)] = rootMidi + ChordMatcher::intervalAt(chord, tone);
    for (int i = 0; i < inversion && i < tones; ++i)
        notes[static_cast<size_t>(i)] += 12;

    const int totalSamples = static_cast<int>(sampleRate * 1.5);
    for (int start = 0; start < totalSamples; start += blockSize)
    {
        buffer.clear();
        for (int sample = 0; sample < blockSize; ++sample)
        {
            const double time = static_cast<double>(start + sample) / sampleRate;
            float value = 0.0f;
            for (int tone = 0; tone < tones; ++tone)
            {
                const double fundamental = midiFrequency(notes[static_cast<size_t>(tone)]);
                for (int harmonic = 1; harmonic <= 5; ++harmonic)
                    value += 0.075f
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
                         [&detected, finalFrame](const ChordFrame& frame)
                         {
                             detected = frame.chord;
                             if (finalFrame != nullptr)
                                 *finalFrame = frame;
                         });
    }
    return detected;
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
}

int main()
{
    try
    {
        int checked = 0;
        for (int chord = 0; chord < ChordMatcher::chordCount; ++chord)
        {
            const int quality = ChordMatcher::qualityOf(chord);
            const int inversions = quality == ChordMatcher::major
                                || quality == ChordMatcher::minor
                                 ? ChordMatcher::toneCount(chord) : 1;
            for (int inversion = 0; inversion < inversions; ++inversion)
            {
                ChordFrame finalFrame;
                const int result = analyseChord(chord, inversion, &finalFrame);
                const bool rootAmbiguousQuality = quality == ChordMatcher::sus2
                                               || quality == ChordMatcher::sus4
                                               || quality == ChordMatcher::diminished;
                const bool accepted = result == chord
                    || (rootAmbiguousQuality && samePitchSet(result, chord));
                if (!accepted)
                {
                    std::cerr << "Expected " << ChordMatcher::name(chord) << " inversion "
                              << inversion << ", got " << ChordMatcher::name(result)
                              << ", alternative " << ChordMatcher::name(finalFrame.alternative)
                              << ", confidence " << finalFrame.confidence << "\nChroma:";
                    for (const float value : finalFrame.chroma)
                        std::cerr << ' ' << value;
                    std::cerr << '\n';
                }
                require(accepted, "Time-domain analyzer failed a chord or inversion");
                ++checked;
            }
        }
        std::cout << "PASS: real FFT path for " << checked
                  << " chord voicings across 96 chord names\n";

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
