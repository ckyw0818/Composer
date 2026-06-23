#include "audio/runtime/ProjectCompiler.h"

#include "domain/Instrument.h"
#include "recording/WavFile.h"

#include <algorithm>
#include <variant>

namespace composer::audio {

ProjectSnapshot ProjectCompiler::compile(
    const domain::Project& project, const domain::Revision revision,
    const std::filesystem::path& assetRoot) {
    const auto& tempo = project.tempoMap;

    ProjectSnapshot snapshot;
    snapshot.revision = revision;
    snapshot.sampleRate = tempo.sampleRate();

    const bool hasSolo = std::any_of(project.tracks.begin(), project.tracks.end(),
        [](const domain::InstrumentTrack& track) { return track.soloed; });

    std::uint32_t trackIndex = 0;
    for (const auto& track : project.tracks) {
        // Muted tracks contribute nothing to the audible render but still advance the index so a
        // given track keeps a stable voice group across edits.
        for (const auto& clip : track.clips) {
            for (const auto& note : clip.notes) {
                // In S1, note start/duration are absolute project ticks. The command layer
                // constrains every note to lie within its clip's tick range, so the compiler maps
                // note ticks straight to samples through the project TempoMap.
                SnapshotNote out;
                out.startSample = tempo.ticksToSamples(note.start);
                out.endSample = tempo.ticksToSamples(note.end());
                out.pitch = note.pitch;
                const bool audible = !track.muted && (!hasSolo || track.soloed);
                out.amplitude = !audible
                    ? 0.0F
                    : static_cast<float>(note.velocity) / 127.0F * track.volume;
                out.pan = std::clamp(track.pan, -1.0F, 1.0F);
                out.timbre = domain::timbreFor(track.instrumentId);
                out.voiceGroup = trackIndex;
                snapshot.notes.push_back(out);
                snapshot.lengthSamples = std::max(snapshot.lengthSamples, out.endSample);
            }
        }
        ++trackIndex;
    }

    const bool hasAudioSolo = std::any_of(project.audioTracks.begin(), project.audioTracks.end(),
        [](const domain::AudioTrack& track) { return track.soloed; });
    for (const auto& track : project.audioTracks) {
        const bool audible = !track.muted && (!hasAudioSolo || track.soloed);
        if (!audible) continue;
        for (const auto& clip : track.clips) {
            if (clip.assetPath.empty() || clip.lengthFrames <= 0 || clip.sourceChannels <= 0) {
                continue;
            }
            const auto sourcePath = std::filesystem::path{clip.assetPath}.is_absolute()
                ? std::filesystem::path{clip.assetPath}
                : assetRoot / clip.assetPath;
            auto loaded = recording::WavFile::readFloatInterleaved(sourcePath);
            if (std::holds_alternative<application::Error>(loaded)) continue;
            auto [info, samples] = std::get<std::pair<recording::WavInfo, std::vector<float>>>(
                std::move(loaded));
            if (info.channels <= 0 || samples.empty()) continue;
            SnapshotAudioClip out;
            out.startSample = clip.startSample;
            out.sourceOffsetFrames = std::max<std::int64_t>(0, clip.sourceOffsetFrames);
            out.lengthFrames = std::min<std::int64_t>(
                clip.lengthFrames, info.frames - out.sourceOffsetFrames);
            if (out.lengthFrames <= 0) continue;
            out.channels = info.channels;
            out.gain = clip.gain * track.volume;
            out.pan = std::clamp(track.pan, -1.0F, 1.0F);
            out.fadeInFrames = std::min(clip.fadeInFrames, out.lengthFrames);
            out.fadeOutFrames = std::min(clip.fadeOutFrames, out.lengthFrames);
            out.interleaved = std::move(samples);
            snapshot.lengthSamples = std::max(snapshot.lengthSamples, out.startSample + out.lengthFrames);
            snapshot.audioClips.push_back(std::move(out));
        }
    }

    std::sort(snapshot.notes.begin(), snapshot.notes.end(),
        [](const SnapshotNote& lhs, const SnapshotNote& rhs) {
            if (lhs.startSample != rhs.startSample) {
                return lhs.startSample < rhs.startSample;
            }
            if (lhs.voiceGroup != rhs.voiceGroup) {
                return lhs.voiceGroup < rhs.voiceGroup;
            }
            return lhs.pitch < rhs.pitch;
        });
    return snapshot;
}

}  // namespace composer::audio
