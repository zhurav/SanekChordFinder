#include "PluginEditor.h"

namespace
{
const juce::Colour background { 0xff0e141b };
const juce::Colour panel { 0xff17212b };
const juce::Colour cyan { 0xff55d6e5 };
const juce::Colour violet { 0xffa78bfa };
const juce::Colour pale { 0xfff2f5f7 };
const juce::Colour muted { 0xff8fa1b3 };

juce::Font uiFont(float size, bool bold = false)
{
    return juce::Font(juce::FontOptions(size, bold ? juce::Font::bold : juce::Font::plain));
}
}

ChordFinderLookAndFeel::ChordFinderLookAndFeel()
{
    setColour(juce::Slider::textBoxTextColourId, pale);
    setColour(juce::Slider::textBoxBackgroundColourId, background);
    setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour(juce::Slider::rotarySliderFillColourId, cyan);
    setColour(juce::TextButton::buttonColourId, panel.brighter(0.08f));
    setColour(juce::TextButton::buttonOnColourId, cyan.darker(0.35f));
    setColour(juce::TextButton::textColourOffId, pale);
    setColour(juce::TextButton::textColourOnId, background);
    setColour(juce::ToggleButton::textColourId, pale);
    setColour(juce::TooltipWindow::backgroundColourId, panel);
    setColour(juce::TooltipWindow::textColourId, pale);
}

void ChordFinderLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int width,
                                              int height, float position, float startAngle,
                                              float endAngle, juce::Slider& slider)
{
    const auto area = juce::Rectangle<float>(static_cast<float>(x), static_cast<float>(y),
                                             static_cast<float>(width), static_cast<float>(height)).reduced(10.0f);
    const auto centre = area.getCentre();
    const float radius = std::min(area.getWidth(), area.getHeight()) * 0.5f;
    const float angle = startAngle + position * (endAngle - startAngle);
    juce::Path track, active;
    track.addCentredArc(centre.x, centre.y, radius, radius, 0.0f, startAngle, endAngle, true);
    active.addCentredArc(centre.x, centre.y, radius, radius, 0.0f, startAngle, angle, true);
    const juce::PathStrokeType stroke(5.0f, juce::PathStrokeType::curved,
                                     juce::PathStrokeType::rounded);
    g.setColour(juce::Colour(0xff334250));
    g.strokePath(track, stroke);
    g.setColour(slider.findColour(juce::Slider::rotarySliderFillColourId));
    g.strokePath(active, stroke);
    const float bodyRadius = radius - 10.0f;
    const auto body = juce::Rectangle<float>(bodyRadius * 2.0f, bodyRadius * 2.0f).withCentre(centre);
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xff374654), centre.x, body.getY(),
                                          juce::Colour(0xff18212a), centre.x, body.getBottom(), false));
    g.fillEllipse(body);
    juce::Path needle;
    needle.addRoundedRectangle(-2.0f, -bodyRadius + 8.0f, 4.0f, bodyRadius * 0.42f, 2.0f);
    g.setColour(pale);
    g.fillPath(needle, juce::AffineTransform::rotation(angle).translated(centre.x, centre.y));
}

