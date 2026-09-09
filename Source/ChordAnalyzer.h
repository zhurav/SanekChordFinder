#pragma once

#include <juce_dsp/juce_dsp.h>
#include "ChordMatcher.h"
#include "SpectrumChroma.h"
#include "TempoTracker.h"

struct AnalysisTiming
{
    double seconds = -1.0;
    double ppq = -1.0;
    double bpm = 120.0;
    int bar = -1;
    float beat = -1.0f;
};

struct ChordFrame
{
    int chord = -1;
    int alternative = -1;
    float confidence = 0.0f;
    bool changed = false;
    AnalysisTiming timing;
    std::array<float, 12> chroma {};
};

class ChordAnalyzer
{
public:
    static constexpr int fftOrder = 13;
    static constexpr int fftSize = 1 << fftOrder;
    static constexpr int hopSize = fftSize / 4;

    ChordAnalyzer()
        : fft(fftOrder), window(static_cast<size_t>(fftSize),
                                juce::dsp::WindowingFunction<float>::hann, false)
    {
    }

    void prepare(double newSampleRate) noexcept
    {
        sampleRate = std::isfinite(newSampleRate) && newSampleRate > 0.0
                   ? newSampleRate : 48000.0;
        smoothingAlpha = static_cast<float>(1.0 - std::exp(-hopSize / (sampleRate * 0.12)));
        stableFramesRequired = std::max(6, static_cast<int>(std::ceil(0.36 * sampleRate / hopSize)));
        extendedStableFramesRequired = std::max(
            9, static_cast<int>(std::ceil(0.75 * sampleRate / hopSize)));
        noChordFramesRequired = std::max(12, static_cast<int>(std::ceil(sampleRate / hopSize)));
        tempoTracker.prepare(sampleRate);
        reset();
    }

    void reset() noexcept
    {
        ring.fill(0.0f);
        fftData.fill(0.0f);
        writePosition = 0;
        filled = 0;
        samplesSinceAnalysis = 0;
        hasAnalysed = false;
        pendingChord = -1;
        pendingFrames = 0;
        pendingTiming = {};
        noChordFrames = 0;
        stableChord = -1;
        smoothedChroma.fill(0.0f);
        rememberedChordByRoot.fill(-1);
        hasSmoothedChroma = false;
        tempoTracker.reset();
    }

    void setBeatsPerBar(int beats) noexcept { tempoTracker.setBeatsPerBar(beats); }
    void setExtendedChords(bool enabled) noexcept { extendedChords = enabled; }
    void markNewBar() noexcept { tempoTracker.markNewBar(); }
    TempoState getTempoState() const noexcept { return tempoTracker.getState(); }

