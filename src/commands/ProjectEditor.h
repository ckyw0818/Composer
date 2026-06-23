#pragma once

#include "application/Result.h"
#include "domain/Chord.h"
#include "domain/IdSource.h"
#include "domain/Project.h"
#include "domain/Rhythm.h"
#include "domain/Types.h"

#include <cstddef>
#include <cstdint>
#include <optional>
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
    application::Result<domain::EntityId> addInstrumentTrackWithMidiClip(
        const std::string& name, const std::string& instrumentId,
        const std::string& clipName, domain::HalfOpenTickRange clipRange);
    application::Result<domain::EntityId> addAudioTrack(
        const std::string& name, const std::string& deviceId, int inputChannel);
    application::Result<domain::EntityId> duplicateTrack(const domain::EntityId& trackId);
    application::Result<std::monostate> removeTrack(const domain::EntityId& trackId);
    application::Result<std::monostate> renameTrack(
        const domain::EntityId& trackId, const std::string& name);
    application::Result<std::monostate> reorderTrack(
        const domain::EntityId& trackId, std::size_t newIndex);
    application::Result<std::monostate> setTrackVolume(
        const domain::EntityId& trackId, float volume);
    application::Result<std::monostate> setTrackPan(
        const domain::EntityId& trackId, float pan);
    application::Result<std::monostate> setTrackMuted(
        const domain::EntityId& trackId, bool muted);
    application::Result<std::monostate> setTrackSoloed(
        const domain::EntityId& trackId, bool soloed);
    application::Result<std::monostate> setAudioTrackArmed(
        const domain::EntityId& trackId, bool armed);
    application::Result<std::monostate> setAudioInputRoute(
        const domain::EntityId& trackId, const std::string& deviceId, int inputChannel,
        domain::MonitoringMode monitoring, std::int64_t latencyCompensationSamples);
    application::Result<std::monostate> setTempoBpm(double beatsPerMinute);

    // Replaces the current session model as one undoable command. MIDI import uses this boundary
    // rather than mutating the editor-owned project from the UI.
    application::Result<std::monostate> replaceProject(domain::Project project);

    // --- Clip commands ---
    application::Result<domain::EntityId> addMidiClip(
        const domain::EntityId& trackId, const std::string& name,
        domain::HalfOpenTickRange range);
    application::Result<domain::EntityId> addRecordedAudioClip(
        const domain::EntityId& trackId, const std::string& name, const std::string& assetPath,
        domain::ProjectSample capturedStartSample, std::int64_t frameCount,
        double sourceSampleRate, int sourceChannels);
    application::Result<std::monostate> moveAudioClip(
        const domain::EntityId& trackId, const domain::EntityId& clipId,
        domain::ProjectSample newStartSample);
    application::Result<std::monostate> trimAudioClip(
        const domain::EntityId& trackId, const domain::EntityId& clipId,
        std::int64_t trimFromStartFrames, std::int64_t trimFromEndFrames);
    application::Result<domain::EntityId> splitAudioClip(
        const domain::EntityId& trackId, const domain::EntityId& clipId,
        std::int64_t splitFrame);
    application::Result<std::monostate> setAudioClipFades(
        const domain::EntityId& trackId, const domain::EntityId& clipId,
        std::int64_t fadeInFrames, std::int64_t fadeOutFrames);
    application::Result<std::monostate> setAudioClipGain(
        const domain::EntityId& trackId, const domain::EntityId& clipId, float gain);
    application::Result<domain::EntityId> setAudioCrossfade(
        const domain::EntityId& trackId, const domain::EntityId& leftClipId,
        const domain::EntityId& rightClipId, std::int64_t lengthFrames);

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
    application::Result<std::monostate> moveNotes(
        const domain::EntityId& trackId, const domain::EntityId& clipId,
        const std::vector<domain::EntityId>& noteIds,
        domain::Tick deltaTick, int deltaPitch);
    application::Result<std::monostate> deleteNotes(
        const domain::EntityId& trackId, const domain::EntityId& clipId,
        const std::vector<domain::EntityId>& noteIds);
    application::Result<std::vector<domain::EntityId>> pasteNotes(
        const domain::EntityId& trackId, const domain::EntityId& clipId,
        domain::Tick insertionStart, const std::vector<domain::GeneratedNote>& relativeNotes);

    // --- Compound command: insert a chord as ordinary notes in one undo step. ---
    application::Result<std::vector<domain::EntityId>> insertChord(
        const domain::EntityId& trackId, const domain::EntityId& clipId,
        domain::Tick start, domain::Tick duration, const domain::ChordSpec& spec);
    [[nodiscard]] application::Result<std::vector<domain::GeneratedNote>> previewChord(
        const domain::EntityId& trackId, const domain::EntityId& clipId,
        domain::Tick start, domain::Tick duration, const domain::ChordSpec& spec) const;

    // --- Rhythm generation ---
    // Specifies which notes feed the generator and how the generated rhythm is committed. The
    // source pitches come from the named notes in the clip; pattern/subdivision/seed control timing.
    // When replaceSource is true the source notes are removed and replaced by the generated rhythm;
    // otherwise the rhythm is added alongside them.
    struct RhythmApply final {
        domain::RhythmPattern pattern{domain::RhythmPattern::block};
        domain::Tick subdivision{};
        std::uint64_t seed{0};
        bool replaceSource{true};
        std::optional<domain::Revision> expectedRevision;
        std::vector<domain::EntityId> sourceNoteIds;
    };

    // Non-destructive preview: builds the generated notes from the named source notes without
    // touching the project. Mirrors exactly what applyRhythm would insert, so the popover can
    // audition before committing. Pure with respect to project state and history.
    [[nodiscard]] application::Result<std::vector<domain::GeneratedNote>> previewRhythm(
        const domain::EntityId& trackId, const domain::EntityId& clipId,
        const RhythmApply& request) const;

    // Applies the generated rhythm as ordinary notes in a single undo step. Validates that every
    // generated note fits the clip before mutating, so the compound edit is all-or-nothing.
    application::Result<std::vector<domain::EntityId>> applyRhythm(
        const domain::EntityId& trackId, const domain::EntityId& clipId,
        const RhythmApply& request);

