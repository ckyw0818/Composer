#include "ui/ArrangementBar.h"

#include "domain/Instrument.h"
#include "ui/Theme.h"

#include <array>

namespace composer::ui {

ArrangementBar::ArrangementBar() {
    for (int index = 0; index < static_cast<int>(domain::kBuiltinInstruments.size()); ++index) {
        const auto& instrument = domain::kBuiltinInstruments[static_cast<std::size_t>(index)];
        instrument_.addItem(juce::String{instrument.displayName.data()}, index + 1);
    }
    instrument_.setSelectedId(1, juce::dontSendNotification);
    instrument_.setTitle("Built-in instrument");

    pattern_.addItem("Sustained", 1);
    pattern_.addItem("Block", 2);
    pattern_.addItem("Arpeggio", 3);
    pattern_.addItem("Strum", 4);
    pattern_.addItem("Bass pulse", 5);
    pattern_.setSelectedId(2, juce::dontSendNotification);
    pattern_.setTitle("Rhythm pattern");

    subdivision_.addItem("1/4", 1);
    subdivision_.addItem("1/8", 2);
    subdivision_.addItem("1/16", 3);
    subdivision_.setSelectedId(2, juce::dontSendNotification);
    subdivision_.setTitle("Piano-roll grid and rhythm subdivision");

    seed_.setRange(0.0, 9999.0, 1.0);
    seed_.setValue(0.0, juce::dontSendNotification);
    seed_.setSliderStyle(juce::Slider::LinearHorizontal);
    seed_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 52, 24);
    seed_.setTitle("Deterministic rhythm seed");

    for (auto* component : std::array<juce::Component*, 19>{&instrument_, &addInstrument_, &addAudio_,
             &pattern_, &subdivision_, &seed_, &chord_, &preview_, &apply_, &cancel_, &undo_, &redo_,
             &importMidi_, &exportMidi_, &openProject_, &saveProject_, &emptyProject_,
             &bandSketch_, &status_}) {
        addAndMakeVisible(*component);
    }

    status_.setColour(juce::Label::textColourId, theme::textSecondary);
    status_.setJustificationType(juce::Justification::centredLeft);
    status_.setText("Select a chord hit, then preview a rhythm.", juce::dontSendNotification);

    addInstrument_.onClick = [this] {
        const int index = instrument_.getSelectedId() - 1;
        if (index >= 0 && index < static_cast<int>(domain::kBuiltinInstruments.size())
            && onAddInstrument) {
            onAddInstrument(std::string{
                domain::kBuiltinInstruments[static_cast<std::size_t>(index)].instrumentId});
        }
    };
    addAudio_.onClick = [this] { if (onAddAudioTrack) onAddAudioTrack(); };
    preview_.onClick = [this] {
        if (onPreviewRhythm) {
            onPreviewRhythm(selectedPattern(), selectedSubdivision(), selectedSeed());
        }
    };
    subdivision_.onChange = [this] {
        if (onGridChanged) onGridChanged(selectedSubdivision());
    };
    apply_.onClick = [this] { if (onApplyRhythm) onApplyRhythm(); };
    cancel_.onClick = [this] { if (onCancelRhythm) onCancelRhythm(); };
    undo_.onClick = [this] { if (onUndo) onUndo(); };
    redo_.onClick = [this] { if (onRedo) onRedo(); };
    importMidi_.onClick = [this] { if (onImportMidi) onImportMidi(); };
    exportMidi_.onClick = [this] { if (onExportMidi) onExportMidi(); };
    openProject_.onClick = [this] { if (onOpenProject) onOpenProject(); };
    saveProject_.onClick = [this] { if (onSaveProject) onSaveProject(); };
    emptyProject_.onClick = [this] { if (onNewEmptyProject) onNewEmptyProject(); };
    bandSketch_.onClick = [this] { if (onLoadBandSketch) onLoadBandSketch(); };
    chord_.onClick = [this] { if (onOpenChord) onOpenChord(); };
}

