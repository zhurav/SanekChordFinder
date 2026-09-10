#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "ChordAnalyzer.h"
#include "ChordTrack.h"
#include "HostLoopCapture.h"

struct ChordEvent
{
    int chord = -1;
    float confidence = 0.0f;
    double seconds = -1.0;
    double ppq = -1.0;
    int bar = -1;
    float beat = -1.0f;
    int bassNote = -1;
    double durationSeconds = 0.0;
};

struct InternalGridPosition
{
    int bar = -1;
    int beat = -1;
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
    int getCurrentBass() const noexcept { return currentBass.load(std::memory_order_relaxed); }
    float getTuningCents() const noexcept { return tuningCents.load(std::memory_order_relaxed); }
    bool isTuningReady() const noexcept { return tuningReady.load(std::memory_order_relaxed); }
    void requestTuningCalibration() noexcept { tuningCalibrationRequested.store(true); }
    int getAlternativeChord() const noexcept { return alternativeChord.load(std::memory_order_relaxed); }
    float getConfidence() const noexcept { return confidence.load(std::memory_order_relaxed); }
    bool isListening() const noexcept { return listening->load(std::memory_order_relaxed) >= 0.5f; }
    std::array<float, 12> getChroma() const noexcept;
    std::vector<ChordEvent> getHistorySnapshot() const;
    HostLoopCapture::Snapshot getLoopSnapshot() const { return loopCapture.snapshot(); }
    void clearHistory() noexcept;
    void requestNewBar() noexcept;
    float getAutoBpm() const noexcept { return autoBpm.load(std::memory_order_relaxed); }
    float getLiveBpm() const noexcept { return liveBpm.load(std::memory_order_relaxed); }
    float getLastHeardBpm() const noexcept { return lastHeardBpm.load(std::memory_order_relaxed); }
    float getLiveTempoConfidence() const noexcept { return liveTempoConfidence.load(std::memory_order_relaxed); }
    float getTempoConfidence() const noexcept
    {
        return tempoConfidence.load(std::memory_order_relaxed);
    }
    bool isTempoLocked() const noexcept { return tempoLocked.load(std::memory_order_relaxed); }
    int getCurrentBar() const noexcept { return currentBar.load(std::memory_order_relaxed); }
    int getCurrentBeat() const noexcept { return currentBeat.load(std::memory_order_relaxed); }
    int getBeatsPerBar() const noexcept { return currentBeatsPerBar.load(std::memory_order_relaxed); }
    float getBeatPhase() const noexcept { return beatPhase.load(std::memory_order_relaxed); }
    InternalGridPosition getInternalPosition(double seconds) const noexcept;
    double getRecordedBpm() const noexcept { return recordedBpm.load(); }
    double getExportBpm() const noexcept { return exportBpm.load(); }
    double getHostGridBpm() const noexcept { return hostGridBpm.load(); }
    double getHostEndPpq() const noexcept { return hostEndPpq.load(); }
    int getHostGridMeter() const noexcept { return hostGridMeter.load(); }
    double getRecordedEndSeconds() const noexcept { return recordedEndSeconds.load(); }
    double getRecordedOrigin() const noexcept { return recordedOrigin.load(); }
    ChordTrack getChordTrack() const { const juce::ScopedLock lock(trackLock); return chordTrack; }
    void setChordTrack(const ChordTrack& value) { const juce::ScopedLock lock(trackLock); chordTrack = value; ++trackRevision; }
    unsigned getTrackRevision() const noexcept { return trackRevision.load(); }

    juce::AudioProcessorValueTreeState parameters;

private:
    static constexpr size_t historyCapacity = 128;
    struct HistorySlot
    {
        std::atomic<juce::uint64> serial { 0 };
        std::atomic<int> chord { -1 };
        std::atomic<int> bassNote { -1 };
        std::atomic<double> durationSeconds { 0.0 };
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
    HostLoopCapture loopCapture;
    std::atomic<bool> resetLoopRequested { false };
    bool captureHostLoop = false;
    double captureLoopStart = 0.0;
    std::atomic<float>* listening = nullptr;
    std::atomic<float>* sensitivity = nullptr;
    std::atomic<float>* meter = nullptr;
    std::atomic<float>* chordSet = nullptr;
    std::atomic<int> currentChord { -1 }, alternativeChord { -1 };
    std::atomic<int> currentBass { -1 }, lastHistoryBass { -1 };
    std::atomic<float> tuningCents { 0.0f };
    std::atomic<bool> tuningReady { false }, tuningCalibrationRequested { false };
    std::atomic<float> confidence { 0.0f };
    std::array<std::atomic<float>, 12> latestChroma;
    std::array<HistorySlot, historyCapacity> history;
    std::atomic<juce::uint64> historyCount { 0 }, historyStart { 0 };
    std::atomic<int> lastHistoryChord { -1 };
    std::atomic<float> autoBpm { 0.0f }, tempoConfidence { 0.0f }, beatPhase { 0.0f };
    std::atomic<float> liveBpm { 0.0f }, lastHeardBpm { 0.0f }, liveTempoConfidence { 0.0f };
    std::atomic<double> tempoOriginSeconds { -1.0 };
    std::atomic<int> currentBar { -1 }, currentBeat { -1 }, currentBeatsPerBar { 4 };
    std::atomic<bool> tempoLocked { false }, newBarRequested { false };
    std::atomic<bool> alignNextChordToBarOrigin { true };
    double sampleRateHz = 48000.0;
    juce::int64 listeningSamples = 0;
    bool wasListening = false;
    std::atomic<double> recordedBpm { 0.0 }, recordedOrigin { -1.0 };
    std::atomic<double> exportBpm { 0.0 };
    std::atomic<double> hostGridBpm { 0.0 }, hostEndPpq { -1.0 };
    int lastHistoryBar = -1;
    std::atomic<int> hostGridMeter {4};
    bool hostTimelineActive = false;
    double expectedHostPpq = 0.0;
    std::atomic<double> recordedEndSeconds { 0.0 };
    mutable juce::CriticalSection trackLock;
    ChordTrack chordTrack;
    std::atomic<unsigned> trackRevision { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SanekChordFinderAudioProcessor)
};
