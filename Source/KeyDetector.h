#pragma once

#include "ChordMatcher.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <string>
#include <string_view>
#include <vector>

struct KeyObservation
{
    int chord = -1;
    float confidence = 0.0f;
    double durationSeconds = 1.0;
};

struct KeyMatch
{
    int key = -1;           // 0-11: major, 12-23: minor.
    float confidence = 0.0f;
    float score = 0.0f;
    float margin = 0.0f;
    int observationCount = 0;
    int distinctRoots = 0;
};

class KeyDetector
{
public:
    static constexpr int rootsPerMode = 12;
    static constexpr int keyCount = 24;

    static constexpr std::array<std::string_view, keyCount> names {
        "C major", "C# major", "D major", "D# major", "E major", "F major",
        "F# major", "G major", "G# major", "A major", "A# major", "B major",
        "C minor", "C# minor", "D minor", "D# minor", "E minor", "F minor",
        "F# minor", "G minor", "G# minor", "A minor", "A# minor", "B minor"
    };

    static bool isValid(int key) noexcept { return key >= 0 && key < keyCount; }
    static bool isMinor(int key) noexcept { return isValid(key) && key >= rootsPerMode; }
    static int tonicOf(int key) noexcept
    {
        return isValid(key) ? key % rootsPerMode : -1;
    }

    static std::string_view name(int key) noexcept
    {
        return isValid(key) ? names[static_cast<size_t>(key)] : std::string_view { "--" };
    }

    static float harmonicFit(int key, int chord) noexcept
    {
        return isValid(key) && ChordMatcher::isValid(chord) ? chordFit(key, chord) : 0.0f;
    }

    KeyMatch analyse(const std::vector<KeyObservation>& input) const
    {
        return analyse(input.data(), input.size());
    }

    // Bounded storage also permits use by the audio-thread context resolver.
    KeyMatch analyse(const KeyObservation* input, size_t count) const noexcept
    {
        ObservationBuffer observations;
        const size_t first = count > maximumHistory ? count - maximumHistory : 0;
        std::array<bool, 12> rootsSeen {};
        int distinctRoots = 0;
        for (size_t i = first; i < count; ++i)
        {
            if (!ChordMatcher::isValid(input[i].chord)
                || !std::isfinite(input[i].durationSeconds) || input[i].durationSeconds <= 0.0)
                continue;
            observations.push_back(input[i]);
            if (!std::isfinite(observations.back().confidence)) observations.back().confidence = 0.0f;
            const int root = ChordMatcher::rootOf(input[i].chord);
            if (!rootsSeen[static_cast<size_t>(root)])
            {
                rootsSeen[static_cast<size_t>(root)] = true;
                ++distinctRoots;
            }
        }

        KeyMatch result;
        result.observationCount = static_cast<int>(observations.size());
        result.distinctRoots = distinctRoots;
        if (observations.size() < 3 || distinctRoots < 3)
            return result;

        std::array<float, keyCount> scores {};
        for (int key = 0; key < keyCount; ++key)
            scores[static_cast<size_t>(key)] = scoreKey(key, observations);

        int best = 0, second = 1;
        if (scores[1] > scores[0])
            std::swap(best, second);
        for (int key = 2; key < keyCount; ++key)
        {
            if (scores[static_cast<size_t>(key)] > scores[static_cast<size_t>(best)])
            {
                second = best;
                best = key;
            }
            else if (scores[static_cast<size_t>(key)] > scores[static_cast<size_t>(second)])
            {
                second = key;
            }
        }

        result.score = scores[static_cast<size_t>(best)];
        result.margin = result.score - scores[static_cast<size_t>(second)];
        const float amount = std::clamp((static_cast<float>(observations.size()) - 2.0f) / 6.0f,
                                        0.0f, 1.0f);
        const float fit = std::clamp((result.score - 0.55f) / 1.55f, 0.0f, 1.0f);
        const float separation = std::clamp(result.margin / 0.85f, 0.0f, 1.0f);
        result.confidence = 100.0f * std::clamp(0.42f * fit + 0.43f * separation
                                                + 0.15f * amount,
                                                0.0f, 1.0f);
        if (result.score >= 0.90f && result.confidence >= 30.0f)
            result.key = best;
        return result;
    }

