#pragma once

#include "application/Result.h"
#include "domain/Chord.h"
#include "domain/IdSource.h"
#include "domain/Project.h"
#include "domain/Types.h"

#include <cstddef>
#include <string>
#include <vector>

namespace composer::commands {

// Editing facade over a domain::Project. Every mutation is a command that records the prior
// project state, so undo/redo restore exact byte-equal snapshots. Compound edits (chord
// insertion) collapse into a single undo step, matching the design's "one Undo command" rule.
//
// This layer owns the only legal write path to the project. The UI, fixtures, and tests all go
// through it; nothing mutates the domain model directly. Validation lives in the domain
// (chordPitches, NoteEvent::isValid) and is enforced here before the command commits.
class ProjectEditor final {
public:
    ProjectEditor(domain::Project project, domain::IdSource& ids)
        : project_(std::move(project)), ids_(ids) {
        history_.push_back(project_);
        cursor_ = 0;
    }

    [[nodiscard]] const domain::Project& project() const noexcept { return project_; }
    [[nodiscard]] domain::Revision revision() const noexcept { return revision_; }

    [[nodiscard]] bool canUndo() const noexcept { return cursor_ > 0; }
    [[nodiscard]] bool canRedo() const noexcept { return cursor_ + 1 < history_.size(); }

    void undo();
    void redo();

    // --- Track commands ---
    application::Result<domain::EntityId> addInstrumentTrack(
        const std::string& name, const std::string& instrumentId);
    application::Result<std::monostate> removeTrack(const domain::EntityId& trackId);
    application::Result<std::monostate> renameTrack(
        const domain::EntityId& trackId, const std::string& name);
    application::Result<std::monostate> reorderTrack(
        const domain::EntityId& trackId, std::size_t newIndex);

    // --- Clip commands ---
    application::Result<domain::EntityId> addMidiClip(
        const domain::EntityId& trackId, const std::string& name,
        domain::HalfOpenTickRange range);

    // --- Note commands ---
    application::Result<domain::EntityId> addNote(
        const domain::EntityId& trackId, const domain::EntityId& clipId,
        domain::Tick start, domain::Tick duration, domain::Pitch pitch, domain::Velocity velocity);
    application::Result<std::monostate> moveNote(
        const domain::EntityId& trackId, const domain::EntityId& clipId,
        const domain::EntityId& noteId, domain::Tick newStart, domain::Pitch newPitch);
    application::Result<std::monostate> resizeNote(
        const domain::EntityId& trackId, const domain::EntityId& clipId,
        const domain::EntityId& noteId, domain::Tick newDuration);
    application::Result<std::monostate> deleteNote(
        const domain::EntityId& trackId, const domain::EntityId& clipId,
        const domain::EntityId& noteId);

    // --- Compound command: insert a chord as ordinary notes in one undo step. ---
    application::Result<std::vector<domain::EntityId>> insertChord(
        const domain::EntityId& trackId, const domain::EntityId& clipId,
        domain::Tick start, domain::Tick duration, const domain::ChordSpec& spec);

private:
    void commit();  // record current project_ as a new history entry, bump revision

    [[nodiscard]] domain::InstrumentTrack* findTrack(const domain::EntityId& trackId);
    [[nodiscard]] domain::MidiClip* findClip(
        const domain::EntityId& trackId, const domain::EntityId& clipId);

    domain::Project project_;
    domain::IdSource& ids_;
    std::vector<domain::Project> history_;
    std::size_t cursor_{0};
    domain::Revision revision_{0};
};

}  // namespace composer::commands
