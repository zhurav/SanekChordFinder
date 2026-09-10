#include "PluginEditor.h"
#include "ChordMidi.h"
#include "EightBarFixture.h"
#include <iostream>

namespace
{
int testEightBars()
{
    struct Host : juce::AudioPlayHead
    {
        double ppq = 0.0;
        bool playing = true, looping = false;
        juce::Optional<PositionInfo> getPosition() const override
        {
            PositionInfo p;
            p.setBpm(120.0); p.setPpqPosition(ppq); p.setIsPlaying(playing);
            p.setTimeSignature(TimeSignature {4,4});
            p.setPpqPositionOfLastBarStart(std::floor(ppq / 4.0) * 4.0);
            p.setBarCount(static_cast<juce::int64>(std::floor(ppq / 4.0)));
            p.setIsLooping(looping);
            if (looping) p.setLoopPoints(LoopPoints {0.0,32.0});
            return p;
        }
    } host;
    int failures = 0;
    const auto expect = [&](bool condition, const char* message)
        { if (!condition) { ++failures; std::cerr << "EIGHT BAR FAIL: " << message << '\n'; } };
    for (bool looping : {false,true})
    {
        host.ppq = 0; host.playing = true; host.looping = looping;
        auto owner = std::make_unique<SanekChordFinderAudioProcessor>();
        auto& processor = *owner;
        processor.setPlayHead(&host);
        processor.prepareToPlay(48000.0, 256);
        processor.parameters.getParameter("listening")->setValueNotifyingHost(1.0f);
        processor.parameters.getParameter("chordSet")->setValueNotifyingHost(1.0f);
        processor.parameters.getParameter("sensitivity")->setValueNotifyingHost(0.80f);
        juce::AudioBuffer<float> buffer(2,256);
        juce::MidiBuffer midi;
        for (int start = 0; start < 48000 * 16; start += 256)
        {
            host.ppq = start / 48000.0 * 2.0;
            for (int i = 0; i < 256; ++i)
            {
                const float value = EightBarFixture::sample((start + i) / 48000.0);
                buffer.setSample(0,i,value); buffer.setSample(1,i,value);
            }
            processor.processBlock(buffer,midi);
        }
        host.playing = false;
        buffer.clear();
        for (int i = 0; i < 200; ++i) processor.processBlock(buffer,midi);
        const auto events = processor.getHistorySnapshot();
        std::cout << "EIGHT BAR " << (looping ? "LOOP" : "LINEAR") << " auto BPM " << processor.getExportBpm() << '\n';
        expect(events.size() == 8, "history must contain exactly eight chords");
        for (size_t i = 0; i < events.size(); ++i)
        {
            std::cout << "BAR " << events[i].bar << '.' << events[i].beat << ' ' << ChordMatcher::name(events[i].chord) << '\n';
            if (i < 8) expect(events[i].chord == EightBarFixture::chords[i]
                && events[i].bar == static_cast<int>(i) + 1 && events[i].beat == 1.0f,
                "exact chord and host bar, even without a detected beat pulse");
        }
        {
            SanekChordFinderAudioProcessorEditor editor(processor);
            bool shown = false;
            for (auto* component : editor.getChildren())
            {
                if (auto* history = dynamic_cast<juce::TextEditor*>(component))
                    shown = history->getText().contains("BAR 2  Cmaj7") && history->getText().contains("BAR 8  Cdim");
                if (auto* track = dynamic_cast<ChordTrackEditor*>(component))
                    for (auto* child : track->getChildren())
                        if (auto* button = dynamic_cast<juce::TextButton*>(child))
                            if (button->getButtonText() == "LOAD HISTORY") button->onClick();
            }
            expect(shown, "history UI must show BAR labels");
            if (!looping)
            {
                juce::Image image(juce::Image::ARGB,editor.getWidth(),editor.getHeight(),true,juce::SoftwareImageType());
                { juce::Graphics g(image); editor.paintEntireComponent(g,true); }
                juce::FileOutputStream output(juce::File::getCurrentWorkingDirectory().getChildFile("eight-bars-preview.png"));
                output.setPosition(0); output.truncate();
                juce::PNGImageFormat png;
                expect(png.writeImageToStream(image,output), "render eight-bar result");
            }
        }
        const auto track = processor.getChordTrack();
        expect(track.rows.size() == 8 && track.recordedEndBeat == 32.0, "MIDI has exactly eight bars");
        for (size_t i = 0; i < track.rows.size() && i < 8; ++i)
            expect(track.rows[i].chord == EightBarFixture::chords[i] && track.rows[i].beat == i * 4.0
                && track.endBeat(i) == (i + 1) * 4.0, "MIDI chord starts/ends match source bars");
        if (!looping) expect(ChordMidi::save(track, juce::File::getCurrentWorkingDirectory().getChildFile("eight-bars-v081.mid")).wasOk(), "save eight-bar MIDI");
        processor.setPlayHead(nullptr);
    }
    return failures;
}
struct TestHost : juce::AudioPlayHead
{
    double bpm = 200.0;
    bool stationaryLoop = false;
    mutable int reads = 0;
    juce::Optional<PositionInfo> getPosition() const override
    {
        ++reads;
        PositionInfo position;
        position.setBpm(bpm);
        if (stationaryLoop)
        {
            position.setIsPlaying(false);
            position.setIsLooping(true);
            position.setPpqPosition(128.0);
            position.setLoopPoints(LoopPoints { 128.0, 144.0 });
        }
        return position;
    }
};

void feedRhythm(SanekChordFinderAudioProcessor& processor, double bpm, double seconds, bool oppositePolarity = false)
{
    constexpr double rate = 48000.0;
    constexpr int blockSize = 256;
    juce::AudioBuffer<float> buffer(2, blockSize);
    juce::MidiBuffer midi;
    const auto total = static_cast<int>(seconds * rate);
    for (int start = 0; start < total; start += blockSize)
    {
        buffer.setSize(2, std::min(blockSize, total - start), false, false, true);
        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            const double time = (start + i) / rate;
            const double phase = std::fmod(time, 60.0 / bpm);
            const double chord = std::sin(juce::MathConstants<double>::twoPi * 130.8128 * time)
                               + std::sin(juce::MathConstants<double>::twoPi * 164.8138 * time)
                               + std::sin(juce::MathConstants<double>::twoPi * 195.9977 * time);
            const float value = static_cast<float>(0.2 * std::exp(-phase * 12.0) * chord);
            buffer.setSample(0, i, value);
            buffer.setSample(1, i, oppositePolarity ? -value : value);
        }
        processor.processBlock(buffer, midi);
    }
}
}