    static std::string degreeName(int key, int chord)
    {
        if (!isValid(key) || !ChordMatcher::isValid(chord))
            return "--";
        static constexpr std::array<std::string_view, 12> majorDegrees {
            "I", "bII", "II", "bIII", "III", "IV",
            "#IV", "V", "bVI", "VI", "bVII", "VII"
        };
        static constexpr std::array<std::string_view, 12> minorDegrees {
            "I", "bII", "II", "III", "#III", "IV",
            "#IV", "V", "VI", "#VI", "VII", "#VII"
        };
        const int interval = (ChordMatcher::rootOf(chord) - tonicOf(key) + 12) % 12;
        std::string result((isMinor(key) ? minorDegrees : majorDegrees)
                           [static_cast<size_t>(interval)]);
        const int quality = ChordMatcher::qualityOf(chord);
        const auto family = ChordMatcher::familyOf(chord);
        if (family == ChordMatcher::Family::minor || family == ChordMatcher::Family::diminished)
        {
            for (auto& character : result)
                if (character == 'I' || character == 'V' || character == 'X')
                    character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
        }
        result += ChordMatcher::definitions[static_cast<size_t>(quality)].degreeSuffix;
        return result;
    }

private:
    static constexpr size_t maximumHistory = 32;
    struct ObservationBuffer
    {
        std::array<KeyObservation, maximumHistory> data {};
        size_t count = 0;
        void push_back(KeyObservation value) noexcept { data[count++] = value; }
        size_t size() const noexcept { return count; }
        KeyObservation& back() noexcept { return data[count - 1]; }
        const KeyObservation& back() const noexcept { return data[count - 1]; }
        const KeyObservation& front() const noexcept { return data[0]; }
        const KeyObservation& operator[](size_t i) const noexcept { return data[i]; }
    };

    enum ExpectedQuality { expectedMajor, expectedMinor, expectedDiminished, expectedEither };

    static int degreeIndex(int key, int root) noexcept
    {
        static constexpr std::array<int, 7> majorScale { 0, 2, 4, 5, 7, 9, 11 };
        static constexpr std::array<int, 7> minorScale { 0, 2, 3, 5, 7, 8, 10 };
        const int interval = (root - tonicOf(key) + 12) % 12;
        const auto& scale = isMinor(key) ? minorScale : majorScale;
        for (int degree = 0; degree < 7; ++degree)
            if (scale[static_cast<size_t>(degree)] == interval)
                return degree;
        // The raised leading tone is a normal part of harmonic minor.
        if (isMinor(key) && interval == 11)
            return 7;
        return -1;
    }

    static ExpectedQuality expectedQuality(int key, int degree) noexcept
    {
        if (!isMinor(key))
        {
            static constexpr std::array<ExpectedQuality, 7> qualities {
                expectedMajor, expectedMinor, expectedMinor, expectedMajor,
                expectedMajor, expectedMinor, expectedDiminished
            };
            return degree >= 0 && degree < 7 ? qualities[static_cast<size_t>(degree)]
                                             : expectedEither;
        }
        // Degree 5 accepts both natural-minor v and the common harmonic-minor V.
        static constexpr std::array<ExpectedQuality, 8> qualities {
            expectedMinor, expectedDiminished, expectedMajor, expectedMinor,
            expectedEither, expectedMajor, expectedMajor, expectedDiminished
        };
        return degree >= 0 && degree < 8 ? qualities[static_cast<size_t>(degree)]
                                         : expectedEither;
    }

