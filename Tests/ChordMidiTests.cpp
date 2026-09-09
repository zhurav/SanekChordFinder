#include "ChordMidi.h"
#include <iostream>
#include <limits>
#include <map>

namespace
{
int failures = 0;
void expect(bool condition, const char* message)
{
    if (!condition) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}

void roundTrip(const ChordTrack& track)
{
    juce::MemoryOutputStream output;
    expect(ChordMidi::write(track, output), "MIDI writes successfully");
    juce::MemoryInputStream input(output.getData(), output.getDataSize(), false);
    juce::MidiFile file;
    expect(file.readFrom(input), "MIDI reads back");
    expect(file.getTimeFormat() == 960 && file.getNumTracks() == 1, "960 PPQ, one track");
    if (file.getNumTracks() != 1) return;
    const auto* sequence = file.getTrack(0);
    size_t chordIndex = 0;
    int noteIndex = 0, ons = 0, offs = 0;
    bool tempoFound = false, meterFound = false;
    std::map<int, bool> active;
    for (int i = 0; i < sequence->getNumEvents(); ++i)
    {
        const auto& message = sequence->getEventPointer(i)->message;
        if (message.isTempoMetaEvent())
        {
            tempoFound = true;
            expect(std::abs(message.getTempoSecondsPerQuarterNote()
                - 60.0 / (track.bpm * track.quarterNotesPerBeat())) < 0.000002, "tempo matches meter units");
        }
        if (message.isTimeSignatureMetaEvent())
        {
            meterFound = true;
            int numerator = 0, denominator = 0;
            message.getTimeSignatureInfo(numerator, denominator);
            expect(numerator == track.meter && denominator == (track.meter == 6 ? 8 : 4), "time signature");
        }
        if (message.isNoteOff())
        {
            ++offs;
            expect(active[message.getNoteNumber()], "note-off has active note");
            active[message.getNoteNumber()] = false;
        }
        if (message.isNoteOn())
        {
            ++ons;
            expect(!active[message.getNoteNumber()], "common notes end before next chord starts");
            active[message.getNoteNumber()] = true;
            if (chordIndex >= track.rows.size()) { expect(false, "unexpected note"); continue; }
            const auto notes = ChordTrack::notes(track.rows[chordIndex].chord);
            expect(message.getNoteNumber() == notes[static_cast<size_t>(noteIndex)], "correct chord tone");
            expect(message.getTimeStamp() == track.rows[chordIndex].beat * 960 * track.quarterNotesPerBeat(), "quantized start tick");
            const auto* off = sequence->getEventPointer(i)->noteOffObject;
            expect(off != nullptr, "every note is paired");
            if (off != nullptr)
                expect(off->message.getTimeStamp() == track.endBeat(chordIndex) * 960 * track.quarterNotesPerBeat(), "note duration");
            if (++noteIndex == static_cast<int>(notes.size())) { ++chordIndex; noteIndex = 0; }
        }
    }
    expect(tempoFound && meterFound, "tempo and meter metadata present");
    expect(ons == offs && chordIndex == track.rows.size(), "all chords have balanced notes");
    for (const auto& note : active) expect(!note.second, "no stuck note");
}
}

int main()
{
    expect(ChordTrack::quantize(1.24, 1.0, 120.0) == 0.0, "round down");
    expect(ChordTrack::quantize(1.25, 1.0, 120.0) == 1.0, "half beat rounds up");
    expect(ChordTrack::quantize(0.5, 1.0, 120.0) == 0.0, "negative time clamps to start");
    expect(ChordTrack::notes(20) == std::vector<int>({56, 59, 63}), "G#m example");
    expect(ChordTrack::notes(6) == std::vector<int>({54, 58, 61}), "F# example");
    expect(ChordTrack::notes(4) == std::vector<int>({52, 56, 59}), "E example");
    expect(ChordTrack::notes(3) == std::vector<int>({51, 55, 58}), "D# example");
    const std::vector<std::vector<int>> intervals {{0,4,7},{0,3,7},{0,4,7,10},{0,4,7,11},
                                                {0,3,7,10},{0,2,7},{0,5,7},{0,3,6}};
    for (int chord = 0; chord < 96; ++chord)
    {
        auto expected = intervals[static_cast<size_t>(chord / 12)];
        for (auto& note : expected) note += 48 + chord % 12;
        expect(ChordTrack::notes(chord) == expected, "all 96 voicings");
    }
    for (int meter : {3, 4, 6})
        for (int duration : {0, 1, 2})
        {
            ChordTrack track;
            track.meter = meter;
            track.duration = duration;
            for (int chord = 0; chord < 96; ++chord) track.rows.push_back({chord, chord * 4.0});
            roundTrip(track);
        }
    ChordTrack track;
    track.duration = 2;
    track.rows = {{0, 0}};
    track.meter = 3;
    track.duration = 1;
    expect(track.endBeat(0) == 1.5, "half of 3/4 bar is 1.5 quarter notes");
    track.meter = 6;
    expect(track.endBeat(0) * track.quarterNotesPerBeat() == 1.5, "half of 6/8 bar is 1.5 quarter notes");
    track.duration = 0;
    expect(track.endBeat(0) * track.quarterNotesPerBeat() == 0.5, "6/8 beat is one eighth note");
    track.meter = 4;
    track.duration = 2;
    track.rows = {{0, 0}, {36, 1}, {12, 2}, {0, 4}};
    expect(track.endBeat(0) == 1.0 && track.endBeat(3) == 8.0, "duration clips at next chord, final chord has full length");
    roundTrip(track); // Closely spaced chords with shared notes.
    track.rows[1].chord = 20;
    roundTrip(track); // Edited chord reaches MIDI.
    track.rows[1].beat = 0;
    juce::MemoryOutputStream invalid;
    expect(!ChordMidi::write(track, invalid) && invalid.getDataSize() == 0, "duplicate beats block export");
    track.rows.clear();
    expect(!ChordMidi::write(track, invalid), "empty track blocked");
    track.rows = {{-1, 0}};
    expect(!ChordMidi::write(track, invalid), "invalid chord blocked");
    track.rows = {{0, std::numeric_limits<double>::quiet_NaN()}};
    expect(!ChordMidi::write(track, invalid), "invalid time blocked");
    track.duration = 3;
    track.rows = {{21,0}, {0,4}, {16,8}, {21,16}, {0,20}, {16,24}};
    track.recordedEndBeat = 32;
    expect(track.endBeat(2) == 16 && track.endBeat(5) == 32,
           "two-bar chords last until the next change and final bar end");
    roundTrip(track);
    std::cout << (failures == 0 ? "All Chord MIDI tests passed.\n" : "Chord MIDI tests failed.\n");
    return failures == 0 ? 0 : 1;
}
