#include "persistence/MidiFile.h"

#include "domain/Instrument.h"
#include "domain/Types.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <deque>
#include <limits>
#include <optional>
#include <string>
#include <vector>

namespace composer::persistence {
namespace {

using application::Error;
using application::ErrorCode;

// SMF division: ticks per quarter note. We emit at the project's PPQ so musical positions survive
// the round-trip without rescaling. domain::kPpq fits comfortably in the 15-bit metrical division.
constexpr std::uint16_t kDivision = static_cast<std::uint16_t>(domain::kPpq);

// Bound on event/track counts so a malformed file cannot drive unbounded allocation. Generous
// relative to real projects, strict relative to a fuzz input claiming billions of events.
constexpr std::size_t kMaxTracks = 4096;
constexpr std::size_t kMaxEventsPerTrack = 1'000'000;
constexpr std::size_t kMaxSmfTracks = std::numeric_limits<std::uint16_t>::max();
constexpr domain::Tick kMaxMidiTick = 0x0FFFFFFF;

// --- Writing -------------------------------------------------------------------------------

void putU16(std::vector<std::uint8_t>& out, const std::uint16_t value) {
    out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFF));
    out.push_back(static_cast<std::uint8_t>(value & 0xFF));
}

void putU32(std::vector<std::uint8_t>& out, const std::uint32_t value) {
    out.push_back(static_cast<std::uint8_t>((value >> 24) & 0xFF));
    out.push_back(static_cast<std::uint8_t>((value >> 16) & 0xFF));
    out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFF));
    out.push_back(static_cast<std::uint8_t>(value & 0xFF));
}

// MIDI variable-length quantity: 7 bits per byte, big-endian, high bit set on all but the last.
void putVarLen(std::vector<std::uint8_t>& out, std::uint32_t value) {
    std::array<std::uint8_t, 5> buffer{};
    std::size_t count = 0;
    buffer[count++] = static_cast<std::uint8_t>(value & 0x7F);
    value >>= 7;
    while (value > 0) {
        buffer[count++] = static_cast<std::uint8_t>((value & 0x7F) | 0x80);
        value >>= 7;
    }
    for (std::size_t i = count; i-- > 0;) {
        out.push_back(buffer[i]);
    }
}

// A single channel-voice event keyed by absolute tick, used to merge a track's note-ons and
// note-offs into one time-ordered stream before delta-encoding.
struct AbsoluteEvent final {
    domain::Tick tick{};
    std::uint8_t status{};  // 0x90 note-on, 0x80 note-off
    std::uint8_t data1{};   // pitch
    std::uint8_t data2{};   // velocity
    int priority{};         // note-off before note-on at the same tick
    std::size_t sequence{}; // stable order within the same event kind
};

[[nodiscard]] std::uint8_t midiChannelFor(
    const domain::InstrumentTrack& track, const std::size_t trackIndex) noexcept {
    if (const auto instrument = domain::findInstrument(track.instrumentId);
        instrument.has_value() && instrument->role == domain::InstrumentRole::drums) {
        return 9;
    }
    const auto channel = static_cast<std::uint8_t>(trackIndex % 15);
    return channel >= 9 ? static_cast<std::uint8_t>(channel + 1) : channel;
}

[[nodiscard]] std::uint8_t midiProgramFor(const std::string_view instrumentId) noexcept {
    if (instrumentId == "builtin.guitar") return 24;
    if (instrumentId == "builtin.bass") return 32;
    if (instrumentId == "builtin.synth") return 80;
    return 0;
}

[[nodiscard]] std::string_view instrumentForMidi(
    const std::optional<std::uint8_t> program, const bool drumChannel) noexcept {
    if (drumChannel) return "builtin.drums";
    if (!program.has_value()) return "builtin.piano";
    if (*program >= 24 && *program <= 31) return "builtin.guitar";
    if (*program >= 32 && *program <= 39) return "builtin.bass";
    if (*program >= 80 && *program <= 95) return "builtin.synth";
    return "builtin.piano";
}

