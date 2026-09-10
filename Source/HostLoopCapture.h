#pragma once

#include "ChordTrack.h"
#include <array>
#include <atomic>

// Audio-thread writer, message-thread snapshot. No allocation or locks in capture.
class HostLoopCapture
{
public:
    struct Snapshot
    {
        bool available = false;
        double length = 0.0; // quarter notes
        double bpm = 0.0;
        int meter = 4;
        std::vector<ChordTrackRow> rows;
    };

    void reset() noexcept
    {
        available.store(false);
        invalidate();
        running = false;
        collecting = false;
        count = 0;
    }

    // Returns true when chord-analysis state must be discarded (start/seek/wrap/stop).
    bool begin(bool playing, double ppq, double start, double end, double blockBeats, double bpm = 120.0, int meter = 4) noexcept
    {
        available.store(true);
        const bool changedLoop = start != loopStart || end != loopEnd;
        if (changedLoop) { invalidate(); running = false; }
        loopStart = start;
        loopEnd = end;
        captureBpm = bpm;
        captureMeter = meter;
        const double tolerance = std::max(0.002, blockBeats * 2.0);
        const bool continuous = running && std::abs(ppq - expectedPpq) <= tolerance;
        const bool discontinuity = playing != running || (playing && !continuous);
        if (discontinuity)
        {
            count = 0;
            collecting = playing && ppq >= start - tolerance && ppq <= start + tolerance;
        }
        running = playing;
        expectedPpq = ppq + blockBeats;
        return discontinuity;
    }

    void add(int chord, double relativePpq, int bassNote = -1) noexcept
    {
        if (!collecting || !ChordMatcher::isValid(chord) || !std::isfinite(relativePpq)) return;
        const double beat = std::max(0.0, relativePpq);
        if (beat >= loopEnd - loopStart) return;
        if (count != 0 && current[count - 1].chord == chord && current[count - 1].bassNote < 0)
        {
            current[count - 1].bassNote = bassNote;
            return;
        }
        if (count != 0 && current[count - 1].chord == chord
            && current[count - 1].bassNote == bassNote) return;
        if (count == capacity) { collecting = false; return; }
        current[count++] = { chord, beat, bassNote };
    }

    void finishBlock() noexcept
    {
        if (!collecting || expectedPpq < loopEnd - 0.0001) return;
        collecting = false;
        // Never stretch a late first detection across an unobserved beginning.
        if (count == 0 || current[0].beat > 0.5) return;
        generation.fetch_add(1);
        for (size_t i = 0; i < count; ++i)
        {
            published[i].chord.store(current[i].chord);
            published[i].beat.store(current[i].beat);
            published[i].bassNote.store(current[i].bassNote);
        }
        publishedLength.store(loopEnd - loopStart);
        publishedBpm.store(captureBpm);
        publishedMeter.store(captureMeter);
        publishedCount.store(count);
        generation.fetch_add(1);
    }

    Snapshot snapshot() const
    {
        Snapshot result;
        result.available = available.load();
        for (int attempt = 0; attempt < 3; ++attempt)
        {
            const auto before = generation.load();
            if ((before & 1) != 0) continue;
            result.rows.clear();
            result.length = publishedLength.load();
            result.bpm = publishedBpm.load();
            result.meter = publishedMeter.load();
            const auto size = publishedCount.load();
            for (size_t i = 0; i < size; ++i)
                result.rows.push_back({ published[i].chord.load(), published[i].beat.load(), published[i].bassNote.load() });
            if (before == generation.load()) return result;
        }
        result.rows.clear();
        return result;
    }

private:
    void invalidate() noexcept
    {
        generation.fetch_add(1);
        publishedCount.store(0);
        publishedLength.store(0.0);
        generation.fetch_add(1);
    }
    static constexpr size_t capacity = 128;
    struct Slot { std::atomic<int> chord { -1 }, bassNote { -1 }; std::atomic<double> beat { 0.0 }; };
    std::array<Slot, capacity> published;
    std::array<ChordTrackRow, capacity> current;
    std::atomic<unsigned> generation { 0 };
    std::atomic<size_t> publishedCount { 0 };
    std::atomic<double> publishedLength { 0.0 };
    std::atomic<double> publishedBpm { 0.0 };
    double captureBpm = 0.0;
    int captureMeter = 4;
    std::atomic<int> publishedMeter {4};
    std::atomic<bool> available { false };
    size_t count = 0;
    double loopStart = 0.0, loopEnd = 0.0, expectedPpq = 0.0;
    bool running = false, collecting = false;
};
