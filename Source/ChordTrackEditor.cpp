#include "ChordTrackEditor.h"
#include "ChordMidi.h"

ChordTrackEditor::ChordTrackEditor(SanekChordFinderAudioProcessor& owner)
    : processor(owner), track(owner.getChordTrack())
{
    for (auto* component : std::initializer_list<juce::Component*> {
             &table, &loadButton, &deleteButton, &exportButton, &durationBox, &meterBox, &scopeBox, &tempo, &status })
        addAndMakeVisible(component);
    table.getHeader().addColumn("#", 1, 35);
    table.getHeader().addColumn("Start beat", 2, 95);
    table.getHeader().addColumn("Root", 3, 90);
    table.getHeader().addColumn("Type", 4, 120);
    table.getHeader().addColumn("Notes", 5, 205);
    table.getHeader().addColumn("Length (beats)", 6, 145);
    table.getHeader().setStretchToFitActive(true);
    table.setRowHeight(29);
    table.setColour(juce::ListBox::backgroundColourId, juce::Colour(0xff17212b));
    table.setMultipleSelectionEnabled(false);
    durationBox.addItemList({ "1 BEAT", "HALF BAR", "WHOLE BAR", "FOLLOW AUDIO" }, 1);
    durationBox.setSelectedId(track.duration + 1, juce::dontSendNotification);
    durationBox.setTooltip("FOLLOW AUDIO sustains each chord until the next change. Other choices cap note length and may leave rests. Changing duration does not move chord starts.");
    durationBox.onChange = [this] { track.duration = durationBox.getSelectedId() - 1; changed(); };
    scopeBox.addItemList({ "ONE LOOP", "FULL TAKE" }, 1);
    scopeBox.setSelectedId(track.scope + 1, juce::dontSendNotification);
    scopeBox.setTooltip("ONE LOOP detects the first repeated chord pattern and exports one cycle. FULL TAKE keeps every detected repetition.");
    scopeBox.onChange = [this] { track.scope = scopeBox.getSelectedId() - 1; changed(); };
    meterBox.addItem("3/4", 3);
    meterBox.addItem("4/4", 4);
    meterBox.addItem("6/8", 6);
    meterBox.setSelectedId(track.meter, juce::dontSendNotification);
    meterBox.onChange = [this] { track.meter = meterBox.getSelectedId(); changed(); };
    tempo.setFont(juce::Font(juce::FontOptions(12.0f, juce::Font::bold)));
    tempo.setColour(juce::Label::textColourId, juce::Colour(0xff55d6e5));
    tempo.setTooltip("LOAD HISTORY measures export tempo from the accumulated audio, independently of Ableton. Edited tracks retain their captured tempo. In 6/8, BPM counts eighth notes.");
    loadButton.setTooltip("Replace the editing table with a snapshot of detected chords, rounded to the nearest beat. Existing edits are replaced.");
    loadButton.onClick = [this] { loadHistory(); };
    deleteButton.onClick = [this]
    {
        const int row = table.getSelectedRow();
        if (row >= 0 && row < getNumRows())
        {
            track.rows.erase(track.rows.begin() + row);
            table.deselectAllRows();
            changed();
        }
    };
    exportButton.onClick = [this] { exportMidi(); };
    status.setFont(juce::Font(juce::FontOptions(12.0f)));
    status.setColour(juce::Label::textColourId, juce::Colour(0xffa78bfa));
    changed();
    startTimerHz(5);
}

ChordTrackEditor::~ChordTrackEditor()
{
    stopTimer();
    chooser.reset();
    table.setModel(nullptr);
}

void ChordTrackEditor::paint(juce::Graphics& g)
{
    g.setColour(juce::Colour(0xfff2f5f7));
    g.setFont(juce::Font(juce::FontOptions(14.0f, juce::Font::bold)));
    g.drawText("CHORD TRACK  /  EDIT BEFORE EXPORT", 0, 0, 430, 24, juce::Justification::centredLeft);
}

void ChordTrackEditor::resized()
{
    loadButton.setBounds(0, 30, 126, 29);
    deleteButton.setBounds(134, 30, 115, 29);
    tempo.setBounds(260, 30, 105, 29);
    meterBox.setBounds(370, 30, 65, 29);
    scopeBox.setBounds(440, 30, 100, 29);
    durationBox.setBounds(545, 30, 100, 29);
    exportButton.setBounds(650, 30, getWidth() - 650, 29);
    table.setBounds(0, 68, getWidth(), getHeight() - 98);
    status.setBounds(0, getHeight() - 26, getWidth(), 26);
}