std::vector<std::uint8_t> finishChunk(std::vector<std::uint8_t> body) {
    std::vector<std::uint8_t> chunk;
    chunk.push_back('M');
    chunk.push_back('T');
    chunk.push_back('r');
    chunk.push_back('k');
    putU32(chunk, static_cast<std::uint32_t>(body.size()));
    chunk.insert(chunk.end(), body.begin(), body.end());
    return chunk;
}

std::vector<std::uint8_t> buildConductorChunk(const domain::Project& project) {
    std::vector<std::uint8_t> body;
    const auto microseconds = static_cast<std::uint32_t>(std::clamp(
        std::llround(60'000'000.0 / project.tempoMap.beatsPerMinute()), 1LL, 0xFFFFFFLL));
    putVarLen(body, 0);
    body.insert(body.end(), {0xFF, 0x51, 0x03,
        static_cast<std::uint8_t>((microseconds >> 16) & 0xFF),
        static_cast<std::uint8_t>((microseconds >> 8) & 0xFF),
        static_cast<std::uint8_t>(microseconds & 0xFF)});

    int denominatorPower = 0;
    int denominator = project.timeSignatureDenominator;
    while (denominator > 1) {
        denominator /= 2;
        ++denominatorPower;
    }
    putVarLen(body, 0);
    body.insert(body.end(), {0xFF, 0x58, 0x04,
        static_cast<std::uint8_t>(project.timeSignatureNumerator),
        static_cast<std::uint8_t>(denominatorPower), 24, 8});
    putVarLen(body, 0);
    body.insert(body.end(), {0xFF, 0x2F, 0x00});
    return finishChunk(std::move(body));
}

std::vector<std::uint8_t> buildTrackChunk(
    const domain::InstrumentTrack& track, const std::size_t trackIndex) {
    std::vector<AbsoluteEvent> events;
    std::size_t sequence = 0;
    const auto channel = midiChannelFor(track, trackIndex);
    for (const auto& clip : track.clips) {
        for (const auto& note : clip.notes) {
            const auto pitch = static_cast<std::uint8_t>(
                std::clamp<domain::Pitch>(note.pitch, 0, 127));
            const auto velocity = static_cast<std::uint8_t>(
                std::clamp<domain::Velocity>(note.velocity, 1, 127));
            events.push_back(AbsoluteEvent{note.start,
                static_cast<std::uint8_t>(0x90 | channel), pitch, velocity, 1, sequence++});
            events.push_back(AbsoluteEvent{note.end(),
                static_cast<std::uint8_t>(0x80 | channel), pitch, 0, 0, sequence++});
        }
    }
    std::sort(events.begin(), events.end(), [](const AbsoluteEvent& a, const AbsoluteEvent& b) {
        if (a.tick != b.tick) {
            return a.tick < b.tick;
        }
        if (a.priority != b.priority) {
            return a.priority < b.priority;
        }
        return a.sequence < b.sequence;
    });

    std::vector<std::uint8_t> body;
    // Track name meta event (FF 03 len text) so external DAWs label the track.
    putVarLen(body, 0);
    body.push_back(0xFF);
    body.push_back(0x03);
    putVarLen(body, static_cast<std::uint32_t>(track.name.size()));
    for (const char c : track.name) {
        body.push_back(static_cast<std::uint8_t>(c));
    }

    // General MIDI program change. Drums use channel 10 and ignore the program value.
    putVarLen(body, 0);
    body.push_back(static_cast<std::uint8_t>(0xC0 | channel));
    body.push_back(midiProgramFor(track.instrumentId));

    domain::Tick previous = 0;
    for (const auto& event : events) {
        const auto delta = static_cast<std::uint32_t>(event.tick - previous);
        previous = event.tick;
        putVarLen(body, delta);
        body.push_back(event.status);  // channel 0; we keep explicit status (no running status)
        body.push_back(event.data1);
        body.push_back(event.data2);
    }
    // End-of-track meta event.
    putVarLen(body, 0);
    body.push_back(0xFF);
    body.push_back(0x2F);
    body.push_back(0x00);

    return finishChunk(std::move(body));
}

