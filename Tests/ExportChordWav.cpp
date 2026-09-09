#include "ChordTrackEditor.h"
#include "ChordMidi.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <iostream>

int main(int argc, char** argv)
{
    if (argc < 3 || argc > 4) { std::cerr << "Usage: ExportChordWav input.wav output.mid [--whole-bar]\n"; return 2; }
    juce::ScopedJuceInitialiser_GUI gui;
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(juce::File(argv[1])));
    if (!reader) return 2;
    SanekChordFinderAudioProcessor processor;
    if (argc == 4 && std::string_view(argv[3]) == "--whole-bar")
    {
        auto settings = processor.getChordTrack();
        settings.duration = 2;
        processor.setChordTrack(settings);
    }
    processor.prepareToPlay(reader->sampleRate, 512);
    processor.parameters.getParameter("listening")->setValueNotifyingHost(1.0f);
    processor.parameters.getParameter("chordSet")->setValueNotifyingHost(1.0f);
    juce::AudioBuffer<float> buffer(2, 512);
    juce::MidiBuffer midi;
    for (juce::int64 start = 0; start < reader->lengthInSamples; start += 512)
    {
        const int size = static_cast<int>(std::min<juce::int64>(512, reader->lengthInSamples - start));
        buffer.setSize(2, size, false, false, true);
        reader->read(&buffer, 0, size, start, true, true);
        processor.processBlock(buffer, midi);
    }
    ChordTrackEditor editor(processor);
    for (auto* control : editor.getChildren())
        if (auto* button = dynamic_cast<juce::TextButton*>(control))
            if (button->getButtonText() == "LOAD HISTORY") button->onClick();
    const auto track = processor.getChordTrack();
    const auto result = ChordMidi::save(track, juce::File(argv[2]));
    if (result.failed()) { std::cerr << result.getErrorMessage() << '\n'; return 1; }
    std::cout << "BPM " << track.bpm << "\n";
    for (size_t i = 0; i < track.rows.size(); ++i)
        std::cout << ChordMatcher::name(track.rows[i].chord) << " beat " << track.rows[i].beat + 1
                  << " length " << track.endBeat(i) - track.rows[i].beat << '\n';
    return 0;
}