private:
    // Shared by previewRhythm/applyRhythm: resolves the source notes and builds the rhythm request
    // spanning their combined tick range. const so preview can reuse it without mutation.
    [[nodiscard]] application::Result<domain::RhythmRequest> buildRhythmRequest(
        const domain::EntityId& trackId, const domain::EntityId& clipId,
        const RhythmApply& request) const;

    void commit();  // record current project_ as a new history entry, bump revision

    [[nodiscard]] domain::InstrumentTrack* findTrack(const domain::EntityId& trackId);
    [[nodiscard]] const domain::InstrumentTrack* findTrack(
        const domain::EntityId& trackId) const;
    [[nodiscard]] domain::AudioTrack* findAudioTrack(const domain::EntityId& trackId);
    [[nodiscard]] const domain::AudioTrack* findAudioTrack(
        const domain::EntityId& trackId) const;
    [[nodiscard]] domain::AudioClip* findAudioClip(
        const domain::EntityId& trackId, const domain::EntityId& clipId);
    void ensureMixedTrackOrder();
    [[nodiscard]] domain::MidiClip* findClip(
        const domain::EntityId& trackId, const domain::EntityId& clipId);
    [[nodiscard]] const domain::MidiClip* findClip(
        const domain::EntityId& trackId, const domain::EntityId& clipId) const;

    domain::Project project_;
    domain::IdSource& ids_;
    std::vector<domain::Project> history_;
    std::size_t cursor_{0};
    domain::Revision revision_{0};
};

}  // namespace composer::commands
