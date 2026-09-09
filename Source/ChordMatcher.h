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
        qualityCount
    };

    static constexpr int rootsPerQuality = 12;
    static constexpr int chordCount = rootsPerQuality * qualityCount;

    // The first 24 IDs intentionally stay compatible with versions 0.1-0.3.
    static constexpr std::array<std::string_view, chordCount> names {
        "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B",
        "Cm", "C#m", "Dm", "D#m", "Em", "Fm", "F#m", "Gm", "G#m", "Am", "A#m", "Bm",
        "C7", "C#7", "D7", "D#7", "E7", "F7", "F#7", "G7", "G#7", "A7", "A#7", "B7",
        "Cmaj7", "C#maj7", "Dmaj7", "D#maj7", "Emaj7", "Fmaj7", "F#maj7", "Gmaj7", "G#maj7", "Amaj7", "A#maj7", "Bmaj7",
        "Cm7", "C#m7", "Dm7", "D#m7", "Em7", "Fm7", "F#m7", "Gm7", "G#m7", "Am7", "A#m7", "Bm7",
        "Csus2", "C#sus2", "Dsus2", "D#sus2", "Esus2", "Fsus2", "F#sus2", "Gsus2", "G#sus2", "Asus2", "A#sus2", "Bsus2",
        "Csus4", "C#sus4", "Dsus4", "D#sus4", "Esus4", "Fsus4", "F#sus4", "Gsus4", "G#sus4", "Asus4", "A#sus4", "Bsus4",
        "Cdim", "C#dim", "Ddim", "D#dim", "Edim", "Fdim", "F#dim", "Gdim", "G#dim", "Adim", "A#dim", "Bdim"
    };

    static std::string_view name(int chord) noexcept
    {
        return isValid(chord) ? names[static_cast<size_t>(chord)]
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

    static constexpr int toneCount(int chord) noexcept
    {
        const int quality = qualityOf(chord);
        return quality == dominant7 || quality == major7 || quality == minor7 ? 4 : 3;
    }

    static constexpr int intervalAt(int chord, int tone) noexcept
    {
        const int quality = qualityOf(chord);
        if (tone == 0)
            return 0;
        if (tone == 1)
        {
            if (quality == minor || quality == minor7 || quality == diminished)
                return 3;
            if (quality == sus2)
                return 2;
            if (quality == sus4)
                return 5;
            return 4;
        }
        if (tone == 2)
            return quality == diminished ? 6 : 7;
        if (tone == 3)
            return quality == major7 ? 11 : 10;
        return -1;
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
            constexpr std::array<float, 4> weights { 1.0f, 0.90f, 0.75f, 0.72f };
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
                    score -= 0.12f * chroma[static_cast<size_t>(pitch)];
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
        if (bestScore < minimumScore || margin < minimumMargin
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
