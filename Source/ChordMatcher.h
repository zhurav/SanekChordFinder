#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <string_view>

struct ChordMatch
{
    int chord = -1;          // See ChordMatcher::Quality; -1 means no reliable chord.
    int alternative = -1;
    float confidence = 0.0f; // A UI score, not a statistical probability.
    float score = 0.0f;
    float margin = 0.0f;
};

class ChordMatcher
{
public:
    enum Quality
    {
        major = 0,
        minor,
        dominant7,
        major7,
        minor7,
        sus2,
        sus4,
        diminished,
        augmented,
        halfDiminished,
        diminished7,
        major6,
        minor6,
        add9,
        minorAdd9,
        power,
        dominant9,
        major9,
        minor9,
        dominant7sus4,
        qualityCount
    };

    static constexpr int rootsPerQuality = 12;
    static constexpr int chordCount = rootsPerQuality * qualityCount;

    static constexpr int maximumTones = 5;
    enum class Family { major, minor, suspended, diminished, augmented, power };
    struct ChordDefinition
    {
        std::string_view label;
        std::string_view suffix;
        std::array<int, maximumTones> intervals;
        int toneCount;
        Family family;
        std::string_view degreeSuffix;
        float minimumToneRatio;
        float penalty;
    };

    // Append definitions: the first 96 IDs are part of the saved-state format.
    // Intervals retain their octave (14 is a ninth) for MIDI, modulo 12 for chroma.
    static constexpr std::array<ChordDefinition, qualityCount> definitions {{
        { "major", "",      {0,4,7},       3, Family::major,      "",     0.0f, 0.0f },
        { "minor", "m",     {0,3,7},       3, Family::minor,      "",     0.0f, 0.0f },
        { "7",     "7",     {0,4,7,10},    4, Family::major,      "7",    0.0f, 0.0f },
        { "maj7",  "maj7",  {0,4,7,11},    4, Family::major,      "maj7", 0.0f, 0.0f },
        { "m7",    "m7",    {0,3,7,10},    4, Family::minor,      "7",    0.0f, 0.0f },
        { "sus2",  "sus2",  {0,2,7},       3, Family::suspended,  "sus2", 0.0f, 0.0f },
        { "sus4",  "sus4",  {0,5,7},       3, Family::suspended,  "sus4", 0.0f, 0.0f },
        { "dim",   "dim",   {0,3,6},       3, Family::diminished, "dim",  0.0f, 0.0f },
        { "aug",   "aug",   {0,4,8},       3, Family::augmented,  "aug",  0.38f,0.035f },
        { "m7b5",  "m7b5",  {0,3,6,10},    4, Family::diminished, "7b5",  0.38f,0.045f },
        { "dim7",  "dim7",  {0,3,6,9},     4, Family::diminished, "dim7", 0.38f,0.045f },
        { "6",     "6",     {0,4,7,9},     4, Family::major,      "6",    0.45f,0.065f },
        { "m6",    "m6",    {0,3,7,9},     4, Family::minor,      "6",    0.45f,0.065f },
        { "add9",  "add9",  {0,4,7,14},    4, Family::major,      "add9", 0.38f,0.065f },
        { "madd9", "madd9", {0,3,7,14},    4, Family::minor,      "add9", 0.38f,0.065f },
        { "5",     "5",     {0,7},         2, Family::power,      "5",    0.35f,0.035f },
        { "9",     "9",     {0,4,7,10,14}, 5, Family::major,      "9",    0.38f,0.055f },
        { "maj9",  "maj9",  {0,4,7,11,14}, 5, Family::major,      "maj9", 0.38f,0.055f },
        { "m9",    "m9",    {0,3,7,10,14}, 5, Family::minor,      "9",    0.38f,0.055f },
        { "7sus4", "7sus4", {0,5,7,10},    4, Family::suspended,  "7sus4",0.38f,0.045f }
    }};
    static constexpr std::array<std::string_view, 12> rootNames {
        "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
    };
    inline static constexpr auto names = []
    {
        std::array<std::array<char, 16>, chordCount> result {};
        for (int chord = 0; chord < chordCount; ++chord)
        {
            size_t index = 0;
            for (char c : rootNames[static_cast<size_t>(chord % 12)]) result[static_cast<size_t>(chord)][index++] = c;
            for (char c : definitions[static_cast<size_t>(chord / 12)].suffix) result[static_cast<size_t>(chord)][index++] = c;
        }
        return result;
    }();

