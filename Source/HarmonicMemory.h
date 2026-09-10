#pragma once
#include <algorithm>
#include <array>
#include <cmath>

class HarmonicMemory
{
public:
    HarmonicMemory() noexcept { reset(); }
    void reset() noexcept { chords.fill(-1); heard.fill(0.0); }
    void remember(int root, int chord, double seconds) noexcept
    {
        chords[static_cast<size_t>(root)] = chord;
        heard[static_cast<size_t>(root)] = seconds;
    }
    double strength(int root, double seconds) const noexcept
    {
        const double age = std::max(0.0, seconds - heard[static_cast<size_t>(root)]);
        return chords[static_cast<size_t>(root)] < 0 || age >= 60.0 ? 0.0 : std::exp(-age / 27.0);
    }
    int recall(int root, double seconds) const noexcept
    {
        return strength(root, seconds) > 0.0 ? chords[static_cast<size_t>(root)] : -1;
    }
private:
    std::array<int, 12> chords;
    std::array<double, 12> heard;
};
