# Releasing

S0 is a private prototype and cannot be distributed. Its release-equivalent gate is:

```powershell
.\scripts\dev.ps1 configure -Preset windows-release
.\scripts\dev.ps1 build -Preset windows-release
.\scripts\dev.ps1 test -Suite release
```

S4 must additionally verify current JUCE and asset rights, build and sign the installer, run
clean-VM install/upgrade/uninstall, perform the rollback drill, and record binary checksums. Never
remove the distribution block in `THIRD_PARTY_LICENSES.md` without recorded license evidence.