    static std::string_view name(int chord) noexcept
    {
        return isValid(chord) ? std::string_view { names[static_cast<size_t>(chord)].data() }
                              : std::string_view { "--" };
    }

    static constexpr bool isValid(int chord) noexcept
    {
        return chord >= 0 && chord < chordCount;
    }

    static constexpr int rootOf(int chord) noexcept
    {
        return isValid(chord) ? chord % rootsPerQuality : -1;
    }

    static constexpr int qualityOf(int chord) noexcept
    {
        return isValid(chord) ? chord / rootsPerQuality : -1;
    }

    static constexpr bool isBasicMajorOrMinor(int chord) noexcept
    {
        return qualityOf(chord) == major || qualityOf(chord) == minor;
    }

    static constexpr Family familyOf(int chord) noexcept
    {
        return isValid(chord) ? definitions[static_cast<size_t>(qualityOf(chord))].family : Family::power;
    }

    static constexpr int toneCount(int chord) noexcept
    {
        return isValid(chord) ? definitions[static_cast<size_t>(qualityOf(chord))].toneCount : 0;
    }

    static constexpr int intervalAt(int chord, int tone) noexcept
    {
        return isValid(chord) && tone >= 0 && tone < toneCount(chord)
            ? definitions[static_cast<size_t>(qualityOf(chord))].intervals[static_cast<size_t>(tone)] : -1;
    }

    static constexpr bool containsPitch(int chord, int pitchClass) noexcept
    {
        if (!isValid(chord) || pitchClass < 0 || pitchClass >= 12)
            return false;
        for (int tone = 0; tone < toneCount(chord); ++tone)
            if ((rootOf(chord) + intervalAt(chord, tone)) % 12 == pitchClass)
                return true;
        return false;
    }

    static constexpr bool samePitchSet(int first, int second) noexcept
    {
        if (!isValid(first) || !isValid(second))
            return false;
        for (int pitch = 0; pitch < 12; ++pitch)
            if (containsPitch(first, pitch) != containsPitch(second, pitch))
                return false;
        return true;
    }

    ChordMatch match(const std::array<float, 12>& input, float rmsDb,
                     float sensitivityPercent, bool extendedChords = false) const noexcept
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

        const auto safeInput = [&input](int pitch)
        {
            const float value = input[static_cast<size_t>((pitch + 12) % 12)];
            return std::isfinite(value) ? std::max(0.0f, value) : 0.0f;
        };
        const float minimumDefiningRatio = 0.18f - 0.10f * sensitivity;
        const auto hasDefiningTones = [&](int chord)
        {
            const int root = rootOf(chord);
            const int quality = qualityOf(chord);
            const int supportInterval = quality == diminished ? 6 : 7;
            const float reference = std::max(safeInput(root),
                                              safeInput(root + supportInterval));
            const auto ratio = [&](int interval)
            {
                return safeInput(root + interval) / std::max(0.0001f, reference);
            };
            if (ratio(intervalAt(chord, 1)) < minimumDefiningRatio)
                return false;
            if (quality == sus2 || quality == sus4)
            {
                const float suspension = ratio(intervalAt(chord, 1));
                return ratio(0) >= minimumDefiningRatio
                    && ratio(7) >= minimumDefiningRatio
                    && ratio(3) < suspension * 0.50f
                    && ratio(4) < suspension * 0.50f;
            }
            if (quality == diminished)
                return ratio(0) >= minimumDefiningRatio
                    && ratio(6) >= minimumDefiningRatio
                    && ratio(7) < ratio(6) * 0.60f;
            return true;
        };

