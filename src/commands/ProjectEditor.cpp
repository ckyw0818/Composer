#include "commands/ProjectEditor.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace composer::commands {
namespace {

using application::Error;
using application::ErrorCode;

constexpr domain::Tick kMaxEditableTick =
    std::numeric_limits<domain::Tick>::max() / 4;

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

const domain::InstrumentTrack* ProjectEditor::findTrack(
    const domain::EntityId& trackId) const {
    const auto position = std::find_if(project_.tracks.begin(), project_.tracks.end(),
        [&](const domain::InstrumentTrack& track) { return track.id == trackId; });
    return position == project_.tracks.end() ? nullptr : &*position;
}

domain::AudioTrack* ProjectEditor::findAudioTrack(const domain::EntityId& trackId) {
    const auto position = std::find_if(project_.audioTracks.begin(), project_.audioTracks.end(),
        [&](const domain::AudioTrack& track) { return track.id == trackId; });
    return position == project_.audioTracks.end() ? nullptr : &*position;
}

const domain::AudioTrack* ProjectEditor::findAudioTrack(
    const domain::EntityId& trackId) const {
    const auto position = std::find_if(project_.audioTracks.begin(), project_.audioTracks.end(),
        [&](const domain::AudioTrack& track) { return track.id == trackId; });
    return position == project_.audioTracks.end() ? nullptr : &*position;
}

domain::AudioClip* ProjectEditor::findAudioClip(
    const domain::EntityId& trackId, const domain::EntityId& clipId) {
    auto* track = findAudioTrack(trackId);
    if (track == nullptr) return nullptr;
    const auto position = std::find_if(track->clips.begin(), track->clips.end(),
        [&](const domain::AudioClip& clip) { return clip.id == clipId; });
    return position == track->clips.end() ? nullptr : &*position;
}

void ProjectEditor::ensureMixedTrackOrder() {
    if (!project_.trackOrder.empty()) return;
    project_.trackOrder.reserve(project_.tracks.size() + project_.audioTracks.size());
    for (const auto& track : project_.tracks) project_.trackOrder.push_back(track.id);
    for (const auto& track : project_.audioTracks) project_.trackOrder.push_back(track.id);
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

const domain::MidiClip* ProjectEditor::findClip(
    const domain::EntityId& trackId, const domain::EntityId& clipId) const {
    const auto trackPos = std::find_if(project_.tracks.begin(), project_.tracks.end(),
        [&](const domain::InstrumentTrack& track) { return track.id == trackId; });
    if (trackPos == project_.tracks.end()) {
        return nullptr;
    }
    const auto position = std::find_if(trackPos->clips.begin(), trackPos->clips.end(),
        [&](const domain::MidiClip& clip) { return clip.id == clipId; });
    return position == trackPos->clips.end() ? nullptr : &*position;
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
    if (!project_.trackOrder.empty()) project_.trackOrder.push_back(newId);
    commit();
    return newId;
}

application::Result<domain::EntityId> ProjectEditor::addInstrumentTrackWithMidiClip(
    const std::string& name, const std::string& instrumentId,
    const std::string& clipName, const domain::HalfOpenTickRange clipRange) {
    if (name.empty() || instrumentId.empty()) {
        return Error{ErrorCode::invalidArgument,
            "instrument track name and instrument id must not be empty"};
    }
    if (!clipRange.isValid() || clipRange.start < 0 || clipRange.length() <= 0) {
        return Error{ErrorCode::invalidArgument,
            "initial MIDI clip must have a positive non-negative range"};
    }
    domain::InstrumentTrack track;
    track.id = ids_.next();
    track.name = name;
    track.instrumentId = instrumentId;
    domain::MidiClip clip;
    clip.id = ids_.next();
    clip.name = clipName;
    clip.range = clipRange;
    track.clips.push_back(std::move(clip));
    const auto newId = track.id;
    project_.tracks.push_back(std::move(track));
    if (!project_.trackOrder.empty()) project_.trackOrder.push_back(newId);
    commit();
    return newId;
}

application::Result<domain::EntityId> ProjectEditor::addAudioTrack(
    const std::string& name, const std::string& deviceId, const int inputChannel) {
    if (name.empty()) {
        return Error{ErrorCode::invalidArgument, "audio track name must not be empty"};
    }
    if (inputChannel < 0) {
        return Error{ErrorCode::invalidArgument, "audio input channel must be non-negative"};
    }
    ensureMixedTrackOrder();
    domain::AudioTrack track;
    track.id = ids_.next();
    track.name = name;
    track.input.deviceId = deviceId;
    track.input.channelIndex = inputChannel;
    const auto newId = track.id;
    project_.audioTracks.push_back(std::move(track));
    project_.trackOrder.push_back(newId);
    commit();
    return newId;
}

application::Result<domain::EntityId> ProjectEditor::duplicateTrack(
    const domain::EntityId& trackId) {
    const auto position = std::find_if(project_.tracks.begin(), project_.tracks.end(),
        [&](const domain::InstrumentTrack& track) { return track.id == trackId; });
    if (position != project_.tracks.end()) {
        domain::InstrumentTrack duplicate = *position;
        duplicate.id = ids_.next();
        duplicate.name += " Copy";
        for (auto& clip : duplicate.clips) {
            clip.id = ids_.next();
            for (auto& note : clip.notes) note.id = ids_.next();
        }
        const auto newId = duplicate.id;
        project_.tracks.insert(position + 1, std::move(duplicate));
        if (!project_.trackOrder.empty()) {
            const auto order = std::find(project_.trackOrder.begin(), project_.trackOrder.end(), trackId);
            project_.trackOrder.insert(order == project_.trackOrder.end() ? project_.trackOrder.end()
                                                                          : order + 1,
                newId);
        }
        commit();
        return newId;
    }

    const auto audioPosition = std::find_if(project_.audioTracks.begin(), project_.audioTracks.end(),
        [&](const domain::AudioTrack& track) { return track.id == trackId; });
    if (audioPosition == project_.audioTracks.end()) {
        return Error{ErrorCode::invalidArgument, "no track with the given id"};
    }
    ensureMixedTrackOrder();
    domain::AudioTrack duplicate = *audioPosition;
    duplicate.id = ids_.next();
    duplicate.name += " Copy";
    duplicate.recordArmed = false;
    for (auto& clip : duplicate.clips) clip.id = ids_.next();
    duplicate.crossfades.clear();
    const auto newId = duplicate.id;
    project_.audioTracks.insert(audioPosition + 1, std::move(duplicate));
    const auto order = std::find(project_.trackOrder.begin(), project_.trackOrder.end(), trackId);
    project_.trackOrder.insert(order == project_.trackOrder.end() ? project_.trackOrder.end()
                                                                  : order + 1,
        newId);
    commit();
    return newId;
}

application::Result<std::monostate> ProjectEditor::removeTrack(const domain::EntityId& trackId) {
    const auto position = std::find_if(project_.tracks.begin(), project_.tracks.end(),
        [&](const domain::InstrumentTrack& track) { return track.id == trackId; });
    if (position != project_.tracks.end()) {
        project_.tracks.erase(position);
    } else {
        const auto audioPosition = std::find_if(
            project_.audioTracks.begin(), project_.audioTracks.end(),
            [&](const domain::AudioTrack& track) { return track.id == trackId; });
        if (audioPosition == project_.audioTracks.end()) {
            return Error{ErrorCode::invalidArgument, "no track with the given id"};
        }
        project_.audioTracks.erase(audioPosition);
    }
    std::erase(project_.trackOrder, trackId);
    commit();
    return std::monostate{};
}

application::Result<std::monostate> ProjectEditor::renameTrack(
    const domain::EntityId& trackId, const std::string& name) {
    if (name.empty()) {
        return Error{ErrorCode::invalidArgument, "track name must not be empty"};
    }
    auto* track = findTrack(trackId);
    if (track != nullptr) {
        if (track->name != name) {
            track->name = name;
            commit();
        }
        return std::monostate{};
    }
    auto* audioTrack = findAudioTrack(trackId);
    if (audioTrack == nullptr) return Error{ErrorCode::invalidArgument, "no track with the given id"};
    if (audioTrack->name != name) {
        audioTrack->name = name;
        commit();
    }
    return std::monostate{};
}

application::Result<std::monostate> ProjectEditor::reorderTrack(
    const domain::EntityId& trackId, const std::size_t newIndex) {
    if (!project_.audioTracks.empty() || !project_.trackOrder.empty()) {
        ensureMixedTrackOrder();
        const auto position = std::find(project_.trackOrder.begin(), project_.trackOrder.end(), trackId);
        if (position == project_.trackOrder.end()) {
            return Error{ErrorCode::invalidArgument, "no track with the given id"};
        }
        if (newIndex >= project_.trackOrder.size()) {
            return Error{ErrorCode::invalidArgument, "reorder index out of range"};
        }
        const auto oldIndex = static_cast<std::size_t>(
            std::distance(project_.trackOrder.begin(), position));
        if (oldIndex == newIndex) return std::monostate{};
        auto moved = *position;
        project_.trackOrder.erase(position);
        project_.trackOrder.insert(
            project_.trackOrder.begin() + static_cast<std::ptrdiff_t>(newIndex), std::move(moved));
        commit();
        return std::monostate{};
    }
    const auto position = std::find_if(project_.tracks.begin(), project_.tracks.end(),
        [&](const domain::InstrumentTrack& track) { return track.id == trackId; });
    if (position == project_.tracks.end()) {
        return Error{ErrorCode::invalidArgument, "no track with the given id"};
    }
    if (newIndex >= project_.tracks.size()) {
        return Error{ErrorCode::invalidArgument, "reorder index out of range"};
    }
    const auto oldIndex = static_cast<std::size_t>(
        std::distance(project_.tracks.begin(), position));
    if (oldIndex == newIndex) {
        return std::monostate{};
    }
    domain::InstrumentTrack moved = std::move(*position);
    project_.tracks.erase(position);
    project_.tracks.insert(
        project_.tracks.begin() + static_cast<std::ptrdiff_t>(newIndex), std::move(moved));
    commit();
    return std::monostate{};
}

application::Result<std::monostate> ProjectEditor::setTrackVolume(
    const domain::EntityId& trackId, const float volume) {
    if (!std::isfinite(volume) || volume < 0.0F || volume > 2.0F) {
        return Error{ErrorCode::invalidArgument, "track volume must be between 0 and 2"};
    }
    auto* track = findTrack(trackId);
    if (track != nullptr) {
        if (track->volume != volume) {
            track->volume = volume;
            commit();
        }
        return std::monostate{};
    }
    auto* audioTrack = findAudioTrack(trackId);
    if (audioTrack == nullptr) return Error{ErrorCode::invalidArgument, "no track with the given id"};
    if (audioTrack->volume != volume) {
        audioTrack->volume = volume;
        commit();
    }
    return std::monostate{};
}

application::Result<std::monostate> ProjectEditor::setTrackPan(
    const domain::EntityId& trackId, const float pan) {
    if (!std::isfinite(pan) || pan < -1.0F || pan > 1.0F) {
        return Error{ErrorCode::invalidArgument, "track pan must be between -1 and 1"};
    }
    auto* track = findTrack(trackId);
    if (track != nullptr) {
        if (track->pan != pan) {
            track->pan = pan;
            commit();
        }
        return std::monostate{};
    }
    auto* audioTrack = findAudioTrack(trackId);
    if (audioTrack == nullptr) return Error{ErrorCode::invalidArgument, "no track with the given id"};
    if (audioTrack->pan != pan) {
        audioTrack->pan = pan;
        commit();
    }
    return std::monostate{};
}

application::Result<std::monostate> ProjectEditor::setTrackMuted(
    const domain::EntityId& trackId, const bool muted) {
    auto* track = findTrack(trackId);
    if (track != nullptr) {
        if (track->muted != muted) {
            track->muted = muted;
            commit();
        }
        return std::monostate{};
    }
    auto* audioTrack = findAudioTrack(trackId);
    if (audioTrack == nullptr) return Error{ErrorCode::invalidArgument, "no track with the given id"};
    if (audioTrack->muted != muted) {
        audioTrack->muted = muted;
        commit();
    }
    return std::monostate{};
}

application::Result<std::monostate> ProjectEditor::setTrackSoloed(
    const domain::EntityId& trackId, const bool soloed) {
    auto* track = findTrack(trackId);
    if (track != nullptr) {
        if (track->soloed != soloed) {
            track->soloed = soloed;
            commit();
        }
        return std::monostate{};
    }
    auto* audioTrack = findAudioTrack(trackId);
    if (audioTrack == nullptr) return Error{ErrorCode::invalidArgument, "no track with the given id"};
    if (audioTrack->soloed != soloed) {
        audioTrack->soloed = soloed;
        commit();
    }
    return std::monostate{};
}

application::Result<std::monostate> ProjectEditor::setAudioTrackArmed(
    const domain::EntityId& trackId, const bool armed) {
    auto* track = findAudioTrack(trackId);
    if (track == nullptr) {
        return Error{ErrorCode::invalidArgument, "record arm is only available on audio tracks"};
    }
    if (armed) {
        for (const auto& other : project_.audioTracks) {
            if (other.id != trackId && other.recordArmed
                && other.input.deviceId == track->input.deviceId
                && other.input.channelIndex == track->input.channelIndex) {
                return Error{ErrorCode::invalidArgument,
                    "this device input is already armed on another track"};
            }
        }
    }
    if (track->recordArmed != armed) {
        track->recordArmed = armed;
        commit();
    }
    return std::monostate{};
}

application::Result<std::monostate> ProjectEditor::setAudioInputRoute(
    const domain::EntityId& trackId, const std::string& deviceId, const int inputChannel,
    const domain::MonitoringMode monitoring, const std::int64_t latencyCompensationSamples) {
    if (inputChannel < 0 || latencyCompensationSamples < 0
        || latencyCompensationSamples > 480000) {
        return Error{ErrorCode::invalidArgument,
            "input channel and latency compensation must be within supported ranges"};
    }
    auto* track = findAudioTrack(trackId);
    if (track == nullptr) {
        return Error{ErrorCode::invalidArgument, "input routing is only available on audio tracks"};
    }
    const domain::AudioInputRoute route{
        deviceId, inputChannel, monitoring, latencyCompensationSamples};
    if (track->input != route) {
        track->input = route;
        commit();
    }
    return std::monostate{};
}

application::Result<std::monostate> ProjectEditor::setTempoBpm(
    const double beatsPerMinute) {
    if (!std::isfinite(beatsPerMinute) || beatsPerMinute < 20.0 || beatsPerMinute > 300.0) {
        return Error{ErrorCode::invalidArgument, "tempo must be between 20 and 300 BPM"};
    }
    if (project_.tempoMap.beatsPerMinute() != beatsPerMinute) {
        project_.tempoMap = domain::TempoMap{beatsPerMinute, project_.tempoMap.sampleRate()};
        commit();
    }
    return std::monostate{};
}

application::Result<std::monostate> ProjectEditor::replaceProject(domain::Project project) {
    if (project.id.empty() || !project.tempoMap.isValid()
        || project.timeSignatureNumerator <= 0 || project.timeSignatureDenominator <= 0) {
        return Error{ErrorCode::invalidArgument, "imported project metadata is invalid"};
    }
    project_ = std::move(project);
    commit();
    return std::monostate{};
}

application::Result<domain::EntityId> ProjectEditor::addRecordedAudioClip(
    const domain::EntityId& trackId, const std::string& name, const std::string& assetPath,
    const domain::ProjectSample capturedStartSample, const std::int64_t frameCount,
    const double sourceSampleRate, const int sourceChannels) {
    auto* track = findAudioTrack(trackId);
    if (track == nullptr) {
        return Error{ErrorCode::invalidArgument, "recorded audio needs an audio track"};
    }
    if (assetPath.empty() || name.empty() || frameCount <= 0 || !std::isfinite(sourceSampleRate)
        || sourceSampleRate <= 0.0 || sourceChannels <= 0 || sourceChannels > 32) {
        return Error{ErrorCode::invalidArgument, "recorded audio metadata is invalid"};
    }

    domain::AudioClip clip;
    clip.id = ids_.next();
    clip.name = name;
    clip.assetPath = assetPath;
    const auto compensatedStart = capturedStartSample - track->input.latencyCompensationSamples;
    clip.startSample = std::max<domain::ProjectSample>(0, compensatedStart);
    clip.sourceOffsetFrames = std::max<std::int64_t>(0, -compensatedStart);
    clip.lengthFrames = frameCount - clip.sourceOffsetFrames;
    clip.sourceSampleRate = sourceSampleRate;
    clip.sourceChannels = sourceChannels;
    if (clip.lengthFrames <= 0) {
        return Error{ErrorCode::invalidArgument,
            "latency compensation removes the entire recorded take"};
    }
    const auto newId = clip.id;
    const auto position = std::lower_bound(track->clips.begin(), track->clips.end(), clip,
        [](const domain::AudioClip& lhs, const domain::AudioClip& rhs) {
            return lhs.startSample < rhs.startSample;
        });
    track->clips.insert(position, std::move(clip));
    commit();
    return newId;
}

application::Result<std::monostate> ProjectEditor::moveAudioClip(
    const domain::EntityId& trackId, const domain::EntityId& clipId,
    const domain::ProjectSample newStartSample) {
    if (newStartSample < 0) {
        return Error{ErrorCode::invalidArgument, "audio clip must stay on the non-negative timeline"};
    }
    auto* track = findAudioTrack(trackId);
    auto* clip = findAudioClip(trackId, clipId);
    if (track == nullptr || clip == nullptr) {
        return Error{ErrorCode::invalidArgument, "no audio clip with the given id"};
    }
    if (clip->startSample == newStartSample) return std::monostate{};
    clip->startSample = newStartSample;
    std::sort(track->clips.begin(), track->clips.end(),
        [](const domain::AudioClip& lhs, const domain::AudioClip& rhs) {
            return lhs.startSample == rhs.startSample ? lhs.id < rhs.id
                                                      : lhs.startSample < rhs.startSample;
        });
    commit();
    return std::monostate{};
}

application::Result<std::monostate> ProjectEditor::trimAudioClip(
    const domain::EntityId& trackId, const domain::EntityId& clipId,
    const std::int64_t trimFromStartFrames, const std::int64_t trimFromEndFrames) {
    auto* clip = findAudioClip(trackId, clipId);
    if (clip == nullptr) return Error{ErrorCode::invalidArgument, "no audio clip with the given id"};
    if (trimFromStartFrames < 0 || trimFromEndFrames < 0
        || trimFromStartFrames + trimFromEndFrames >= clip->lengthFrames) {
        return Error{ErrorCode::invalidArgument, "trim must leave at least one audio frame"};
    }
    if (trimFromStartFrames == 0 && trimFromEndFrames == 0) return std::monostate{};
    clip->startSample += trimFromStartFrames;
    clip->sourceOffsetFrames += trimFromStartFrames;
    clip->lengthFrames -= trimFromStartFrames + trimFromEndFrames;
    clip->fadeInFrames = std::min(clip->fadeInFrames, clip->lengthFrames);
    clip->fadeOutFrames = std::min(clip->fadeOutFrames, clip->lengthFrames);
    commit();
    return std::monostate{};
}

application::Result<domain::EntityId> ProjectEditor::splitAudioClip(
    const domain::EntityId& trackId, const domain::EntityId& clipId,
    const std::int64_t splitFrame) {
    auto* track = findAudioTrack(trackId);
    auto* clip = findAudioClip(trackId, clipId);
    if (track == nullptr || clip == nullptr) {
        return Error{ErrorCode::invalidArgument, "no audio clip with the given id"};
    }
    if (splitFrame <= 0 || splitFrame >= clip->lengthFrames) {
        return Error{ErrorCode::invalidArgument, "split must be inside the audio clip"};
    }
    domain::AudioClip right = *clip;
    right.id = ids_.next();
    right.name += " Split";
    right.startSample += splitFrame;
    right.sourceOffsetFrames += splitFrame;
    right.lengthFrames -= splitFrame;
    right.fadeInFrames = 0;
    clip->lengthFrames = splitFrame;
    clip->fadeOutFrames = 0;
    const auto newId = right.id;
    const auto leftPosition = std::find_if(track->clips.begin(), track->clips.end(),
        [&](const domain::AudioClip& candidate) { return candidate.id == clipId; });
    track->clips.insert(leftPosition + 1, std::move(right));
    commit();
    return newId;
}

application::Result<std::monostate> ProjectEditor::setAudioClipFades(
    const domain::EntityId& trackId, const domain::EntityId& clipId,
    const std::int64_t fadeInFrames, const std::int64_t fadeOutFrames) {
    auto* clip = findAudioClip(trackId, clipId);
    if (clip == nullptr) return Error{ErrorCode::invalidArgument, "no audio clip with the given id"};
    if (fadeInFrames < 0 || fadeOutFrames < 0
        || fadeInFrames + fadeOutFrames > clip->lengthFrames) {
        return Error{ErrorCode::invalidArgument, "audio fades must fit inside the clip"};
    }
    if (clip->fadeInFrames != fadeInFrames || clip->fadeOutFrames != fadeOutFrames) {
        clip->fadeInFrames = fadeInFrames;
        clip->fadeOutFrames = fadeOutFrames;
        commit();
    }
    return std::monostate{};
}

application::Result<std::monostate> ProjectEditor::setAudioClipGain(
    const domain::EntityId& trackId, const domain::EntityId& clipId, const float gain) {
    auto* clip = findAudioClip(trackId, clipId);
    if (clip == nullptr) return Error{ErrorCode::invalidArgument, "no audio clip with the given id"};
    if (!std::isfinite(gain) || gain < 0.0F || gain > 4.0F) {
        return Error{ErrorCode::invalidArgument, "audio clip gain must be between 0 and 4"};
    }
    if (clip->gain != gain) {
        clip->gain = gain;
        commit();
    }
    return std::monostate{};
}

application::Result<domain::EntityId> ProjectEditor::setAudioCrossfade(
    const domain::EntityId& trackId, const domain::EntityId& leftClipId,
    const domain::EntityId& rightClipId, const std::int64_t lengthFrames) {
    auto* track = findAudioTrack(trackId);
    auto* left = findAudioClip(trackId, leftClipId);
    auto* right = findAudioClip(trackId, rightClipId);
    if (track == nullptr || left == nullptr || right == nullptr || left == right) {
        return Error{ErrorCode::invalidArgument, "crossfade needs two clips on the same audio track"};
    }
    if (lengthFrames <= 0 || lengthFrames > left->lengthFrames
        || lengthFrames > right->lengthFrames) {
        return Error{ErrorCode::invalidArgument, "crossfade length must fit both audio clips"};
    }
    if (left->startSample > right->startSample) std::swap(left, right);
    right->startSample = left->endSample() - lengthFrames;
    left->fadeOutFrames = lengthFrames;
    right->fadeInFrames = lengthFrames;
    const auto existing = std::find_if(track->crossfades.begin(), track->crossfades.end(),
        [&](const domain::AudioCrossfade& fade) {
            return fade.leftClipId == left->id && fade.rightClipId == right->id;
        });
    domain::EntityId id;
    if (existing == track->crossfades.end()) {
        id = ids_.next();
        track->crossfades.push_back({id, left->id, right->id, lengthFrames});
    } else {
        existing->lengthFrames = lengthFrames;
        id = existing->id;
    }
    std::sort(track->clips.begin(), track->clips.end(),
        [](const domain::AudioClip& lhs, const domain::AudioClip& rhs) {
            return lhs.startSample == rhs.startSample ? lhs.id < rhs.id
                                                      : lhs.startSample < rhs.startSample;
        });
    commit();
    return id;
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
    if (!note.isValid() || note.start > kMaxEditableTick - note.duration) {
        return Error{ErrorCode::invalidArgument,
            "note must have positive duration and valid pitch/velocity"};
    }
    if (note.start < clip->range.start) {
        return Error{ErrorCode::invalidArgument, "note must not start before the clip"};
    }
    const auto newId = note.id;
    clip->range.end = std::max(clip->range.end, note.end());
    insertSorted(clip->notes, std::move(note));
    commit();
    return newId;
}

application::Result<std::monostate> ProjectEditor::moveNote(
    const domain::EntityId& trackId, const domain::EntityId& clipId,
    const domain::EntityId& noteId, const domain::Tick newStart, const domain::Pitch newPitch) {
    const auto* clip = findClip(trackId, clipId);
    if (clip == nullptr) {
        return Error{ErrorCode::invalidArgument, "no clip with the given id"};
    }
    const auto position = std::find_if(clip->notes.begin(), clip->notes.end(),
        [&](const domain::NoteEvent& note) { return note.id == noteId; });
    if (position == clip->notes.end()) {
        return Error{ErrorCode::invalidArgument, "no note with the given id"};
    }
    return moveNotes(trackId, clipId, {noteId},
        newStart - position->start, static_cast<int>(newPitch - position->pitch));
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
    if (position->start > kMaxEditableTick - newDuration) {
        return Error{ErrorCode::invalidArgument, "resized note exceeds the timeline limit"};
    }
    position->duration = newDuration;
    clip->range.end = std::max(clip->range.end, position->end());
    commit();
    return std::monostate{};
}

application::Result<std::monostate> ProjectEditor::deleteNote(
    const domain::EntityId& trackId, const domain::EntityId& clipId,
    const domain::EntityId& noteId) {
    return deleteNotes(trackId, clipId, {noteId});
}

application::Result<std::monostate> ProjectEditor::moveNotes(
    const domain::EntityId& trackId, const domain::EntityId& clipId,
    const std::vector<domain::EntityId>& noteIds,
    const domain::Tick deltaTick, const int deltaPitch) {
    auto* clip = findClip(trackId, clipId);
    if (clip == nullptr) {
        return Error{ErrorCode::invalidArgument, "no clip with the given id"};
    }
    if (noteIds.empty()) {
        return Error{ErrorCode::invalidArgument, "at least one note must be selected"};
    }

    auto sortedIds = noteIds;
    std::sort(sortedIds.begin(), sortedIds.end());
    if (std::adjacent_find(sortedIds.begin(), sortedIds.end()) != sortedIds.end()) {
        return Error{ErrorCode::invalidArgument, "selected note ids must be unique"};
    }

    auto updatedNotes = clip->notes;
    std::size_t matched = 0;
    domain::Tick requiredEnd = clip->range.end;
    for (auto& note : updatedNotes) {
        if (!std::binary_search(sortedIds.begin(), sortedIds.end(), note.id)) {
            continue;
        }
        ++matched;
        if (deltaTick < clip->range.start - note.start
            || deltaTick > kMaxEditableTick - note.end()
            || deltaPitch < domain::kMinPitch - note.pitch
            || deltaPitch > domain::kMaxPitch - note.pitch) {
            return Error{ErrorCode::invalidArgument,
                "every moved note must stay within the clip and MIDI pitch range"};
        }
        note.start += deltaTick;
        note.pitch += deltaPitch;
        requiredEnd = std::max(requiredEnd, note.end());
    }
    if (matched != sortedIds.size()) {
        return Error{ErrorCode::invalidArgument, "one or more selected notes do not exist"};
    }
    if (deltaTick == 0 && deltaPitch == 0) {
        return std::monostate{};
    }

    std::sort(updatedNotes.begin(), updatedNotes.end(), noteLess);
    clip->notes = std::move(updatedNotes);
    clip->range.end = requiredEnd;
    commit();
    return std::monostate{};
}

application::Result<std::monostate> ProjectEditor::deleteNotes(
    const domain::EntityId& trackId, const domain::EntityId& clipId,
    const std::vector<domain::EntityId>& noteIds) {
    auto* clip = findClip(trackId, clipId);
    if (clip == nullptr) {
        return Error{ErrorCode::invalidArgument, "no clip with the given id"};
    }
    if (noteIds.empty()) {
        return Error{ErrorCode::invalidArgument, "at least one note must be selected"};
    }
    auto sortedIds = noteIds;
    std::sort(sortedIds.begin(), sortedIds.end());
    if (std::adjacent_find(sortedIds.begin(), sortedIds.end()) != sortedIds.end()) {
        return Error{ErrorCode::invalidArgument, "selected note ids must be unique"};
    }
    const auto matched = static_cast<std::size_t>(std::count_if(
        clip->notes.begin(), clip->notes.end(), [&](const domain::NoteEvent& note) {
            return std::binary_search(sortedIds.begin(), sortedIds.end(), note.id);
        }));
    if (matched != sortedIds.size()) {
        return Error{ErrorCode::invalidArgument, "one or more selected notes do not exist"};
    }
    std::erase_if(clip->notes, [&](const domain::NoteEvent& note) {
        return std::binary_search(sortedIds.begin(), sortedIds.end(), note.id);
    });
    commit();
    return std::monostate{};
}

application::Result<std::vector<domain::EntityId>> ProjectEditor::pasteNotes(
    const domain::EntityId& trackId, const domain::EntityId& clipId,
    const domain::Tick insertionStart,
    const std::vector<domain::GeneratedNote>& relativeNotes) {
    auto* clip = findClip(trackId, clipId);
    if (clip == nullptr) {
        return Error{ErrorCode::invalidArgument, "no clip with the given id"};
    }
    if (relativeNotes.empty()) {
        return Error{ErrorCode::invalidArgument, "clipboard contains no notes"};
    }

    std::vector<domain::NoteEvent> pasted;
    pasted.reserve(relativeNotes.size());
    for (const auto& source : relativeNotes) {
        if (insertionStart < clip->range.start || insertionStart > kMaxEditableTick
            || source.start < 0 || source.duration <= 0
            || !domain::isValidPitch(source.pitch)
            || !domain::isValidVelocity(source.velocity)
            || source.start > kMaxEditableTick - insertionStart
            || source.duration > kMaxEditableTick - insertionStart - source.start) {
            return Error{ErrorCode::invalidArgument,
                "every pasted note must stay within the timeline and MIDI ranges"};
        }
        domain::NoteEvent note;
        note.id = {"pending"};
        note.start = insertionStart + source.start;
        note.duration = source.duration;
        note.pitch = source.pitch;
        note.velocity = source.velocity;
        pasted.push_back(std::move(note));
    }

    std::vector<domain::EntityId> insertedIds;
    insertedIds.reserve(pasted.size());
    domain::Tick requiredEnd = clip->range.end;
    for (auto& note : pasted) {
        note.id = ids_.next();
        insertedIds.push_back(note.id);
        requiredEnd = std::max(requiredEnd, note.end());
        insertSorted(clip->notes, std::move(note));
    }
    clip->range.end = requiredEnd;
    commit();
    return insertedIds;
}

application::Result<std::vector<domain::EntityId>> ProjectEditor::insertChord(
    const domain::EntityId& trackId, const domain::EntityId& clipId,
    const domain::Tick start, const domain::Tick duration, const domain::ChordSpec& spec) {
    const auto preview = previewChord(trackId, clipId, start, duration, spec);
    if (std::holds_alternative<Error>(preview)) {
        return std::get<Error>(preview);
    }
    auto* clip = findClip(trackId, clipId);
    const auto& generated = std::get<std::vector<domain::GeneratedNote>>(preview);

    std::vector<domain::EntityId> insertedIds;
    insertedIds.reserve(generated.size());
    domain::Tick requiredEnd = clip->range.end;
    for (const auto& generatedNote : generated) {
        domain::NoteEvent note;
        note.id = ids_.next();
        note.start = generatedNote.start;
        note.duration = generatedNote.duration;
        note.pitch = generatedNote.pitch;
        note.velocity = generatedNote.velocity;
        insertedIds.push_back(note.id);
        requiredEnd = std::max(requiredEnd, note.end());
        insertSorted(clip->notes, std::move(note));
    }
    clip->range.end = requiredEnd;
    commit();  // one undo step for the whole chord
    return insertedIds;
}

application::Result<std::vector<domain::GeneratedNote>> ProjectEditor::previewChord(
    const domain::EntityId& trackId, const domain::EntityId& clipId,
    const domain::Tick start, const domain::Tick duration, const domain::ChordSpec& spec) const {
    const auto* track = findTrack(trackId);
    if (track == nullptr) {
        return Error{ErrorCode::invalidArgument, "no track with the given id"};
    }
    const auto instrument = domain::findInstrument(track->instrumentId);
    if (instrument.has_value() && !instrument->isPitched()) {
        return Error{ErrorCode::invalidArgument, "chord insertion is unavailable for drums"};
    }
    const auto* clip = findClip(trackId, clipId);
    if (clip == nullptr) {
        return Error{ErrorCode::invalidArgument, "no clip with the given id"};
    }
    if (duration <= 0 || start < clip->range.start
        || start > kMaxEditableTick - duration) {
        return Error{ErrorCode::invalidArgument, "chord note duration must be positive"};
    }
    auto pitchesResult = domain::chordPitches(spec);
    if (std::holds_alternative<Error>(pitchesResult)) {
        return std::get<Error>(pitchesResult);
    }
    const auto& pitches = std::get<std::vector<domain::Pitch>>(pitchesResult);
    std::vector<domain::GeneratedNote> notes;
    notes.reserve(pitches.size());
    for (const auto pitch : pitches) {
        notes.push_back({start, duration, pitch, spec.velocity});
    }
    return notes;
}

application::Result<domain::RhythmRequest> ProjectEditor::buildRhythmRequest(
    const domain::EntityId& trackId, const domain::EntityId& clipId,
    const RhythmApply& request) const {
    const auto* clip = findClip(trackId, clipId);
    if (clip == nullptr) {
        return Error{ErrorCode::invalidArgument, "no clip with the given id"};
    }
    if (request.sourceNoteIds.empty()) {
        return Error{ErrorCode::invalidArgument, "rhythm generation needs at least one source note"};
    }
    if (request.subdivision <= 0) {
        return Error{ErrorCode::invalidArgument, "rhythm subdivision must be positive"};
    }

    // Resolve the source notes and the span they cover. The generated rhythm fills exactly the
    // tick range spanned by the selection, so it stays inside the source's footprint.
    domain::RhythmRequest generated;
    generated.pattern = request.pattern;
    generated.subdivision = request.subdivision;
    generated.seed = request.seed;

    domain::Tick spanStart = std::numeric_limits<domain::Tick>::max();
    domain::Tick spanEnd = std::numeric_limits<domain::Tick>::min();
    for (const auto& noteId : request.sourceNoteIds) {
        const auto position = std::find_if(clip->notes.begin(), clip->notes.end(),
            [&](const domain::NoteEvent& note) { return note.id == noteId; });
        if (position == clip->notes.end()) {
            return Error{ErrorCode::invalidArgument, "source note is not in the clip"};
        }
        generated.voices.push_back(domain::RhythmVoice{position->pitch, position->velocity});
        spanStart = std::min(spanStart, position->start);
        spanEnd = std::max(spanEnd, position->end());
    }

    generated.start = spanStart;
    generated.length = spanEnd - spanStart;
    return generated;
}

application::Result<std::vector<domain::GeneratedNote>> ProjectEditor::previewRhythm(
    const domain::EntityId& trackId, const domain::EntityId& clipId,
    const RhythmApply& request) const {
    auto requestResult = buildRhythmRequest(trackId, clipId, request);
    if (std::holds_alternative<Error>(requestResult)) {
        return std::get<Error>(requestResult);
    }
    return domain::generateRhythm(std::get<domain::RhythmRequest>(requestResult));
}

application::Result<std::vector<domain::EntityId>> ProjectEditor::applyRhythm(
    const domain::EntityId& trackId, const domain::EntityId& clipId,
    const RhythmApply& request) {
    if (request.expectedRevision.has_value() && *request.expectedRevision != revision_) {
        return Error{ErrorCode::staleRevision, "rhythm preview is stale; preview again before apply"};
    }
    auto requestResult = buildRhythmRequest(trackId, clipId, request);
    if (std::holds_alternative<Error>(requestResult)) {
        return std::get<Error>(requestResult);
    }
    auto generatedResult = domain::generateRhythm(std::get<domain::RhythmRequest>(requestResult));
    if (std::holds_alternative<Error>(generatedResult)) {
        return std::get<Error>(generatedResult);
    }
    const auto& generated = std::get<std::vector<domain::GeneratedNote>>(generatedResult);

    auto* clip = findClip(trackId, clipId);
    if (clip == nullptr) {
        return Error{ErrorCode::invalidArgument, "no clip with the given id"};
    }

    // Validate every generated note fits the clip before mutating, so apply is all-or-nothing.
    for (const auto& note : generated) {
        if (note.duration <= 0 || !domain::isValidPitch(note.pitch)
            || !domain::isValidVelocity(note.velocity)) {
            return Error{ErrorCode::invalidArgument, "generated note is invalid"};
        }
        if (!clip->range.contains(note.start) || note.end() > clip->range.end) {
            return Error{ErrorCode::invalidArgument, "generated note must lie within the clip range"};
        }
    }

    // Optionally remove the source notes so the rhythm replaces them rather than layering on top.
    if (request.replaceSource) {
        for (const auto& noteId : request.sourceNoteIds) {
            const auto position = std::find_if(clip->notes.begin(), clip->notes.end(),
                [&](const domain::NoteEvent& note) { return note.id == noteId; });
            if (position != clip->notes.end()) {
                clip->notes.erase(position);
            }
        }
    }

    std::vector<domain::EntityId> insertedIds;
    insertedIds.reserve(generated.size());
    for (const auto& note : generated) {
        domain::NoteEvent event;
        event.id = ids_.next();
        event.start = note.start;
        event.duration = note.duration;
        event.pitch = note.pitch;
        event.velocity = note.velocity;
        insertedIds.push_back(event.id);
        insertSorted(clip->notes, std::move(event));
    }
    commit();  // one undo step for the whole generated rhythm
    return insertedIds;
}

}  // namespace composer::commands
