#include "ChordAnalyzer.h"
#include "ChordTrack.h"
#include "EightBarFixture.h"

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
    auto analyzerStorage = std::make_unique<ChordAnalyzer>();
    auto& analyzer = *analyzerStorage;
    analyzer.prepare(sampleRate);
    analyzer.setExtendedChords(true);
    juce::AudioBuffer<float> buffer(2, blockSize);
    int detected = -1;
    const int tones = ChordMatcher::toneCount(chord);
    const int rootMidi = (tones >= 4 ? 60 : 48) + ChordMatcher::rootOf(chord);
    std::array<int, ChordMatcher::maximumTones> notes {};
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

void testSwitching()
{
    auto analyzerStorage = std::make_unique<ChordAnalyzer>();
    auto& analyzer = *analyzerStorage;
    analyzer.prepare(sampleRate);
    analyzer.setExtendedChords(true);
    const std::array<int, 9> sequence {51, 32, 37, 51, 32, 37, 51, 32, 37};
    std::array<bool, 9> heard {};
    juce::AudioBuffer<float> buffer(2, blockSize);
    const int segmentSamples = static_cast<int>(0.5 * sampleRate);
    const int total = segmentSamples * static_cast<int>(sequence.size());
    double worstLatency = 0.0;
    bool wrongChord = false;
    for (int start = 0; start < total; start += blockSize)
    {
        buffer.setSize(2, std::min(blockSize, total - start), false, false, true);
        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
        {
            const int absolute = start + sample;
            const int chord = sequence[static_cast<size_t>(absolute / segmentSamples)];
            const double time = absolute / sampleRate;
            const double attack = std::min(1.0, (absolute % segmentSamples) / (0.008 * sampleRate));
            float value = 0.0f;
            for (int tone = 0; tone < ChordMatcher::toneCount(chord); ++tone)
                for (int harmonic = 1; harmonic <= 5; ++harmonic)
                    value += static_cast<float>(attack * 0.075 / harmonic
                        * std::sin(juce::MathConstants<double>::twoPi * harmonic * time
                            * midiFrequency(60 + ChordMatcher::rootOf(chord) + ChordMatcher::intervalAt(chord, tone))));
            buffer.setSample(0, sample, value);
            buffer.setSample(1, sample, value);
        }
        AnalysisTiming timing;
        timing.seconds = start / sampleRate;
        analyzer.process(buffer, 65.0f, timing, [&](const ChordFrame& frame)
        {
            if (!frame.changed || frame.chord < 0) return;
            const auto index = static_cast<size_t>(start / segmentSamples);
            const double latency = (start + buffer.getNumSamples()) / sampleRate - index * 0.5;
            std::cout << "SWITCH " << ChordMatcher::name(frame.chord) << " confidence " << frame.confidence
                      << " latency " << latency << '\n';
            wrongChord |= frame.chord != sequence[index];
            heard[index] = true;
            worstLatency = std::max(worstLatency, latency);
        });
    }
    require(!wrongChord, "Quarter-note transition produced wrong chord");
    for (bool accepted : heard) require(accepted, "Quarter-note extended chord was skipped");
    require(worstLatency < 0.5, "Extended confirmation arrived after the chord ended");
    std::cout << "PASS: D#m7 G#7 C#maj7 quarter notes, worst latency " << worstLatency << " seconds\n";
}

void testShortTransient()
{
    auto analyzerStorage = std::make_unique<ChordAnalyzer>();
    auto& analyzer = *analyzerStorage;
    analyzer.prepare(sampleRate);
    analyzer.setExtendedChords(true);
    juce::AudioBuffer<float> buffer(2, blockSize);
    bool heardC = false;
    bool wrongChord = false;
    for (int start = 0; start < static_cast<int>(sampleRate * 3); start += blockSize)
    {
        for (int i = 0; i < blockSize; ++i)
        {
            const double time = (start + i) / sampleRate;
            const int chord = time >= 1.5 && time < 1.54 ? 51 : 0;
            float value = 0.0f;
            for (int tone = 0; tone < ChordMatcher::toneCount(chord); ++tone)
                value += static_cast<float>(0.15 * std::sin(juce::MathConstants<double>::twoPi * time
                    * midiFrequency(60 + ChordMatcher::rootOf(chord) + ChordMatcher::intervalAt(chord, tone))));
            buffer.setSample(0, i, value);
            buffer.setSample(1, i, value);
        }
        AnalysisTiming timing;
        timing.seconds = start / sampleRate;
        analyzer.process(buffer, 65.0f, timing, [&](const ChordFrame& frame)
        {
            if (frame.changed && frame.chord >= 0)
            {
                wrongChord |= frame.chord != 0;
                heardC = true;
            }
        });
    }
    require(heardC, "Transient test must detect the sustained chord");
    require(!wrongChord, "40 ms intrusion must not replace the sustained chord");
    std::cout << "PASS: 40 ms transient does not cause false chord switching\n";
}

