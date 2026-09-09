#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

struct TempoState
{
    float exportBpm = 0.0f;
    float estimatedBpm = 0.0f;
    float estimatedConfidence = 0.0f;
    float bpm = 0.0f;
    float confidence = 0.0f;
    bool locked = false;
    int bar = -1;
    int beat = -1;
    int beatsPerBar = 4;
    float beatPhase = 0.0f;
    double originSeconds = -1.0;
    double currentSeconds = 0.0;
};

class TempoTracker
{
public:
    void prepare(double newSampleRate) noexcept
    {
        sampleRate = std::isfinite(newSampleRate) && newSampleRate > 0.0
                   ? newSampleRate : 48000.0;
        envelopeFrameSize = std::clamp(static_cast<int>(std::lround(sampleRate / 100.0)),
                                       256, 2048);
        envelopeRate = sampleRate / envelopeFrameSize;
        estimateIntervalFrames = std::max(1, static_cast<int>(std::lround(0.5 * envelopeRate)));
        reset();
    }

    void reset() noexcept
    {
        onsetHistory.fill(0.0f);
        dbHistory.fill(-120.0f);
        writePosition = 0;
        onsetFrameCount = 0;
        samplesInFrame = 0;
        processedSamples = 0;
        squareSum = 0.0;
        bpm = 0.0f;
        confidence = 0.0f;
        locked = false;
        pendingBpm = 0.0f;
        pendingEstimates = 0;
        exportBpm = 0.0f;
        estimatedBpm = 0.0f;
        estimatedConfidence = 0.0f;
        lastOnsetFrame = 0;
        originSeconds = -1.0;
    }

    void setBeatsPerBar(int value) noexcept
    {
        beatsPerBar = value == 3 || value == 6 ? value : 4;
    }

    void processSample(float sample) noexcept
    {
        const float safe = std::isfinite(sample) ? sample : 0.0f;
        squareSum += static_cast<double>(safe) * safe;
        ++samplesInFrame;
        ++processedSamples;
        if (samplesInFrame >= envelopeFrameSize)
        {
            processEnvelopeFrame();
            samplesInFrame = 0;
            squareSum = 0.0;
        }
    }

    void markNewBar() noexcept
    {
        originSeconds = getCurrentSeconds();
    }

    TempoState getState() const noexcept
    {
        TempoState state;
        state.exportBpm = exportBpm;
        state.estimatedBpm = estimatedBpm;
        state.estimatedConfidence = estimatedConfidence;
        state.bpm = bpm;
        state.confidence = confidence;
        state.locked = locked;
        state.beatsPerBar = beatsPerBar;
        state.originSeconds = originSeconds;
        state.currentSeconds = getCurrentSeconds();
        if (!locked || bpm <= 0.0f || originSeconds < 0.0)
            return state;

        const double exactBeat = std::max(0.0, (state.currentSeconds - originSeconds)
                                              * static_cast<double>(bpm) / 60.0);
        const auto beatIndex = static_cast<std::int64_t>(std::floor(exactBeat));
        state.beatPhase = static_cast<float>(exactBeat - std::floor(exactBeat));
        state.bar = static_cast<int>(beatIndex / beatsPerBar) + 1;
        state.beat = static_cast<int>(beatIndex % beatsPerBar) + 1;
        return state;
    }

    double attackBefore(double seconds, double searchSeconds = 0.45) const noexcept
    {
        if (!std::isfinite(seconds) || seconds < 0.0) return seconds;
        float strongest = 3.0f;
        double attack = seconds;
        const int available = static_cast<int>(std::min<std::uint64_t>(onsetFrameCount, historyCapacity));
        for (int age = 0; age < available; ++age)
        {
            const double time = (static_cast<double>(onsetFrameCount) - age - 1.0) / envelopeRate;
            if (time > seconds) continue;
            if (time < seconds - searchSeconds) break;
            const float value = onsetAgo(age);
            if (value >= strongest) { strongest = value; attack = std::max(0.0, time - 0.01); }
        }
        return attack;
    }

private:
    static constexpr int historyCapacity = 2048;
    static constexpr int dbHistorySize = 8;

    double getCurrentSeconds() const noexcept
    {
        return static_cast<double>(processedSamples) / sampleRate;
    }

