#include "ChordMatcher.h"
#include <iostream>
#include <limits>
#include <stdexcept>

namespace
{
void require(bool okay, const char* message)
{
    if (!okay)
        throw std::runtime_error(message);
}

std::array<float, 12> chordChroma(int chord, int inversion)
{
    std::array<float, 12> result {};
    result.fill(0.02f);
    const int tones = ChordMatcher::toneCount(chord);
    const std::array<float, 4> levels { 1.0f, 0.78f, 0.66f, 0.58f };
    for (int tone = 0; tone < tones; ++tone)
    {
        const int selected = (tone + inversion) % tones;
        const int pitch = (ChordMatcher::rootOf(chord)
                         + ChordMatcher::intervalAt(chord, selected)) % 12;
        result[static_cast<size_t>(pitch)] += levels[static_cast<size_t>(tone)];
    }
    return result;
}
}

int main()
{
    try
    {
        ChordMatcher matcher;
        int checked = 0;
        for (int chord = 0; chord < ChordMatcher::chordCount; ++chord)
        {
            const int quality = ChordMatcher::qualityOf(chord);
            const int inversions = quality == ChordMatcher::sus2
                                || quality == ChordMatcher::sus4
                                || quality == ChordMatcher::diminished
                                 ? 1 : ChordMatcher::toneCount(chord);
            for (int inversion = 0; inversion < inversions; ++inversion)
            {
                const auto result = matcher.match(chordChroma(chord, inversion),
                                                  -20.0f, 65.0f, true);
                if (result.chord != chord)
                    std::cerr << "Expected " << ChordMatcher::name(chord) << " inversion "
                              << inversion << ", got " << ChordMatcher::name(result.chord)
                              << ", alternative " << ChordMatcher::name(result.alternative)
                              << ", score " << result.score << ", margin " << result.margin << '\n';
                require(result.chord == chord, "Failed an extended chord or its inversion");
                require(result.confidence > 20.0f, "Clean triad confidence is unexpectedly low");
                ++checked;
            }
        }
        std::cout << "PASS: " << checked << " chord voicings across 96 chord names\n";

        std::array<float, 12> single {};
        single[0] = 1.0f;
        require(matcher.match(single, -20.0f, 65.0f).chord == -1,
                "A single note must not be called a chord");
        std::array<float, 12> powerChord {};
        powerChord[0] = 1.0f;
        powerChord[7] = 0.75f;
        const auto powerResult = matcher.match(powerChord, -20.0f, 100.0f);
        if (powerResult.chord >= 0)
            std::cerr << "Power chord became " << ChordMatcher::name(powerResult.chord)
                      << ", score " << powerResult.score
                      << ", margin " << powerResult.margin << '\n';
        require(powerResult.chord == -1,
                "A root and fifth without a third must not invent major or minor");
        std::array<float, 12> silence {};
        require(matcher.match(silence, -120.0f, 100.0f).chord == -1,
                "Silence must not be called a chord");
        require(matcher.match(chordChroma(0, 0), -65.0f, 0.0f).chord == -1,
                "Low sensitivity must reject a very quiet signal");
        require(matcher.match(chordChroma(0, 0), -65.0f, 100.0f).chord == 0,
                "High sensitivity must accept a quiet clean chord");
        std::cout << "PASS: silence, single-note and sensitivity gates\n";

        require(matcher.match(chordChroma(0, 0), -20.0f, 65.0f).chord == 0,
                "A plain C triad must not be upgraded to C7 or Cmaj7");
        auto invalid = chordChroma(9, 0);
        invalid[3] = std::numeric_limits<float>::quiet_NaN();
        require(matcher.match(invalid, -20.0f, 65.0f).chord == 9,
                "One invalid pitch class must not poison the frame");
        std::cout << "PASS: triad preservation and invalid input handling\n";
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
