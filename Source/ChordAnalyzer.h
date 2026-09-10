#pragma once

#include <juce_dsp/juce_dsp.h>
#include "ChordMatcher.h"
#include "SpectrumChroma.h"
#include "TempoTracker.h"
#include "SpectralPitch.h"
#include "HarmonicMemory.h"

struct AnalysisTiming
{
    double seconds = -1.0;
    double ppq = -1.0;
    double bpm = 120.0;
    int bar = -1;
    float beat = -1.0f;
    double hostBarStart = 0.0, hostBarLength = 4.0;
    int hostBarNumber = -1, hostDenominator = 4;
    void updateBar() noexcept
    {
        if (ppq < 0.0 || hostBarNumber < 1 || hostBarLength <= 0.0) return;
        const double unit = 4.0 / hostDenominator;
        const double relative = std::round((ppq - hostBarStart) / unit) * unit;
        const int offset = static_cast<int>(std::floor(relative / hostBarLength));
        bar = hostBarNumber + offset;
        beat = static_cast<float>((relative - offset * hostBarLength) / unit + 1.0);
    }
};

struct ChordFrame
{
    int chord = -1;
    int alternative = -1;
    float confidence = 0.0f;
    bool changed = false;
    AnalysisTiming timing;
    std::array<float, 12> chroma {};
    int bassNote = -1;
    double audibleSeconds = 0.0;
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
        noChordFramesRequired = std::max(12, static_cast<int>(std::ceil(sampleRate / hopSize)));
        tempoTracker.prepare(sampleRate);
        reset();
    }

    void reset() noexcept
    {
        analysisSeconds = 0.0;
        memory.reset();
        pitch.reset();
        resetChords();
        tempoTracker.reset();
    }

    void resetChords() noexcept
    {
        for (auto& channel : ring) channel.fill(0.0f);
        fftData.fill(0.0f);
        writePosition = 0;
        filled = 0;
        samplesSinceAnalysis = 0;
        hasAnalysed = false;
        pendingChord = -1;
        pendingFrames = 0;
        pendingEvidence = 0.0;
        pendingAttack = false;
        pendingTiming = {};
        noChordFrames = 0;
        stableChord = -1;
        stableOnset = -1.0;
        stableBar = -1;
        subsetReleased = false;
        smoothedChroma.fill(0.0f);
        pitch.resetBass();
        stableBass = -1;
        pendingBass = -1;
        bassEvidence = 0.0;
        hasSmoothedChroma = false;
    }

    void setBeatsPerBar(int beats) noexcept { tempoTracker.setBeatsPerBar(beats); }
    void setExtendedChords(bool enabled) noexcept { extendedChords = enabled; }
    void markNewBar() noexcept { tempoTracker.markNewBar(); }
    TempoState getTempoState() const noexcept { return tempoTracker.getState(); }
    double getTuningCents() const noexcept { return pitch.tuningCents(); }
    bool isTuningReady() const noexcept { return pitch.tuningReady(); }
    void calibrateTuning() noexcept { pitch.reset(); }

    template <typename Callback>
    void process(const juce::AudioBuffer<float>& buffer, float sensitivity,
                 const AnalysisTiming& blockTiming, Callback&& callback) noexcept
    {
        const int channels = std::min(2, buffer.getNumChannels());
        if (channels <= 0)
            return;

        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
        {
            activeChannels = channels;
            double tempoEnergy = 0.0;
            for (int channel = 0; channel < channels; ++channel)
            {
                const float input = buffer.getReadPointer(channel)[sample];
                const float safe = std::isfinite(input) ? input : 0.0f;
                ring[static_cast<size_t>(channel)][static_cast<size_t>(writePosition)] = safe;
                tempoEnergy += static_cast<double>(safe) * safe;
            }
            // Measure channel energy so opposite-polarity stereo cannot cancel the rhythm.
            tempoTracker.processSample(static_cast<float>(std::sqrt(tempoEnergy / channels)));
            analysisSeconds += 1.0 / sampleRate;
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
                timing.updateBar();
                callback(analyse(sensitivity, timing));
            }
        }
    }