void testStereoBassAndTuning()
{
    const std::array<std::pair<int, int>, 5> cases {{{0, 40}, {0, 43}, {21, 40}, {2, 42}, {7, 47}}};
    for (const auto& item : cases)
    {
        auto analyzerStorage = std::make_unique<ChordAnalyzer>();
        auto& analyzer = *analyzerStorage;
        analyzer.prepare(sampleRate);
        juce::AudioBuffer<float> buffer(2, blockSize);
        ChordFrame last;
        const auto notes = ChordTrack::notes(item.first, item.second);
        for (int start = 0; start < sampleRate * 4.0; start += blockSize)
        {
            for (int i = 0; i < blockSize; ++i)
            {
                const double time = (start + i) / sampleRate;
                float value = 0.0f;
                for (const auto note : notes)
                    for (int harmonic = 1; harmonic <= 4; ++harmonic)
                        value += static_cast<float>(0.08 / harmonic * std::sin(juce::MathConstants<double>::twoPi
                            * time * harmonic * midiFrequency(note)));
                buffer.setSample(0, i, value);
                buffer.setSample(1, i, -value); // Complete waveform cancellation, same spectrum.
            }
            analyzer.process(buffer, 65.0f, {start / sampleRate}, [&](const ChordFrame& frame) { last = frame; });
        }
        std::cout << "BASS expected " << ChordTrack::name(item.first, item.second) << ' ' << item.second
                  << " got " << ChordTrack::name(last.chord, last.bassNote) << ' ' << last.bassNote << '\n';
        require(last.chord == item.first, "Opposite-polarity stereo lost chord identity");
        require(last.bassNote == item.second, "Lowest bass note/inversion was not detected");
    }
    // Split chord tones across channels: neither channel contains the full chord.
    auto splitStorage = std::make_unique<ChordAnalyzer>();
    auto& split = *splitStorage;
    split.prepare(sampleRate);
    juce::AudioBuffer<float> buffer(2, blockSize);
    ChordFrame last;
    for (int start = 0; start < sampleRate * 2.0; start += blockSize)
    {
        for (int i = 0; i < blockSize; ++i)
        {
            const double time = (start + i) / sampleRate;
            buffer.setSample(0, i, static_cast<float>(0.2 * std::sin(juce::MathConstants<double>::twoPi * time * midiFrequency(48))));
            buffer.setSample(1, i, static_cast<float>(0.2 * (std::sin(juce::MathConstants<double>::twoPi * time * midiFrequency(52))
                + std::sin(juce::MathConstants<double>::twoPi * time * midiFrequency(55)))));
        }
        split.process(buffer, 65.0f, {start / sampleRate}, [&](const ChordFrame& frame) { last = frame; });
    }
    require(last.chord == 0, "Channel spectra were not combined before matching");
    for (double cents : {-25.0, -5.0, 18.0})
    {
        auto analyzerStorage = std::make_unique<ChordAnalyzer>();
        auto& analyzer = *analyzerStorage;
        analyzer.prepare(sampleRate);
        for (int start = 0; start < sampleRate * 5.0; start += blockSize)
        {
            for (int i = 0; i < blockSize; ++i)
            {
                const double time = (start + i) / sampleRate;
                float value = 0.0f;
                for (int note : {57, 60, 64})
                    value += static_cast<float>(0.15 * std::sin(juce::MathConstants<double>::twoPi
                        * time * midiFrequency(note) * std::exp2(cents / 1200.0)));
                buffer.setSample(0, i, value); buffer.setSample(1, i, value);
            }
            analyzer.process(buffer, 65.0f, {start / sampleRate}, [&](const ChordFrame& frame) { last = frame; });
        }
        std::cout << "TUNING expected " << cents << " got " << analyzer.getTuningCents() << '\n';
        require(analyzer.isTuningReady() && std::abs(analyzer.getTuningCents() - cents) < 2.5,
                "Common tuning offset was not estimated accurately");
        require(last.chord == 21, "Detuned A minor was not compensated");
        analyzer.calibrateTuning();
        require(!analyzer.isTuningReady() && analyzer.getTuningCents() == 0.0, "Calibration must clear stale tuning evidence");
    }
    HarmonicMemory memory;
    memory.remember(0, 0, 0.0);
    require(memory.strength(0, 0.0) == 1.0 && std::abs(memory.strength(0, 10.0) - 0.7) < 0.02
            && std::abs(memory.strength(0, 30.0) - 0.3) < 0.04 && memory.recall(0, 60.0) == -1,
            "Harmonic memory must decay and expire");
    memory.remember(0, 12, 61.0);
    require(memory.recall(0, 61.0) == 12, "New minor evidence must replace old major memory");

    auto changingBassStorage = std::make_unique<ChordAnalyzer>();
    auto& changingBass = *changingBassStorage;
    changingBass.prepare(sampleRate);
    std::array<bool, 3> inversionsHeard {};
    const std::array<int, 3> basses {40,43,40};
    for (int start = 0; start < sampleRate * 6.0; start += blockSize)
    {
        for (int i = 0; i < blockSize; ++i)
        {
            const double time = (start + i) / sampleRate;
            const int segment = std::min(2, static_cast<int>(time / 2.0));
            float value = 0.0f;
            for (int note : ChordTrack::notes(0, basses[static_cast<size_t>(segment)]))
                value += static_cast<float>(0.15 * std::sin(juce::MathConstants<double>::twoPi * time * midiFrequency(note)));
            buffer.setSample(0, i, value); buffer.setSample(1, i, value);
        }
        changingBass.process(buffer, 65.0f, {start / sampleRate}, [&](const ChordFrame& frame)
        {
            const auto segment = static_cast<size_t>(std::min(2, static_cast<int>(start / sampleRate / 2.0)));
            if (frame.changed && frame.chord == 0 && frame.bassNote == basses[segment]) inversionsHeard[segment] = true;
        });
    }
    for (bool heard : inversionsHeard) require(heard, "Changing only the bass must emit a new inversion without requiring a loud attack");
}
}

