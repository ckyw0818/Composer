# Composer TODOs

Deferred work from `/autoplan`. These items are intentionally outside the first public Windows release.

## P2: VST3 Instrument And Effect Hosting

**What:** Load, scan, route, persist, and recover third-party VST3 instruments and effects.

**Why:** Users eventually need their existing sound libraries, but arbitrary plugins introduce compatibility, trust, latency, scanning, and crash-isolation concerns.

**Pros:** Broadens the sound ecosystem and makes Composer viable in established workflows.

**Cons:** Adds a large support matrix and lets third-party code destabilize the host.

**Context:** The v1 processor graph must use stable processor and parameter IDs without exposing plugin hosting. Start after built-in instruments, project migrations, and crash recovery pass their release gates.

**Effort:** Human XL / Codex L. **Depends on:** stable processor graph, plugin trust policy, out-of-process scan/crash strategy.

## P2: Pitch Correction And Take Comping

**What:** Add non-destructive pitch editing and multi-take lane comping for vocal and instrument recordings.

**Why:** These are expected production tools after basic recording is trustworthy.

**Pros:** Makes vocal production substantially more complete.

**Cons:** Time/pitch DSP and take-state UX can dominate the product before reliable single-take recording is proven.

**Context:** v1 supports record, trim, split, fades, gain and crossfades. Begin only after device-loss, latency compensation, disk-full and recovery tests are stable.

**Effort:** Human XL / Codex L. **Depends on:** reliable recording engine and versioned audio-edit schema.

## P2: Automation Lanes

**What:** Add sample-accurate automation for mixer and effect parameters.

**Why:** Advanced arrangements need parameter changes over time.

**Pros:** Enables expressive mixes and prepares for third-party effects.

**Cons:** Creates data-volume, UI-density, scheduling, undo and project-migration complexity.

**Context:** Keep stable parameter IDs in v1 so automation can attach later without changing the track model.

**Effort:** Human L / Codex M. **Depends on:** stable processor graph and parameter registry.

## P3: Time Stretching

**What:** Add tempo-aware, non-destructive audio time stretching.

**Why:** Recorded audio eventually needs to follow tempo edits.

**Pros:** Improves arrangement flexibility.

**Cons:** Quality, latency and licensing vary by algorithm; poor stretching damages trust quickly.

**Context:** v1 records `stretch-disabled` explicitly in `AudioClip` and preserves raw audio.

**Effort:** Human L / Codex M. **Depends on:** audio clip schema and DSP evaluation fixtures.

## P3: macOS And Linux Distribution

**What:** Add signed/notarized macOS and packaged Linux builds with platform device testing.

**Why:** JUCE is cross-platform, but reliable audio devices and installers require platform-specific verification.

**Pros:** Expands reach and validates architecture portability.

**Cons:** Triples device, packaging and support matrices before Windows reliability is proven.

**Context:** Avoid Windows-only assumptions in domain and persistence modules, while keeping v1 testing focused on Windows.

**Effort:** Human XL / Codex L. **Depends on:** Windows v1 stability and portable CI agents.