        const auto scoreChord = [&](int chord)
        {
            constexpr std::array<float, maximumTones> weights { 1.0f, 0.90f, 0.75f, 0.72f, 0.68f };
            float score = 0.0f;
            float weightSquares = 0.0f;
            for (int tone = 0; tone < toneCount(chord); ++tone)
            {
                const float weight = weights[static_cast<size_t>(tone)];
                const int pitch = (rootOf(chord) + intervalAt(chord, tone)) % 12;
                score += weight * chroma[static_cast<size_t>(pitch)];
                weightSquares += weight * weight;
            }
            for (int pitch = 0; pitch < 12; ++pitch)
                if (!containsPitch(chord, pitch))
                {
                    const int relative = (pitch - rootOf(chord) + 12) % 12;
                    // Third/fifth partials of the root and fifth naturally produce
                    // these weak pitch classes even in a two-note guitar voicing.
                    const bool powerHarmonic = familyOf(chord) == Family::power
                        && (relative == 2 || relative == 4 || relative == 11);
                    if (!powerHarmonic) score -= 0.12f * chroma[static_cast<size_t>(pitch)];
                }
            return score / (toneCount(chord) == 3 ? 1.5818028f
                                                   : std::sqrt(weightSquares));
        };

        const auto extensionForBase = [&](int baseChord)
        {
            const int quality = qualityOf(baseChord);
            if (quality != major && quality != minor)
                return -1;
            const int root = rootOf(baseChord);
            const float reference = std::max(safeInput(root), safeInput(root + 7));
            const auto ratio = [&](int interval)
            {
                return safeInput(root + interval) / std::max(0.0001f, reference);
            };
            const float flatSeventh = ratio(10);
            if (quality == major)
            {
                const float majorSeventh = ratio(11);
                if (flatSeventh >= 0.32f - 0.08f * sensitivity
                    && flatSeventh >= 0.50f * majorSeventh)
                    return dominant7 * rootsPerQuality + root;
                if (majorSeventh >= 0.55f - 0.08f * sensitivity
                    && majorSeventh >= 1.80f * flatSeventh)
                    return major7 * rootsPerQuality + root;
            }
            else if (flatSeventh >= 0.50f - 0.10f * sensitivity)
            {
                return minor7 * rootsPerQuality + root;
            }
            return -1;
        };

        // First determine a stable three-note harmonic core. Seventh chords are
        // classified afterwards, but only for that same root and major/minor
        // quality. This prevents an overtone from turning C into an unrelated
        // Em7 or a guitar inversion into a spurious extended chord.
        std::array<float, chordCount> scores {};
        scores.fill(-1000.0f);
        for (int chord = 0; chord < chordCount; ++chord)
        {
            const int quality = qualityOf(chord);
            if (quality == major || quality == minor
                || (extendedChords && (quality == sus2 || quality == sus4
                                       || quality == diminished)))
            {
                scores[static_cast<size_t>(chord)] = !extendedChords
                    || hasDefiningTones(chord)
                    ? scoreChord(chord) - (quality == sus2 || quality == sus4
                                           || quality == diminished ? 0.035f : 0.0f)
                    : -1000.0f;
                if (extendedChords && (quality == major || quality == minor)
                    && scores[static_cast<size_t>(chord)] > -999.0f)
                {
                    const int extension = extensionForBase(chord);
                    if (extension >= 0)
                    {
                        float extensionPenalty = qualityOf(extension) == minor7 ? 0.065f : 0.045f;
                        if (qualityOf(extension) == major7)
                        {
                            const int extensionRoot = rootOf(extension);
                            const float reference = std::max(safeInput(extensionRoot),
                                safeInput(extensionRoot + 7));
                            const float seventhRatio = safeInput(extensionRoot + 11)
                                                     / std::max(0.0001f, reference);
                            extensionPenalty = 0.105f - 0.085f * std::clamp(
                                (seventhRatio - 0.35f) / 0.50f, 0.0f, 1.0f);
                        }
                        scores[static_cast<size_t>(extension)] = scoreChord(extension)
                            - extensionPenalty;
                    }
                }
            }
        }

