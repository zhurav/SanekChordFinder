#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "ChordAnalyzer.h"

struct ChordEvent
{
    int chord = -1;
    float confidence = 0.0f;
    double seconds = -1.0;
    double ppq = -1.0;
    int bar = -1;
    float beat = -1.0f;
};

class SanekChordFinderAudioProcessor final : public juce::AudioProcessor
{
public:
    SanekChordFinderAudioProcessor();
    void prepareToPlay(double sampleRate, int maximumBlockSize) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout&) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;
    using juce::AudioProcessor::processBlockBypassed;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Sanek Chord Finder"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    int getCurrentChord() const noexcept { return currentChord.load(std::memory_order_relaxed); }
    int getAlternativeChord() const noexcept { return alternativeChord.load(std::memory_order_relaxed); }
    float getConfidence() const noexcept { return confidence.load(std::memory_order_relaxed); }
    bool isListening() const noexcept { return listening->load(std::memory_order_relaxed) >= 0.5f; }
    std::array<float, 12> getChroma() const noexcept;
    std::vector<ChordEvent> getHistorySnapshot() const;
    void clearHistory() noexcept;

    juce::AudioProcessorValueTreeState parameters;

private:
    static constexpr size_t historyCapacity = 128;
    struct HistorySlot
    {
        std::atomic<juce::uint64> serial { 0 };
        std::atomic<int> chord { -1 };
        std::atomic<float> confidence { 0.0f };
        std::atomic<double> seconds { -1.0 };
        std::atomic<double> ppq { -1.0 };
        std::atomic<int> bar { -1 };
        std::atomic<float> beat { -1.0f };
    };

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameters();
    AnalysisTiming readTiming() const noexcept;
    void receiveFrame(const ChordFrame&) noexcept;
    void pushHistory(const ChordEvent&) noexcept;

    ChordAnalyzer analyzer;
    std::atomic<float>* listening = nullptr;
    std::atomic<float>* sensitivity = nullptr;
    std::atomic<int> currentChord { -1 }, alternativeChord { -1 };
    std::atomic<float> confidence { 0.0f };
    std::array<std::atomic<float>, 12> latestChroma;
    std::array<HistorySlot, historyCapacity> history;
    std::atomic<juce::uint64> historyCount { 0 }, historyStart { 0 };
    std::atomic<int> lastHistoryChord { -1 };
    double sampleRateHz = 48000.0;
    juce::int64 processedSamples = 0;
    bool wasListening = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SanekChordFinderAudioProcessor)
};