domain::RhythmPattern ArrangementBar::selectedPattern() const noexcept {
    switch (pattern_.getSelectedId()) {
        case 1: return domain::RhythmPattern::sustained;
        case 3: return domain::RhythmPattern::arpeggio;
        case 4: return domain::RhythmPattern::strum;
        case 5: return domain::RhythmPattern::bassPulse;
        default: return domain::RhythmPattern::block;
    }
}

domain::Tick ArrangementBar::selectedSubdivision() const noexcept {
    switch (subdivision_.getSelectedId()) {
        case 1: return domain::kPpq;
        case 3: return domain::kPpq / 4;
        default: return domain::kPpq / 2;
    }
}

std::uint64_t ArrangementBar::selectedSeed() const noexcept {
    return static_cast<std::uint64_t>(seed_.getValue());
}

void ArrangementBar::setStatus(const juce::String& text, const bool isError) {
    status_.setColour(juce::Label::textColourId, isError ? theme::warning : theme::textSecondary);
    status_.setText(text, juce::dontSendNotification);
}

void ArrangementBar::setUndoRedoEnabled(const bool canUndo, const bool canRedo) {
    undo_.setEnabled(canUndo);
    redo_.setEnabled(canRedo);
}

void ArrangementBar::setInstrumentRole(const domain::InstrumentRole role) {
    const auto pattern = domain::defaultPatternFor(role);
    int selected = 2;
    switch (pattern) {
        case domain::RhythmPattern::sustained: selected = 1; break;
        case domain::RhythmPattern::block: selected = 2; break;
        case domain::RhythmPattern::arpeggio: selected = 3; break;
        case domain::RhythmPattern::strum: selected = 4; break;
        case domain::RhythmPattern::bassPulse: selected = 5; break;
    }
    pattern_.setSelectedId(selected, juce::dontSendNotification);
}

void ArrangementBar::paint(juce::Graphics& graphics) {
    graphics.fillAll(theme::raised);
    graphics.setColour(theme::border);
    graphics.drawHorizontalLine(getHeight() - 1, 0.0F, static_cast<float>(getWidth()));
}

void ArrangementBar::resized() {
    auto area = getLocalBounds().reduced(8, 5);
    auto firstRow = area.removeFromTop(32);
    instrument_.setBounds(firstRow.removeFromLeft(116));
    firstRow.removeFromLeft(4);
    addInstrument_.setBounds(firstRow.removeFromLeft(112));
    firstRow.removeFromLeft(4);
    addAudio_.setBounds(firstRow.removeFromLeft(82));
    firstRow.removeFromLeft(6);
    emptyProject_.setBounds(firstRow.removeFromLeft(58));
    bandSketch_.setBounds(firstRow.removeFromLeft(82));
    firstRow.removeFromLeft(6);
    undo_.setBounds(firstRow.removeFromLeft(54));
    redo_.setBounds(firstRow.removeFromLeft(54));
    firstRow.removeFromLeft(10);
    openProject_.setBounds(firstRow.removeFromLeft(58));
    saveProject_.setBounds(firstRow.removeFromLeft(58));
    firstRow.removeFromLeft(8);
    importMidi_.setBounds(firstRow.removeFromLeft(88));
    exportMidi_.setBounds(firstRow.removeFromLeft(88));
    firstRow.removeFromLeft(8);
    status_.setBounds(firstRow);

    area.removeFromTop(4);
    auto secondRow = area.removeFromTop(32);
    pattern_.setBounds(secondRow.removeFromLeft(112));
    secondRow.removeFromLeft(4);
    subdivision_.setBounds(secondRow.removeFromLeft(68));
    secondRow.removeFromLeft(4);
    seed_.setBounds(secondRow.removeFromLeft(170));
    secondRow.removeFromLeft(4);
    chord_.setBounds(secondRow.removeFromLeft(60));
    preview_.setBounds(secondRow.removeFromLeft(68));
    apply_.setBounds(secondRow.removeFromLeft(56));
    cancel_.setBounds(secondRow.removeFromLeft(60));
}

}  // namespace composer::ui