    template <typename Callback>
    void process(const juce::AudioBuffer<float>& buffer, float sensitivity,
                 const AnalysisTiming& blockTiming, Callback&& callback) noexcept
    {
        const int channels = buffer.getNumChannels();
        if (channels <= 0)
            return;

        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
        {
            float mono = 0.0f;
            for (int channel = 0; channel < channels; ++channel)
                mono += buffer.getReadPointer(channel)[sample];
            mono /= static_cast<float>(channels);
            tempoTracker.processSample(mono);
            ring[static_cast<size_t>(writePosition)] = std::isfinite(mono) ? mono : 0.0f;
            writePosition = (writePosition + 1) % fftSize;
            filled = std::min(fftSize, filled + 1);
            ++samplesSinceAnalysis;

            if (filled == fftSize && (!hasAnalysed || samplesSinceAnalysis >= hopSize))
            {
                hasAnalysed = true;
                samplesSinceAnalysis = 0;
                auto timing = blockTiming;
                const double centreOffsetSeconds = (static_cast<double>(sample) - fftSize * 0.5)
                                                 / sampleRate;
                if (timing.seconds >= 0.0)
                    timing.seconds = std::max(0.0, timing.seconds + centreOffsetSeconds);
                if (timing.ppq >= 0.0 && timing.bpm > 0.0)
                    timing.ppq = std::max(0.0, timing.ppq + centreOffsetSeconds * timing.bpm / 60.0);
                callback(analyse(sensitivity, timing));
            }
        }
    }

private:
    ChordFrame analyse(float sensitivity, const AnalysisTiming& timing) noexcept
    {
        fftData.fill(0.0f);
        double squareSum = 0.0;
        for (int i = 0; i < fftSize; ++i)
        {
            const float sample = ring[static_cast<size_t>((writePosition + i) % fftSize)];
            fftData[static_cast<size_t>(i)] = sample;
            squareSum += static_cast<double>(sample) * sample;
        }
        const float rms = static_cast<float>(std::sqrt(squareSum / fftSize));
        const float rmsDb = juce::Decibels::gainToDecibels(rms, -120.0f);

        window.multiplyWithWindowingTable(fftData.data(), static_cast<size_t>(fftSize));
        fft.performFrequencyOnlyForwardTransform(fftData.data(), true);

        const auto instantaneousChroma = SpectrumChroma::convert(fftData.data(), fftSize / 2 + 1,
                                                                 fftSize, sampleRate);
        if (!hasSmoothedChroma)
        {
            smoothedChroma = instantaneousChroma;
            hasSmoothedChroma = true;
        }
        else
        {
            for (size_t i = 0; i < smoothedChroma.size(); ++i)
                smoothedChroma[i] += smoothingAlpha
                                  * (instantaneousChroma[i] - smoothedChroma[i]);
        }

        const auto raw = matcher.match(smoothedChroma, rmsDb, sensitivity, extendedChords);
        const int candidateChord = applyHarmonicMemory(raw, smoothedChroma);
        const int candidateAlternative = candidateChord != raw.chord && raw.chord >= 0
                                       ? raw.chord : raw.alternative;
        const float candidateConfidence = candidateChord == raw.chord ? raw.confidence
                                        : (candidateChord >= 0 ? 42.0f : 0.0f);
        bool changed = false;
        if (candidateChord < 0)
        {
            pendingChord = -1;
            pendingFrames = 0;
            if (++noChordFrames >= noChordFramesRequired && stableChord != -1)
            {
                stableChord = -1;
                changed = true;
            }
        }
        else
        {
            noChordFrames = 0;
            if (candidateChord == pendingChord
                || (ChordMatcher::samePitchSet(candidateChord, pendingChord)
                    && (ChordMatcher::qualityOf(candidateChord) == ChordMatcher::sus2
                        || ChordMatcher::qualityOf(candidateChord) == ChordMatcher::sus4
                        || ChordMatcher::qualityOf(candidateChord) == ChordMatcher::diminished)))
                ++pendingFrames;
            else
            {
                pendingChord = candidateChord;
                pendingFrames = 1;
                pendingTiming = timing;
            }
            auto resultTiming = timing;
            const int requiredFrames = ChordMatcher::isBasicMajorOrMinor(pendingChord)
                                     ? stableFramesRequired : extendedStableFramesRequired;
            if (pendingFrames >= requiredFrames && stableChord != pendingChord)
            {
                stableChord = pendingChord;
                changed = true;
                resultTiming = pendingTiming;
                if (raw.chord == stableChord
                    && ChordMatcher::isBasicMajorOrMinor(stableChord))
                    rememberedChordByRoot[static_cast<size_t>(ChordMatcher::rootOf(stableChord))]
                        = stableChord;
            }

            const float displayConfidence = candidateChord == stableChord ? candidateConfidence
                                                        : std::min(candidateConfidence, 35.0f);
            return { stableChord, candidateAlternative, displayConfidence, changed, resultTiming,
                     smoothedChroma };
        }

        const float displayConfidence = candidateChord == stableChord ? candidateConfidence
                                                    : std::min(candidateConfidence, 35.0f);
        return { stableChord, candidateAlternative, displayConfidence, changed, timing,
                 smoothedChroma };
    }