void ChordTrackEditor::paintRowBackground(juce::Graphics& g, int row, int width, int height, bool selected)
{
    g.fillAll(juce::Colour(selected ? 0xff304859 : (row % 2 == 0 ? 0xff17212b : 0xff1c2935)));
    juce::ignoreUnused(width, height);
}

void ChordTrackEditor::paintCell(juce::Graphics& g, int row, int column, int width, int height, bool)
{
    if (row < 0 || row >= getNumRows()) return;
    const auto index = static_cast<size_t>(row);
    juce::String text;
    if (column == 1) text = juce::String(row + 1);
    if (column == 5)
        for (const int note : ChordTrack::notes(track.rows[index].chord))
            text += juce::String(ChordMatcher::name(note % 12).data()) + " ";
    if (column == 6) text = juce::String(std::max(0.0, track.endBeat(index) - track.rows[index].beat), 1);
    g.setColour(juce::Colour(0xfff2f5f7));
    g.setFont(juce::Font(juce::FontOptions(13.0f)));
    g.drawText(text, 7, 0, width - 10, height, juce::Justification::centredLeft);
}

juce::Component* ChordTrackEditor::refreshComponentForCell(int row, int column, bool, juce::Component* existing)
{
    if (row < 0 || row >= getNumRows() || column < 2 || column > 4)
    {
        delete existing;
        return nullptr;
    }
    const auto index = static_cast<size_t>(row);
    if (column == 2)
    {
        auto* label = dynamic_cast<juce::Label*>(existing);
        if (label == nullptr) { delete existing; label = new juce::Label(); }
        label->setEditable(true);
        label->setColour(juce::Label::textColourId, juce::Colour(0xff55d6e5));
        label->setTooltip("Click to edit. Beats start at 1; in 4/4, beats 1, 5, 9 are bar starts.");
        label->setText(juce::String(track.rows[index].beat + 1.0, 0), juce::dontSendNotification);
        label->onTextChange = [this, label, index]
        {
            if (index >= track.rows.size()) return;
            const auto input = label->getText().trim();
            const int beat = input.getIntValue();
            if (input.containsOnly("0123456789") && input.isNotEmpty() && beat >= 1 && beat <= 100001)
                track.rows[index].beat = beat - 1.0;
            label->setText(juce::String(track.rows[index].beat + 1.0, 0), juce::dontSendNotification);
            changed();
        };
        return label;
    }
    auto* box = dynamic_cast<juce::ComboBox*>(existing);
    if (box == nullptr) { delete existing; box = new juce::ComboBox(); }
    box->onChange = nullptr;
    box->clear(juce::dontSendNotification);
    if (column == 3)
    {
        box->addItemList({ "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }, 1);
        box->setSelectedId(ChordMatcher::rootOf(track.rows[index].chord) + 1, juce::dontSendNotification);
    }
    else
    {
        box->addItemList({ "major", "minor", "7", "maj7", "m7", "sus2", "sus4", "dim" }, 1);
        box->setSelectedId(ChordMatcher::qualityOf(track.rows[index].chord) + 1, juce::dontSendNotification);
    }
    box->onChange = [this, box, index, column]
    {
        if (index >= track.rows.size() || box->getSelectedId() <= 0) return;
        const int old = track.rows[index].chord;
        track.rows[index].chord = column == 3
            ? ChordMatcher::qualityOf(old) * 12 + box->getSelectedId() - 1
            : (box->getSelectedId() - 1) * 12 + ChordMatcher::rootOf(old);
        changed();
    };
    return box;
}

void ChordTrackEditor::changed()
{
    refreshTempoLabel();
    processor.setChordTrack(track);
    revision = processor.getTrackRevision();
    table.updateContent();
    table.repaint();
    const auto error = track.error();
    exportButton.setEnabled(error.empty() && !choosing);
    status.setText(error.empty() ? "Edit root, type or start beat. Length stops at the next chord. Ready to export."
                                 : juce::String(error), juce::dontSendNotification);
}

void ChordTrackEditor::timerCallback()
{
    refreshTempoLabel();
    if (revision == processor.getTrackRevision()) return;
    revision = processor.getTrackRevision();
    track = processor.getChordTrack();
    refreshTempoLabel();
    meterBox.setSelectedId(track.meter, juce::dontSendNotification);
    durationBox.setSelectedId(track.duration + 1, juce::dontSendNotification);
    scopeBox.setSelectedId(track.scope + 1, juce::dontSendNotification);
    table.updateContent();
    table.repaint();
    exportButton.setEnabled(track.error().empty() && !choosing);
    status.setText("Chord Track restored from saved plugin state.", juce::dontSendNotification);
}

void ChordTrackEditor::refreshTempoLabel()
{
    const double bpm = track.rows.empty() ? processor.getExportBpm() : track.bpm;
    tempo.setText(bpm > 0.0 ? (track.rows.empty() ? "AUTO " : "MIDI ") + juce::String(bpm, 1) + " BPM"
                            : "AUTO BPM --", juce::dontSendNotification);
}

void ChordTrackEditor::loadHistory()
{
    const auto history = processor.getHistorySnapshot();
    if (history.empty())
    {
        status.setText("No detected chords. Start listening and play audio first.", juce::dontSendNotification);
        return;
    }
    const auto detectedBpm = processor.getExportBpm();
    const bool locked = detectedBpm >= 30.0 && detectedBpm <= 300.0;
    if (!locked)
    {
        status.setText("Auto BPM is still listening. Play a steady rhythm for a few more seconds, then load history.",
                       juce::dontSendNotification);
        return;
    }
    track.bpm = detectedBpm;
    const auto meterIndex = static_cast<int>(processor.parameters.getRawParameterValue("meter")->load());
    track.meter = meterIndex == 0 ? 3 : (meterIndex == 2 ? 6 : 4);
    // The first accepted chord is the beginning of the exported clip. Time spent
    // waiting before playback must never become silence at the front of the MIDI.
    const double origin = history.front().seconds;
    const double end = std::max(0.0, processor.getRecordedEndSeconds() - origin) * track.bpm / 60.0;
    track.recordedEndBeat = std::ceil(end / track.meter) * track.meter;
    track.rows.clear();
    for (const auto& event : history)
        if (ChordMatcher::isValid(event.chord) && std::isfinite(event.seconds))
            track.rows.push_back({ event.chord, ChordTrack::quantize(event.seconds, origin, track.bpm) });
    const bool loopFound = track.keepOneLoop();
    track.splitHeldChords();
    meterBox.setSelectedId(track.meter, juce::dontSendNotification);
    table.deselectAllRows();
    changed();
    if (track.scope == 0)
        status.setText(loopFound ? "Loaded one loop from beat 1; held chords were split by the selected length."
                                 : "No complete repetition found yet. Loaded the available take from beat 1.",
                       juce::dontSendNotification);
}

void ChordTrackEditor::exportMidi()
{
    if (!track.error().empty() || choosing) return;
    const auto snapshot = track;
    choosing = true;
    exportButton.setEnabled(false);
    chooser = std::make_unique<juce::FileChooser>("Export Chord Track MIDI",
        juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("Sanek Chord Track.mid"), "*.mid");
    chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
        [safe = juce::Component::SafePointer<ChordTrackEditor>(this), snapshot](const juce::FileChooser& dialog)
        {
            if (safe == nullptr) return;
            safe->choosing = false;
            safe->changed();
            const auto chosen = dialog.getResult();
            if (chosen == juce::File()) return;
            const auto target = chosen.withFileExtension("mid");
            if (target.existsAsFile())
            {
                juce::AlertWindow::showAsync(juce::MessageBoxOptions()
                    .withIconType(juce::MessageBoxIconType::QuestionIcon)
                    .withTitle("Replace MIDI file?").withMessage(target.getFullPathName())
                    .withButton("Replace").withButton("Cancel"),
                    [safe, snapshot, target](int result)
                    {
                        if (result == 1 && safe != nullptr) safe->saveMidi(snapshot, target);
                    });
            }
            else safe->saveMidi(snapshot, target);
        });
}

void ChordTrackEditor::saveMidi(const ChordTrack& snapshot, const juce::File& target)
{
    const auto result = ChordMidi::save(snapshot, target);
    status.setText(result.wasOk() ? "Saved: " + target.getFullPathName() : "Export failed: " + result.getErrorMessage(),
                   juce::dontSendNotification);
    status.setTooltip(status.getText());
}
