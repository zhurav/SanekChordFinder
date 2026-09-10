#include "ChordTrackEditor.h"
#include "ChordMidi.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <iostream>

namespace
{
struct Host : juce::AudioPlayHead
{
    double ppq = 128.0, length = 16.0, bpm = 117.0;
    bool playing = false;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info;
        info.setPpqPosition(ppq);
        info.setBpm(bpm);
        info.setIsPlaying(playing);
        info.setIsLooping(true);
        info.setLoopPoints(LoopPoints { 128.0, 128.0 + length });
        return info;
    }
};

void load(SanekChordFinderAudioProcessor& processor)
{
    ChordTrackEditor editor(processor);
    for (auto* component : editor.getChildren())
        if (auto* button = dynamic_cast<juce::TextButton*>(component))
            if (button->getButtonText() == "LOAD HISTORY") button->onClick();
}
}

int main(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader;
    if (argc > 1) reader.reset(formats.createReaderFor(juce::File(argv[1])));
    if (argc > 1 && !reader) return 2;
    int failures = 0;
    auto expect = [&](bool condition, const char* message)
    {
        if (!condition) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
    };
    {
        HostLoopCapture capture;
        capture.begin(true, 128.0, 128.0, 144.0, 8.0);
        capture.add(16, 0.0, 47);
        capture.finishBlock();
        expect(capture.snapshot().rows.empty(), "half of a constant chord loop is incomplete");
        capture.begin(true, 136.0, 128.0, 144.0, 8.0);
        capture.finishBlock();
        expect(capture.snapshot().rows.size() == 1 && capture.snapshot().length == 16,
               "constant chord loop retains its complete duration without chord changes");
        expect(capture.snapshot().rows[0].bassNote == 47, "completed loop snapshot retains bass register");
        capture.begin(true, 128.0, 128.0, 160.0, 1.0);
        expect(capture.snapshot().rows.empty(), "moving loop locators invalidates old capture");
        capture.reset();
        expect(!capture.snapshot().available && capture.snapshot().rows.empty(), "reset clears capture metadata");
    }
    for (const int bars : { 4, 8 })
    {
        Host host;
        host.length = bars * 4.0;
        SanekChordFinderAudioProcessor processor;
        processor.setPlayHead(&host);
        const double rate = reader ? reader->sampleRate : 48000.0;
        constexpr int blockSize = 512;
        processor.prepareToPlay(rate, blockSize);
        processor.parameters.getParameter("listening")->setValueNotifyingHost(1.0f);
        processor.parameters.getParameter("sensitivity")->setValueNotifyingHost(0.75f);
        processor.parameters.getParameter("chordSet")->setValueNotifyingHost(1.0f);
        auto settings = processor.getChordTrack();
        settings.duration = 2;
        processor.setChordTrack(settings);
        juce::AudioBuffer<float> buffer(2, blockSize);
        juce::MidiBuffer midi;
        const auto loopSamples = static_cast<juce::int64>(std::llround(host.length * 60.0 / 117.0 * rate));
        auto feed = [&](juce::int64 first, juce::int64 samples, bool playing, bool monitoring = false)
        {
            host.playing = playing;
            for (juce::int64 processed = 0; processed < samples; processed += blockSize)
            {
                const int size = static_cast<int>(std::min<juce::int64>(blockSize, samples - processed));
                buffer.setSize(2, size, false, false, true);
                buffer.clear();
                const auto offset = (first + processed) % loopSamples;
                host.ppq = 128.0 + offset / rate * 117.0 / 60.0;
                if (playing || monitoring)
                {
                    if (reader)
                    {
                        // The supplied WAV has two repeats. For four-bar tests take
                        // its first cycle; pad its short final tail for eight bars.
                        const auto available = std::max<juce::int64>(0, reader->lengthInSamples - offset);
                        const int readSize = static_cast<int>(std::min<juce::int64>(size, available));
                        if (readSize > 0) reader->read(&buffer, 0, readSize, offset, true, true);
                    }
                    else
                        for (int i = 0; i < size; ++i)
                        {
                            const double seconds = (offset + i) / rate;
                            const double beat = seconds * 117.0 / 60.0;
                            const int bar = static_cast<int>(beat / 4.0) % 4;
                            const int chord = bar == 0 ? 21 : (bar == 1 ? 0 : 16);
                            double value = 0.0;
                            for (const int note : ChordTrack::notes(chord))
                            {
                                const double hz = 440.0 * std::pow(2.0, (note - 69) / 12.0);
                                value += std::sin(juce::MathConstants<double>::twoPi * hz * seconds);
                            }
                            const float sample = static_cast<float>(value * 0.15
                                * (0.3 + 0.7 * std::exp(-std::fmod(beat, 1.0) * 8.0)));
                            buffer.setSample(0, i, sample);
                            buffer.setSample(1, i, sample);
                        }
                }
                processor.processBlock(buffer, midi);
            }
        };
        feed(0, static_cast<juce::int64>(rate * 3), false);
        expect(processor.getLoopSnapshot().rows.empty(), "waiting while stopped cannot complete a loop");
        // Start in the middle: this partial pass must not enter the export table.
        feed(loopSamples / 2, loopSamples / 2, true);
        load(processor);
        expect(processor.getChordTrack().rows.empty(), "partial first pass is not exported as a complete loop");
        for (int pass = 0; pass < 2; ++pass)
        {
            feed(0, loopSamples, true);
            load(processor);
            const auto track = processor.getChordTrack();
            if (reader)
                expect(std::abs(track.bpm - 117.0) < 10.0,
                       "guitar fixture must not regress to an unrelated slow rhythmic period");
            std::cout << (reader ? "WAV" : "Synthetic") << " host bars " << bars << " pass " << pass
                      << " export beats " << track.recordedEndBeat << " audio BPM " << track.bpm << '\n';
            for (const auto& row : track.rows)
                std::cout << ChordTrack::name(row.chord, row.bassNote) << " " << row.beat << " bass " << row.bassNote << '\n';
            expect(track.rows.size() == 4, "one harmonic cycle has exactly four rows");
            expect(track.recordedEndBeat == 16.0, "one harmonic cycle lasts exactly sixteen beats");
            if (track.rows.size() == 4)
            {
                const int expected[] = {21, 0, 16, 16};
                for (size_t i = 0; i < 4; ++i)
                {
                    expect(track.rows[i].chord == expected[i], "Am C Em Em pitches");
                    expect(track.rows[i].beat == i * 4.0 && track.endBeat(i) == (i + 1) * 4.0,
                           "every chord occupies its actual bar, including the second Em");
                }
            }
            if (argc > 2 && pass == 1 && bars == 4)
                expect(ChordMidi::save(track, juce::File(argv[2])).wasOk(), "save real-audio regression MIDI");
        }
        const auto before = processor.getLoopSnapshot();
        feed(0, static_cast<juce::int64>(rate * 5), false);
        load(processor);
        expect(processor.getChordTrack().recordedEndBeat == 16.0, "stopped listening cannot lengthen exported loop");
        expect(processor.getLoopSnapshot().rows.size() == before.rows.size(), "stop preserves complete capture");
        feed(0, static_cast<juce::int64>(rate * 5), false, true);
        expect(processor.getCurrentChord() >= 0, "live monitoring resumes recognition after transport stops");
        const auto afterMonitoring = processor.getLoopSnapshot();
        bool sameCapture = afterMonitoring.length == before.length && afterMonitoring.rows.size() == before.rows.size();
        for (size_t i = 0; sameCapture && i < before.rows.size(); ++i)
            sameCapture = afterMonitoring.rows[i].chord == before.rows[i].chord
                       && afterMonitoring.rows[i].beat == before.rows[i].beat;
        expect(sameCapture, "live monitoring cannot overwrite a completed MIDI loop");
        // A seek inside the loop must not overwrite the previous complete pass.
        feed(loopSamples / 3, loopSamples / 4, true);
        expect(processor.getLoopSnapshot().rows.size() == before.rows.size(), "seek preserves complete capture");
        processor.setPlayHead(nullptr);
    }
    return failures == 0 ? 0 : 1;
}
