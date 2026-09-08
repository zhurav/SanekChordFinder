#pragma once

#include "PluginProcessor.h"

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
    static juce::String eventPosition(const ChordEvent&);

    SanekChordFinderAudioProcessor& processor;
    ChordFinderLookAndFeel look;
    juce::ToggleButton listeningButton { "START LISTENING" };
    juce::Slider sensitivityKnob;
    juce::Label sensitivityLabel;
    juce::Label currentChordLabel;
    juce::Label confidenceLabel;
    juce::Label alternativeLabel;
    juce::TextEditor historyBox;
    juce::TextButton clearButton { "CLEAR" };
    juce::TextButton copyButton { "COPY SEQUENCE" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> listeningAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sensitivityAttachment;
    std::vector<ChordEvent> displayedEvents;
    std::array<float, 12> displayedChroma {};
    float displayedConfidence = 0.0f;
    int displayedChord = -1;
    juce::TooltipWindow tooltips { this, 500 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SanekChordFinderAudioProcessorEditor)
};
