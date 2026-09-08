#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <string_view>

struct ChordMatch
{
    int chord = -1;          // 0..11 major, 12..23 minor, -1 = no reliable chord.
    int alternative = -1;
    float confidence = 0.0f; // A match score for the UI, not a statistical probability.
    float score = 0.0f;
    float margin = 0.0f;
};

class ChordMatcher
{
public:
    static constexpr int chordCount = 24;

    static constexpr std::array<std::string_view, chordCount> names {
        "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B",
        "Cm", "C#m", "Dm", "D#m", "Em", "Fm", "F#m", "Gm", "G#m", "Am", "A#m", "Bm"
    };

    static std::string_view name(int chord) noexcept
    {
        return chord >= 0 && chord < chordCount ? names[static_cast<size_t>(chord)]
                                                : std::string_view { "--" };
    }

    ChordMatch match(const std::array<float, 12>& input, float rmsDb,
                     float sensitivityPercent) const noexcept
    {
        const float sensitivity = bounded(sensitivityPercent, 0.0f, 100.0f, 65.0f) * 0.01f;
        const float minimumRmsDb = -38.0f - 32.0f * sensitivity;
        if (!std::isfinite(rmsDb) || rmsDb < minimumRmsDb)
            return {};

        std::array<float, 12> chroma {};
        float squareSum = 0.0f;
        for (size_t i = 0; i < chroma.size(); ++i)
        {
            chroma[i] = std::sqrt(std::max(0.0f, std::isfinite(input[i]) ? input[i] : 0.0f));
            squareSum += chroma[i] * chroma[i];
        }
        if (squareSum <= 1.0e-9f)
            return {};
        const float length = std::sqrt(squareSum);
        for (auto& value : chroma)
            value /= length;

        std::array<float, chordCount> scores {};
        constexpr float templateLength = 1.5818028f;
        for (int chord = 0; chord < chordCount; ++chord)
        {
            const int root = chord % 12;
            const int third = (root + (chord < 12 ? 4 : 3)) % 12;
            const int fifth = (root + 7) % 12;
            float score = chroma[static_cast<size_t>(root)]
                        + 0.90f * chroma[static_cast<size_t>(third)]
                        + 0.75f * chroma[static_cast<size_t>(fifth)];
            for (int note = 0; note < 12; ++note)
                if (note != root && note != third && note != fifth)
                    score -= 0.12f * chroma[static_cast<size_t>(note)];
            scores[static_cast<size_t>(chord)] = score / templateLength;
        }

        int best = 0, second = 1;
        if (scores[1] > scores[0])
            std::swap(best, second);
        for (int chord = 2; chord < chordCount; ++chord)
        {
            if (scores[static_cast<size_t>(chord)] > scores[static_cast<size_t>(best)])
            {
                second = best;
                best = chord;
            }
            else if (scores[static_cast<size_t>(chord)] > scores[static_cast<size_t>(second)])
            {
                second = chord;
            }
        }

        const float bestScore = scores[static_cast<size_t>(best)];
        const float margin = bestScore - scores[static_cast<size_t>(second)];
        const int bestRoot = best % 12;
        const int bestThird = (bestRoot + (best < 12 ? 4 : 3)) % 12;
        const int bestFifth = (bestRoot + 7) % 12;
        const auto safeInput = [&input](int note)
        {
            const float value = input[static_cast<size_t>(note)];
            return std::isfinite(value) ? std::max(0.0f, value) : 0.0f;
        };
        const float referenceTone = std::max(safeInput(bestRoot), safeInput(bestFifth));
        const float thirdRatio = safeInput(bestThird)
                               / std::max(0.0001f, referenceTone);
        const float minimumScore = 0.50f - 0.12f * sensitivity;
        const float minimumMargin = 0.035f - 0.030f * sensitivity;
        const float minimumThirdRatio = 0.18f - 0.10f * sensitivity;
        if (bestScore < minimumScore || margin < minimumMargin || thirdRatio < minimumThirdRatio)
            return { -1, best, 0.0f, bestScore, margin };

        const float quality = (bestScore - minimumScore) / std::max(0.001f, 1.0f - minimumScore);
        const float separation = (margin - minimumMargin) / 0.25f;
        const float thirdEvidence = (thirdRatio - minimumThirdRatio) / 0.60f;
        const float confidence = 100.0f * std::clamp(0.55f * quality + 0.25f * separation
                                                     + 0.20f * thirdEvidence,
                                                     0.0f, 1.0f);
        return { best, second, confidence, bestScore, margin };
    }

private:
    static float bounded(float value, float low, float high, float fallback) noexcept
    {
        return std::isfinite(value) ? std::clamp(value, low, high) : fallback;
    }
};
