# Project Format Contract

Project persistence starts in S1. S3 advances the manifest to schema v2 while continuing to read
S1/S2 schema v1 manifests. The frozen bundle shape is
`project.composer/` containing a
versioned `project.json`, immutable `Audio/` sources, disposable `Peaks/`, rotating `Autosave/`, and
recording `Recovery/` journals. Migrations preserve the original and newer major schemas open
read-only.

## `project.json` schema v2 (S3)

S1 implemented the MIDI-composition subset of the manifest; S2 activated the stored mixer fields
and added Standard MIDI import/export. S3 adds audio tracks, immutable audio clip references,
input routing, monitoring, latency compensation and mixed track order. The reader and writer live in
`src/persistence/ProjectSerializer.{h,cpp}` and are governed by the determinism rules below.

```jsonc
{
  "schemaVersion": 2,
  "id": "verse-00000001",        // stable project UUID
  "name": "Canonical Verse",
  "bpm": 120,                     // TempoMap authority (constant tempo in S1)
  "sampleRate": 48000,
  "timeSignatureNumerator": 4,
  "timeSignatureDenominator": 4,
  "tracks": [
    {
      "id": "verse-00000002",
      "name": "Piano",
      "instrumentId": "builtin.piano",
      "volume": 1, "pan": 0, "muted": false, "soloed": false,
      "clips": [
        {
          "id": "verse-00000005",
          "name": "Verse chords",
          "start": 0, "end": 30720,         // half-open [start, end) tick range
          "notes": [
            { "id": "verse-00000008", "start": 0, "duration": 3840,
              "pitch": 60, "velocity": 96 }
          ]
        }
      ]
    }
  ],
  "audioTracks": [
    {
      "id": "take-track-0001",
      "name": "Voice",
      "volume": 1,
      "pan": 0,
      "muted": false,
      "soloed": false,
      "recordArmed": false,
      "inputDeviceId": "WASAPI microphone",
      "inputChannel": 0,
      "monitoring": "software",          // off | direct | software
      "latencyCompensationSamples": 240,
      "clips": [
        {
          "id": "take-clip-0001",
          "name": "Recorded take",
          "assetPath": "Audio/take-0001.wav",
          "startSample": 96000,
          "sourceOffsetFrames": 0,
          "lengthFrames": 1440000,
          "sourceSampleRate": 48000,
          "sourceChannels": 1,
          "gain": 1,
          "fadeInFrames": 2400,
          "fadeOutFrames": 2400,
          "stretchEnabled": false
        }
      ],
      "crossfades": [
        { "id": "xfade-0001", "leftClipId": "take-clip-0001",
          "rightClipId": "take-clip-0002", "lengthFrames": 1200 }
      ]
    }
  ],
  "trackOrder": ["verse-00000002", "take-track-0001"]
}
```

### Time and identity

- Musical positions (`start`, `duration`, clip `start`/`end`) are signed 64-bit ticks at 960 PPQ.
- Every entity carries a stable string UUID. Array position is never identity; reordering preserves
  IDs. The deterministic fixture uses `SequentialIdSource` (`verse-NNNNNNNN`); production uses random
  UUIDs.
- `bpm` and `sampleRate` define the project `TempoMap`, the sole ticks <-> samples authority.
- `instrumentId`, `volume`, `pan`, `muted`, and `soloed` select the S2 built-in voice and mixer
  state. Save/reopen preserves the exact deterministic render.
- Recorded audio is absolute-time in samples. `AudioClip` stores an immutable WAV `assetPath`, a
  timeline `startSample`, a source window (`sourceOffsetFrames`, `lengthFrames`), gain/fade edits,
  and `stretchEnabled: false` so future time-stretching can migrate explicitly.
- `AudioTrack.input` stores the selected device label/channel, monitoring mode and latency
  compensation. Armed state is persisted as UI state but recordings are always finalized into clips.

### Determinism guarantees

The serializer is the backbone of the S1 exit gate ("8-bar fixture save/reload/render"). It
guarantees:

- `parse(serialize(p)) == p` (model round-trip equality), and
- `serialize(parse(serialize(p))) == serialize(p)` (byte-stable text),

so a saved-and-reopened project compiles to a bit-identical audio render. Doubles use
`std::to_chars` shortest round-trip formatting; the schema field order is fixed.

### Forward and backward compatibility

- Unknown object fields are ignored on read, so newer minor revisions remain loadable.
- Schema v1 manifests without `audioTracks` or `trackOrder` parse as MIDI-only projects with empty
  S3 fields, preserving S0-S2 saved projects.
- A manifest whose `schemaVersion` is greater than the app's `Project::kSchemaVersion` is rejected
  with `DEPENDENCY-001` ("open read-only"), never silently truncated. Migrations (`vN -> vN+1`)
  arrive with later slices and always preserve the original.
