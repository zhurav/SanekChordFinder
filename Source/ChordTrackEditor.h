#pragma once

#include "PluginProcessor.h"

class ChordTrackEditor final : public juce::Component, private juce::TableListBoxModel, private juce::Timer
{
public:
    explicit ChordTrackEditor(SanekChordFinderAudioProcessor&);
    ~ChordTrackEditor() override;
    void resized() override;
    void paint(juce::Graphics&) override;

private:
    int getNumRows() override { return static_cast<int>(track.rows.size()); }
    void paintRowBackground(juce::Graphics&, int, int, int, bool) override;
    void paintCell(juce::Graphics&, int, int, int, int, bool) override;
    juce::Component* refreshComponentForCell(int, int, bool, juce::Component*) override;
    void changed();
    void timerCallback() override;
    void refreshTempoLabel();
    void loadHistory();
    void exportMidi();
    void saveMidi(const ChordTrack&, const juce::File&);

    SanekChordFinderAudioProcessor& processor;
    ChordTrack track;
    juce::TableListBox table { "Chord Track", this };
    juce::TextButton loadButton { "LOAD HISTORY" }, deleteButton { "DELETE ROW" };
    juce::TextButton exportButton { "EXPORT MIDI" };
    juce::ComboBox durationBox, meterBox;
    juce::Label tempo;
    juce::Label status;
    std::unique_ptr<juce::FileChooser> chooser;
    bool choosing = false;
    unsigned revision = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChordTrackEditor)
};