SanekChordFinderAudioProcessorEditor::SanekChordFinderAudioProcessorEditor(
    SanekChordFinderAudioProcessor& owner)
    : AudioProcessorEditor(&owner), processor(owner)
{
    setLookAndFeel(&look);

    listeningButton.setTooltip("Start or stop chord detection. Audio always passes through unchanged.");
    addAndMakeVisible(listeningButton);
    listeningAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processor.parameters, "listening", listeningButton);

    sensitivityKnob.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    sensitivityKnob.setRotaryParameters(juce::MathConstants<float>::pi * 1.25f,
                                        juce::MathConstants<float>::pi * 2.75f, true);
    sensitivityKnob.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 88, 25);
    sensitivityKnob.setTextValueSuffix(" %");
    sensitivityKnob.setDoubleClickReturnValue(true, 65.0);
    sensitivityKnob.setScrollWheelEnabled(false);
    sensitivityKnob.setTooltip("Higher values accept quieter and less certain chords. Double-click: 65%.");
    addAndMakeVisible(sensitivityKnob);
    sensitivityAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.parameters, "sensitivity", sensitivityKnob);
    sensitivityLabel.setText("SENSITIVITY", juce::dontSendNotification);
    sensitivityLabel.setFont(uiFont(12.0f, true));
    sensitivityLabel.setColour(juce::Label::textColourId, muted);
    sensitivityLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(sensitivityLabel);

    currentChordLabel.setText("--", juce::dontSendNotification);
    currentChordLabel.setFont(uiFont(78.0f, true));
    currentChordLabel.setColour(juce::Label::textColourId, pale);
    currentChordLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(currentChordLabel);
    confidenceLabel.setFont(uiFont(14.0f, true));
    confidenceLabel.setColour(juce::Label::textColourId, cyan);
    confidenceLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(confidenceLabel);
    alternativeLabel.setFont(uiFont(13.0f));
    alternativeLabel.setColour(juce::Label::textColourId, muted);
    alternativeLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(alternativeLabel);

    historyBox.setMultiLine(true);
    historyBox.setReadOnly(true);
    historyBox.setScrollbarsShown(true);
    historyBox.setCaretVisible(false);
    historyBox.setFont(uiFont(16.0f));
    historyBox.setColour(juce::TextEditor::backgroundColourId, panel);
    historyBox.setColour(juce::TextEditor::textColourId, pale);
    historyBox.setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    historyBox.setColour(juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
    historyBox.setText("Press START LISTENING, then play audio in Ableton.", false);
    addAndMakeVisible(historyBox);

    clearButton.setTooltip("Clear the detected chord sequence.");
    clearButton.onClick = [this]
    {
        processor.clearHistory();
        displayedEvents.clear();
        historyBox.setText("No chords recorded yet.", false);
    };
    addAndMakeVisible(clearButton);
    copyButton.setTooltip("Copy the sequence as: | Am | F | C | G |");
    copyButton.onClick = [this]
    {
        const auto sequence = buildSequence();
        if (sequence.isNotEmpty())
            juce::SystemClipboard::copyTextToClipboard(sequence);
        copyButton.setButtonText(sequence.isNotEmpty() ? "COPIED" : "NO CHORDS");
        juce::Timer::callAfterDelay(900, [safe = juce::Component::SafePointer<juce::TextButton>(&copyButton)]
        {
            if (safe != nullptr)
                safe->setButtonText("COPY SEQUENCE");
        });
    };
    addAndMakeVisible(copyButton);

    setSize(820, 560);
    refreshHistory();
    startTimerHz(20);
}

SanekChordFinderAudioProcessorEditor::~SanekChordFinderAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel(nullptr);
}

void SanekChordFinderAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(background);
    g.setColour(cyan);
    g.fillRect(0, 0, getWidth(), 3);
    g.setColour(muted);
    g.setFont(uiFont(11.0f, true));
    g.drawText("S A N E K   A U D I O", 26, 17, 300, 18, juce::Justification::centredLeft);
    g.setColour(pale);
    g.setFont(uiFont(29.0f, true));
    g.drawText("CHORD FINDER", 25, 37, 380, 40, juce::Justification::centredLeft);
    g.setColour(muted);
    g.setFont(uiFont(13.0f));
    g.drawText("REAL-TIME MAJOR / MINOR DETECTION", 27, 78, 410, 18,
               juce::Justification::centredLeft);

    g.setColour(panel);
    g.fillRoundedRectangle(25.0f, 116.0f, 480.0f, 242.0f, 12.0f);
    g.fillRoundedRectangle(525.0f, 116.0f, 270.0f, 382.0f, 12.0f);
    g.setColour(muted);
    g.setFont(uiFont(12.0f, true));
    g.drawText("CURRENT CHORD", 43, 130, 200, 20, juce::Justification::centredLeft);
    g.drawText("SEQUENCE", 543, 130, 160, 20, juce::Justification::centredLeft);

    const auto confidenceArea = juce::Rectangle<float>(55.0f, 297.0f, 420.0f, 8.0f);
    g.setColour(juce::Colour(0xff31404d));
    g.fillRoundedRectangle(confidenceArea, 4.0f);
    g.setColour(displayedConfidence >= 45.0f ? cyan : violet);
    g.fillRoundedRectangle(confidenceArea.withWidth(confidenceArea.getWidth()
                           * juce::jlimit(0.0f, 1.0f, displayedConfidence / 100.0f)), 4.0f);

    g.setFont(uiFont(10.0f, true));
    const std::array<const char*, 12> noteNames { "C", "C#", "D", "D#", "E", "F",
                                                  "F#", "G", "G#", "A", "A#", "B" };
    const float startX = 32.0f, width = 38.0f, baseY = 456.0f;
    for (size_t i = 0; i < displayedChroma.size(); ++i)
    {
        const float height = 68.0f * juce::jlimit(0.0f, 1.0f, displayedChroma[i]);
        g.setColour(juce::Colour(0xff263441));
        g.fillRoundedRectangle(startX + static_cast<float>(i) * width, baseY - 68.0f,
                               width - 7.0f, 68.0f, 3.0f);
        const int root = displayedChord >= 0 ? displayedChord % 12 : -1;
        const int third = root >= 0 ? (root + (displayedChord < 12 ? 4 : 3)) % 12 : -1;
        const int fifth = root >= 0 ? (root + 7) % 12 : -1;
        g.setColour(static_cast<int>(i) == root || static_cast<int>(i) == third
                    || static_cast<int>(i) == fifth ? cyan : violet);
        g.fillRoundedRectangle(startX + static_cast<float>(i) * width, baseY - height,
                               width - 7.0f, height, 3.0f);
        g.setColour(muted);
        g.drawText(noteNames[i], juce::roundToInt(startX + static_cast<float>(i) * width),
                   463, juce::roundToInt(width - 7.0f), 18, juce::Justification::centred);
    }
    g.setColour(muted);
    g.setFont(uiFont(11.0f));
    g.drawText("PITCH CLASSES", 32, 492, 200, 18, juce::Justification::centredLeft);
    g.drawText("v0.2", 730, 523, 64, 18, juce::Justification::centredRight);
}