    float onsetAgo(int age) const noexcept
    {
        int index = writePosition - 1 - age;
        while (index < 0)
            index += historyCapacity;
        return onsetHistory[static_cast<size_t>(index % historyCapacity)];
    }

    void processEnvelopeFrame() noexcept
    {
        const float rms = static_cast<float>(std::sqrt(squareSum / envelopeFrameSize));
        const float db = 20.0f * std::log10(std::max(rms, 1.0e-6f));
        const int dbIndex = static_cast<int>(onsetFrameCount % dbHistorySize);
        dbHistory[static_cast<size_t>(dbIndex)] = db;
        const auto dbAgo = [this](int age)
        {
            int index = static_cast<int>(onsetFrameCount % dbHistorySize) - age;
            while (index < 0)
                index += dbHistorySize;
            return dbHistory[static_cast<size_t>(index % dbHistorySize)];
        };
        const float smoothedDb = 0.50f * dbAgo(0) + 0.30f * dbAgo(1) + 0.20f * dbAgo(2);
        const float previousSmoothed = 0.50f * dbAgo(4) + 0.30f * dbAgo(5)
                                     + 0.20f * dbAgo(6);
        const float onset = db > -70.0f ? std::clamp(smoothedDb - previousSmoothed, 0.0f, 20.0f)
                                        : 0.0f;
        onsetHistory[static_cast<size_t>(writePosition)] = onset;
        writePosition = (writePosition + 1) % historyCapacity;
        ++onsetFrameCount;
        if (onset > 1.0f)
            lastOnsetFrame = onsetFrameCount;

        if (originSeconds < 0.0 && onset > 8.0f)
            originSeconds = std::max(0.0, getCurrentSeconds()
                                          - static_cast<double>(envelopeFrameSize) / sampleRate);

        const auto minimumFrames = static_cast<std::uint64_t>(std::ceil(3.0 * envelopeRate));
        if (onsetFrameCount >= minimumFrames
            && onsetFrameCount % static_cast<std::uint64_t>(estimateIntervalFrames) == 0)
        {
            updateTempoEstimate();
            updateExportTempo();
        }
    }

    void updateExportTempo() noexcept
    {
        if (!locked) return;
        if (exportBpm <= 0.0f) exportBpm = bpm;
        if (onsetFrameCount < static_cast<std::uint64_t>(8.0 * envelopeRate)
            || onsetFrameCount - lastOnsetFrame > static_cast<std::uint64_t>(2.0 * envelopeRate)) return;
        const int available = static_cast<int>(std::min<std::uint64_t>(onsetFrameCount, historyCapacity));
        float mean = 0.0f;
        for (int age = 0; age < available; ++age) mean += onsetAgo(age);
        mean /= static_cast<float>(available);
        const int minimum = static_cast<int>(std::lround(60.0 * envelopeRate / 160.0));
        const int maximum = static_cast<int>(std::lround(60.0 * envelopeRate / 80.0));
        int best = minimum;
        float score = -1.0f;
        for (int lag = minimum; lag <= maximum; ++lag)
        {
            const float candidate = correlationAtLag(lag, available, mean);
            if (candidate > score) { score = candidate; best = lag; }
        }
        if (score < 0.1f) return;
        float lag = static_cast<float>(best);
        if (best > minimum && best < maximum)
        {
            const float left = correlationAtLag(best - 1, available, mean);
            const float right = correlationAtLag(best + 1, available, mean);
            const float denominator = left - 2.0f * score + right;
            if (std::abs(denominator) > 1.0e-6f)
                lag += std::clamp(0.5f * (left - right) / denominator, -0.5f, 0.5f);
        }
        exportBpm = static_cast<float>(60.0 * envelopeRate / lag);
    }

    float correlationAtLag(int lag, int available, float mean) const noexcept
    {
        double product = 0.0, firstEnergy = 0.0, secondEnergy = 0.0;
        for (int age = 0; age < available - lag; ++age)
        {
            const float first = onsetAgo(age) - mean;
            const float second = onsetAgo(age + lag) - mean;
            product += static_cast<double>(first) * second;
            firstEnergy += static_cast<double>(first) * first;
            secondEnergy += static_cast<double>(second) * second;
        }
        const double denominator = std::sqrt(firstEnergy * secondEnergy);
        return denominator > 1.0e-12 ? static_cast<float>(product / denominator) : 0.0f;
    }

