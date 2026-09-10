#include "ChordTrack.h"
#include <iostream>
#include <stdexcept>

static void require(bool okay, const char* message)
{
    if (!okay) throw std::runtime_error(message);
}

int main()
{
    try
    {
        using M = ChordMatcher;
        for (int tonic = 0; tonic < 12; ++tonic)
        {
            const auto chord = [=](int quality, int root) { return quality * 12 + (root + tonic) % 12; };
            const int six = chord(M::major6, 0), am7 = chord(M::minor7, 9);
            const int fmaj7 = chord(M::major7, 5), dm7 = chord(M::minor7, 2);
            const int g7 = chord(M::dominant7, 7), d7 = chord(M::dominant7, 2), g = chord(M::major, 7);
            auto result = ChordContextResolver::resolve(am7, {-1, tonic, 90.0f, fmaj7, dm7, g7});
            require(result.chord == six, "IVmaj7 -> I6 -> ii7 -> V7");
            result = ChordContextResolver::resolve(six, {-1, tonic, 90.0f, dm7, d7, g});
            require(result.chord == am7, "ii7 -> vi7 -> V7/V -> V");
            result = ChordContextResolver::resolve(six, {36 + (tonic + 9) % 12, tonic, 100.0f, fmaj7, dm7, g7});
            require(result.chord == am7, "Bass must outweigh weak key/movement preferences");
            result = ChordContextResolver::resolve(am7, {36 + tonic, tonic, 90.0f, dm7, d7, g});
            require(result.chord == six, "Root bass must survive secondary-dominant context");
            result = ChordContextResolver::resolve(chord(M::minor7, 3),
                {-1, -1, 0, chord(M::major7, 1)});
            require(result.chord == chord(M::minor7, 3), "A weak fifth must not rename a minor seventh as a sixth");
            result = ChordContextResolver::resolve(chord(M::diminished7, 2),
                {-1, tonic, 100.0f});
            require(result.chord == chord(M::diminished7, 11), "Key evidence selects the leading-tone diminished seventh");

            ChordTrack track;
            track.bpm = 120; track.recordedEndBeat = 16;
            track.rows = {{fmaj7,0}, {am7,4}, {dm7,8}, {g7,12}};
            track.resolveContext();
            require(track.rows[1].chord == six, "Offline lookahead first example");
            track.rows = {{dm7,0}, {six,4}, {d7,8}, {g,12}};
            track.resolveContext();
            require(track.rows[1].chord == am7, "Offline lookahead second example");
            require(track.rows.size() == 4 && track.rows[1].beat == 4 && track.recordedEndBeat == 16,
                    "Context must not change the beat grid or clip length");

            LiveChordContext live;
            live.observe(fmaj7, 90, 2.0);
            result = live.resolve(am7, -1);
            require(result.chord == six, "Live previous harmony should support plagal movement");
            live.observe(result.chord, 80, 0.1);
            for (int frame = 0; frame < 100; ++frame)
            {
                require(live.resolve(am7, 45).chord == six, "Held equivalent chord must not flicker");
                live.observe(six, 80, 0.04);
            }
            live.reset();
            require(live.resolve(am7, -1).chord == am7, "Reset clears harmonic context");
            live.observe(fmaj7, 90, 2.0);
            live.silence(31.0);
            require(live.resolve(am7, -1).chord == am7, "Long silence clears stale context");
        }
        for (int id = 0; id < M::chordCount; ++id)
        {
            const auto unknown = ChordContextResolver::resolve(id, {});
            require(unknown.chord == id, "No evidence must preserve original label");
            if (ChordContextResolver::candidateCount(id) > 1)
                require(unknown.ambiguous && unknown.alternative != id
                        && M::samePitchSet(id, unknown.alternative), "Report equivalent alternative");
            for (int bass = -1; bass < 12; ++bass)
                for (int key = -1; key < 24; ++key)
                {
                    const auto resolved = ChordContextResolver::resolve(id, {bass, key, 100, 41, 26, 7});
                    require(M::samePitchSet(id, resolved.chord), "Context must never change detected notes");
                    if (bass >= 0)
                        for (int quality = 0; quality < M::qualityCount; ++quality)
                            if (M::samePitchSet(id, quality * 12 + bass))
                                require(M::rootOf(resolved.chord) == bass, "Supported root bass takes priority for every equivalent set");
                }
        }
        require(ChordContextResolver::resolve(-1, {}).chord == -1, "Silence stays silence");
        std::cout << "Chord context: transposed progressions, bass conflicts, ambiguity, stability and pitch-set preservation passed.\n";
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n'; return 1;
    }
}