int main()
{
    try
    {
        {
            auto analyzerStorage = std::make_unique<ChordAnalyzer>();
            auto& analyzer = *analyzerStorage;
            analyzer.prepare(sampleRate);
            analyzer.setExtendedChords(true);
            juce::AudioBuffer<float> buffer(2, blockSize);
            std::vector<int> events;
            for (int start = 0; start < sampleRate * 16; start += blockSize)
            {
                for (int i = 0; i < blockSize; ++i)
                {
                    const float value = EightBarFixture::sample((start + i) / sampleRate);
                    buffer.setSample(0,i,value); buffer.setSample(1,i,value);
                }
                analyzer.process(buffer, 80.0f, {start / sampleRate}, [&](const ChordFrame& frame)
                {
                    if (frame.changed && frame.chord >= 0 && (events.empty() || events.back() != frame.chord))
                    {
                        events.push_back(frame.chord);
                        std::cout << "EIGHT BAR " << frame.timing.seconds << ' ' << ChordMatcher::name(frame.chord) << '\n';
                    }
                });
            }
            require(events == std::vector<int>(EightBarFixture::chords.begin(), EightBarFixture::chords.end()),
                    "Eight bars must not acquire C/Cm events as sevenths decay");
        }
        int checked = 0;
        {
            auto owner = std::make_unique<ChordAnalyzer>();
            owner->prepare(sampleRate); owner->setExtendedChords(true);
            juce::AudioBuffer<float> buffer(2,blockSize);
            std::vector<int> events;
            for (int start = 0; start < sampleRate * 6; start += blockSize)
            {
                for (int i = 0; i < blockSize; ++i)
                {
                    const double time = (start + i) / sampleRate;
                    const float value = EightBarFixture::sample(time,120.0,time < 4.0 ? 1 : 0);
                    buffer.setSample(0,i,value); buffer.setSample(1,i,value);
                }
                AnalysisTiming timing { start / sampleRate, start / sampleRate * 2.0,120.0 };
                timing.hostBarNumber = 1;
                owner->process(buffer,80.0f,timing,[&](const ChordFrame& frame)
                { if (frame.changed && frame.chord >= 0 && (events.empty() || events.back()!=frame.chord)) events.push_back(frame.chord); });
            }
            if (events != std::vector<int>({36,0}))
                for (int chord : events) std::cout << "REPEATED " << ChordMatcher::name(chord) << '\n';
            require(events == std::vector<int>({36,0}), "Repeated Cmaj7 must survive decay but a new C attack must replace it");
        }
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
                const bool accepted = result == chord
                    || samePitchSet(result, chord);
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
                  << " chord voicings across " << ChordMatcher::chordCount << " chord names\n";
        testSwitching();
        testShortTransient();
        testStereoBassAndTuning();

        auto analyzerStorage = std::make_unique<ChordAnalyzer>();
        auto& analyzer = *analyzerStorage;
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