void SanekChordFinderAudioProcessorEditor::resized()
{
    listeningButton.setBounds(565, 35, 172, 38);
    sensitivityLabel.setBounds(414, 21, 119, 18);
    sensitivityKnob.setBounds(420, 36, 106, 72);
    currentChordLabel.setBounds(45, 151, 440, 112);
    confidenceLabel.setBounds(55, 311, 420, 21);
    alternativeLabel.setBounds(55, 332, 420, 20);
    historyBox.setBounds(541, 157, 238, 280);
    clearButton.setBounds(541, 453, 90, 31);
    copyButton.setBounds(642, 453, 137, 31);
}

void SanekChordFinderAudioProcessorEditor::timerCallback()
{
    const bool active = processor.isListening();
    listeningButton.setButtonText(active ? "LISTENING" : "START LISTENING");
    listeningButton.setColour(juce::ToggleButton::tickColourId, active ? cyan : muted);
    displayedChord = active ? processor.getCurrentChord() : -1;
    currentChordLabel.setText(active ? juce::String(ChordMatcher::name(displayedChord).data()) : "--",
                              juce::dontSendNotification);
    displayedConfidence = active ? processor.getConfidence() : 0.0f;
    confidenceLabel.setText(displayedChord >= 0 ? "CONFIDENCE  " + juce::String(displayedConfidence, 0) + "%"
                                       : (active ? "LISTENING FOR A STABLE CHORD" : "ANALYSIS STOPPED"),
                            juce::dontSendNotification);
    const int alternative = processor.getAlternativeChord();
    alternativeLabel.setText(displayedChord >= 0 && alternative >= 0
                                 ? "Alternative: " + juce::String(ChordMatcher::name(alternative).data())
                                 : juce::String(),
                             juce::dontSendNotification);
    displayedChroma = active ? processor.getChroma() : std::array<float, 12> {};
    refreshHistory();
    repaint();
}

void SanekChordFinderAudioProcessorEditor::refreshHistory()
{
    const auto snapshot = processor.getHistorySnapshot();
    if (snapshot.size() == displayedEvents.size()
        && (snapshot.empty() || (snapshot.back().chord == displayedEvents.back().chord
                                 && snapshot.back().seconds == displayedEvents.back().seconds)))
        return;
    displayedEvents = snapshot;
    if (displayedEvents.empty())
    {
        historyBox.setText("No chords recorded yet.", false);
        return;
    }
    juce::String text;
    const size_t first = displayedEvents.size() > 40 ? displayedEvents.size() - 40 : 0;
    for (size_t i = first; i < displayedEvents.size(); ++i)
        text << eventPosition(displayedEvents[i]) << "    "
             << juce::String(ChordMatcher::name(displayedEvents[i].chord).data()) << "    "
             << juce::String(displayedEvents[i].confidence, 0) << "%\n";
    historyBox.setText(text, false);
    historyBox.moveCaretToEnd();
}

juce::String SanekChordFinderAudioProcessorEditor::buildSequence() const
{
    const auto snapshot = processor.getHistorySnapshot();
    if (snapshot.empty())
        return {};
    juce::String result("| ");
    for (const auto& event : snapshot)
        result << juce::String(ChordMatcher::name(event.chord).data()) << " | ";
    return result;
}

juce::String SanekChordFinderAudioProcessorEditor::eventPosition(const ChordEvent& event)
{
    if (event.bar > 0 && event.beat > 0.0f)
        return juce::String(event.bar) + "." + juce::String(static_cast<int>(std::floor(event.beat)));
    const int totalSeconds = std::max(0, static_cast<int>(std::floor(event.seconds)));
    const int minutes = totalSeconds / 60;
    const int seconds = totalSeconds % 60;
    return juce::String(minutes).paddedLeft('0', 2) + ":"
         + juce::String(seconds).paddedLeft('0', 2);
}
