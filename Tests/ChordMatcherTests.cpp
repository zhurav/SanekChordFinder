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
    const int root = chord % 12;
    const int third = (root + (chord < 12 ? 4 : 3)) % 12;
    const int fifth = (root + 7) % 12;
    const std::array<float, 3> levels { 1.0f, 0.74f, 0.58f };
    const std::array<int, 3> tones { root, third, fifth };
    for (int i = 0; i < 3; ++i)
        result[static_cast<size_t>(tones[static_cast<size_t>((i + inversion) % 3)])] += levels[static_cast<size_t>(i)];
    return result;
}
}

int main()
{
    try
    {
        ChordMatcher matcher;
        for (int chord = 0; chord < ChordMatcher::chordCount; ++chord)
            for (int inversion = 0; inversion < 3; ++inversion)
            {
                const auto result = matcher.match(chordChroma(chord, inversion), -20.0f, 65.0f);
                require(result.chord == chord, "Failed one of 24 chords or its inversion");
                require(result.confidence > 20.0f, "Clean triad confidence is unexpectedly low");
            }
        std::cout << "PASS: 24 major/minor chords in three inversions with background notes\n";

        std::array<float, 12> single {};
        single[0] = 1.0f;
        require(matcher.match(single, -20.0f, 65.0f).chord == -1,
                "A single note must not be called a chord");
        std::array<float, 12> powerChord {};
        powerChord[0] = 1.0f;
        powerChord[7] = 0.75f;
        require(matcher.match(powerChord, -20.0f, 100.0f).chord == -1,
                "A root and fifth without a third must not invent major or minor");
        std::array<float, 12> silence {};
        require(matcher.match(silence, -120.0f, 100.0f).chord == -1,
                "Silence must not be called a chord");
        require(matcher.match(chordChroma(0, 0), -65.0f, 0.0f).chord == -1,
                "Low sensitivity must reject a very quiet signal");
        require(matcher.match(chordChroma(0, 0), -65.0f, 100.0f).chord == 0,
                "High sensitivity must accept a quiet clean chord");
        std::cout << "PASS: silence, single-note and sensitivity gates\n";

        auto ambiguous = chordChroma(0, 0);
        ambiguous[9] = ambiguous[7]; // C-E-G-A: equally plausible C and Am in this vocabulary.
        require(matcher.match(ambiguous, -20.0f, 65.0f).chord == -1,
                "An ambiguous pitch set must be rejected");
        auto invalid = chordChroma(9, 0);
        invalid[3] = std::numeric_limits<float>::quiet_NaN();
        require(matcher.match(invalid, -20.0f, 65.0f).chord == 9,
                "One invalid pitch class must not poison the frame");
        std::cout << "PASS: ambiguity and invalid input handling\n";
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
