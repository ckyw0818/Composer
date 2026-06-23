#pragma once

#include "commands/ProjectEditor.h"
#include "domain/Project.h"
#include "domain/Rhythm.h"
#include "domain/Types.h"
#include "ui/TimelineViewport.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <optional>
#include <vector>

namespace composer::ui {

// Direct-manipulation piano roll for a single MIDI clip.
//
// Interactions (DESIGN.md Interaction Design):
//   - double-click empty cell  -> add a one-beat note (snapped)
//   - drag empty space         -> marquee-select notes (Shift toggles selection)
//   - drag note body           -> move selected notes together
//   - drag note right edge     -> resize duration
//   - Ctrl+C / Ctrl+V          -> copy/paste selected notes at the insertion cursor
//   - Delete / Backspace       -> delete selected notes
//   - hold Alt                 -> suspend grid snap during a drag
//
// All edits go through ProjectEditor, so undo/redo and validation are uniform. After any successful
// edit the onProjectChanged callback fires so the host can republish the project to playback.
class PianoRoll final : public juce::Component {
public:
    PianoRoll(commands::ProjectEditor& editor, double tempoBpm);

    // Point the roll at a track/clip to edit. Pass empty ids to clear.
    void showClip(const domain::EntityId& trackId, const domain::EntityId& clipId);
    void refreshFromProject();
    void setGridTicks(domain::Tick gridTicks);
    [[nodiscard]] domain::Tick insertionTick() const noexcept { return insertionTick_; }
    [[nodiscard]] domain::Tick gridTicks() const noexcept { return gridTicks_; }
    [[nodiscard]] juce::Rectangle<int> chordAnchorBoundsInParent() const;
    [[nodiscard]] std::vector<domain::EntityId> rhythmSourceNoteIds() const;
    void showRhythmPreview(std::vector<domain::GeneratedNote> notes);
    void clearRhythmPreview();

    std::function<void()> onProjectChanged;
    std::function<void()> onChordShortcut;
    std::function<void(domain::Tick tick)> onScrubToTick;  // optional: click ruler to seek

    void paint(juce::Graphics& graphics) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;
    void mouseDoubleClick(const juce::MouseEvent& event) override;
    void mouseWheelMove(
        const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override;
    void mouseMagnify(const juce::MouseEvent& event, float scaleFactor) override;
    bool keyPressed(const juce::KeyPress& key) override;

private:
    static constexpr int kLowPitch = 36;   // C2
    static constexpr int kHighPitch = 84;  // C6
    static constexpr int kPitchCount = kHighPitch - kLowPitch + 1;

    enum class DragMode { none, move, resize, marquee };

    [[nodiscard]] const domain::MidiClip* currentClip() const;
    [[nodiscard]] int rowForPitch(domain::Pitch pitch) const;
    [[nodiscard]] domain::Pitch pitchForY(int y) const;
    [[nodiscard]] domain::Tick tickForX(int x) const;
    [[nodiscard]] int xForTick(domain::Tick tick) const;
    [[nodiscard]] int yForPitch(domain::Pitch pitch) const;
    [[nodiscard]] domain::Tick snap(domain::Tick tick, bool suspend) const;
    [[nodiscard]] juce::Rectangle<int> boundsForNote(const domain::NoteEvent& note) const;
    [[nodiscard]] std::optional<domain::EntityId> noteAt(juce::Point<int> point) const;
    [[nodiscard]] bool isSelected(const domain::EntityId& noteId) const;
    [[nodiscard]] juce::Rectangle<int> marqueeBounds() const;
    void updateMarqueeSelection();
    void copySelection();
    void pasteClipboard();
    void notifyProjectChanged();
    void zoomAt(int anchorX, double scaleFactor);

    commands::ProjectEditor& editor_;
    double tempoBpm_{120.0};
    domain::EntityId trackId_;
    domain::EntityId clipId_;

    std::vector<domain::EntityId> selectedNoteIds_;
    std::vector<domain::EntityId> marqueeBaseSelection_;
    std::vector<domain::NoteEvent> dragOriginalNotes_;
    std::vector<domain::GeneratedNote> clipboardNotes_;
    std::vector<domain::GeneratedNote> rhythmPreview_;
    domain::Tick insertionTick_{};
    domain::Tick gridTicks_{domain::kPpq / 2};
    DragMode dragMode_{DragMode::none};
    domain::Tick dragStartTick_{};
    domain::Pitch dragStartPitch_{};
    domain::Tick dragOriginalStart_{};
    domain::Tick dragOriginalDuration_{};
    domain::Tick dragDeltaTick_{};
    int dragDeltaPitch_{};
    domain::Tick dragPreviewDuration_{};
    bool dragDidMove_{false};
    bool marqueeToggle_{false};
    juce::Point<int> marqueeAnchor_;
    juce::Point<int> marqueeCurrent_;

    TimelineViewport viewport_;
    int rowHeight_{12};
};

}  // namespace composer::ui
