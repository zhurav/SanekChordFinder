#include "ChordAnalyzer.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <iomanip>
#include <iostream>

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        std::cerr << "Usage: AnalyzeChordWav <recording.wav>\n";
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
    constexpr int blockSize = 512;
    const int channels = static_cast<int>(std::min<juce::uint64>(2, reader->numChannels));
    juce::AudioBuffer<float> buffer(std::max(1, channels), blockSize);
    int lastPrintedChord = -1;
    for (juce::int64 position = 0; position < reader->lengthInSamples; position += blockSize)
    {
        const int samples = static_cast<int>(std::min<juce::int64>(
            blockSize, reader->lengthInSamples - position));
        buffer.clear();
        reader->read(&buffer, 0, samples, position, true, channels > 1);
        AnalysisTiming timing;
        timing.seconds = static_cast<double>(position) / reader->sampleRate;
        analyzer.process(buffer, 65.0f, timing, [&](const ChordFrame& frame)
        {
            if (frame.changed && frame.chord >= 0 && frame.chord != lastPrintedChord)
            {
                lastPrintedChord = frame.chord;
                std::cout << std::fixed << std::setprecision(2) << frame.timing.seconds << "  "
                          << ChordMatcher::name(frame.chord) << '\n';
            }
        });
    }
    return 0;
}
