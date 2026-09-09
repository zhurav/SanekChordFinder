#pragma once

#include "ChordMatcher.h"
#include <vector>
#include <string>

struct ChordTrackRow
{
    int chord = 0;
    double beat = 0.0; // Zero-based meter units; eighth notes in 6/8.
};

struct ChordTrack
{
    std::vector<ChordTrackRow> rows;
    double bpm = 120.0; // Meter units per minute, matching the internal detector.
    int meter = 4;
    double recordedEndBeat = -1.0;
    int duration = 3; // 0: one beat, 1: half bar, 2: whole bar, 3: until next change.

    double quarterNotesPerBeat() const { return meter == 6 ? 0.5 : 1.0; }
    double durationBeats() const { return duration == 0 ? 1.0 : (duration == 1 ? meter * 0.5 : meter); }
    double endBeat(size_t row) const
    {
        if (duration == 3 && row + 1 < rows.size()) return rows[row + 1].beat;
        if (duration == 3 && recordedEndBeat > rows[row].beat) return recordedEndBeat;
        const double end = rows[row].beat + durationBeats();
        return row + 1 < rows.size() ? std::min(end, rows[row + 1].beat) : end;
    }
    static double quantize(double seconds, double origin, double tempo)
    {
        return std::round(std::max(0.0, seconds - origin) * tempo / 60.0);
    }
    std::string error() const
    {
        if (rows.empty()) return "Load detected chords to start editing.";
        if (!std::isfinite(bpm) || bpm < 30.0 || bpm > 300.0) return "Tempo must be 30-300 BPM.";
        if (meter != 3 && meter != 4 && meter != 6) return "Choose 3/4, 4/4 or 6/8.";
        if (duration < 0 || duration > 3) return "Choose a note duration.";
        for (size_t i = 0; i < rows.size(); ++i)
        {
            if (!ChordMatcher::isValid(rows[i].chord)) return "Choose a valid chord in every row.";
            if (!std::isfinite(rows[i].beat) || rows[i].beat < 0.0 || rows[i].beat > 100000.0
                || rows[i].beat != std::floor(rows[i].beat)) return "Start beats must be whole numbers from 1 to 100001.";
            if (i > 0 && rows[i].beat <= rows[i - 1].beat)
                return "Conflicting beats: move or delete a row before export.";
        }
        return {};
    }
    static std::vector<int> notes(int chord)
    {
        std::vector<int> result;
        if (ChordMatcher::isValid(chord))
            for (int tone = 0; tone < ChordMatcher::toneCount(chord); ++tone)
                result.push_back(48 + ChordMatcher::rootOf(chord) + ChordMatcher::intervalAt(chord, tone));
        return result;
    }
};
