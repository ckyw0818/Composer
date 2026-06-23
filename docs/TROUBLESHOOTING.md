# Troubleshooting

Every developer-facing failure has a stable ID, cause, exact recovery step, and this anchor.

### DX-COMPILER-001

No C++20 compiler is visible. Open a Visual Studio 2022 Developer PowerShell with the Desktop C++
workload, or add a supported MinGW compiler to PATH, then run `./scripts/dev.ps1 doctor`.

### DX-CMAKE-002

CMake is absent or older than 3.22, the minimum required by JUCE 8.0.13. Install or upgrade CMake,
reopen the terminal, and run `./scripts/dev.ps1 doctor`.

### DX-NINJA-003

Ninja is absent. Install Ninja 1.11 or newer, reopen the terminal, and run
`./scripts/dev.ps1 doctor`.

### DX-JUCE-004

The configured offline JUCE archive path is missing. Remove the stale
`COMPOSER_JUCE_ARCHIVE` cache value or provide the official 8.0.13 archive, then configure again.

### DX-FIXTURE-005

The fixture name is invalid or its runner has not been built. Run `./scripts/dev.ps1 bootstrap`,
then `./scripts/dev.ps1 run --fixture canonical-verse`.

### DX-BUILD-006

A configure, build, or test stage failed. Read the named file under `build/diagnostics/`, fix the
first underlying error, and rerun the same `dev.ps1` command.

### DX-SCOPE-007

Packaging is intentionally unavailable in S0. Run `./scripts/dev.ps1 test -Suite release` for the
current gate. Installer/signing work begins in S4 after license verification.

## Silent Output

Use `composer_fixture` to separate renderer health from a physical device problem. If the fixture
prints a nonzero peak and the app is silent, check the Windows output device, exclusive-mode owner,
sample rate, buffer size, and system mixer. The S0 app requests the JUCE default device and falls
back to no output without affecting fake-device tests.
