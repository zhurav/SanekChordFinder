#pragma once
#include <array>
#include <cmath>

namespace EightBarFixture
{
inline constexpr std::array<int, 8> chords {0,36,24,60,72,12,48,84};
// Independent notes supplied by the user, not generated from the matcher table.
inline constexpr std::array<std::array<int,4>,8> notes {{{48,52,55,-1},{48,52,55,59},
    {48,52,55,58},{48,50,55,-1},{48,53,55,-1},{48,51,55,-1},{48,51,55,58},{48,51,54,-1}}};
inline float sample(double time, double bpm = 120.0, int overrideBar = -1)
{
    const double barSeconds = 240.0 / bpm;
    const int bar = static_cast<int>(time / barSeconds);
    if (bar < 0 || bar >= 8) return 0.0f;
    const double local = time - bar * barSeconds;
    const double attack = std::min(1.0, local / 0.008);
    double value = 0.0;
    for (size_t tone = 0; tone < 4; ++tone)
    {
        const int note = notes[static_cast<size_t>(overrideBar >= 0 ? overrideBar : bar)][tone];
        if (note < 0) continue;
        // The seventh decays faster; its disappearance is not a new C/Cm attack.
        const double envelope = std::exp(-local / (tone == 3 ? 0.40 : 2.0));
        for (int harmonic = 1; harmonic <= 4; ++harmonic)
            value += 0.085 * attack * envelope / harmonic
                * std::sin(6.283185307179586 * time * harmonic * 440.0 * std::exp2((note - 69) / 12.0));
    }
    return static_cast<float>(value);
}
}
