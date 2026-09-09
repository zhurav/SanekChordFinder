#pragma once

#include "PluginProcessor.h"
#include "KeyDetector.h"
#include "ChordTrackEditor.h"

class ChordFinderLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    ChordFinderLookAndFeel();
    void drawRotarySlider(juce::Graphics&, int, int, int, int, float, float, float,
                          juce::Slider&) override;
};

class SanekChordFinderAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                                  private juce::Timer
{
public:
    explicit SanekChordFinderAudioProcessorEditor(SanekChordFinderAudioProcessor&);
    ~SanekChordFinderAudioProcessorEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void refreshHistory();
    juce::String buildSequence() const;
    juce::String eventPosition(const ChordEvent&) const;

    SanekChordFinderAudioProcessor& processor;
    ChordFinderLookAndFeel look;
    ChordTrackEditor trackEditor;
    juce::ToggleButton listeningButton { "START LISTENING" };
    juce::Slider sensitivityKnob;
    juce::Label sensitivityLabel;
    juce::ComboBox meterBox;
    juce::Label meterLabel;
    juce::ComboBox chordSetBox;
    juce::Label chordSetLabel;
    juce::Label currentChordLabel;
    juce::Label confidenceLabel;
    juce::Label alternativeLabel;
    juce::Label keyLabel;
    juce::Label bpmLabel;
    juce::TextEditor historyBox;
    juce::TextButton newBarButton { "NEW BAR" };
    juce::TextButton clearButton { "RESET" };
    juce::TextButton copyButton { "COPY SEQUENCE" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> listeningAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sensitivityAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> meterAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> chordSetAttachment;
    std::vector<ChordEvent> displayedEvents;
    std::array<float, 12> displayedChroma {};
    float displayedConfidence = 0.0f;
    float displayedBpm = 0.0f;
    float displayedBeatPhase = 0.0f;
    float historyBpm = -1.0f;
    int displayedChord = -1;
    int displayedBar = -1;
    int displayedBeat = -1;
    int displayedBeatsPerBar = 4;
    bool displayedTempoLocked = false;
    bool historyTempoLocked = false;
    KeyMatch displayedKey;
    juce::TooltipWindow tooltips { this, 500 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SanekChordFinderAudioProcessorEditor)
};
