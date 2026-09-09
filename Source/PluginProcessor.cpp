#include "PluginProcessor.h"
#include "PluginEditor.h"

SanekChordFinderAudioProcessor::SanekChordFinderAudioProcessor()
    : AudioProcessor(BusesProperties()
          .withInput("Input", juce::AudioChannelSet::stereo(), true)
          .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters(*this, nullptr, "SanekChordFinderState", createParameters())
{
    listening = parameters.getRawParameterValue("listening");
    sensitivity = parameters.getRawParameterValue("sensitivity");
    meter = parameters.getRawParameterValue("meter");
    chordSet = parameters.getRawParameterValue("chordSet");
    for (auto& value : latestChroma)
        value.store(0.0f, std::memory_order_relaxed);
}

juce::AudioProcessorValueTreeState::ParameterLayout
SanekChordFinderAudioProcessor::createParameters()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID { "listening", 1 }, "Listening", false));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "sensitivity", 1 }, "Sensitivity",
        juce::NormalisableRange<float> { 0.0f, 100.0f, 1.0f }, 65.0f,
        juce::AudioParameterFloatAttributes().withLabel("%")));
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { "meter", 1 }, "Meter",
        juce::StringArray { "3/4", "4/4", "6/8" }, 1));
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { "chordSet", 1 }, "Chord Set",
        juce::StringArray { "TRIADS", "EXTENDED" }, 0));
    return layout;
}

void SanekChordFinderAudioProcessor::prepareToPlay(double rate, int maximumBlockSize)
{
    juce::ignoreUnused(maximumBlockSize);
    sampleRateHz = std::isfinite(rate) && rate > 0.0 ? rate : 48000.0;
    listeningSamples = 0;
    analyzer.prepare(sampleRateHz);
    wasListening = false;
    currentChord.store(-1, std::memory_order_relaxed);
    alternativeChord.store(-1, std::memory_order_relaxed);
    confidence.store(0.0f, std::memory_order_relaxed);
    autoBpm.store(0.0f, std::memory_order_relaxed);
    tempoConfidence.store(0.0f, std::memory_order_relaxed);
    tempoLocked.store(false, std::memory_order_relaxed);
    tempoOriginSeconds.store(-1.0, std::memory_order_relaxed);
    currentBar.store(-1, std::memory_order_relaxed);
    currentBeat.store(-1, std::memory_order_relaxed);
    currentBeatsPerBar.store(4, std::memory_order_relaxed);
    beatPhase.store(0.0f, std::memory_order_relaxed);
    newBarRequested.store(false, std::memory_order_relaxed);
    alignNextChordToBarOrigin.store(true, std::memory_order_relaxed);
}

bool SanekChordFinderAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto output = layouts.getMainOutputChannelSet();
    return (output == juce::AudioChannelSet::mono() || output == juce::AudioChannelSet::stereo())
        && output == layouts.getMainInputChannelSet();
}

void SanekChordFinderAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                                  juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    midi.clear();
    for (int channel = getTotalNumInputChannels(); channel < getTotalNumOutputChannels(); ++channel)
        buffer.clear(channel, 0, buffer.getNumSamples());

    const bool active = isListening();
    if (active && !wasListening)
    {
        analyzer.reset();
        listeningSamples = 0;
        newBarRequested.store(false, std::memory_order_relaxed);
        alignNextChordToBarOrigin.store(true, std::memory_order_relaxed);
    }
    if (!active && wasListening)
    {
        analyzer.reset();
        currentChord.store(-1, std::memory_order_relaxed);
        alternativeChord.store(-1, std::memory_order_relaxed);
        confidence.store(0.0f, std::memory_order_relaxed);
        for (auto& value : latestChroma)
            value.store(0.0f, std::memory_order_relaxed);
        autoBpm.store(0.0f, std::memory_order_relaxed);
        tempoConfidence.store(0.0f, std::memory_order_relaxed);
        tempoLocked.store(false, std::memory_order_relaxed);
        tempoOriginSeconds.store(-1.0, std::memory_order_relaxed);
        currentBar.store(-1, std::memory_order_relaxed);
        currentBeat.store(-1, std::memory_order_relaxed);
        beatPhase.store(0.0f, std::memory_order_relaxed);
        newBarRequested.store(false, std::memory_order_relaxed);
        alignNextChordToBarOrigin.store(true, std::memory_order_relaxed);
    }
    wasListening = active;

    if (active)
    {
        const int meterIndex = static_cast<int>(std::lround(meter->load(std::memory_order_relaxed)));
        const int beats = meterIndex == 0 ? 3 : (meterIndex == 2 ? 6 : 4);
        analyzer.setBeatsPerBar(beats);
        analyzer.setExtendedChords(chordSet->load(std::memory_order_relaxed) >= 0.5f);
        if (newBarRequested.exchange(false, std::memory_order_acq_rel))
        {
            analyzer.markNewBar();
            tempoOriginSeconds.store(analyzer.getTempoState().originSeconds,
                                     std::memory_order_relaxed);
        }
        const auto timing = readTiming();
        analyzer.process(buffer, sensitivity->load(std::memory_order_relaxed), timing,
                         [this](const ChordFrame& frame) noexcept { receiveFrame(frame); });
        listeningSamples += buffer.getNumSamples();
        const auto tempo = analyzer.getTempoState();
        autoBpm.store(tempo.bpm, std::memory_order_relaxed);
        tempoConfidence.store(tempo.confidence, std::memory_order_relaxed);
        tempoLocked.store(tempo.locked, std::memory_order_relaxed);
        tempoOriginSeconds.store(tempo.originSeconds, std::memory_order_relaxed);
        currentBar.store(tempo.bar, std::memory_order_relaxed);
        currentBeat.store(tempo.beat, std::memory_order_relaxed);
        currentBeatsPerBar.store(tempo.beatsPerBar, std::memory_order_relaxed);
        beatPhase.store(tempo.beatPhase, std::memory_order_relaxed);
    }
}

void SanekChordFinderAudioProcessor::processBlockBypassed(juce::AudioBuffer<float>& buffer,
                                                          juce::MidiBuffer& midi)
{
    processBlock(buffer, midi);
}

AnalysisTiming SanekChordFinderAudioProcessor::readTiming() const noexcept
{
    AnalysisTiming timing;
    timing.seconds = static_cast<double>(listeningSamples) / sampleRateHz;
    return timing;
}

InternalGridPosition SanekChordFinderAudioProcessor::getInternalPosition(double seconds) const noexcept
{
    InternalGridPosition position;
    if (!tempoLocked.load(std::memory_order_relaxed))
        return position;
    const double origin = tempoOriginSeconds.load(std::memory_order_relaxed);
    const float bpm = autoBpm.load(std::memory_order_relaxed);
    const int beats = currentBeatsPerBar.load(std::memory_order_relaxed);
    if (!std::isfinite(seconds) || !std::isfinite(origin) || origin < 0.0 || bpm <= 0.0f
        || beats <= 0 || seconds < origin)
        return position;
    const auto beatIndex = static_cast<juce::int64>(std::llround((seconds - origin) * bpm / 60.0));
    position.bar = static_cast<int>(beatIndex / beats) + 1;
    position.beat = static_cast<int>(beatIndex % beats) + 1;
    return position;
}

void SanekChordFinderAudioProcessor::receiveFrame(const ChordFrame& frame) noexcept
{
    currentChord.store(frame.chord, std::memory_order_relaxed);
    alternativeChord.store(frame.alternative, std::memory_order_relaxed);
    confidence.store(frame.confidence, std::memory_order_relaxed);
    for (size_t i = 0; i < latestChroma.size(); ++i)
        latestChroma[i].store(frame.chroma[i], std::memory_order_relaxed);

    const bool needsHistoryEvent = frame.changed
        || lastHistoryChord.load(std::memory_order_relaxed) < 0;
    if (needsHistoryEvent && frame.chord >= 0
        && lastHistoryChord.exchange(frame.chord, std::memory_order_relaxed) != frame.chord)
    {
        double eventSeconds = frame.timing.seconds;
        if (alignNextChordToBarOrigin.load(std::memory_order_relaxed))
        {
            const double origin = tempoOriginSeconds.load(std::memory_order_relaxed);
            if (std::isfinite(origin) && origin >= 0.0)
            {
                eventSeconds = origin;
                alignNextChordToBarOrigin.store(false, std::memory_order_relaxed);
            }
        }
        pushHistory({ frame.chord, frame.confidence, eventSeconds, frame.timing.ppq,
                      frame.timing.bar, frame.timing.beat });
    }
}