int main()
{
    juce::ScopedJuceInitialiser_GUI gui;
    int failures = testEightBars();
    auto expect = [&failures](bool condition, const char* message)
    {
        if (!condition) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
    };
    {
        TestHost host;
        SanekChordFinderAudioProcessor listeningProcessor;
        listeningProcessor.setPlayHead(&host);
        listeningProcessor.prepareToPlay(48000.0, 256);
        auto* listening = listeningProcessor.parameters.getParameter("listening");
        listening->setValueNotifyingHost(1.0f);
        feedRhythm(listeningProcessor, 90.0, 8.0);
        expect(std::abs(listeningProcessor.getLiveBpm() - 90.0f) < 1.5f, "90 BPM audio detected while host is 200 BPM");
        expect(host.reads > 0, "host position can be queried without using its BPM for analysis");
        expect(!listeningProcessor.getHistorySnapshot().empty(), "rhythmic chord enters history");
        const auto capturedBpm = listeningProcessor.getRecordedBpm();
        {
            ChordTrackEditor trackEditor(listeningProcessor);
            for (auto* control : trackEditor.getChildren())
            {
                expect(dynamic_cast<juce::Slider*>(control) == nullptr, "no manual BPM slider in track editor");
                if (auto* button = dynamic_cast<juce::TextButton*>(control))
                    if (button->getButtonText() == "LOAD HISTORY") button->onClick();
            }
            expect(!listeningProcessor.getChordTrack().rows.empty()
                && std::abs(listeningProcessor.getChordTrack().bpm - 90.0) < 1.5,
                "MIDI track automatically uses detected audio tempo");
        }
        host.bpm = 60.0;
        feedRhythm(listeningProcessor, 150.0, 8.0, true);
        expect(std::abs(listeningProcessor.getLiveBpm() - 150.0f) < 1.5f,
               "live BPM follows new stereo rhythm even when mono cancels");
        expect(listeningProcessor.getRecordedBpm() == capturedBpm, "recorded grid stays stable");
        const float last = listeningProcessor.getLastHeardBpm();
        listening->setValueNotifyingHost(0.0f);
        juce::AudioBuffer<float> silence(2, 256);
        silence.clear();
        juce::MidiBuffer midi;
        listeningProcessor.processBlock(silence, midi);
        expect(listeningProcessor.getLastHeardBpm() == last, "stopping keeps last heard tempo");
        {
            SanekChordFinderAudioProcessorEditor editor(listeningProcessor);
            bool shown = false;
            for (auto* child : editor.getChildren())
                if (auto* label = dynamic_cast<juce::Label*>(child))
                    shown |= label->getText().contains("LAST HEARD")
                          && label->getText().contains(juce::String(last, 1));
            expect(shown, "editor displays last heard tempo after stopping");
        }
        listening->setValueNotifyingHost(1.0f);
        listeningProcessor.processBlock(silence, midi);
        expect(listeningProcessor.getLiveBpm() == 0.0f && listeningProcessor.getLastHeardBpm() == 0.0f
            && listeningProcessor.getRecordedBpm() == 0.0, "new listening clears previous estimates");
        expect(host.reads > 0, "audio analysis remains independent of host transport metadata");
        listeningProcessor.setPlayHead(nullptr);
    }
    SanekChordFinderAudioProcessor processor;
    {
        SanekChordFinderAudioProcessor bassProcessor;
        bassProcessor.prepareToPlay(48000.0, 256);
        bassProcessor.parameters.getParameter("listening")->setValueNotifyingHost(1.0f);
        juce::AudioBuffer<float> buffer(2, 256);
        juce::MidiBuffer midi;
        for (int start = 0; start < 48000 * 3; start += 256)
        {
            for (int i = 0; i < 256; ++i)
            {
                float value = 0.0f;
                for (const int note : {40,43,48})
                    for (int harmonic = 1; harmonic <= 3; ++harmonic)
                        value += static_cast<float>(0.10 / harmonic * std::sin(juce::MathConstants<double>::twoPi
                            * (start + i) / 48000.0 * harmonic * 440.0 * std::exp2((note - 69) / 12.0)));
                buffer.setSample(0, i, value); buffer.setSample(1, i, -value);
            }
            bassProcessor.processBlock(buffer, midi);
        }
        const auto heard = bassProcessor.getHistorySnapshot();
        expect(heard.size() == 1 && heard[0].chord == 0 && heard[0].bassNote == 40,
               "processor captures C/E once through opposite-polarity live input");
        expect(!heard.empty() && heard[0].durationSeconds > 2.0 && heard[0].durationSeconds < 3.1,
               "history accumulates audible chord duration");
        {
            SanekChordFinderAudioProcessorEditor editor(bassProcessor);
            bool slashShown = false, tuningShown = false;
            for (auto* child : editor.getChildren())
                if (auto* label = dynamic_cast<juce::Label*>(child))
                {
                    slashShown |= label->getText() == "C/E";
                    tuningShown |= label->getText().contains("Hz") && label->getText().contains("cents");
                }
            expect(slashShown && tuningShown, "live editor shows slash chord and estimated tuning");
        }
        buffer.clear();
        for (int block = 0; block < 200; ++block) bassProcessor.processBlock(buffer, midi);
        const auto afterSilence = bassProcessor.getHistorySnapshot();
        expect(!afterSilence.empty() && afterSilence[0].durationSeconds < 3.3,
               "silence must not increase harmonic duration");
    }
    {
        TestHost stoppedHost;
        stoppedHost.stationaryLoop = true;
        SanekChordFinderAudioProcessor liveInput;
        liveInput.setPlayHead(&stoppedHost);
        liveInput.prepareToPlay(48000.0, 256);
        liveInput.parameters.getParameter("listening")->setValueNotifyingHost(1.0f);
        // Inspect while the last strum is audible, not at the silent end of its decay.
        feedRhythm(liveInput, 90.0, 8.25);
        expect(liveInput.getCurrentChord() == 0 && liveInput.getConfidence() > 0.0f,
               "live C guitar/keyboard input is displayed while transport is stopped with loop enabled");
        const auto history = liveInput.getHistorySnapshot();
        expect(!history.empty() && history.back().chord == 0 && history.back().ppq < 0.0,
               "stopped live input enters audio history without a fabricated host beat");
        const auto chroma = liveInput.getChroma();
        expect(*std::max_element(chroma.begin(), chroma.end()) > 0.0f,
               "live pitch display updates while stopped");
        expect(std::abs(liveInput.getLiveBpm() - 90.0f) < 1.5f,
               "live tempo follows input while stopped at unrelated host BPM");
        expect(liveInput.getLoopSnapshot().available && liveInput.getLoopSnapshot().rows.empty(),
               "stationary live input cannot fabricate a completed host loop");
        liveInput.parameters.getParameter("listening")->setValueNotifyingHost(0.0f);
        feedRhythm(liveInput, 90.0, 1.0);
        expect(liveInput.getCurrentChord() < 0 && liveInput.getHistorySnapshot().size() == history.size(),
               "turning Listening off still disables live recognition");
        liveInput.setPlayHead(nullptr);
    }
    ChordTrack track;
    track.duration = 2;
    track.bpm = 122.2;
    track.rows = {{20,0}, {6,4}, {4,8}, {3,12}};
    processor.setChordTrack(track);
    {
        SanekChordFinderAudioProcessorEditor editor(processor);
        editor.setVisible(true);
        juce::TableListBox* table = nullptr;
        for (auto* child : editor.getChildren())
            if (auto* trackEditor = dynamic_cast<ChordTrackEditor*>(child))
                for (auto* control : trackEditor->getChildren())
                    if (auto* candidate = dynamic_cast<juce::TableListBox*>(control)) table = candidate;
        expect(table != nullptr, "editing table exists");
        if (table != nullptr)
        {
            expect(table->getTableListBoxModel()->getNumRows() == 4, "table loads processor track");
            std::unique_ptr<juce::Component> cell(table->getTableListBoxModel()->refreshComponentForCell(0, 4, false, nullptr));
            auto* quality = dynamic_cast<juce::ComboBox*>(cell.get());
            expect(quality != nullptr, "quality cell is editable");
            if (quality != nullptr)
            {
                quality->setSelectedId(5, juce::dontSendNotification);
                quality->onChange();
                expect(processor.getChordTrack().rows[0].chord == 56, "manual correction G#m7 stored");
            }
            std::unique_ptr<juce::Component> bassCell(table->getTableListBoxModel()->refreshComponentForCell(0, 7, false, nullptr));
            auto* bass = dynamic_cast<juce::ComboBox*>(bassCell.get());
            expect(bass != nullptr, "bass cell is editable");
            if (bass != nullptr)
            {
                bass->setSelectedId(5, juce::dontSendNotification); // D#, MIDI 51.
                bass->onChange();
                expect(processor.getChordTrack().rows[0].bassNote == 51, "manual bass edit is retained");
            }
        }
        // Render the real JUCE editor offscreen for layout inspection.
        juce::Image image(juce::Image::ARGB, editor.getWidth(), editor.getHeight(), true, juce::SoftwareImageType());
        {
            juce::Graphics graphics(image);
            editor.paintEntireComponent(graphics, true);
        }
        expect(image.getPixelAt(10, 10).getAlpha() != 0, "preview contains painted interface");
        juce::FileOutputStream output(juce::File::getCurrentWorkingDirectory().getChildFile("chord-track-preview.png"));
        output.setPosition(0);
        output.truncate();
        juce::PNGImageFormat png;
        expect(png.writeImageToStream(image, output), "editor preview rendered");
    }
    expect(processor.getChordTrack().rows[0].chord == 56, "closing editor keeps correction");
    juce::MemoryBlock state;
    processor.getStateInformation(state);
    SanekChordFinderAudioProcessor restored;
    restored.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    const auto saved = restored.getChordTrack();
    expect(saved.rows.size() == 4 && saved.rows[0].chord == 56 && saved.rows[3].beat == 12,
           "Live Set state restores edited chords and positions");
    expect(saved.bpm == 122.2 && saved.duration == 2 && saved.meter == 4, "state restores export settings");
    expect(saved.rows[0].bassNote == 51 && saved.rows[1].bassNote == -1, "state restores bass and legacy root-position rows");
    {
        ChordTrack extendedTrack;
        extendedTrack.duration = 2;
        for (int quality = ChordMatcher::augmented; quality < ChordMatcher::qualityCount; ++quality)
            extendedTrack.rows.push_back({ quality * 12, (quality - ChordMatcher::augmented) * 4.0 });
        SanekChordFinderAudioProcessor extendedProcessor;
        extendedProcessor.setChordTrack(extendedTrack);
        ChordTrackEditor editor(extendedProcessor);
        juce::TableListBox* table = nullptr;
        for (auto* control : editor.getChildren())
            if (auto* candidate = dynamic_cast<juce::TableListBox*>(control)) table = candidate;
        expect(table != nullptr, "new types appear in the editing table");
        if (table != nullptr)
        {
            std::unique_ptr<juce::Component> cell(table->getTableListBoxModel()->refreshComponentForCell(0, 4, false, nullptr));
            auto* quality = dynamic_cast<juce::ComboBox*>(cell.get());
            expect(quality != nullptr, "new chord type is editable");
            if (quality != nullptr)
                for (int type = ChordMatcher::augmented; type < ChordMatcher::qualityCount; ++type)
                {
                    quality->setSelectedId(type + 1, juce::dontSendNotification);
                    quality->onChange();
                    expect(extendedProcessor.getChordTrack().rows[0].chord == type * 12,
                           "every new type can be selected through the actual editor");
                }
        }
        extendedProcessor.setChordTrack(extendedTrack);
        juce::MemoryBlock extendedState;
        extendedProcessor.getStateInformation(extendedState);
        SanekChordFinderAudioProcessor reloaded;
        reloaded.setStateInformation(extendedState.getData(), static_cast<int>(extendedState.getSize()));
        const auto reloadedTrack = reloaded.getChordTrack();
        expect(reloadedTrack.rows.size() == extendedTrack.rows.size(), "state restores all twelve new types");
        if (reloadedTrack.rows.size() == extendedTrack.rows.size())
            for (size_t row = 0; row < extendedTrack.rows.size(); ++row)
                expect(reloadedTrack.rows[row].chord == extendedTrack.rows[row].chord,
                       "new type IDs survive saving a Live Set");
    }
    const auto target = juce::File::getCurrentWorkingDirectory().getChildFile("chord-track-example.mid");
    expect(ChordMidi::save(track, target).wasOk(), "MIDI saved to disk");
    auto stream = target.createInputStream();
    juce::MidiFile midi;
    expect(stream != nullptr && midi.readFrom(*stream), "saved file reads as MIDI");
    juce::MemoryBlock original;
    const auto before = target.loadFileAsData(original);
    ChordTrack empty;
    expect(ChordMidi::save(empty, target).failed(), "invalid export fails");
    juce::MemoryBlock after;
    expect(before && target.loadFileAsData(after) && original == after, "failed export preserves existing file");
    std::cout << (failures == 0 ? "Chord Track integration passed.\n" : "Chord Track integration failed.\n");
    return failures == 0 ? 0 : 1;
}
