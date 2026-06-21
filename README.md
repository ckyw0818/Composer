# Composer

Composer is a track-first Windows DAW. The current implementation is **S0 Foundation**: a JUCE
application shell, frozen module/thread contracts, a deterministic 8-bar fixture, and a physical
device-free audio release gate. MIDI composition, instruments, recording, and release packaging
arrive in the later slices defined by [DESIGN.md](DESIGN.md).

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

The normal `composer_app` target opens the JUCE app shell and sends the canonical click to the
selected Windows device. `composer_fixture` runs the same renderer through the fake device.

## Status

- External distribution is blocked until a JUCE license path is selected and recorded.
- No samples or third-party content are bundled in S0.
- Deferred product work is tracked in `DESIGN.md`; `TODOS.md` is intentionally outside this scope.

See [ARCHITECTURE.md](ARCHITECTURE.md), [CONTRIBUTING.md](CONTRIBUTING.md), and
[troubleshooting](docs/TROUBLESHOOTING.md).
