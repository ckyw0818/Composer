#pragma once

#include "domain/Rhythm.h"
#include "domain/Types.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <cstdint>
#include <string>

namespace composer::ui {

// S2 arrangement commands kept separate from transport. The bar selects built-in instruments and
// deterministic rhythm settings, while the host owns project/file operations.
class ArrangementBar final : public juce::Component {
public:
    ArrangementBar();

    std::function<void(const std::string& instrumentId)> onAddInstrument;
    std::function<void()> onAddAudioTrack;
    std::function<void(domain::RhythmPattern pattern, domain::Tick subdivision,
        std::uint64_t seed)> onPreviewRhythm;
    std::function<void(domain::Tick subdivision)> onGridChanged;
    std::function<void()> onApplyRhythm;
    std::function<void()> onCancelRhythm;
    std::function<void()> onUndo;
    std::function<void()> onRedo;
    std::function<void()> onImportMidi;
    std::function<void()> onExportMidi;
    std::function<void()> onOpenProject;
    std::function<void()> onSaveProject;
    std::function<void()> onNewEmptyProject;
    std::function<void()> onLoadBandSketch;
    std::function<void()> onOpenChord;

    void setStatus(const juce::String& text, bool isError = false);
    void setUndoRedoEnabled(bool canUndo, bool canRedo);
    void setInstrumentRole(domain::InstrumentRole role);

    void paint(juce::Graphics& graphics) override;
    void resized() override;

private:
    [[nodiscard]] domain::RhythmPattern selectedPattern() const noexcept;
    [[nodiscard]] domain::Tick selectedSubdivision() const noexcept;
    [[nodiscard]] std::uint64_t selectedSeed() const noexcept;

    juce::ComboBox instrument_;
    juce::TextButton addInstrument_{"Add instrument"};
    juce::TextButton addAudio_{"Add audio"};
    juce::ComboBox pattern_;
    juce::ComboBox subdivision_;
    juce::Slider seed_;
    juce::TextButton preview_{"Preview"};
    juce::TextButton chord_{"Chord"};
    juce::TextButton apply_{"Apply"};
    juce::TextButton cancel_{"Cancel"};
    juce::TextButton undo_{"Undo"};
    juce::TextButton redo_{"Redo"};
    juce::TextButton importMidi_{"Import MIDI"};
    juce::TextButton exportMidi_{"Export MIDI"};
    juce::TextButton openProject_{"Open"};
    juce::TextButton saveProject_{"Save"};
    juce::TextButton emptyProject_{"Empty"};
    juce::TextButton bandSketch_{"Band sketch"};
    juce::Label status_;
};

}  // namespace composer::ui