void SanekChordFinderAudioProcessor::requestNewBar() noexcept
{
    alignNextChordToBarOrigin.store(true, std::memory_order_release);
    newBarRequested.store(true, std::memory_order_release);
}

void SanekChordFinderAudioProcessor::pushHistory(const ChordEvent& event) noexcept
{
    const auto index = historyCount.load(std::memory_order_relaxed);
    auto& slot = history[static_cast<size_t>(index % historyCapacity)];
    slot.chord.store(event.chord, std::memory_order_relaxed);
    slot.confidence.store(event.confidence, std::memory_order_relaxed);
    slot.seconds.store(event.seconds, std::memory_order_relaxed);
    slot.ppq.store(event.ppq, std::memory_order_relaxed);
    slot.bar.store(event.bar, std::memory_order_relaxed);
    slot.beat.store(event.beat, std::memory_order_relaxed);
    slot.serial.store(index + 1, std::memory_order_release);
    historyCount.store(index + 1, std::memory_order_release);
}

std::array<float, 12> SanekChordFinderAudioProcessor::getChroma() const noexcept
{
    std::array<float, 12> result {};
    for (size_t i = 0; i < result.size(); ++i)
        result[i] = latestChroma[i].load(std::memory_order_relaxed);
    return result;
}

std::vector<ChordEvent> SanekChordFinderAudioProcessor::getHistorySnapshot() const
{
    std::vector<ChordEvent> result;
    const auto end = historyCount.load(std::memory_order_acquire);
    const auto cleared = historyStart.load(std::memory_order_acquire);
    const auto begin = std::max(cleared, end > historyCapacity ? end - historyCapacity : 0);
    result.reserve(static_cast<size_t>(end - begin));
    for (auto index = begin; index < end; ++index)
    {
        const auto& slot = history[static_cast<size_t>(index % historyCapacity)];
        const auto serialBefore = slot.serial.load(std::memory_order_acquire);
        if (serialBefore != index + 1)
            continue;
        ChordEvent event { slot.chord.load(std::memory_order_relaxed),
                           slot.confidence.load(std::memory_order_relaxed),
                           slot.seconds.load(std::memory_order_relaxed),
                           slot.ppq.load(std::memory_order_relaxed),
                           slot.bar.load(std::memory_order_relaxed),
                           slot.beat.load(std::memory_order_relaxed) };
        if (slot.serial.load(std::memory_order_acquire) == serialBefore)
            result.push_back(event);
    }
    return result;
}

void SanekChordFinderAudioProcessor::clearHistory() noexcept
{
    historyStart.store(historyCount.load(std::memory_order_acquire), std::memory_order_release);
    lastHistoryChord.store(-1, std::memory_order_relaxed);
}

juce::AudioProcessorEditor* SanekChordFinderAudioProcessor::createEditor()
{
    return new SanekChordFinderAudioProcessorEditor(*this);
}

void SanekChordFinderAudioProcessor::getStateInformation(juce::MemoryBlock& destination)
{
    if (auto xml = parameters.copyState().createXml())
        copyXmlToBinary(*xml, destination);
}

void SanekChordFinderAudioProcessor::setStateInformation(const void* data, int size)
{
    if (auto xml = getXmlFromBinary(data, size))
        if (xml->hasTagName(parameters.state.getType()))
            parameters.replaceState(juce::ValueTree::fromXml(*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SanekChordFinderAudioProcessor();
}
