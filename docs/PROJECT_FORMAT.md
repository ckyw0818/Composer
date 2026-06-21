# Project Format Contract

Project persistence starts in S1. The frozen bundle shape is `project.composer/` containing a
versioned `project.json`, immutable `Audio/` sources, disposable `Peaks/`, rotating `Autosave/`, and
recording `Recovery/` journals. Migrations preserve the original and newer major schemas open
read-only. S0 creates no user project files.

## `project.json` schema v1 (S1)

S1 implements the MIDI-composition subset of the manifest. The reader and writer live in
`src/persistence/ProjectSerializer.{h,cpp}` and are governed by the determinism rules below.

```jsonc
{
  "schemaVersion": 1,
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
  ]
}
```

### Time and identity

- Musical positions (`start`, `duration`, clip `start`/`end`) are signed 64-bit ticks at 960 PPQ.
- Every entity carries a stable string UUID. Array position is never identity; reordering preserves
  IDs. The deterministic fixture uses `SequentialIdSource` (`verse-NNNNNNNN`); production uses random
  UUIDs.
- `bpm` and `sampleRate` define the project `TempoMap`, the sole ticks <-> samples authority.

### Determinism guarantees

The serializer is the backbone of the S1 exit gate ("8-bar fixture save/reload/render"). It
guarantees:

- `parse(serialize(p)) == p` (model round-trip equality), and
- `serialize(parse(serialize(p))) == serialize(p)` (byte-stable text),

so a saved-and-reopened project compiles to a bit-identical audio render. Doubles use
`std::to_chars` shortest round-trip formatting; the schema field order is fixed.

### Forward and backward compatibility

- Unknown object fields are ignored on read, so newer minor revisions remain loadable.
- A manifest whose `schemaVersion` is greater than the app's `Project::kSchemaVersion` is rejected
  with `DEPENDENCY-001` ("open read-only"), never silently truncated. Migrations (`vN -> vN+1`)
  arrive with later slices and always preserve the original.
