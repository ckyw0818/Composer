#pragma once

#include "domain/TempoMap.h"
#include "domain/Types.h"

#include <cstdint>
#include <string>
#include <vector>

namespace composer::domain {

// A single MIDI note. start/duration are musical ticks at kPpq; the clip that owns the note
// gives them an absolute timeline position. duration is always > 0 for a valid note.
struct NoteEvent final {
    EntityId id;
    Tick start{};
    Tick duration{};
    Pitch pitch{};
    Velocity velocity{kMaxVelocity};

    [[nodiscard]] Tick end() const noexcept { return start + duration; }
    [[nodiscard]] bool operator==(const NoteEvent&) const = default;

    [[nodiscard]] bool isValid() const noexcept {
        return !id.empty() && start >= 0 && duration > 0 && isValidPitch(pitch)
            && isValidVelocity(velocity);
    }
};

// A MIDI clip placed on an instrument track. Notes are stored in canonical order (start, then
// pitch, then id) so two projects built by different command paths compare equal byte-for-byte.
struct MidiClip final {
    EntityId id;
    std::string name;
    HalfOpenTickRange range;  // position on the track timeline, in ticks
    std::vector<NoteEvent> notes;

    [[nodiscard]] bool operator==(const MidiClip&) const = default;
};

// A pitched instrument track. The instrumentId selects a built-in instrument (piano/bass/drums in
// S1). Drums are unpitched; the chord tool is disabled for them at the command layer.
struct InstrumentTrack final {
    EntityId id;
    std::string name;
    std::string instrumentId;
    float volume{1.0F};
    float pan{0.0F};
    bool muted{false};
    bool soloed{false};
    std::vector<MidiClip> clips;

    [[nodiscard]] bool operator==(const InstrumentTrack&) const = default;
};

enum class MonitoringMode : std::uint8_t {
    off,
    direct,
    software
};

struct AudioInputRoute final {
    std::string deviceId;
    int channelIndex{};
    MonitoringMode monitoring{MonitoringMode::off};
    std::int64_t latencyCompensationSamples{};

    [[nodiscard]] bool operator==(const AudioInputRoute&) const = default;
};

// Audio sources are immutable. Editing only changes the timeline/source window and gain envelope.
// Sample ranges are half-open [start, start + length), matching the runtime time model.
struct AudioClip final {
    EntityId id;
    std::string name;
    std::string assetPath;
    ProjectSample startSample{};
    std::int64_t sourceOffsetFrames{};
    std::int64_t lengthFrames{};
    double sourceSampleRate{48000.0};
    int sourceChannels{1};
    float gain{1.0F};
    std::int64_t fadeInFrames{};
    std::int64_t fadeOutFrames{};
    bool stretchEnabled{false};

    [[nodiscard]] ProjectSample endSample() const noexcept {
        return startSample + lengthFrames;
    }
    [[nodiscard]] bool operator==(const AudioClip&) const = default;
};

struct AudioCrossfade final {
    EntityId id;
    EntityId leftClipId;
    EntityId rightClipId;
    std::int64_t lengthFrames{};

    [[nodiscard]] bool operator==(const AudioCrossfade&) const = default;
};

struct AudioTrack final {
    EntityId id;
    std::string name;
    float volume{1.0F};
    float pan{0.0F};
    bool muted{false};
    bool soloed{false};
    bool recordArmed{false};
    AudioInputRoute input;
    std::vector<AudioClip> clips;
    std::vector<AudioCrossfade> crossfades;

    [[nodiscard]] bool operator==(const AudioTrack&) const = default;
};

// The versioned project root. trackOrder is implied by vector position for tracks/clips/notes,
// but every entity also carries a stable UUID so identity survives reordering and persistence.
struct Project final {
    static constexpr int kSchemaVersion = 2;

    EntityId id;
    std::string name;
    TempoMap tempoMap{};
    int timeSignatureNumerator{4};
    int timeSignatureDenominator{4};
    // Kept under its original name for v1 source and manifest compatibility.
    std::vector<InstrumentTrack> tracks;
    std::vector<AudioTrack> audioTracks;
    // Mixed instrument/audio presentation order. Empty means legacy order: instruments then audio.
    std::vector<EntityId> trackOrder;

    [[nodiscard]] bool operator==(const Project&) const = default;
};

}  // namespace composer::domain