// --- Reading -------------------------------------------------------------------------------

class Reader final {
public:
    Reader(const std::uint8_t* data, const std::size_t size) : data_(data), size_(size) {}

    [[nodiscard]] std::size_t position() const noexcept { return pos_; }

    [[nodiscard]] bool readU8(std::uint8_t& out) {
        if (pos_ >= size_) {
            return false;
        }
        out = data_[pos_++];
        return true;
    }

    [[nodiscard]] bool readU16(std::uint16_t& out) {
        if (pos_ > size_ || 2 > size_ - pos_) {
            return false;
        }
        out = static_cast<std::uint16_t>((data_[pos_] << 8) | data_[pos_ + 1]);
        pos_ += 2;
        return true;
    }

    [[nodiscard]] bool readU32(std::uint32_t& out) {
        if (pos_ > size_ || 4 > size_ - pos_) {
            return false;
        }
        out = (static_cast<std::uint32_t>(data_[pos_]) << 24)
            | (static_cast<std::uint32_t>(data_[pos_ + 1]) << 16)
            | (static_cast<std::uint32_t>(data_[pos_ + 2]) << 8)
            | static_cast<std::uint32_t>(data_[pos_ + 3]);
        pos_ += 4;
        return true;
    }

    [[nodiscard]] bool readVarLen(std::uint32_t& out) {
        out = 0;
        for (int i = 0; i < 4; ++i) {
            std::uint8_t byte = 0;
            if (!readU8(byte)) {
                return false;
            }
            out = (out << 7) | (byte & 0x7F);
            if ((byte & 0x80) == 0) {
                return true;
            }
        }
        return false;  // varlen longer than 4 bytes is malformed for our value range
    }

    [[nodiscard]] bool skip(const std::size_t count) {
        if (pos_ > size_ || count > size_ - pos_) {
            return false;
        }
        pos_ += count;
        return true;
    }

    [[nodiscard]] bool readBytes(const std::size_t count, std::string& out) {
        if (pos_ > size_ || count > size_ - pos_) {
            return false;
        }
        out.assign(reinterpret_cast<const char*>(data_ + pos_), count);
        pos_ += count;
        return true;
    }

private:
    const std::uint8_t* data_{};
    std::size_t size_{};
    std::size_t pos_{0};
};

// An in-progress note awaiting its note-off, keyed by pitch on a single channel.
struct PendingNote final {
    domain::Tick start{};
    std::uint8_t velocity{};
};

}  // namespace

application::Result<std::vector<std::uint8_t>> MidiFile::exportBytes(
    const domain::Project& project) {
    if (!project.tempoMap.isValid()) {
        return Error{ErrorCode::invalidArgument, "project tempo map is invalid"};
    }
    const auto denominator = project.timeSignatureDenominator;
    const bool powerOfTwo = denominator > 0 && (denominator & (denominator - 1)) == 0;
    if (project.timeSignatureNumerator <= 0 || project.timeSignatureNumerator > 255
        || !powerOfTwo || denominator > 128) {
        return Error{ErrorCode::invalidArgument, "project time signature cannot be represented in MIDI"};
    }
    if (project.tracks.size() + 1 > kMaxSmfTracks) {
        return Error{ErrorCode::invalidArgument, "project has too many tracks for Standard MIDI"};
    }
    for (const auto& track : project.tracks) {
        for (const auto& clip : track.clips) {
            for (const auto& note : clip.notes) {
                if (!note.isValid() || note.start < 0 || note.end() > kMaxMidiTick) {
                    return Error{ErrorCode::invalidArgument,
                        "project contains a note outside the Standard MIDI tick range"};
                }
            }
        }
    }

    std::vector<std::uint8_t> out;

    // Header chunk: format 1, ntrks, division.
    out.push_back('M');
    out.push_back('T');
    out.push_back('h');
    out.push_back('d');
    putU32(out, 6);
    putU16(out, 1);  // format 1: one or more simultaneous tracks
    putU16(out, static_cast<std::uint16_t>(project.tracks.size() + 1));
    putU16(out, kDivision);

    const auto conductor = buildConductorChunk(project);
    out.insert(out.end(), conductor.begin(), conductor.end());
    for (std::size_t index = 0; index < project.tracks.size(); ++index) {
        const auto chunk = buildTrackChunk(project.tracks[index], index);
        out.insert(out.end(), chunk.begin(), chunk.end());
    }
    return out;
}

