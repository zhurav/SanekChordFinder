#pragma once

#include "KeyDetector.h"

// Musical preferences, not learned probabilities. Only equivalent pitch sets
// compete: context must never invent a missing third, seventh or suspension.
struct ChordContext
{
    int bassNote = -1;
    int key = -1;
    float keyConfidence = 0.0f;
    int previous = -1;
    int next = -1;
    int following = -1;
};

struct ChordResolution
{
    int chord = -1, alternative = -1;
    float margin = 0.0f;
    bool ambiguous = false;
};

class ChordContextResolver
{
public:
    static int candidateCount(int chord) noexcept
    {
        if (!ChordMatcher::isValid(chord)) return 0;
        int count = 0;
        for (int id = 0; id < ChordMatcher::chordCount; ++id)
            count += ChordMatcher::samePitchSet(chord, id) ? 1 : 0;
        return count;
    }

    static ChordResolution resolve(int input, const ChordContext& context) noexcept
    {
        ChordResolution result { input };
        if (!ChordMatcher::isValid(input)) return result;
        float bestScore = score(input, context), secondScore = -100.0f;
        int best = input, second = -1;
        for (int id = 0; id < ChordMatcher::chordCount; ++id)
        {
            if (id == input || !ChordMatcher::samePitchSet(input, id)) continue;
            const float value = score(id, context);
            if (value > bestScore + 0.0001f)
            {
                second = best; secondScore = bestScore;
                best = id; bestScore = value;
            }
            else if (value > secondScore) { second = id; secondScore = value; }
        }
        if (second < 0) return result;
        result.margin = std::max(0.0f, bestScore - secondScore);
        result.ambiguous = result.margin < 0.30f;
        result.chord = result.ambiguous ? input : best;
        result.alternative = result.chord == best ? second : best;
        return result;
    }

private:
    static float movement(int from, int to) noexcept
    {
        if (!ChordMatcher::isValid(from) || !ChordMatcher::isValid(to)) return 0.0f;
        const int distance = (ChordMatcher::rootOf(to) - ChordMatcher::rootOf(from) + 12) % 12;
        const auto fromFamily = ChordMatcher::familyOf(from);
        const auto toFamily = ChordMatcher::familyOf(to);
        // Root movement is deliberately weaker than a supported bass.
        float value = distance == 5 ? 0.25f : 0.0f;
        if (distance == 7 && fromFamily == ChordMatcher::Family::major
                          && toFamily == ChordMatcher::Family::major) value += 0.60f;
        const int quality = ChordMatcher::qualityOf(to);
        if (distance == 5 && fromFamily == ChordMatcher::Family::minor
            && (quality == ChordMatcher::dominant7 || quality == ChordMatcher::dominant9))
            value += 0.90f; // ii -> V, including a secondary dominant.
        if (distance == 1 && fromFamily == ChordMatcher::Family::diminished)
            value += 0.50f;
        return value;
    }

    static float score(int chord, const ChordContext& context) noexcept
    {
        const int root = ChordMatcher::rootOf(chord);
        float value = context.bassNote >= 0 && context.bassNote <= 127
                   && root == context.bassNote % 12 ? 4.0f : 0.0f;
        if (KeyDetector::isValid(context.key) && std::isfinite(context.keyConfidence)
            && context.keyConfidence >= 45.0f)
        {
            const int degree = (root - KeyDetector::tonicOf(context.key) + 12) % 12;
            const bool minorKey = KeyDetector::isMinor(context.key);
            const auto family = ChordMatcher::familyOf(chord);
            const bool tonic = degree == 0 && family == (minorKey ? ChordMatcher::Family::minor
                                                                 : ChordMatcher::Family::major);
            if (tonic) value += 0.35f * std::clamp(context.keyConfidence / 100.0f, 0.0f, 1.0f);
            value += 0.18f * KeyDetector::harmonicFit(context.key, chord)
                   * std::clamp(context.keyConfidence / 100.0f, 0.0f, 1.0f);
        }
        value += movement(context.previous, chord) + movement(chord, context.next);
        // A following resolution strengthens a genuine ii-V-I, not any root fifth.
        if (ChordMatcher::isValid(context.next) && ChordMatcher::isValid(context.following)
            && (ChordMatcher::rootOf(context.following) - ChordMatcher::rootOf(context.next) + 12) % 12 == 5
            && movement(chord, context.next) > 1.0f) value += 0.30f;
        return value;
    }
};

// Causal, allocation-free history. Ambiguous names cannot teach the key tracker
// their own interpretation. Duration is audible time, capped to a recent window.
class LiveChordContext
{
public:
    void reset() noexcept { *this = {}; }
    void silence(double seconds) noexcept
    {
        if (std::isfinite(seconds) && seconds > 0.0) silentSeconds += seconds;
        if (silentSeconds >= 30.0) reset();
    }
    ChordResolution resolve(int chord, int bass) const noexcept
    {
        if (ChordMatcher::samePitchSet(chord, current))
        {
            auto result = ChordContextResolver::resolve(current, { bass, key.key, key.confidence, previous });
            result.chord = current; // Do not rename a held chord frame by frame.
            if (result.alternative == current) result.alternative = chord != current ? chord : -1;
            return result;
        }
        return ChordContextResolver::resolve(chord, { bass, key.key, key.confidence, current });
    }
    void observe(int chord, float confidence, double seconds) noexcept
    {
        if (!ChordMatcher::isValid(chord) || !std::isfinite(seconds) || seconds <= 0.0) return;
        silentSeconds = 0.0;
        if (!ChordMatcher::samePitchSet(chord, current))
        {
            previous = current;
            current = chord;
            if (count == history.size())
            {
                for (size_t i = 1; i < count; ++i) history[i - 1] = history[i];
                --count;
            }
            history[count++] = { ChordContextResolver::candidateCount(chord) == 1 ? chord : -1,
                                 confidence, 0.0 };
            key = KeyDetector().analyse(history.data(), count);
        }
        if (count > 0) history[count - 1].durationSeconds += seconds;
        double age = 0.0;
        for (size_t i = count; i > 0; --i)
        {
            auto& entry = history[i - 1];
            const double duration = entry.durationSeconds;
            entry.durationSeconds = std::min(duration, std::max(0.0, 30.0 - age));
            age += duration;
        }
    }
private:
    std::array<KeyObservation, 32> history {};
    size_t count = 0;
    int current = -1, previous = -1;
    KeyMatch key;
    double silentSeconds = 0.0;
};
