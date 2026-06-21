#pragma once

#include "commands/ProjectEditor.h"
#include "domain/Project.h"
#include "domain/Types.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <optional>

namespace composer::ui {

// Direct-manipulation piano roll for a single MIDI clip.
//
// Interactions (DESIGN.md Interaction Design):
//   - double-click empty cell  -> add a one-beat note (snapped)
//   - drag note body           -> move pitch + time
//   - drag note right edge     -> resize duration
//   - Delete / Backspace       -> delete selected note
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

    std::function<void()> onProjectChanged;
    std::function<void(domain::Tick tick)> onScrubToTick;  // optional: click ruler to seek

    void paint(juce::Graphics& graphics) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;
    void mouseDoubleClick(const juce::MouseEvent& event) override;
    bool keyPressed(const juce::KeyPress& key) override;

private:
    static constexpr int kLowPitch = 36;   // C2
    static constexpr int kHighPitch = 84;  // C6
    static constexpr int kPitchCount = kHighPitch - kLowPitch + 1;

    enum class DragMode { none, move, resize };

    [[nodiscard]] const domain::MidiClip* currentClip() const;
    [[nodiscard]] int rowForPitch(domain::Pitch pitch) const;
    [[nodiscard]] domain::Pitch pitchForY(int y) const;
    [[nodiscard]] domain::Tick tickForX(int x) const;
    [[nodiscard]] int xForTick(domain::Tick tick) const;
    [[nodiscard]] int yForPitch(domain::Pitch pitch) const;
    [[nodiscard]] domain::Tick snap(domain::Tick tick, bool suspend) const;
    [[nodiscard]] juce::Rectangle<int> boundsForNote(const domain::NoteEvent& note) const;
    [[nodiscard]] std::optional<domain::EntityId> noteAt(juce::Point<int> point) const;

    commands::ProjectEditor& editor_;
    double tempoBpm_{120.0};
    domain::EntityId trackId_;
    domain::EntityId clipId_;

    domain::EntityId selectedNote_;
    DragMode dragMode_{DragMode::none};
    domain::Tick dragStartTick_{};
    domain::Pitch dragStartPitch_{};
    domain::Tick dragOriginalStart_{};
    domain::Pitch dragOriginalPitch_{};
    domain::Tick dragOriginalDuration_{};

    double pixelsPerBeat_{48.0};
    int rowHeight_{12};
};

}  // namespace composer::ui
