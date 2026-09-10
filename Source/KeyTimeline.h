#pragma once
#include "KeyDetector.h"

struct KeyChange { size_t observation = 0; int key = -1; };
struct KeyTimelineResult { KeyMatch current; int initialKey = -1; std::vector<KeyChange> changes; };

// Recomputed from captured durations, so reopening the editor yields the same result.
class KeyTimeline
{
public:
    static KeyTimelineResult analyse(const std::vector<KeyObservation>& observations)
    {
        KeyTimelineResult result;
        int pending = -1, confirmations = 0;
        double pendingDuration = 0.0;
        std::array<bool, 12> pendingRoots {};
        for (size_t end = 0; end < observations.size(); ++end)
        {
            std::vector<KeyObservation> window;
            double remaining = 16.0;
            for (size_t i = end + 1; i > 0 && remaining > 0.0;)
            {
                auto observation = observations[--i];
                if (!std::isfinite(observation.durationSeconds) || observation.durationSeconds <= 0.0) continue;
                observation.durationSeconds = std::min(remaining, observation.durationSeconds);
                remaining -= observation.durationSeconds;
                window.push_back(observation);
            }
            std::reverse(window.begin(), window.end());
            const auto match = KeyDetector().analyse(window);
            if (result.current.key < 0)
            {
                result.current = match;
                if (match.key >= 0) result.initialKey = match.key;
            }
            if (match.key < 0 || match.key == result.current.key)
            {
                if (match.key >= 0) result.current = match;
                pending = -1; confirmations = 0; pendingDuration = 0.0; pendingRoots.fill(false);
                continue;
            }
            if (match.confidence < 45.0f || match.margin < 0.10f) continue;
            if (pending != match.key)
            {
                pending = match.key; confirmations = 0; pendingDuration = 0.0; pendingRoots.fill(false);
            }
            ++confirmations;
            pendingDuration += observations[end].durationSeconds;
            if (ChordMatcher::isValid(observations[end].chord))
                pendingRoots[static_cast<size_t>(ChordMatcher::rootOf(observations[end].chord))] = true;
            if (confirmations >= 3 && pendingDuration >= 6.0
                && std::count(pendingRoots.begin(), pendingRoots.end(), true) >= 2)
            {
                result.current = match;
                result.changes.push_back({ end, match.key }); // Position of confirmation, not guessed onset.
                pending = -1; confirmations = 0; pendingDuration = 0.0; pendingRoots.fill(false);
            }
        }
        return result;
    }
};