    int applyHarmonicMemory(const ChordMatch& raw,
                            const std::array<float, 12>& chroma) const noexcept
    {
        if (raw.chord >= 0)
        {
            // Extended chords contain their own defining tone, so never replace
            // them with a previously heard triad of the same root.
            if (!ChordMatcher::isBasicMajorOrMinor(raw.chord))
                return raw.chord;

            const int root = ChordMatcher::rootOf(raw.chord);
            const int remembered = rememberedChordByRoot[static_cast<size_t>(root)];
            if (remembered >= 0 && remembered != raw.chord)
            {
                const int newThird = (root + ChordMatcher::intervalAt(raw.chord, 1)) % 12;
                const int oldThird = (root + ChordMatcher::intervalAt(remembered, 1)) % 12;
                const int fifth = (root + 7) % 12;
                const float reference = std::max(chroma[static_cast<size_t>(root)],
                                                  chroma[static_cast<size_t>(fifth)]);
                const float newEvidence = chroma[static_cast<size_t>(newThird)];
                const float oldEvidence = chroma[static_cast<size_t>(oldThird)];
                // Do not flip major/minor because of a weak transient overtone.
                if (newEvidence < 0.28f * reference || newEvidence < 1.75f * oldEvidence)
                    return remembered;
            }
            return raw.chord;
        }

        // A real guitar voicing sometimes leaves only the root and fifth clearly
        // audible. Reuse an earlier reliable quality for that root instead of
        // inventing major/minor from noise.
        int bestRoot = 0, secondRoot = 1;
        std::array<float, 12> rootScores {};
        for (int root = 0; root < 12; ++root)
            rootScores[static_cast<size_t>(root)] = chroma[static_cast<size_t>(root)]
                                                  + 0.65f * chroma[static_cast<size_t>((root + 7) % 12)];
        if (rootScores[1] > rootScores[0])
            std::swap(bestRoot, secondRoot);
        for (int root = 2; root < 12; ++root)
        {
            if (rootScores[static_cast<size_t>(root)] > rootScores[static_cast<size_t>(bestRoot)])
            {
                secondRoot = bestRoot;
                bestRoot = root;
            }
            else if (rootScores[static_cast<size_t>(root)] > rootScores[static_cast<size_t>(secondRoot)])
            {
                secondRoot = root;
            }
        }
        const int remembered = rememberedChordByRoot[static_cast<size_t>(bestRoot)];
        if (remembered < 0 || rootScores[static_cast<size_t>(bestRoot)] < 0.70f
            || rootScores[static_cast<size_t>(bestRoot)]
                 - rootScores[static_cast<size_t>(secondRoot)] < 0.20f)
            return -1;
        const int fifth = (bestRoot + 7) % 12;
        const float reference = std::max(chroma[static_cast<size_t>(bestRoot)],
                                          chroma[static_cast<size_t>(fifth)]);
        const float strongestThird = std::max(chroma[static_cast<size_t>((bestRoot + 3) % 12)],
                                               chroma[static_cast<size_t>((bestRoot + 4) % 12)]);
        return strongestThird < 0.12f * reference ? remembered : -1;
    }

    juce::dsp::FFT fft;
    juce::dsp::WindowingFunction<float> window;
    ChordMatcher matcher;
    TempoTracker tempoTracker;
    std::array<float, fftSize> ring {};
    std::array<float, fftSize * 2> fftData {};
    std::array<float, 12> smoothedChroma {};
    std::array<int, 12> rememberedChordByRoot {};
    double sampleRate = 48000.0;
    float smoothingAlpha = 0.30f;
    int writePosition = 0, filled = 0, samplesSinceAnalysis = 0;
    int pendingChord = -1, pendingFrames = 0, noChordFrames = 0, stableChord = -1;
    AnalysisTiming pendingTiming;
    int stableFramesRequired = 9, noChordFramesRequired = 24;
    int extendedStableFramesRequired = 18;
    bool hasAnalysed = false, hasSmoothedChroma = false;
    bool extendedChords = false;
};
