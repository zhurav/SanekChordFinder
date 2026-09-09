#pragma once

#include "ChordTrack.h"
#include <juce_audio_basics/juce_audio_basics.h>

namespace ChordMidi
{
constexpr int ticksPerQuarter = 960;

inline bool write(const ChordTrack& track, juce::OutputStream& output)
{
    if (!track.error().empty()) return false;
    juce::MidiMessageSequence sequence;
    sequence.addEvent(juce::MidiMessage::textMetaEvent(3, "Sanek Chord Track"));
    sequence.addEvent(juce::MidiMessage::tempoMetaEvent(static_cast<int>(std::lround(
        60000000.0 / (track.bpm * track.quarterNotesPerBeat())))));
    sequence.addEvent(juce::MidiMessage::timeSignatureMetaEvent(track.meter, track.meter == 6 ? 8 : 4));
    const double ticksPerBeat = ticksPerQuarter * track.quarterNotesPerBeat();
    for (size_t row = 0; row < track.rows.size(); ++row)
    {
        const auto& chord = track.rows[row];
        sequence.addEvent(juce::MidiMessage::textMetaEvent(1,
            juce::String(ChordMatcher::name(chord.chord).data())), chord.beat * ticksPerBeat);
        for (const auto note : ChordTrack::notes(chord.chord))
        {
            sequence.addEvent(juce::MidiMessage::noteOn(1, note, static_cast<juce::uint8>(96)),
                              chord.beat * ticksPerBeat);
            sequence.addEvent(juce::MidiMessage::noteOff(1, note), track.endBeat(row) * ticksPerBeat);
        }
    }
    sequence.updateMatchedPairs();
    sequence.addEvent(juce::MidiMessage::endOfTrack(), track.endBeat(track.rows.size() - 1) * ticksPerBeat);
    juce::MidiFile midi;
    midi.setTicksPerQuarterNote(ticksPerQuarter);
    midi.addTrack(sequence);
    return midi.writeTo(output, 0);
}

inline juce::Result save(const ChordTrack& track, const juce::File& target)
{
    const auto error = track.error();
    if (!error.empty()) return juce::Result::fail(juce::String(error));
    juce::TemporaryFile temporary(target);
    {
        juce::FileOutputStream stream(temporary.getFile());
        if (!stream.openedOk()) return stream.getStatus();
        if (!write(track, stream)) return juce::Result::fail("Could not write MIDI data.");
        stream.flush();
        if (stream.getStatus().failed()) return stream.getStatus();
    }
    return temporary.overwriteTargetFileWithTemporary() ? juce::Result::ok()
        : juce::Result::fail("Could not replace the destination file.");
}
}
