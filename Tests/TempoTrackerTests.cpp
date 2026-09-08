#include "TempoTracker.h"

#include <iostream>
#include <stdexcept>

namespace
{
void require(bool okay, const char* message)
{
    if (!okay)
        throw std::runtime_error(message);
}

void feedClickTrack(TempoTracker& tracker, double sampleRate, float bpm, double seconds,
                    std::int64_t startSample = 0)
{
    const auto totalSamples = static_cast<std::int64_t>(std::lround(seconds * sampleRate));
    const double samplesPerBeat = 60.0 * sampleRate / bpm;
    const int clickLength = static_cast<int>(std::lround(0.012 * sampleRate));
    for (std::int64_t sample = 0; sample < totalSamples; ++sample)
    {
        const auto withinBeat = static_cast<int>(std::fmod(static_cast<double>(sample + startSample),
                                                           samplesPerBeat));
        const float value = withinBeat < clickLength
                          ? 0.8f * (1.0f - static_cast<float>(withinBeat) / clickLength)
                          : 0.0005f;
        tracker.processSample(value);
    }
}

TempoState runClickTrack(double sampleRate, float bpm, double seconds)
{
    TempoTracker tracker;
    tracker.prepare(sampleRate);
    feedClickTrack(tracker, sampleRate, bpm, seconds);
    return tracker.getState();
}
}

int main()
{
    try
    {
        for (const double sampleRate : { 44100.0, 48000.0, 96000.0 })
            for (const float expected : { 90.0f, 120.0f, 150.0f })
            {
                const auto state = runClickTrack(sampleRate, expected, 14.0);
                require(state.locked, "Periodic click track did not lock");
                require(std::abs(state.bpm - expected) < 1.5f,
                        "Tempo estimate is outside tolerance");
                require(state.confidence > 40.0f, "Clean click confidence is too low");
                require(state.bar > 0 && state.beat > 0, "Internal bar position is unavailable");
            }
        std::cout << "PASS: 90/120/150 BPM at 44.1/48/96 kHz\n";

        TempoTracker silence;
        silence.prepare(48000.0);
        for (int sample = 0; sample < 48000 * 10; ++sample)
            silence.processSample(0.0f);
        require(!silence.getState().locked, "Silence must not produce a tempo");
        std::cout << "PASS: silence rejection and internal bar position\n";

        TempoTracker fixedTempo;
        fixedTempo.prepare(48000.0);
        feedClickTrack(fixedTempo, 48000.0, 120.0f, 8.0);
        const auto initiallyLocked = fixedTempo.getState();
        feedClickTrack(fixedTempo, 48000.0, 150.0f, 8.0, 48000 * 8);
        const auto afterPush = fixedTempo.getState();
        require(initiallyLocked.locked && std::abs(initiallyLocked.bpm - 120.0f) < 1.5f,
                "Initial tempo did not lock at 120 BPM");
        require(std::abs(afterPush.bpm - initiallyLocked.bpm) < 0.01f,
                "Locked tempo must not chase performance changes");
        fixedTempo.markNewBar();
        const auto marked = fixedTempo.getState();
        require(marked.bar == 1 && marked.beat == 1,
                "Manual new-bar marker did not reset the internal grid");
        std::cout << "PASS: locked tempo stability and manual new-bar marker\n";
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
