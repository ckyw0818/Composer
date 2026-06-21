# Composer Architecture

## Dependency Direction

```text
composer_app -> audio_runtime -> audio_contracts -> domain
tests -------------------------^                    ^
application ---------------------------------------+
```

Future modules retain the complete DAG in `DESIGN.md`: UI dispatches typed application commands;
persistence and instruments depend inward; diagnostics consumes typed boundary events. The domain
has no JUCE UI, device, or filesystem dependency.

## Time And Identity

- Musical time is signed 64-bit ticks at 960 PPQ.
- Runtime audio time is signed 64-bit project samples.
- Ranges are half-open `[start, end)`.
- Stable entity IDs, never array positions, become identity in S1.

`unit.contracts` enforces these S0 definitions.

## Realtime Contract

The callback consumes only an immutable `RuntimeSnapshot`. It may not access UI state, a mutable
project model, files, logs, allocators, mutexes, or blocking APIs. The S0 callback clears and fills
caller-owned buffers and changes no graph structure.

The release gate combines:

- a global allocation probe while `RealtimeSafety::Scope` is active;
- a source contract scan for allocation, lock, and file APIs;
- 3,000 fake callbacks at 48 kHz / 256 samples;
- a measured p99 budget below 3.2 ms.

CTest is the orchestrator. Supported MSVC builds additionally register the S0 contracts through
JUCE `UnitTest`; the compiler-independent harness keeps the realtime checks runnable without
physical hardware.

Later slices publish immutable snapshots through an SPSC queue at buffer boundaries. The message
thread remains the sole project-model writer; workers carry revision tokens and stale work is
discarded.

## Ownership

- Message thread: project model, commands, undo, and UI selection.
- Worker threads: snapshot compilation, persistence, decode, peaks, and render.
- Audio thread: immutable snapshot render and bounded lock-free event/FIFO publication only.

Persistence, recording, and UI modules are attachment points only in S0; their behavior starts in
the execution slice assigned by `DESIGN.md`.