private:
    ChordFrame analyse(float sensitivity, const AnalysisTiming& timing) noexcept
    {
        spectrum.fill(0.0f);
        double squareSum = 0.0;
        for (int channel = 0; channel < activeChannels; ++channel)
        {
            fftData.fill(0.0f);
            for (int i = 0; i < fftSize; ++i)
            {
                const float sample = ring[static_cast<size_t>(channel)][static_cast<size_t>((writePosition + i) % fftSize)];
                fftData[static_cast<size_t>(i)] = sample;
                squareSum += static_cast<double>(sample) * sample;
            }
            window.multiplyWithWindowingTable(fftData.data(), static_cast<size_t>(fftSize));
            fft.performFrequencyOnlyForwardTransform(fftData.data(), true);
            for (size_t bin = 0; bin < spectrum.size(); ++bin)
                spectrum[bin] += fftData[bin] * fftData[bin] / static_cast<float>(activeChannels);
        }
        for (auto& bin : spectrum) bin = std::sqrt(bin);
        const float rms = static_cast<float>(std::sqrt(squareSum / (fftSize * activeChannels)));
        const float rmsDb = juce::Decibels::gainToDecibels(rms, -120.0f);

        pitch.process(spectrum.data(), static_cast<int>(spectrum.size()), fftSize, sampleRate,
                      hopSize / sampleRate, rmsDb > -65.0f);
        const auto instantaneousChroma = SpectrumChroma::convert(spectrum.data(), fftSize / 2 + 1,
                                                                 fftSize, sampleRate, pitch.a4());
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
        const auto instantaneous = matcher.match(instantaneousChroma, rmsDb, sensitivity, extendedChords);
        int candidateChord = applyHarmonicMemory(raw, smoothedChroma);
        // Chroma alone cannot distinguish Gsus4/Csus2 or Eb6/Cm7.
        // Prefer the equivalent interpretation rooted at a supported bass.
        const int bass = pitch.bassNote();
        if (bass >= 0 && ChordMatcher::isValid(candidateChord))
            for (int quality = 0; quality < (extendedChords ? ChordMatcher::qualityCount : 2); ++quality)
            {
                const int rooted = quality * 12 + bass % 12;
                if (ChordMatcher::samePitchSet(rooted, candidateChord)) { candidateChord = rooted; break; }
            }
        const double recentAttack = tempoTracker.attackBefore(timing.seconds, 0.20);
        if (stableOnset >= 0.0 && recentAttack < timing.seconds - 0.001
            && recentAttack > stableOnset + 0.35)
        {
            subsetReleased = !ChordMatcher::samePitchSet(instantaneous.chord, stableChord);
            if (!subsetReleased) stableOnset = recentAttack;
        }
        const int actualBar = timing.hostBarNumber > 0
            ? timing.hostBarNumber + static_cast<int>(std::floor((timing.ppq - timing.hostBarStart) / timing.hostBarLength)) : -1;
        if (actualBar > stableBar && stableBar > 0)
        {
            subsetReleased = !ChordMatcher::samePitchSet(instantaneous.chord, stableChord);
            if (!subsetReleased) stableBar = actualBar;
        }
        bool subset = ChordMatcher::isValid(candidateChord) && ChordMatcher::isValid(stableChord)
                   && ChordMatcher::toneCount(candidateChord) < ChordMatcher::toneCount(stableChord);
        for (int note = 0; note < 12 && subset; ++note)
            if (ChordMatcher::containsPitch(candidateChord, note) && !ChordMatcher::containsPitch(stableChord, note))
                subset = false;
        if (extendedChords && subset && !subsetReleased)
            candidateChord = stableChord;
        const int candidateAlternative = candidateChord != raw.chord && raw.chord >= 0
                                       ? raw.chord : raw.alternative;
        const float candidateConfidence = ChordMatcher::samePitchSet(candidateChord, raw.chord) ? raw.confidence
                                        : (candidateChord >= 0 ? 42.0f : 0.0f);
        bool changed = false;
        if (candidateChord < 0)
        {
            pendingChord = -1;
            pendingFrames = 0;
            pendingEvidence = 0.0;
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
                || ChordMatcher::samePitchSet(candidateChord, pendingChord))
                ++pendingFrames;
            else
            {
                pendingChord = candidateChord;
                pendingFrames = 1;
                pendingTiming = timing;
                pendingEvidence = 0.0;
                pendingAttack = tempoTracker.hasAttackNear(timing.seconds);
            }
            auto resultTiming = timing;
            const auto balancedTones = [&](const std::array<float, 12>& chroma)
            {
                const float strongest = *std::max_element(chroma.begin(), chroma.end());
                for (int tone = 0; tone < ChordMatcher::toneCount(pendingChord); ++tone)
                    if (chroma[static_cast<size_t>((ChordMatcher::rootOf(pendingChord)
                        + ChordMatcher::intervalAt(pendingChord, tone)) % 12)] < 0.40f * strongest)
                        return false;
                return strongest > 0.0f;
            };
            // UI confidence can saturate when competing templates were rejected.
            // Fast switching additionally needs audible support for EVERY tone.
            const bool supported = ChordMatcher::samePitchSet(raw.chord, pendingChord)
                                && ChordMatcher::samePitchSet(instantaneous.chord, pendingChord)
                                && balancedTones(instantaneousChroma) && balancedTones(smoothedChroma);
            double confirmationSeconds = ChordMatcher::isBasicMajorOrMinor(pendingChord) ? 0.36 : 0.75;
            if (supported && candidateConfidence >= 85.0f) confirmationSeconds = 0.12;
            else if (supported && candidateConfidence >= 60.0f) confirmationSeconds = 0.20;
            if (supported && pendingAttack && candidateConfidence >= 60.0f
                && (raw.margin >= 0.015f || ChordMatcher::samePitchSet(raw.chord, raw.alternative)))
                confirmationSeconds = std::min(confirmationSeconds, 0.16);
            // Accumulated evidence prevents a single high-confidence frame from
            // instantly confirming a candidate that was weak until this frame.
            pendingEvidence += (hopSize / sampleRate) / confirmationSeconds;
            if (pendingFrames >= 3 && pendingEvidence >= 1.0 && stableChord != pendingChord)
            {
                const bool firstChord = stableChord < 0;
                stableChord = pendingChord;
                changed = true;
                resultTiming = pendingTiming;
                resultTiming.seconds = tempoTracker.attackBefore(pendingTiming.seconds,
                                                                  firstChord ? 1.0 : 0.45);
                if (resultTiming.ppq >= 0.0)
                    resultTiming.ppq += (resultTiming.seconds - pendingTiming.seconds)
                                      * pendingTiming.bpm / 60.0;
                stableOnset = resultTiming.seconds;
                resultTiming.updateBar();
                stableBar = resultTiming.bar;
                subsetReleased = false;
            }

            // Refresh only directly supported thirds, never a recalled guess.
            if (raw.chord == stableChord && ChordMatcher::isBasicMajorOrMinor(stableChord)
                && balancedTones(instantaneousChroma))
                memory.remember(ChordMatcher::rootOf(stableChord), stableChord, analysisSeconds);

            const int heardBass = pitch.bassNote();
            const int nextBass = heardBass >= 0 && ChordMatcher::containsPitch(stableChord, heardBass % 12)
                               ? heardBass : -1;
            if (nextBass != pendingBass || !ChordMatcher::samePitchSet(instantaneous.chord, stableChord))
            {
                pendingBass = nextBass;
                bassEvidence = 0.0;
                bassTiming = timing;
            }
            else bassEvidence += hopSize / sampleRate;
            if (changed) { stableBass = nextBass; bassEvidence = 0.0; }
            else if (nextBass >= 0 && stableBass != nextBass && candidateChord == stableChord
                     && bassEvidence >= 0.4)
            {
                stableBass = nextBass;
                changed = true;
                // Bass confirmation is independent of harmonic confirmation.
                resultTiming = bassTiming;
            }

            const float displayConfidence = candidateChord == stableChord ? candidateConfidence
                                                        : std::min(candidateConfidence, 35.0f);
            return { stableChord, candidateAlternative, displayConfidence, changed, resultTiming,
                     smoothedChroma, stableBass,
                     rmsDb > -65.0f && candidateChord == stableChord ? hopSize / sampleRate : 0.0 };
        }

        const float displayConfidence = candidateChord == stableChord ? candidateConfidence
                                                    : std::min(candidateConfidence, 35.0f);
        return { stableChord, candidateAlternative, displayConfidence, changed, timing,
                 smoothedChroma, stableChord >= 0 ? stableBass : -1 };
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
            const int remembered = memory.recall(root, analysisSeconds);
            const float strength = static_cast<float>(memory.strength(root, analysisSeconds));
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
                if (newEvidence < strength * 0.28f * reference || newEvidence < strength * 1.75f * oldEvidence)
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
        const int remembered = memory.recall(bestRoot, analysisSeconds);
        if (remembered < 0 || memory.strength(bestRoot, analysisSeconds) < 0.25
            || rootScores[static_cast<size_t>(bestRoot)] < 0.70f
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
    std::array<std::array<float, fftSize>, 2> ring {};
    std::array<float, fftSize * 2> fftData {};
    std::array<float, fftSize / 2 + 1> spectrum {};
    std::array<float, 12> smoothedChroma {};
    HarmonicMemory memory;
    SpectralPitch pitch;
    double analysisSeconds = 0.0;
    int activeChannels = 1, stableBass = -1, pendingBass = -1;
    double bassEvidence = 0.0;
    AnalysisTiming bassTiming;
    double sampleRate = 48000.0;
    float smoothingAlpha = 0.30f;
    int writePosition = 0, filled = 0, samplesSinceAnalysis = 0;
    int pendingChord = -1, pendingFrames = 0, noChordFrames = 0, stableChord = -1;
    AnalysisTiming pendingTiming;
    int noChordFramesRequired = 24;
    double pendingEvidence = 0.0;
    bool pendingAttack = false;
    bool hasAnalysed = false, hasSmoothedChroma = false;
    bool extendedChords = false;
    double stableOnset = -1.0;
    bool subsetReleased = false;
    int stableBar = -1;
};
