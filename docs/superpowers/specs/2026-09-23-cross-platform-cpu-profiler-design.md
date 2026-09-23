# CPU profiling for native platforms

**Status:** Design approved by Helena on 2026-09-23. Implementation is planned; no profiler implementation has started.

**Goal:** Report native frame CPU time in milliseconds by Core phase, updateable component, and platform render phase. Integrate PS1 first; let N64 and later hosts use the same Core probes.

## Context and scope

The PS1 host already logs 600-frame averages for Update, frame boundary, Draw, and between-frame time. On unchanged `cube_test` with authored `-O3`, Update averages 2.727 ms and Draw 7.676 ms. Temporary generated-C++ probes identified `ObjectManager.Update` and the 2D visit/flush as large costs, but those probes must be recreated after code generation. N64 already has a native microsecond clock (`get_ticks_us`) and a generated-Core frame loop, yet no matching detailed report.

The first deliverable is an opt-in PS1 CPU profiler that prints one log report per 600-frame sample window. It covers Core Update stages, each `IUpdateable.Update` call, Core Draw, and PS1 camera/3D preparation, 2D queue visit, packet flush, and submission. Host frame-boundary and between-frame times appear separately. The N64 platform owner may adopt the shared contract later; this work neither edits its repository nor moves its engine revision. The common contract and log schema must support that adoption.

GPU raster time, N64 RSP/RDP busy time, call-stack sampling, in-game overlays, and arbitrary C# method tracing are outside this milestone. CPU Draw measures submission work, not GPU execution.

## Approach and Core contract

Add an optional probe contract to the shared `helengine` Core. Host-only timers cannot measure individual updateables; patching generated C++ would be brittle. Existing `RuntimeProfilerMetrics` counts and FPS overlay remain separate from these timing samples.

`CoreInitializationOptions` accepts a nullable CPU profiling sink. The sink supplies a monotonic microsecond timestamp, accepts durations for fixed numeric Core stage IDs, accepts each updateable's duration and identity, and receives a type-name legend on first encounter. The interface uses codegen-supported scalar and string types and does not require RTTI in a native host. `Core.AdvanceUpdate`, `Core.Draw`, and `ObjectManager.Update` probe around existing calls only when the sink is present. With no sink, the Core makes no clock, formatting, or allocation calls; measure the remaining branch cost before accepting the change. The host chooses profiling at startup rather than changing it during a scene.

The Core stage set includes early input, frame counters, object-manager update, audio, physics, late input, pointer interaction, frame-boundary work, and Core Draw/render-manager work. An updateable sample carries object identity, ordered index, the existing stable type-name hash, owner scene-entity ID, and elapsed microseconds. The PS1 sink accumulates by object identity and uses the first-encounter legend to print a readable type name. Hash and owner ID remain in the log for lookup. Type-name conversion and text formatting never run per sample. If updateable membership or order changes during a window, the collector discards the mixed window and emits a reset marker.

The PS1 host and renderer add native-only stages to the same collector. The existing host timing values used by the FPS overlay remain intact. The profiler does not write into overlay text because doing so would alter UI cost. PS1 uses its HSync-derived monotonic clock, which advances in 64-microsecond steps; N64 can later use `get_ticks_us`. Reports include clock source and quantum. Hundreds of samples give useful PS1 averages despite single-sample quantization.

## Sampling and output

Skip the first 600 frames for startup and cache warmup; then collect 600 complete frames. Accumulate integer microseconds in bounded storage. Print only after the last frame's CPU timestamps have been captured, and exclude the next frame so log I/O cannot enter the next sample window. Include frame count and range, scene ID, platform, profiling build identity, clock, and dropped/reset counts. Record VBlank cadence alongside CPU time without folding waits into Update or Draw.

Use stable key/value records. For example (values below illustrate formatting, not measured results):

```text
HE_CPU_WINDOW platform=ps1 scene=cube_test frames=600 first=601 last=1200 clock=hsync64us update_ms=2.727 draw_ms=7.676 boundary_ms=0.131 between_ms=6.352
HE_CPU_STAGE parent=update name=object_manager avg_ms=2.600 calls=600
HE_CPU_COMPONENT index=3 type=DemoDiscOrbitCameraComponent hash=12345678 owner=8 avg_ms=0.700 calls=600
HE_CPU_STAGE parent=draw name=ui_queue_visit avg_ms=3.900 calls=600
HE_CPU_WINDOW_END dropped=0 resets=0
```

PS1 formats integer microseconds as `whole.mmm` milliseconds without float `printf`. Stage values include their clock/probe overhead. `ObjectManager` and its component rows are parent/child measurements and are never summed as siblings. Label unaccounted time instead of forcing rounded substages to equal the parent. Omit zero-call rows and keep multiple instances of one type separate. Fixed component/stage capacities bound PS1 RAM; overflow increments `dropped` and appears in the report rather than merging records silently.

## Compatibility and failure behavior

Default PS1 Release does not instantiate the sink and retains current UI and FPS timings. An explicit PS1 CPU-profile build option enables it with the same authored scene, generated Core revision, and Release optimization flags as its comparison build. The build manifest records profiling on/off. Startup and scene transitions reset the collector. A backward or unavailable clock sample suppresses that window with a clear marker rather than producing a huge unsigned duration. Timed frames have no profiler heap allocations or log writes.

The Core API must compile in managed tests and generated C++ under the PS1 runtime profile. Work in the shared engine checkout must not reset it or implicitly move another platform pin. N64 integration is a separate platform change using this contract and schema.

## Validation and acceptance

1. Managed Core tests with a deterministic fake clock verify stage boundaries, updateable identity, call counts, disabled behavior, and reset behavior. They assert observations, not source substrings.
2. PS1 collector tests with synthetic samples verify averaging, `whole.mmm` formatting, distinct instances, clock rollback, overflow, and scene-change reset. The PS1 host suite and authored native build pass.
3. The profile build boots the authored scene in DuckStation and logs a complete 600-frame window with named updateables and PS1 draw substages. The user verifies UI and motion visually.
4. Compare profiling on/off with the same scene, Core revision, and emulator settings. Record Update, Draw, VBlank cadence, static RAM, heap capacity, observer overhead, and both build identities in `docs/Validation.md`. Compare steady windows, not startup or frames adjacent to printing.
5. The report distinguishes Core Update, component time, renderer CPU work, boundary work, and waits. It never claims PS1 GPU raster or N64 RSP/RDP timing.

A materially slower unprofiled default build fails acceptance even if the opt-in report works. If sampling overhead makes component readings misleading, revise the capture path before calling the profiler ready.