application::Result<domain::Project> MidiFile::importBytes(
    const std::vector<std::uint8_t>& bytes) {
    Reader reader{bytes.data(), bytes.size()};

    std::string magic;
    if (!reader.readBytes(4, magic) || magic != "MThd") {
        return Error{ErrorCode::invalidArgument, "not a Standard MIDI File (missing MThd header)"};
    }
    std::uint32_t headerLength = 0;
    std::uint16_t format = 0;
    std::uint16_t trackCount = 0;
    std::uint16_t division = 0;
    if (!reader.readU32(headerLength) || headerLength < 6) {
        return Error{ErrorCode::invalidArgument, "malformed MIDI header length"};
    }
    if (!reader.readU16(format) || !reader.readU16(trackCount) || !reader.readU16(division)) {
        return Error{ErrorCode::invalidArgument, "truncated MIDI header"};
    }
    if (format > 1 || trackCount == 0 || (format == 0 && trackCount != 1)) {
        return Error{ErrorCode::invalidArgument, "only Standard MIDI format 0 and 1 are supported"};
    }
    if ((division & 0x8000) != 0 || division == 0) {
        return Error{ErrorCode::invalidArgument, "SMPTE/zero MIDI division is not supported"};
    }
    if (trackCount > kMaxTracks) {
        return Error{ErrorCode::invalidArgument, "MIDI file declares too many tracks"};
    }
    // Skip any extra header bytes beyond the standard 6.
    if (headerLength > 6 && !reader.skip(headerLength - 6)) {
        return Error{ErrorCode::invalidArgument, "truncated MIDI header padding"};
    }

    domain::Project project;
    project.id = domain::EntityId{"midi-import"};
    project.name = "Imported MIDI";
    double importedBpm = 120.0;
    bool tempoSeen = false;
    bool timeSignatureSeen = false;

    std::uint32_t idCounter = 0;
    const auto nextId = [&idCounter]() {
        return domain::EntityId{"midi-" + std::to_string(++idCounter)};
    };

    // Tick-to-project rescale: our project PPQ is fixed at kPpq, the file may use another division.
    const auto rescale = [division](const domain::Tick fileTick) -> domain::Tick {
        if (division == kDivision) {
            return fileTick;
        }
        return static_cast<domain::Tick>(
            (static_cast<std::int64_t>(fileTick) * domain::kPpq) / division);
    };

    for (std::uint16_t t = 0; t < trackCount; ++t) {
        std::string chunkId;
        if (!reader.readBytes(4, chunkId)) {
            return Error{ErrorCode::invalidArgument, "truncated MIDI track header"};
        }
        std::uint32_t chunkLength = 0;
        if (!reader.readU32(chunkLength)) {
            return Error{ErrorCode::invalidArgument, "truncated MIDI track length"};
        }
        if (static_cast<std::size_t>(chunkLength) > bytes.size() - reader.position()) {
            return Error{ErrorCode::invalidArgument, "MIDI track length exceeds file"};
        }
        if (chunkId != "MTrk") {
            // Unknown chunk: skip it per the SMF spec.
            if (!reader.skip(chunkLength)) {
                return Error{ErrorCode::invalidArgument, "truncated unknown MIDI chunk"};
            }
            continue;
        }
        Reader trackReader{bytes.data() + reader.position(), chunkLength};
        if (!reader.skip(chunkLength)) {
            return Error{ErrorCode::invalidArgument, "truncated MIDI track"};
        }

        domain::InstrumentTrack track;
        track.id = nextId();
        track.name = "MIDI Track " + std::to_string(t + 1);
        std::optional<std::uint8_t> program;
        bool drumChannel = false;

        domain::MidiClip clip;
        clip.id = nextId();
        clip.name = "Imported";

        // 128 pitches x 16 channels of pending note-ons. Index = channel*128 + pitch.
        std::array<std::deque<PendingNote>, 16 * 128> pending{};

        domain::Tick absoluteTick = 0;
        std::uint8_t runningStatus = 0;
        std::size_t eventCount = 0;
        domain::Tick maxEnd = 0;

        const auto finishNote = [&](const std::size_t slot, const domain::Pitch pitch,
                                    const domain::Tick endTick) {
            if (pending[slot].empty()) {
                return;
            }
            const PendingNote started = pending[slot].front();
            pending[slot].pop_front();
            const auto start = rescale(started.start);
            const auto end = rescale(endTick);
            if (end <= start) {
                return;
            }
            domain::NoteEvent note;
            note.id = nextId();
            note.start = start;
            note.duration = end - start;
            note.pitch = pitch;
            note.velocity = static_cast<domain::Velocity>(
                std::max<std::uint8_t>(started.velocity, 1));
            clip.notes.push_back(note);
            maxEnd = std::max(maxEnd, end);
        };

        while (trackReader.position() < chunkLength) {
            if (++eventCount > kMaxEventsPerTrack) {
                return Error{ErrorCode::invalidArgument, "MIDI track has too many events"};
            }
            std::uint32_t delta = 0;
            if (!trackReader.readVarLen(delta)) {
                return Error{ErrorCode::invalidArgument, "malformed MIDI delta time"};
            }
            absoluteTick += static_cast<domain::Tick>(delta);

            std::uint8_t status = 0;
            if (!trackReader.readU8(status)) {
                return Error{ErrorCode::invalidArgument, "truncated MIDI event"};
            }

            if (status == 0xFF) {
                std::uint8_t metaType = 0;
                std::uint32_t metaLength = 0;
                if (!trackReader.readU8(metaType) || !trackReader.readVarLen(metaLength)) {
                    return Error{ErrorCode::invalidArgument, "malformed MIDI meta event"};
                }
                if (metaType == 0x03) {  // track name
                    std::string name;
                    if (metaLength > 4096 || !trackReader.readBytes(metaLength, name)) {
                        return Error{ErrorCode::invalidArgument, "truncated MIDI track name"};
                    }
                    if (!name.empty()) {
                        track.name = name;
                    }
                } else if (metaType == 0x51 && metaLength == 3) {
                    std::uint8_t high = 0;
                    std::uint8_t middle = 0;
                    std::uint8_t low = 0;
                    if (!trackReader.readU8(high) || !trackReader.readU8(middle)
                        || !trackReader.readU8(low)) {
                        return Error{ErrorCode::invalidArgument, "truncated MIDI tempo event"};
                    }
                    const auto microseconds = (static_cast<std::uint32_t>(high) << 16)
                        | (static_cast<std::uint32_t>(middle) << 8)
                        | static_cast<std::uint32_t>(low);
                    if (!tempoSeen && microseconds > 0) {
                        importedBpm = 60'000'000.0 / static_cast<double>(microseconds);
                        tempoSeen = true;
                    }
                } else if (metaType == 0x58 && metaLength == 4) {
                    std::uint8_t numerator = 0;
                    std::uint8_t denominatorPower = 0;
                    std::uint8_t clocks = 0;
                    std::uint8_t thirtySeconds = 0;
                    if (!trackReader.readU8(numerator)
                        || !trackReader.readU8(denominatorPower)
                        || !trackReader.readU8(clocks)
                        || !trackReader.readU8(thirtySeconds)) {
                        return Error{ErrorCode::invalidArgument, "truncated MIDI time signature"};
                    }
                    static_cast<void>(clocks);
                    static_cast<void>(thirtySeconds);
                    if (!timeSignatureSeen && numerator > 0 && denominatorPower <= 7) {
                        project.timeSignatureNumerator = numerator;
                        project.timeSignatureDenominator = 1 << denominatorPower;
                        timeSignatureSeen = true;
                    }
                } else if (!trackReader.skip(metaLength)) {
                    return Error{ErrorCode::invalidArgument, "truncated MIDI meta data"};
                }
                runningStatus = 0;
                continue;
            }

            if (status == 0xF0 || status == 0xF7) {
                // SysEx: varlen length + data, skipped.
                std::uint32_t sysexLength = 0;
                if (!trackReader.readVarLen(sysexLength) || !trackReader.skip(sysexLength)) {
                    return Error{ErrorCode::invalidArgument, "truncated MIDI sysex"};
                }
                runningStatus = 0;
                continue;
            }

            // Channel-voice event, possibly using running status (status byte omitted).
            std::uint8_t data1 = 0;
            if ((status & 0x80) != 0) {
                runningStatus = status;
                if (!trackReader.readU8(data1)) {
                    return Error{ErrorCode::invalidArgument, "truncated MIDI channel event"};
                }
            } else {
                if (runningStatus == 0) {
                    return Error{ErrorCode::invalidArgument, "MIDI running status without status"};
                }
                data1 = status;
                status = runningStatus;
            }

            const std::uint8_t type = status & 0xF0;
            const std::uint8_t channel = status & 0x0F;
            drumChannel = drumChannel || channel == 9;
            const auto slot = static_cast<std::size_t>(channel) * 128
                + static_cast<std::size_t>(data1 & 0x7F);

            switch (type) {
                case 0x80: {  // note-off (1 more data byte)
                    std::uint8_t velocity = 0;
                    if (!trackReader.readU8(velocity)) {
                        return Error{ErrorCode::invalidArgument, "truncated note-off"};
                    }
                    static_cast<void>(velocity);
                    finishNote(slot, static_cast<domain::Pitch>(data1 & 0x7F), absoluteTick);
                    break;
                }
                case 0x90: {  // note-on (velocity 0 == note-off)
                    std::uint8_t velocity = 0;
                    if (!trackReader.readU8(velocity)) {
                        return Error{ErrorCode::invalidArgument, "truncated note-on"};
                    }
                    if (velocity == 0) {
                        finishNote(slot, static_cast<domain::Pitch>(data1 & 0x7F), absoluteTick);
                    } else {
                        pending[slot].push_back(PendingNote{absoluteTick, velocity});
                    }
                    break;
                }
                case 0xA0:  // poly aftertouch (2 data bytes)
                case 0xB0:  // control change
                case 0xE0:  // pitch bend
                    if (!trackReader.skip(1)) {
                        return Error{ErrorCode::invalidArgument, "truncated 2-byte channel event"};
                    }
                    break;
                case 0xC0:  // program change (1 data byte, already read as data1)
                    if (!program.has_value()) {
                        program = static_cast<std::uint8_t>(data1 & 0x7F);
                    }
                    break;
                case 0xD0:  // channel aftertouch (1 data byte)
                    break;
                default:
                    return Error{ErrorCode::invalidArgument, "unrecognized MIDI status byte"};
            }
        }

        if (!clip.notes.empty()) {
            // Canonical note order (start, pitch, id) to match the command-layer invariant.
            std::sort(clip.notes.begin(), clip.notes.end(),
                [](const domain::NoteEvent& a, const domain::NoteEvent& b) {
                    if (a.start != b.start) {
                        return a.start < b.start;
                    }
                    if (a.pitch != b.pitch) {
                        return a.pitch < b.pitch;
                    }
                    return a.id.value < b.id.value;
                });
            clip.range = domain::HalfOpenTickRange{0, maxEnd};
            track.instrumentId = std::string(instrumentForMidi(program, drumChannel));
            track.clips.push_back(std::move(clip));
            project.tracks.push_back(std::move(track));
        }
    }

    project.tempoMap = domain::TempoMap{importedBpm, 48000.0};
    return project;
}

}  // namespace composer::persistence
