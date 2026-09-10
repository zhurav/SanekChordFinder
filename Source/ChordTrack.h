#pragma once

#include "ChordMatcher.h"
#include <vector>
#include <string>

struct ChordTrackRow
{
    int chord = 0;
    double beat = 0.0; // Zero-based meter units; eighth notes in 6/8.
    int bassNote = -1; // Absolute MIDI note; -1 preserves legacy root-position voicing.
};

struct ChordTrack
{
    std::vector<ChordTrackRow> rows;
    double bpm = 120.0; // Meter units per minute, matching the internal detector.
    int meter = 4;
    double recordedEndBeat = -1.0;
    int duration = 3; // 0: one beat, 1: half bar, 2: whole bar, 3: until next change.
    int scope = 0; // 0: detect one repeated loop, 1: keep the full take.
    bool hostTempoFallback = false;

    double quarterNotesPerBeat() const { return meter == 6 ? 0.5 : 1.0; }
    double durationBeats() const { return duration == 0 ? 1.0 : (duration == 1 ? meter * 0.5 : meter); }
    double endBeat(size_t row) const
    {
        if (duration == 3 && row + 1 < rows.size()) return rows[row + 1].beat;
        if (duration == 3 && recordedEndBeat > rows[row].beat) return recordedEndBeat;
        const double end = rows[row].beat + durationBeats();
        const double bounded = recordedEndBeat > rows[row].beat ? std::min(end, recordedEndBeat) : end;
        return row + 1 < rows.size() ? std::min(bounded, rows[row + 1].beat) : bounded;
    }
    static double quantize(double seconds, double origin, double tempo)
    {
        return std::round(std::max(0.0, seconds - origin) * tempo / 60.0);
    }
    bool keepOneLoop()
    {
        if (scope != 0 || rows.size() < 4) return false;
        for (size_t cycleRows = 2; cycleRows * 2 <= rows.size(); ++cycleRows)
        {
            bool same = true;
            for (size_t i = 0; i < cycleRows; ++i)
                same &= rows[i].chord == rows[cycleRows + i].chord
                     && rows[i].bassNote == rows[cycleRows + i].bassNote;
            if (!same) continue;
            bool hasDifferentChord = false;
            for (size_t i = 1; i < cycleRows; ++i)
                hasDifferentChord |= rows[i].chord != rows[0].chord;
            const double cycleBeats = rows[cycleRows].beat;
            if (!hasDifferentChord || cycleBeats < meter) continue;
            // Matching names alone cannot establish a loop: waits/seeks may have
            // inserted arbitrarily long gaps. Compare musical positions too.
            for (size_t i = 0; i < cycleRows; ++i)
                same &= std::abs(rows[cycleRows + i].beat - rows[i].beat - cycleBeats) <= 1.0;
            if (!same || recordedEndBeat < cycleBeats * 2.0 - 0.5
                || std::abs(cycleBeats - std::round(cycleBeats / meter) * meter) > 0.5)
                continue;
            recordedEndBeat = std::max(static_cast<double>(meter),
                std::round(cycleBeats / meter) * meter);
            rows.resize(cycleRows);
            return true;
        }
        return false;
    }
    void splitHeldChords()
    {
        if (duration == 3 || rows.empty()) return;
        const auto original = rows;
        std::vector<ChordTrackRow> split;
        const double step = durationBeats();
        for (size_t row = 0; row < original.size(); ++row)
        {
            const double boundary = row + 1 < original.size() ? original[row + 1].beat
                                                               : recordedEndBeat;
            split.push_back(original[row]);
            if (!std::isfinite(boundary) || boundary <= original[row].beat) continue;
            for (double beat = original[row].beat + step; beat < boundary - 0.0001; beat += step)
                split.push_back({ original[row].chord, beat, original[row].bassNote });
        }
        rows = std::move(split);
    }
    std::string error() const
    {
        if (rows.empty()) return "Load detected chords to start editing.";
        if (!std::isfinite(bpm) || bpm < 30.0 || bpm > 300.0) return "Tempo must be 30-300 BPM.";
        if (meter != 3 && meter != 4 && meter != 6) return "Choose 3/4, 4/4 or 6/8.";
        if (duration < 0 || duration > 3) return "Choose a note duration.";
        if (scope < 0 || scope > 1) return "Choose ONE LOOP or FULL TAKE.";
        for (size_t i = 0; i < rows.size(); ++i)
        {
            if (!ChordMatcher::isValid(rows[i].chord)) return "Choose a valid chord in every row.";
            if (rows[i].bassNote < -1 || rows[i].bassNote > 108) return "Bass note is outside the supported MIDI range.";
            if (!std::isfinite(rows[i].beat) || rows[i].beat < 0.0 || rows[i].beat > 100000.0
                || rows[i].beat * 2.0 != std::floor(rows[i].beat * 2.0)) return "Start beats must use half-beat steps from 1 to 100001.";
            if (i > 0 && rows[i].beat <= rows[i - 1].beat)
                return "Conflicting beats: move or delete a row before export.";
        }
        return {};
    }
    static std::string name(int chord, int bassNote = -1)
    {
        std::string result(ChordMatcher::name(chord));
        if (ChordMatcher::isValid(chord) && bassNote >= 0
            && bassNote % 12 != ChordMatcher::rootOf(chord))
            result += "/" + std::string(ChordMatcher::name(bassNote % 12));
        return result;
    }
    static std::string toneName(int chord, int tone)
    {
        if (!ChordMatcher::isValid(chord) || tone < 0 || tone >= ChordMatcher::toneCount(chord)) return "--";
        constexpr std::array<int, 7> natural {0,2,4,5,7,9,11};
        constexpr std::array<char, 7> letters {'C','D','E','F','G','A','B'};
        const int root = ChordMatcher::rootOf(chord);
        const auto rootName = ChordMatcher::rootNames[static_cast<size_t>(root)];
        const auto rootLetter = static_cast<int>(std::find(letters.begin(),letters.end(),rootName.front()) - letters.begin());
        const int interval = ChordMatcher::intervalAt(chord,tone);
        int degree = tone * 2;
        if (tone == 1 && interval == 2) degree = 1;
        if (tone == 1 && interval == 5) degree = 3;
        if (tone == 1 && interval == 7) degree = 4;
        if (tone == 3 && interval == 9 && ChordMatcher::qualityOf(chord) != ChordMatcher::diminished7) degree = 5;
        if (interval == 14) degree = 8;
        const int letter = (rootLetter + degree) % 7;
        int alteration = (root + interval - natural[static_cast<size_t>(letter)] + 12) % 12;
        if (alteration > 6) alteration -= 12;
        return std::string(1, letters[static_cast<size_t>(letter)])
            + std::string(static_cast<size_t>(std::abs(alteration)), alteration < 0 ? 'b' : '#');
    }
    static std::vector<int> notes(int chord, int bassNote = -1)
    {
        std::vector<int> result;
        if (ChordMatcher::isValid(chord))
        {
            const int root = ChordMatcher::rootOf(chord);
            const int anchor = bassNote < 0 ? 48 + root : bassNote - (bassNote % 12 - root + 12) % 12;
            for (int tone = 0; tone < ChordMatcher::toneCount(chord); ++tone)
            {
                int note = anchor + ChordMatcher::intervalAt(chord, tone);
                if (bassNote >= 0)
                {
                    if ((note % 12 + 12) % 12 == bassNote % 12) note = bassNote;
                    while (note < bassNote) note += 12;
                }
                result.push_back(note);
            }
            if (bassNote >= 0 && std::find(result.begin(), result.end(), bassNote) == result.end())
                result.push_back(bassNote);
            std::sort(result.begin(), result.end());
        }
        return result;
    }
};
