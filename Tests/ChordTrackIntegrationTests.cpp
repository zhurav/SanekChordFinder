#include "PluginEditor.h"
#include "ChordMidi.h"
#include <iostream>

namespace
{
struct TestHost : juce::AudioPlayHead
{
    double bpm = 200.0;
    mutable int reads = 0;
    juce::Optional<PositionInfo> getPosition() const override
    {
        ++reads;
        PositionInfo position;
        position.setBpm(bpm);
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
    int failures = 0;
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
        expect(host.reads == 0, "host tempo is never queried");
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
        expect(host.reads == 0, "changing host tempo does not affect audio analysis");
        listeningProcessor.setPlayHead(nullptr);
    }
    SanekChordFinderAudioProcessor processor;
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
