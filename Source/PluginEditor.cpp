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
    : AudioProcessorEditor(&owner), processor(owner), trackEditor(owner)
{
    setLookAndFeel(&look);
    addAndMakeVisible(trackEditor);

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

    meterBox.addItemList({ "3/4", "4/4", "6/8" }, 1);
    meterBox.setTooltip("Internal bar size. This does not read Ableton's tempo or meter.");
    addAndMakeVisible(meterBox);
    meterAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        processor.parameters, "meter", meterBox);
    meterLabel.setText("METER", juce::dontSendNotification);
    meterLabel.setFont(uiFont(12.0f, true));
    meterLabel.setColour(juce::Label::textColourId, muted);
    meterLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(meterLabel);

    chordSetBox.addItemList({ "TRIADS", "EXTENDED" }, 1);
    chordSetBox.setTooltip("TRIADS detects major/minor. EXTENDED enables all 20 types, including 5, aug, m7b5, dim7, 6, m6, add9, madd9, 9, maj9, m9 and 7sus4.");
    addAndMakeVisible(chordSetBox);
    chordSetAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        processor.parameters, "chordSet", chordSetBox);
    chordSetLabel.setText("CHORD SET", juce::dontSendNotification);
    chordSetLabel.setFont(uiFont(11.0f, true));
    chordSetLabel.setColour(juce::Label::textColourId, muted);
    chordSetLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(chordSetLabel);

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
    keyLabel.setFont(uiFont(15.0f, true));
    keyLabel.setColour(juce::Label::textColourId, violet);
    keyLabel.setJustificationType(juce::Justification::centred);
    keyLabel.setTooltip("Estimated key, confidence and the current chord's harmonic degree. The estimate improves as more different chords arrive.");
    addAndMakeVisible(keyLabel);
    bpmLabel.setFont(uiFont(13.0f, true));
    bpmLabel.setTooltip("Current tempo measured from incoming audio only. Ableton BPM is never read. The bar grid keeps its first confirmed tempo; restart Listening for a new grid.");
    bpmLabel.setColour(juce::Label::textColourId, cyan);
    bpmLabel.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(bpmLabel);
    tuningLabel.setFont(uiFont(12.0f, true));
    tuningLabel.setColour(juce::Label::textColourId, violet);
    tuningLabel.setTooltip("Automatic common tuning offset from audio. Audio and MIDI pitches are not retuned. Calibration restarts estimation; play a sustained clean note or chord.");
    addAndMakeVisible(tuningLabel);
    addAndMakeVisible(calibrateButton);
    calibrateButton.onClick = [this] { processor.requestTuningCalibration(); };

    historyBox.setMultiLine(true);
    historyBox.setReadOnly(true);
    historyBox.setScrollbarsShown(true);
    historyBox.setCaretVisible(false);
    historyBox.setFont(uiFont(14.0f));
    historyBox.setColour(juce::TextEditor::backgroundColourId, panel);
    historyBox.setColour(juce::TextEditor::textColourId, pale);
    historyBox.setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    historyBox.setColour(juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
    historyBox.setText("Press START LISTENING, then play audio in Ableton.", false);
    addAndMakeVisible(historyBox);

    newBarButton.setTooltip("Make the current moment bar 1 beat 1 and clear the old sequence.");
    newBarButton.onClick = [this]
    {
        processor.requestNewBar();
        processor.clearHistory();
        displayedEvents.clear();
        displayedKey = {};
        historyBox.setText("New bar marked. Listening for chords...", false);
    };
    addAndMakeVisible(newBarButton);

    clearButton.setTooltip("Reset the detected sequence and key analysis.");
    clearButton.onClick = [this]
    {
        processor.clearHistory();
        displayedEvents.clear();
        displayedKey = {};
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

    setSize(820, 900);
    timerCallback();
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
    g.drawText("CHORDS  /  KEY  /  TEMPO  /  BARS", 27, 78, 350, 18,
               juce::Justification::centredLeft);

    g.setColour(panel);
    g.fillRoundedRectangle(25.0f, 116.0f, 480.0f, 290.0f, 12.0f);
    g.fillRoundedRectangle(525.0f, 116.0f, 270.0f, 440.0f, 12.0f);
    g.setColour(muted);
    g.setFont(uiFont(12.0f, true));
    g.drawText("CURRENT CHORD", 43, 130, 200, 20, juce::Justification::centredLeft);
    g.drawText("SEQUENCE / DEGREE", 543, 130, 200, 20, juce::Justification::centredLeft);

    const auto confidenceArea = juce::Rectangle<float>(55.0f, 297.0f, 420.0f, 8.0f);
    g.setColour(juce::Colour(0xff31404d));
    g.fillRoundedRectangle(confidenceArea, 4.0f);
    g.setColour(displayedConfidence >= 45.0f ? cyan : violet);
    g.fillRoundedRectangle(confidenceArea.withWidth(confidenceArea.getWidth()
                           * juce::jlimit(0.0f, 1.0f, displayedConfidence / 100.0f)), 4.0f);

    g.setFont(uiFont(11.0f, true));
    if (displayedTempoLocked)
    {
        g.setColour(muted);
        g.drawText("BAR " + juce::String(displayedBar), 55, 382, 72, 18,
                   juce::Justification::centredLeft);
        const float flash = 1.0f - juce::jlimit(0.0f, 1.0f, displayedBeatPhase / 0.24f);
        const float start = 145.0f;
        for (int beat = 1; beat <= displayedBeatsPerBar; ++beat)
        {
            const auto area = juce::Rectangle<float>(
                                                      start + static_cast<float>(beat - 1) * 35.0f,
                                                      382.0f,
                                                      18.0f, 18.0f);
            if (beat == displayedBeat)
                g.setColour(cyan.withAlpha(0.45f + 0.55f * flash));
            else
                g.setColour(juce::Colour(0xff334250));
            g.fillEllipse(area);
            g.setColour(background);
            g.setFont(uiFont(9.0f, true));
            g.drawText(juce::String(beat), area.toNearestInt(), juce::Justification::centred);
        }
    }
    else
    {
        g.setColour(muted);
        g.drawText("AUTO TEMPO: LISTENING FOR A STEADY RHYTHM", 55, 382, 420, 18,
                   juce::Justification::centredLeft);
    }

    g.setFont(uiFont(10.0f, true));
    const std::array<const char*, 12> noteNames { "C", "C#", "D", "D#", "E", "F",
                                                  "F#", "G", "G#", "A", "A#", "B" };
    const float startX = 32.0f, width = 38.0f, baseY = 520.0f;
    for (size_t i = 0; i < displayedChroma.size(); ++i)
    {
        const float height = 68.0f * juce::jlimit(0.0f, 1.0f, displayedChroma[i]);
        g.setColour(juce::Colour(0xff263441));
        g.fillRoundedRectangle(startX + static_cast<float>(i) * width, baseY - 68.0f,
                               width - 7.0f, 68.0f, 3.0f);
        g.setColour(ChordMatcher::containsPitch(displayedChord, static_cast<int>(i))
                        ? cyan : violet);
        g.fillRoundedRectangle(startX + static_cast<float>(i) * width, baseY - height,
                               width - 7.0f, height, 3.0f);
        g.setColour(muted);
        g.drawText(noteNames[i], juce::roundToInt(startX + static_cast<float>(i) * width),
                   527, juce::roundToInt(width - 7.0f), 18, juce::Justification::centred);
    }
    g.setColour(muted);
    g.setFont(uiFont(11.0f));
    g.drawText("PITCH CLASSES", 32, 556, 200, 18, juce::Justification::centredLeft);
    g.drawText("v0.8.1 Chord Track", 630, 878, 164, 18, juce::Justification::centredRight);
}

void SanekChordFinderAudioProcessorEditor::resized()
{
    listeningButton.setBounds(615, 35, 165, 38);
    sensitivityLabel.setBounds(386, 21, 112, 18);
    sensitivityKnob.setBounds(392, 36, 100, 72);
    meterLabel.setBounds(510, 21, 82, 18);
    meterBox.setBounds(510, 44, 82, 29);
    chordSetLabel.setBounds(592, 79, 88, 18);
    chordSetBox.setBounds(681, 76, 105, 27);
    currentChordLabel.setBounds(45, 151, 440, 112);
    keyLabel.setBounds(55, 266, 420, 27);
    confidenceLabel.setBounds(55, 311, 420, 21);
    alternativeLabel.setBounds(55, 332, 420, 20);
    bpmLabel.setBounds(55, 353, 275, 25);
    newBarButton.setBounds(350, 350, 125, 31);
    tuningLabel.setBounds(27, 413, 308, 28);
    calibrateButton.setBounds(337, 413, 168, 28);
    historyBox.setBounds(541, 157, 238, 335);
    clearButton.setBounds(541, 510, 90, 31);
    copyButton.setBounds(642, 510, 137, 31);
    trackEditor.setBounds(25, 585, 770, 290);
}

void SanekChordFinderAudioProcessorEditor::timerCallback()
{
    const bool active = processor.isListening();
    listeningButton.setButtonText(active ? "LISTENING" : "START LISTENING");
    listeningButton.setColour(juce::ToggleButton::tickColourId, active ? cyan : muted);
    displayedChord = active ? processor.getCurrentChord() : -1;
    currentChordLabel.setText(active ? juce::String(ChordTrack::name(displayedChord, processor.getCurrentBass())) : "--",
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
    displayedTempoLocked = active && processor.isTempoLocked();
    displayedBpm = displayedTempoLocked ? processor.getAutoBpm() : 0.0f;
    displayedBeatPhase = processor.getBeatPhase();
    displayedBar = processor.getCurrentBar();
    displayedBeat = processor.getCurrentBeat();
    displayedBeatsPerBar = processor.getBeatsPerBar();
    const bool hasLiveTempo = active && processor.getLiveBpm() > 0.0f;
    const float audibleBpm = hasLiveTempo ? processor.getLiveBpm() : processor.getLastHeardBpm();
    bpmLabel.setText(audibleBpm > 0.0f
        ? "AUTO BPM  " + juce::String(audibleBpm, 1)
            + (hasLiveTempo ? "   " + juce::String(processor.getLiveTempoConfidence(), 0) + "%" : "   LAST HEARD")
        : (active ? "AUTO BPM  LISTENING FOR RHYTHM..." : "AUTO BPM  --"), juce::dontSendNotification);
    newBarButton.setEnabled(active);
    calibrateButton.setEnabled(active);
    const double cents = processor.getTuningCents();
    tuningLabel.setText(active && processor.isTuningReady()
        ? "A = " + juce::String(440.0 * std::exp2(cents / 1200.0), 1) + " Hz   "
            + (cents >= 0.0 ? "+" : "") + juce::String(cents, 1) + " cents"
        : (active ? "TUNING  LEARNING..." : "TUNING  --"), juce::dontSendNotification);
    refreshHistory();
    if (displayedKey.key >= 0)
    {
        juce::String text = "KEY  "
            + juce::String(KeyDetector::name(displayedKey.key).data()).toUpperCase()
            + "  " + juce::String(displayedKey.confidence, 0) + "%";
        if (displayedChord >= 0)
            text += "     DEGREE  "
                 + juce::String(KeyDetector::degreeName(displayedKey.key, displayedChord));
        keyLabel.setText(text, juce::dontSendNotification);
    }
    else if (!displayedEvents.empty())
    {
        keyLabel.setText("KEY  LEARNING...  " + juce::String(displayedKey.distinctRoots)
                         + "/3 DIFFERENT CHORDS", juce::dontSendNotification);
    }
    else
    {
        keyLabel.setText(active ? "KEY  LEARNING..." : "KEY  --", juce::dontSendNotification);
    }
    repaint();
}

void SanekChordFinderAudioProcessorEditor::refreshHistory()
{
    const auto snapshot = processor.getHistorySnapshot();
    if (snapshot.size() == displayedEvents.size()
        && (snapshot.empty() || (snapshot.back().chord == displayedEvents.back().chord
                                 && snapshot.back().seconds == displayedEvents.back().seconds
                                 && snapshot.back().bassNote == displayedEvents.back().bassNote
                                 && std::floor(snapshot.back().durationSeconds) == std::floor(displayedEvents.back().durationSeconds)))
        && historyTempoLocked == displayedTempoLocked
        && std::abs(historyBpm - displayedBpm) < 0.25f)
        return;
    displayedEvents = snapshot;
    std::vector<KeyObservation> observations;
    observations.reserve(displayedEvents.size());
    for (const auto& event : displayedEvents)
        observations.push_back({ event.chord, event.confidence, event.durationSeconds });
    const auto timeline = KeyTimeline::analyse(observations);
    displayedKey = timeline.current;
    historyTempoLocked = displayedTempoLocked;
    historyBpm = displayedBpm;
    if (displayedEvents.empty())
    {
        historyBox.setText("No chords recorded yet.", false);
        return;
    }
    juce::String text;
    const size_t first = displayedEvents.size() > 40 ? displayedEvents.size() - 40 : 0;
    for (size_t i = first; i < displayedEvents.size(); ++i)
    {
        int eventKey = timeline.initialKey;
        for (const auto& change : timeline.changes)
        {
            if (change.observation <= i) eventKey = change.key;
            if (change.observation == i)
                text << eventPosition(displayedEvents[i]) << " KEY CHANGE -> "
                     << juce::String(KeyDetector::name(change.key).data()) << "\n";
        }
        text << eventPosition(displayedEvents[i]) << "  "
             << juce::String(ChordTrack::name(displayedEvents[i].chord, displayedEvents[i].bassNote));
        if (displayedEvents[i].bar > 0)
        {
            text << "  ";
            for (int tone = 0; tone < ChordMatcher::toneCount(displayedEvents[i].chord); ++tone)
                text << juce::String(ChordTrack::toneName(displayedEvents[i].chord, tone)) << " ";
        }
        else if (eventKey >= 0)
            text << "  " << juce::String(
                KeyDetector::degreeName(eventKey, displayedEvents[i].chord));
        if (displayedEvents[i].bar <= 0) text << "  " << juce::String(displayedEvents[i].confidence, 0) << "%";
        text << "\n";
    }
    historyBox.setText(text, false);
    historyBox.moveCaretToEnd();
}

juce::String SanekChordFinderAudioProcessorEditor::buildSequence() const
{
    const auto snapshot = processor.getHistorySnapshot();
    if (snapshot.empty())
        return {};
    juce::String result("| ");
    int currentBar = -1;
    for (const auto& event : snapshot)
    {
        const auto position = processor.getInternalPosition(event.seconds);
        if (position.bar > 0 && position.bar != currentBar)
        {
            if (currentBar >= 0)
                result << "| ";
            currentBar = position.bar;
        }
        result << juce::String(ChordTrack::name(event.chord, event.bassNote)) << " ";
    }
    result << "|";
    return result;
}

juce::String SanekChordFinderAudioProcessorEditor::eventPosition(const ChordEvent& event) const
{
    if (event.bar > 0)
        return "BAR " + juce::String(event.bar)
            + (event.beat > 1.0f ? "." + juce::String(event.beat, 0) : juce::String());
    const auto position = processor.getInternalPosition(event.seconds);
    if (position.bar > 0 && position.beat > 0)
        return juce::String(position.bar) + "." + juce::String(position.beat);
    const int totalSeconds = std::max(0, static_cast<int>(std::floor(event.seconds)));
    const int minutes = totalSeconds / 60;
    const int seconds = totalSeconds % 60;
    return juce::String(minutes).paddedLeft('0', 2) + ":"
         + juce::String(seconds).paddedLeft('0', 2);
}
