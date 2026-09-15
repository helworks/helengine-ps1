# Task 3 Report: PS1 v0.1 timed rotation and diagnostics

## Scope

Task 3 implements elapsed-time cube rotation, bounded packet and guard diagnostics, GPU completion telemetry, and controlled failure builds for the PS1 v0.1 renderer. The implementation was checkpointed in commit `d459d1b` (`Implement PS1 timed rotation and bounded diagnostics`) from baseline `8b80553fdac5ba0c1d3ac24ee6903e0675c8ae1f`. Delivery-owned documentation and launcher changes were left unstaged.

## Implementation

- `Ps1RotationClock` accumulates independent X and Y phase remainders using raw 2,048-unit turns, 12,000,000 us and 8,000,000 us periods, a 100,000 us elapsed-time cap, and unsigned timestamp-wrap subtraction.
- `Ps1CubeScene` resets the clock from the PSYQo GPU timestamp and advances it on each Rotate frame before passing both phases to `QueueCube`.
- `Ps1FrameContext` accepts a per-build capacity, keeps packet counts and high-water bytes, and places guard words before and after the persistent triangle storage. Begin, submit, and the capacity diagnostic verify those guards.
- `Ps1RasterRenderer` retains lifetime packet and byte peaks across per-frame `ResetStats`, reports capacity, ownership, guard, projection, depth, and submission failures, and bounds CompletionStress to 64 triangles in the existing packet pool.
- The SDK patch records transfer, marker, and presented events. The marker remains default-on; `PS1_GPU_OMIT_MARKER` is a test-controlled negative-build define. `getPresentedSequence()` is the VBlank frame-count domain, while transfer and marker getters are event-count domains; the report and runtime log keep these domains distinct. The presented value is assigned after display-area/parity swap setup.
- `scripts/build.ps1`, `Makefile`, and the recursive `psyqo.mk` patch propagate optional SDK defines into the SDK compile and capture Docker stdout/stderr independently so compiler warnings do not turn a successful build into a PowerShell failure.

## Deterministic host verification

`powershell.exe -NoProfile -File scripts/test.ps1` completed with exit code 0. The latest summary is:

`C:\dev\helworks\builds\helengine-ps1\host-tests-20260915-114217-922-d87fa9d092ff4960a3c65d640d4cb1dd\summary.json`

The clock tests cover equal elapsed time at 10 ms and 20 ms steps, the 100 ms debugger cap, zero delta, and timestamp wrap. Existing packet, depth, ownership, and render-policy tests also pass.

## Target build artifacts

The corrected Release Rotate wrapper completed with exit code 0 and passed the retail memory checker. Root launched this artifact for runtime validation:

- Rotate: `C:\dev\helworks\builds\helengine-ps1\20260915-115143-663-Release-6bcd00766209404cb8032153ecdbe683\manifest.json`
- Capacity: `C:\dev\helworks\builds\helengine-ps1\20260915-115449-930-Release-c311f3421f80473ea15de0413b553fe6\manifest.json`
- InvalidPose (TRZ=200): `C:\dev\helworks\builds\helengine-ps1\20260915-115648-370-Release-7afd515fcedf477580ef0ca4fb551a11\manifest.json`
- CompletionStress (64 bounded triangles): `C:\dev\helworks\builds\helengine-ps1\20260915-115936-140-Release-2b4e8f3dc3f24e21bc00eac3a080d9a5\manifest.json`
- Omitted-marker negative build (`PS1_GPU_OMIT_MARKER`): `C:\dev\helworks\builds\helengine-ps1\20260915-120151-460-Release-0338b2be099140d5b84927d2259f52ee\manifest.json`

The manifests record the pinned Docker image, SDK patch hash, copied input hashes, ELF/PS-X EXE/map hashes, and retail memory results. They were produced before the checkpoint commit, so their source revision is the baseline with `dirty: true`; the copied input hashes capture the Task 3 source used for each build.

The omitted-marker build log contains both the recursive `make -C /work/third_party/nugget/psyqo ... DEFINES="PS1_DEMO_MODE_COMPLETIONSTRESS PS1_GPU_OMIT_MARKER"` invocation and SDK `-DPS1_GPU_OMIT_MARKER` compile commands, confirming that the diagnostic define reaches PSYQo.

## Runtime status and pending evidence

Runtime validation remains root-owned and is intentionally not claimed complete here. Root reports that the Rotate artifact is running under the portable DuckStation/GDB harness: phases change, both packet contexts carry moving triangles, guards remain intact, failures remain zero, and the observed lifetime peak is 7 packets / 136 bytes. The ten-minute soak is still in progress and must record debugger pauses/resumes, five resets, frame count, heap high-water, stack guard, and allocator-breakpoint observations.

Remaining Task 3 evidence is the root-owned CompletionStress ordering observation (transfer -> marker -> fresh VBlank -> presentation), the intentional omitted-marker timeout behavior, and completion of the soak. Capacity, InvalidPose, CompletionStress, and the omitted-marker artifacts are built; their runtime fault logs are pending the coordinated harness runs.
