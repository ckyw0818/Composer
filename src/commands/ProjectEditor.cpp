#include "commands/ProjectEditor.h"

#include <algorithm>

namespace composer::commands {
namespace {

using application::Error;
using application::ErrorCode;

// Canonical note ordering: start, then pitch, then id. Keeps two equivalently-built clips equal.
[[nodiscard]] bool noteLess(const domain::NoteEvent& lhs, const domain::NoteEvent& rhs) noexcept {
    if (lhs.start != rhs.start) {
        return lhs.start < rhs.start;
    }
    if (lhs.pitch != rhs.pitch) {
        return lhs.pitch < rhs.pitch;
    }
    return lhs.id.value < rhs.id.value;
}

void insertSorted(std::vector<domain::NoteEvent>& notes, domain::NoteEvent note) {
    const auto position = std::lower_bound(notes.begin(), notes.end(), note, noteLess);
    notes.insert(position, std::move(note));
}

}  // namespace

void ProjectEditor::commit() {
    // Drop any redo branch, then append the new state.
    if (cursor_ + 1 < history_.size()) {
        history_.erase(history_.begin() + static_cast<std::ptrdiff_t>(cursor_) + 1, history_.end());
    }
    history_.push_back(project_);
    cursor_ = history_.size() - 1;
    ++revision_;
}

void ProjectEditor::undo() {
    if (!canUndo()) {
        return;
    }
    --cursor_;
    project_ = history_[cursor_];
    ++revision_;
}

void ProjectEditor::redo() {
    if (!canRedo()) {
        return;
    }
    ++cursor_;
    project_ = history_[cursor_];
    ++revision_;
}

domain::InstrumentTrack* ProjectEditor::findTrack(const domain::EntityId& trackId) {
    const auto position = std::find_if(project_.tracks.begin(), project_.tracks.end(),
        [&](const domain::InstrumentTrack& track) { return track.id == trackId; });
    return position == project_.tracks.end() ? nullptr : &*position;
}

domain::MidiClip* ProjectEditor::findClip(
    const domain::EntityId& trackId, const domain::EntityId& clipId) {
    auto* track = findTrack(trackId);
    if (track == nullptr) {
        return nullptr;
    }
    const auto position = std::find_if(track->clips.begin(), track->clips.end(),
        [&](const domain::MidiClip& clip) { return clip.id == clipId; });
    return position == track->clips.end() ? nullptr : &*position;
}

application::Result<domain::EntityId> ProjectEditor::addInstrumentTrack(
    const std::string& name, const std::string& instrumentId) {
    if (name.empty()) {
        return Error{ErrorCode::invalidArgument, "track name must not be empty"};
    }
    if (instrumentId.empty()) {
        return Error{ErrorCode::invalidArgument, "track must reference an instrument"};
    }
    domain::InstrumentTrack track;
    track.id = ids_.next();
    track.name = name;
    track.instrumentId = instrumentId;
    project_.tracks.push_back(std::move(track));
    const auto newId = project_.tracks.back().id;
    commit();
    return newId;
}

application::Result<std::monostate> ProjectEditor::removeTrack(const domain::EntityId& trackId) {
    const auto position = std::find_if(project_.tracks.begin(), project_.tracks.end(),
        [&](const domain::InstrumentTrack& track) { return track.id == trackId; });
    if (position == project_.tracks.end()) {
        return Error{ErrorCode::invalidArgument, "no track with the given id"};
    }
    project_.tracks.erase(position);
    commit();
    return std::monostate{};
}

application::Result<std::monostate> ProjectEditor::renameTrack(
    const domain::EntityId& trackId, const std::string& name) {
    if (name.empty()) {
        return Error{ErrorCode::invalidArgument, "track name must not be empty"};
    }
    auto* track = findTrack(trackId);
    if (track == nullptr) {
        return Error{ErrorCode::invalidArgument, "no track with the given id"};
    }
    track->name = name;
    commit();
    return std::monostate{};
}

application::Result<std::monostate> ProjectEditor::reorderTrack(
    const domain::EntityId& trackId, const std::size_t newIndex) {
    const auto position = std::find_if(project_.tracks.begin(), project_.tracks.end(),
        [&](const domain::InstrumentTrack& track) { return track.id == trackId; });
    if (position == project_.tracks.end()) {
        return Error{ErrorCode::invalidArgument, "no track with the given id"};
    }
    if (newIndex >= project_.tracks.size()) {
        return Error{ErrorCode::invalidArgument, "reorder index out of range"};
    }
    domain::InstrumentTrack moved = std::move(*position);
    project_.tracks.erase(position);
    project_.tracks.insert(
        project_.tracks.begin() + static_cast<std::ptrdiff_t>(newIndex), std::move(moved));
    commit();
    return std::monostate{};
}

application::Result<domain::EntityId> ProjectEditor::addMidiClip(
    const domain::EntityId& trackId, const std::string& name, domain::HalfOpenTickRange range) {
    if (!range.isValid()) {
        return Error{ErrorCode::invalidArgument, "clip range must be non-negative length"};
    }
    if (range.start < 0) {
        return Error{ErrorCode::invalidArgument, "clip range must start at a non-negative tick"};
    }
    auto* track = findTrack(trackId);
    if (track == nullptr) {
        return Error{ErrorCode::invalidArgument, "no track with the given id"};
    }
    domain::MidiClip clip;
    clip.id = ids_.next();
    clip.name = name;
    clip.range = range;
    track->clips.push_back(std::move(clip));
    const auto newId = track->clips.back().id;
    commit();
    return newId;
}

application::Result<domain::EntityId> ProjectEditor::addNote(
    const domain::EntityId& trackId, const domain::EntityId& clipId,
    const domain::Tick start, const domain::Tick duration,
    const domain::Pitch pitch, const domain::Velocity velocity) {
    auto* clip = findClip(trackId, clipId);
    if (clip == nullptr) {
        return Error{ErrorCode::invalidArgument, "no clip with the given id"};
    }
    domain::NoteEvent note;
    note.id = ids_.next();
    note.start = start;
    note.duration = duration;
    note.pitch = pitch;
    note.velocity = velocity;
    if (!note.isValid()) {
        return Error{ErrorCode::invalidArgument,
            "note must have positive duration and valid pitch/velocity"};
    }
    if (!clip->range.contains(note.start) || note.end() > clip->range.end) {
        return Error{ErrorCode::invalidArgument, "note must lie within the clip range"};
    }
    const auto newId = note.id;
    insertSorted(clip->notes, std::move(note));
    commit();
    return newId;
}

application::Result<std::monostate> ProjectEditor::moveNote(
    const domain::EntityId& trackId, const domain::EntityId& clipId,
    const domain::EntityId& noteId, const domain::Tick newStart, const domain::Pitch newPitch) {
    auto* clip = findClip(trackId, clipId);
    if (clip == nullptr) {
        return Error{ErrorCode::invalidArgument, "no clip with the given id"};
    }
    const auto position = std::find_if(clip->notes.begin(), clip->notes.end(),
        [&](const domain::NoteEvent& note) { return note.id == noteId; });
    if (position == clip->notes.end()) {
        return Error{ErrorCode::invalidArgument, "no note with the given id"};
    }
    domain::NoteEvent updated = *position;
    updated.start = newStart;
    updated.pitch = newPitch;
    if (!updated.isValid()) {
        return Error{ErrorCode::invalidArgument, "moved note has invalid pitch or position"};
    }
    if (!clip->range.contains(updated.start) || updated.end() > clip->range.end) {
        return Error{ErrorCode::invalidArgument, "moved note must stay within the clip range"};
    }
    clip->notes.erase(position);
    insertSorted(clip->notes, std::move(updated));
    commit();
    return std::monostate{};
}

application::Result<std::monostate> ProjectEditor::resizeNote(
    const domain::EntityId& trackId, const domain::EntityId& clipId,
    const domain::EntityId& noteId, const domain::Tick newDuration) {
    auto* clip = findClip(trackId, clipId);
    if (clip == nullptr) {
        return Error{ErrorCode::invalidArgument, "no clip with the given id"};
    }
    const auto position = std::find_if(clip->notes.begin(), clip->notes.end(),
        [&](const domain::NoteEvent& note) { return note.id == noteId; });
    if (position == clip->notes.end()) {
        return Error{ErrorCode::invalidArgument, "no note with the given id"};
    }
    if (newDuration <= 0) {
        return Error{ErrorCode::invalidArgument, "note duration must be positive"};
    }
    if (position->start + newDuration > clip->range.end) {
        return Error{ErrorCode::invalidArgument, "resized note must stay within the clip range"};
    }
    position->duration = newDuration;
    commit();
    return std::monostate{};
}

application::Result<std::monostate> ProjectEditor::deleteNote(
    const domain::EntityId& trackId, const domain::EntityId& clipId,
    const domain::EntityId& noteId) {
    auto* clip = findClip(trackId, clipId);
    if (clip == nullptr) {
        return Error{ErrorCode::invalidArgument, "no clip with the given id"};
    }
    const auto position = std::find_if(clip->notes.begin(), clip->notes.end(),
        [&](const domain::NoteEvent& note) { return note.id == noteId; });
    if (position == clip->notes.end()) {
        return Error{ErrorCode::invalidArgument, "no note with the given id"};
    }
    clip->notes.erase(position);
    commit();
    return std::monostate{};
}

application::Result<std::vector<domain::EntityId>> ProjectEditor::insertChord(
    const domain::EntityId& trackId, const domain::EntityId& clipId,
    const domain::Tick start, const domain::Tick duration, const domain::ChordSpec& spec) {
    auto* clip = findClip(trackId, clipId);
    if (clip == nullptr) {
        return Error{ErrorCode::invalidArgument, "no clip with the given id"};
    }
    if (duration <= 0) {
        return Error{ErrorCode::invalidArgument, "chord note duration must be positive"};
    }

    auto pitchesResult = domain::chordPitches(spec);
    if (std::holds_alternative<Error>(pitchesResult)) {
        return std::get<Error>(pitchesResult);
    }
    const auto& pitches = std::get<std::vector<domain::Pitch>>(pitchesResult);

    // Validate every voice fits the clip before mutating, so the compound edit is all-or-nothing.
    const domain::Tick end = start + duration;
    if (!clip->range.contains(start) || end > clip->range.end) {
        return Error{ErrorCode::invalidArgument, "chord must lie within the clip range"};
    }

    std::vector<domain::EntityId> insertedIds;
    insertedIds.reserve(pitches.size());
    for (const auto pitch : pitches) {
        domain::NoteEvent note;
        note.id = ids_.next();
        note.start = start;
        note.duration = duration;
        note.pitch = pitch;
        note.velocity = spec.velocity;
        insertedIds.push_back(note.id);
        insertSorted(clip->notes, std::move(note));
    }
    commit();  // one undo step for the whole chord
    return insertedIds;
}

}  // namespace composer::commands
