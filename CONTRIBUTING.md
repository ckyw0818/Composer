# Contributing

Run `./scripts/dev.ps1 doctor` first, then reproduce changes against `canonical-verse`. Keep
dependencies pointed inward as documented in `ARCHITECTURE.md`; never mutate project state from UI
handlers or add allocation, locks, logging, or I/O to callback code.

Before submitting a change:

```powershell
.\scripts\dev.ps1 test -Suite quick
.\scripts\dev.ps1 configure -Preset windows-release
.\scripts\dev.ps1 build -Preset windows-release
.\scripts\dev.ps1 test -Suite release
```

Report the exact command, stable error ID, compiler, device mode, and fixture. Do not attach private
projects or audio unless they were created specifically as public test data.
