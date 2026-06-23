# Composer

Composer is a track-first Windows DAW. The current implementation is **S3 Audio Recording**:
dynamic MIDI instrument and audio tracks, chord and piano-roll editing, deterministic rhythm
preview/apply, five built-in synth voices, mixer basics, Standard MIDI import/export, count-in
recording, input monitoring, latency compensation, waveform edits, recovery journals, and
physical-device-free render/recording gates. Release packaging remains in a later slice in
[DESIGN.md](DESIGN.md).

## Quick Start

Prerequisites: Windows x64, Git, CMake 3.22+, Ninja 1.11+, and either the Visual Studio 2022 C++
workload or a C++20-capable MinGW toolchain. No account, sample download, microphone, or audio
interface is required.

```powershell
.\scripts\dev.ps1 bootstrap
.\scripts\dev.ps1 run --fixture canonical-verse
```

The wrapper prints every raw CMake command and writes local logs to `build/diagnostics/`. It never
installs system packages or uploads project, audio, source, or machine-path data.

Useful commands:

```powershell
.\scripts\dev.ps1 doctor
.\scripts\dev.ps1 test -Suite quick
.\scripts\dev.ps1 configure -Preset windows-release
.\scripts\dev.ps1 build -Preset windows-release
.\scripts\dev.ps1 test -Suite release
```

The normal `composer_app` target opens the JUCE workspace and plays the canonical arranged verse
through the selected Windows device. `composer_fixture` runs the same renderer through the fake
device.

Audio-recording controls:

- `Add audio` creates an audio track. Click the `R` badge in its track header to arm it.
- Select the audio track to show device setup, monitoring mode, latency compensation, and waveform
  editing controls.
- Press `Record` in the transport for a one-bar count-in, then press it again to stop and commit the
  take as an editable audio clip.
- Automated recording tests use `FakeRecordingDevice`, so CI does not need a microphone or audio
  interface.

Piano-roll selection and clipboard controls:

- Drag empty grid space to marquee-select notes; hold `Shift` to toggle notes in the rectangle.
- `Shift`-click toggles one note, and `Ctrl+A` selects every note in the current clip.
- Drag any selected note to move the whole selection. `Delete` removes it as one undoable edit.
- `Ctrl+C` copies selected notes. Click an empty grid location, then press `Ctrl+V` to paste there.

## Status

- External distribution is blocked until a JUCE license path is selected and recorded.
- No samples or third-party content are bundled; built-in instruments use deterministic synthesis.
- Deferred product work is tracked in `DESIGN.md`; `TODOS.md` is intentionally outside this scope.

See [ARCHITECTURE.md](ARCHITECTURE.md), [CONTRIBUTING.md](CONTRIBUTING.md), and
[troubleshooting](docs/TROUBLESHOOTING.md).
