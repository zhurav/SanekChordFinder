#include "ChordAnalyzer.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <iomanip>
#include <iostream>
#include <string_view>

int main(int argc, char** argv)
{
    if (argc < 2 || argc > 4)
    {
        std::cerr << "Usage: AnalyzeChordWav <recording.wav> [--extended] [sensitivity 0-100]\n";
        return 2;
    }

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(juce::File(argv[1])));
    if (reader == nullptr)
    {
        std::cerr << "Could not read the audio file\n";
        return 2;
    }

    ChordAnalyzer analyzer;
    analyzer.prepare(reader->sampleRate);
    analyzer.setExtendedChords(argc >= 3 && std::string_view(argv[2]) == "--extended");
    const float sensitivity = argc == 4 ? juce::String(argv[3]).getFloatValue() : 65.0f;
    if (!std::isfinite(sensitivity) || sensitivity < 0.0f || sensitivity > 100.0f) return 2;
    constexpr int blockSize = 512;
    const int channels = static_cast<int>(std::min<juce::uint64>(2, reader->numChannels));
    juce::AudioBuffer<float> buffer(std::max(1, channels), blockSize);
    int lastPrintedChord = -1;
    bool tempoLockPrinted = false;
    for (juce::int64 position = 0; position < reader->lengthInSamples; position += blockSize)
    {
        const int samples = static_cast<int>(std::min<juce::int64>(
            blockSize, reader->lengthInSamples - position));
        buffer.clear();
        reader->read(&buffer, 0, samples, position, true, channels > 1);
        AnalysisTiming timing;
        timing.seconds = static_cast<double>(position) / reader->sampleRate;
        analyzer.process(buffer, sensitivity, timing, [&](const ChordFrame& frame)
        {
            if (frame.changed && frame.chord >= 0 && frame.chord != lastPrintedChord)
            {
                lastPrintedChord = frame.chord;
                std::cout << std::fixed << std::setprecision(2) << frame.timing.seconds << "  "
                          << ChordMatcher::name(frame.chord) << "  "
                          << std::setprecision(0) << frame.confidence << "%  alt "
                          << ChordMatcher::name(frame.alternative) << '\n';
            }
        });
        const auto liveTempo = analyzer.getTempoState();
        if (liveTempo.locked && !tempoLockPrinted)
        {
            tempoLockPrinted = true;
            std::cerr << std::fixed << std::setprecision(1)
                      << "TEMPO_LOCK " << liveTempo.bpm << " BPM at "
                      << liveTempo.currentSeconds << " s, confidence "
                      << liveTempo.confidence << "%\n";
        }
    }
    const auto tempo = analyzer.getTempoState();
    std::cerr << std::fixed << std::setprecision(1)
              << "AUTO_BPM " << tempo.bpm
              << " EXPORT_BPM " << tempo.exportBpm
              << "  CONFIDENCE " << tempo.confidence
              << "%  " << (tempo.locked ? "LOCKED" : "LEARNING")
              << "  BAR " << tempo.bar << "  BEAT " << tempo.beat << '\n';
    return 0;
}