    void updateTempoEstimate() noexcept
    {
        const int available = std::min({ static_cast<int>(onsetFrameCount), historyCapacity,
                                         static_cast<int>(std::lround(6.0 * envelopeRate)) });
        if (onsetFrameCount - lastOnsetFrame > static_cast<std::uint64_t>(2.0 * envelopeRate))
        {
            estimatedBpm = 0.0f;
            estimatedConfidence = 0.0f;
            pendingEstimates = 0;
            return;
        }
        float mean = 0.0f;
        for (int age = 0; age < available; ++age)
            mean += onsetAgo(age);
        mean /= static_cast<float>(std::max(1, available));

        const int minimumLag = std::max(2, static_cast<int>(std::lround(60.0 * envelopeRate / 160.0)));
        const int maximumLag = std::min(available / 2,
            static_cast<int>(std::lround(60.0 * envelopeRate / 80.0)));
        int bestLag = minimumLag;
        float bestCorrelation = -1.0f;
        for (int lag = minimumLag; lag <= maximumLag; ++lag)
        {
            const float correlation = correlationAtLag(lag, available, mean);
            if (correlation > bestCorrelation)
            {
                bestCorrelation = correlation;
                bestLag = lag;
            }
        }

        float refinedLag = static_cast<float>(bestLag);
        if (bestLag > minimumLag && bestLag < maximumLag)
        {
            const float left = correlationAtLag(bestLag - 1, available, mean);
            const float centre = correlationAtLag(bestLag, available, mean);
            const float right = correlationAtLag(bestLag + 1, available, mean);
            const float denominator = left - 2.0f * centre + right;
            if (std::abs(denominator) > 1.0e-6f)
                refinedLag += std::clamp(0.5f * (left - right) / denominator, -0.5f, 0.5f);
        }
        const float candidate = static_cast<float>(60.0 * envelopeRate / refinedLag);
        const float candidateConfidence = 100.0f
            * std::clamp((bestCorrelation - 0.08f) / 0.25f, 0.0f, 1.0f);

        if (bestCorrelation < 0.14f)
        {
            estimatedBpm = 0.0f;
            estimatedConfidence = 0.0f;
            pendingEstimates = 0;
            return;
        }
        // Publish the audio estimate before grid confirmation, and keep analysing
        // after it. The fixed bar grid must not move already recorded chords.
        estimatedBpm = candidate;
        estimatedConfidence = candidateConfidence;

        if (!locked)
        {
            if (pendingEstimates == 0 || std::abs(candidate - pendingBpm) > 5.0f)
            {
                pendingBpm = candidate;
                pendingEstimates = 1;
            }
            else
            {
                pendingBpm = 0.65f * pendingBpm + 0.35f * candidate;
                ++pendingEstimates;
            }
            if (pendingEstimates >= 3)
            {
                bpm = pendingBpm;
                confidence = candidateConfidence;
                locked = true;
                if (originSeconds < 0.0)
                    originSeconds = 0.0;
            }
            return;
        }

        // A locked estimate is deliberately immutable. Real musicians naturally
        // push and pull individual chords; following those variations would make
        // already assigned bar positions drift. Stopping and starting Listening
        // begins a fresh measurement when a different tempo is needed.
    }

    std::array<float, historyCapacity> onsetHistory {};
    std::array<float, dbHistorySize> dbHistory {};
    double sampleRate = 48000.0;
    double squareSum = 0.0;
    double envelopeRate = 100.0;
    double originSeconds = -1.0;
    std::int64_t processedSamples = 0;
    std::uint64_t onsetFrameCount = 0;
    std::uint64_t lastOnsetFrame = 0;
    int envelopeFrameSize = 480;
    int estimateIntervalFrames = 50;
    int samplesInFrame = 0;
    int writePosition = 0;
    int beatsPerBar = 4;
    int pendingEstimates = 0;
    float pendingBpm = 0.0f;
    float exportBpm = 0.0f;
    float estimatedBpm = 0.0f;
    float estimatedConfidence = 0.0f;
    float bpm = 0.0f;
    float confidence = 0.0f;
    bool locked = false;
};