    static float chordFit(int key, int chord) noexcept
    {
        const int root = ChordMatcher::rootOf(chord);
        const int degree = degreeIndex(key, root);
        if (degree < 0)
            return -1.20f;

        const int quality = ChordMatcher::qualityOf(chord);
        const auto expected = expectedQuality(key, degree);
        const auto family = ChordMatcher::familyOf(chord);
        const bool majorLike = family == ChordMatcher::Family::major;
        const bool minorLike = family == ChordMatcher::Family::minor;
        float fit = 0.0f;
        if (family == ChordMatcher::Family::power)
            fit = 0.30f; // No third: do not vote for major versus minor.
        else if (family == ChordMatcher::Family::suspended)
            fit = degree == 0 || degree == 3 || degree == 4 ? 0.95f : 0.55f;
        else if (family == ChordMatcher::Family::diminished)
            fit = expected == expectedDiminished ? 1.75f : -0.75f;
        else if (expected == expectedEither)
            fit = majorLike || minorLike ? 1.35f : 0.30f;
        else if ((expected == expectedMajor && majorLike)
                 || (expected == expectedMinor && minorLike))
            fit = 1.50f;
        else
            fit = -0.85f;

        if (quality == ChordMatcher::dominant7 || quality == ChordMatcher::dominant9)
            fit += degree == 4 ? 0.55f : -0.15f;
        else if ((quality == ChordMatcher::major7 || quality == ChordMatcher::major9) && expected == expectedMajor)
            fit += degree == 0 || degree == 3 ? 0.20f : 0.05f;
        else if ((quality == ChordMatcher::minor7 || quality == ChordMatcher::minor9) && expected == expectedMinor)
            fit += 0.15f;

        if (degree == 0)
            fit += 0.35f;
        else if (degree == 4)
            fit += 0.15f;
        return fit;
    }

    static float scoreKey(int key, const ObservationBuffer& observations) noexcept
    {
        float weightedScore = 0.0f;
        float totalWeight = 0.0f;
        const float denominator = observations.size() > 1
                                ? static_cast<float>(observations.size() - 1) : 1.0f;
        for (size_t i = 0; i < observations.size(); ++i)
        {
            const float recency = 0.78f + 0.22f * static_cast<float>(i) / denominator;
            const float rawConfidence = std::isfinite(observations[i].confidence)
                                      ? observations[i].confidence : 0.0f;
            const float detection = std::clamp(rawConfidence / 100.0f, 0.0f, 1.0f);
            const float weight = recency * detection * static_cast<float>(observations[i].durationSeconds);
            weightedScore += weight * chordFit(key, observations[i].chord);
            totalWeight += weight;
        }
        float score = weightedScore / std::max(0.001f, totalWeight);
        const int tonic = tonicOf(key);
        const int dominant = (tonic + 7) % 12;
        const int firstRoot = ChordMatcher::rootOf(observations.front().chord);
        const int lastRoot = ChordMatcher::rootOf(observations.back().chord);
        if (firstRoot == tonic)
            score += 0.20f * static_cast<float>(std::min(1.0, observations.front().durationSeconds));
        if (lastRoot == tonic)
            score += 0.42f * static_cast<float>(std::min(1.0, observations.back().durationSeconds));

        double tonicDuration = 0.0, totalDuration = 0.0, authenticCadences = 0.0;
        for (size_t i = 0; i < observations.size(); ++i)
        {
            const int root = ChordMatcher::rootOf(observations[i].chord);
            const double duration = observations[i].durationSeconds
                                  * std::clamp(observations[i].confidence / 100.0f, 0.0f, 1.0f);
            totalDuration += duration;
            if (root == tonic)
                tonicDuration += duration;
            if (i > 0 && root == tonic
                && ChordMatcher::rootOf(observations[i - 1].chord) == dominant)
                authenticCadences += std::min({1.0, observations[i].durationSeconds,
                                               observations[i - 1].durationSeconds});
        }
        score += 0.32f * static_cast<float>(tonicDuration / std::max(0.001, totalDuration));
        score += std::min(0.45f, 0.25f * static_cast<float>(authenticCadences));
        return score;
    }
};
