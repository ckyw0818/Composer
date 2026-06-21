#include "audio/runtime/ProjectCompiler.h"

#include <algorithm>

namespace composer::audio {

ProjectSnapshot ProjectCompiler::compile(
    const domain::Project& project, const domain::Revision revision) {
    const auto& tempo = project.tempoMap;

    ProjectSnapshot snapshot;
    snapshot.revision = revision;
    snapshot.sampleRate = tempo.sampleRate();

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
                out.amplitude = track.muted
                    ? 0.0F
                    : static_cast<float>(note.velocity) / 127.0F * track.volume;
                out.voiceGroup = trackIndex;
                snapshot.notes.push_back(out);
                snapshot.lengthSamples = std::max(snapshot.lengthSamples, out.endSample);
            }
        }
        ++trackIndex;
    }

    std::sort(snapshot.notes.begin(), snapshot.notes.end(),
        [](const SnapshotNote& lhs, const SnapshotNote& rhs) {
            if (lhs.startSample != rhs.startSample) {
                return lhs.startSample < rhs.startSample;
            }
            return lhs.pitch < rhs.pitch;
        });
    return snapshot;
}

}  // namespace composer::audio