        if (extendedChords)
            for (int chord = augmented * rootsPerQuality; chord < chordCount; ++chord)
            {
                const auto& definition = definitions[static_cast<size_t>(qualityOf(chord))];
                float strongest = 0.0f;
                for (int pitch = 0; pitch < 12; ++pitch) strongest = std::max(strongest, safeInput(pitch));
                bool supported = strongest > 0.0f;
                for (int tone = 0; tone < definition.toneCount; ++tone)
                    supported &= safeInput(rootOf(chord) + definition.intervals[static_cast<size_t>(tone)])
                               >= definition.minimumToneRatio * strongest;
                if (definition.family == Family::power)
                    for (int pitch = 0; pitch < 12; ++pitch)
                        if (!containsPitch(chord, pitch))
                        {
                            const int relative = (pitch - rootOf(chord) + 12) % 12;
                            supported &= safeInput(pitch) < (relative == 2 ? 0.35f : 0.25f) * strongest;
                        }
                if (supported) scores[static_cast<size_t>(chord)] = scoreChord(chord) - definition.penalty;
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

        if (isBasicMajorOrMinor(best) && toneCount(second) == 4
            && rootOf(best) == rootOf(second))
        {
            int unrelatedSecond = -1;
            for (int chord = 0; chord < chordCount; ++chord)
                if (chord != best && chord != second
                    && (unrelatedSecond < 0
                        || scores[static_cast<size_t>(chord)]
                           > scores[static_cast<size_t>(std::max(0, unrelatedSecond))]))
                    unrelatedSecond = chord;
            if (unrelatedSecond >= 0)
                second = unrelatedSecond;
        }

        best = std::clamp(best, 0, chordCount - 1);
        second = std::clamp(second, 0, chordCount - 1);
        const float bestScore = scores[static_cast<size_t>(best)];
        const bool equivalent = samePitchSet(best, second);
        const float margin = bestScore - scores[static_cast<size_t>(second)];
        const int root = rootOf(best);
        const int bestQuality = qualityOf(best);
        const int supportInterval = bestQuality == diminished ? 6 : 7;
        const float referenceTone = std::max(safeInput(root), safeInput(root + supportInterval));
        const auto ratio = [&safeInput, root, referenceTone](int interval)
        {
            return safeInput(root + interval) / std::max(0.0001f, referenceTone);
        };

        const float minimumScore = 0.50f - 0.12f * sensitivity;
        const float minimumMargin = !extendedChords
            ? 0.035f - 0.030f * sensitivity
            : (bestQuality == sus2 || bestQuality == sus4
                                  || bestQuality == diminished
                ? 0.0f
                : (toneCount(best) == 4 ? 0.0020f - 0.0020f * sensitivity
                                        : 0.0040f - 0.0030f * sensitivity));
        if (bestScore < minimumScore || (margin < minimumMargin && !equivalent)
            || (!extendedChords && !hasDefiningTones(best)))
            return { -1, best, 0.0f, bestScore, margin };

        const float qualityScore = (bestScore - minimumScore)
                                 / std::max(0.001f, 1.0f - minimumScore);
        const float separation = (margin - minimumMargin) / 0.25f;
        const float evidence = (ratio(intervalAt(best, 1)) - minimumDefiningRatio) / 0.60f;
        const float confidence = 100.0f * std::clamp(0.55f * qualityScore
                                                     + 0.25f * separation
                                                     + 0.20f * evidence,
                                                     0.0f, 1.0f);
        return { best, second, confidence, bestScore, margin };
    }

private:
    static float bounded(float value, float low, float high, float fallback) noexcept
    {
        return std::isfinite(value) ? std::clamp(value, low, high) : fallback;
    }
};
