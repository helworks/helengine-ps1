# PS1 v0.1 validation and evidence

Validation separates portable build checks, target packet/debugger evidence,
emulator presentation markers, framebuffer appearance, and retail hardware.
A successful compile or a guest log does not prove pixels or hardware timing.
No user-window screenshots were captured.

## Controller configuration

The runtime reference is DuckStation `0.1-11894-gc66b2694d` using its isolated
portable directory, Software renderer, resolution scale 1, PGXP disabled,
retail 2 MiB RAM, and CPU overclock disabled. The supplied OpenBIOS image is
selected as the relative filename `openbios.bin` under the portable
`bios\` directory; its SHA-256 is
`713ea2aed58606282b3ff5e91d77f8c5892ea36f5adcd279d2c7dd26604a625d`.

The supplied PCSX-Redux `25363.20260913.2.x64` / changeset
`c2e2dec197d3eb8f3db2ee63b8037321dbe1085e` was retained for comparison. Its
GPU parser rejects GP0(1Fh), so its `PS1_GPU_COMPLETION_TIMEOUT` is an
emulator limitation, not evidence that the native completion fence is wrong.
DuckStation is used for the target observations below.

## Portable checks

Run the host smoke, renderer-policy, and Python memory suites with:

```powershell
.\scripts\test.ps1
```

The final Task 4 run uses fresh SmallDebug, Release Rotate, and Release
CompletionStress directories. Each manifest records the exact SDK/submodule
pins, artifact hashes, and the memory-check result. The final host-test
summary is `host-tests-20260915-123606-289-269ae42e9f46469fa6f62169dd30e460/summary.json`;
all five recorded exit codes are zero and its Python log reports 16 tests
passed. The updated checker also revalidated all three fresh final artifacts.
Native C++ success is recorded in the four `*.log` files referenced by that
summary, each with an explicit `*_EXIT_CODE=0` marker. The earlier
`host-tests-20260915-122053-619-dea9977349fa4b4a85c8a57d909daaeb/summary.json`
is retained as pre-review evidence only.

| Fresh artifact | PS-X EXE SHA-256 | ELF SHA-256 | Static footprint | Heap start | Heap capacity |
| --- | --- | --- | ---: | --- | ---: |
| `20260915-121306-792-SmallDebug-a1d9eee469c54e6188ffddbd9e3209b6` | `ae3bd53c8eec35f2a0df88fedb2b870d1fc7a7a5a46bdb19fe4e3e7e18bfee7b` | `f764ecaad8cea72732e25efec06edbd1ff669b79e3eda36327115f0361a79831` | 57,544 | `0x8001e0c8` | 1,908,536 |
| `20260915-121533-239-Release-1cd41c739a9f4bf6a32a7b21f0c63150` | `46a63dd1b4137a02db7c5d8bcbb8766e43dd116af177e8b9e3649d6f0690f5c5` | `3e56589cdba6efffddb885de2bee769e8d3b5679d30191c77d501249075c8a76` | 45,256 | `0x8001b0c8` | 1,920,824 |
| `20260915-121813-830-Release-0187763356e348c2ad427dddea433d29` | `1ca86fc1528a1cf4be62faa44d7ec61a3c996eb00161aff6242f0588da1fa4c4` | `6931ac67d6a63ec5b390fefdac39b3a62e4901d5cda611973b71dcfab692524b` | 41,160 | `0x8001a0c8` | 1,924,920 |

All three manifests record Nugget `625da805c4a9bfd56dd9b2b17409805c0591dd42`,
the three locked submodule revisions, and the digest-pinned Docker image.
The memory checker reserves the 64 KiB stack and accounts for the two packet
contexts once in static/BSS. Record the measured static allocation, heap
interval, stack reservation, packet-context accounting, and exact VRAM
rectangles in the table and manifest:

| Measurement | Required record |
| --- | --- |
| Retail RAM | `0x80010000..0x80200000`, with `0x801f0000` reserved for the 64 KiB stack. |
| Initial stack | `0x801fff00`; preserve the stack guard. |
| Static allocation | Actual checker-reported end and footprint for each fresh build. |
| Heap interval | Actual start/high-water/capacity; do not count packet contexts twice. |
| Packet contexts | Two persistent contexts and their checked budget, accounted once in static/BSS. |
| Display rectangles | `(0,0)-(320,240)` and `(0,256)-(320,496)`. |

The existing final Task 2 Static reference was static footprint `45,184` bytes,
heap start `0x8001b080`, and heap capacity `1,920,896` bytes. It is retained as
prior evidence only; fresh Task 4 manifests supersede it if the rotation and
fault diagnostics change the layout.

## Acceptance table

Paths in the evidence column are relative to
`C:\dev\helworks\builds\helengine-ps1`. The configuration column uses a
64-character PS-X EXE SHA-256 where available; shorter values are explicitly
run IDs that identify the directory whose manifest supplies the hash. Rows
marked pending must remain pending until actual Task 3 or Task 4 evidence is
appended; do not infer a pass from a plan or a cold boot.

| Acceptance row | Configuration / hash | Procedure | Observed result | Evidence | Status |
| --- | --- | --- | --- | --- | --- |
| Clean boot | Release Static / `49f8bdd4021e1beb648ed1f80e195d1676fb7293ba1dc5c8002b9b3b96a66684` | Launch the explicit manifest with the verified DuckStation portable setup. | Guest booted to the PSYQo application; packet diagnostics ran with zero projection/capacity/ownership failures. | `20260915-110639-258-Release-56798e0174d34c888b0da8eb1e8bc414/manifest.json`; `controller-duck-probe/static-final-110639.log` | Passed for guest boot; pixels pending |
| Clear-only | Release Clear / `b9a49f27010ab5c76fd067f3190a34e1f83b9be5945864df7a62a35c7a720a99` | Launch the explicit Clear manifest and inspect the guest marker after presentation. | `PS1_FIRST_FRAME_PRESENTED parity=0 frame=5`; sustained approximately 59.82 FPS in the recorded run. | `20260915-100536-280-Release-dcce99772898406ab4a3f47ebaef1700/manifest.json`; `controller-duck-probe/clear-first-presentation.log` | Passed for marker/pacing; pixels pending |
| Static poses | Release Static / `b45c168c43b32088d2a7291ec77e5e028b50720d6e9c425ba5317a438ca343bb` | Inspect identity, Y=512, and X=512 target packet chains. | Identity, Y=512, and X=512 each produced two triangles on the near, left, and bottom face respectively. | `20260915-105904-176-Release-e8395eae7de7438aa1883a73490530b5/manifest.json`; `controller-duck-probe/static-official-packets`; `controller-duck-probe/static-poses/pose-results.json` | Passed for packet evidence; pixels pending |
| Winding reversal | Release Static / same `b45c168c43b32088d2a7291ec77e5e028b50720d6e9c425ba5317a438ca343bb` | Temporarily reverse the first near triangle and restore the mesh bytes. | Near count fell from two to one, proving reversed-winding rejection. | `controller-duck-probe/static-poses/pose-results.json` | Passed for target packet evidence |
| Distinct-depth insertion order | Release DepthOrder / `0d1dd2aec6a35446698c2e06c91c9d5935479f6cf4b4194340e879fd75a69da0` | Run both far/near insertion orders at identical screen coordinates. | Both chains traversed far green before near red. | `20260915-110317-174-Release-41fa895b60c64ad3a7b291d4337ffb4d/manifest.json`; `controller-duck-probe/depth-official-packets/packet-chain.json` | Passed for target packet evidence |
| Rotation/time tests | Fresh SmallDebug and Release Rotate | Observe raw phase accumulation, 100 ms delta cap, and wrap fixture. | SmallDebug launcher smoke reaches first presented frame 8 with no abort. Final Release smoke reaches scene sequences 120, 240, 360, 480, and 600 with changing phases; failures remain zero. | `20260915-121306-792-SmallDebug-a1d9eee469c54e6188ffddbd9e3209b6/manifest.json`; `20260915-121533-239-Release-1cd41c739a9f4bf6a32a7b21f0c63150/manifest.json`; `controller-duck-probe/final-smalldebug.log`; `controller-duck-probe/final-release.log` | Passed for diagnostic/runtime evidence |
| Invalid pose | Release InvalidPose / run ID `7afd515fcedf477580ef0ca4fb551a11` | Launch the diagnostic pose with `TRZ=200`. | Reports `PS1_RENDER_PROJECTION_FAILED`, with invalid=1, submit=0, and last=0. | `20260915-115648-370-Release-7afd515fcedf477580ef0ca4fb551a11/manifest.json`; `controller-duck-probe/invalid-pose.log`; `controller-duck-probe/fault-results.json` | Passed |
| Capacity failure | Release Capacity / run ID `c311f3421f80473ea15de0413b553fe6` | Run the reduced-slot diagnostic. | Reports `PS1_RENDER_CAPACITY_FAILED` with capacitylimit=1, capacity=1, last=0, and intact guards. | `20260915-115449-930-Release-c311f3421f80473ea15de0413b553fe6/manifest.json`; `controller-duck-probe/capacity.log`; `controller-duck-probe/fault-results.json` | Passed |
| Completion timeout | Release omitted-marker / run ID `0338b2be099140d5b84927d2259f52ee` | Exercise the bounded completion wait and failure path. | Reaches `PS1_GPU_COMPLETION_TIMEOUT`. PCSX-Redux cannot decode GP0(1Fh), so it is not used for a completion pass. | `20260915-120151-460-Release-0338b2be099140d5b84927d2259f52ee/manifest.json`; `controller-duck-probe/omitted-marker.log`; `controller-duck-probe/fault-results.json` | Passed for failure path |
| Completion stress | Fresh Release CompletionStress | Run the bounded large-packet stress and inspect transfer/marker/presentation sequences. | 22-second run records six diagnostic rows and five completion records through scene 600; transfer/marker=599, presented VBlank=1211, guards=1, failures=0, and 64 triangles / 65 packets / 1,296 bytes. | `20260915-121813-830-Release-0187763356e348c2ad427dddea433d29/manifest.json`; `controller-duck-probe/final-completion-stress.log`; `controller-duck-probe/final-stress-summary.json` | Passed for runtime evidence |
| Ten-minute soak | Release Rotate / run ID `6bcd00766209404cb8032153ecdbe683` | Run the configured ten-minute allocator/stack-guard soak. | Passed 600.015 active seconds / 604.015 wall seconds with two pauses, no malloc/free breakpoint hits, unchanged heap maximum, intact stack guard, and zero failure counters. | `20260915-115143-663-Release-6bcd00766209404cb8032153ecdbe683/manifest.json`; `controller-duck-probe/rotate-soak/soak.json`; `controller-duck-probe/rotate-soak/log-summary.json` | Passed for emulator soak; pixels pending |
| Memory checker negative fixtures | Pinned host test suite | Run Python checker fixtures, including malformed ELF/header, overlap, and reserved-stack payload cases. | Sixteen Python tests passed; all five wrapper exit codes are zero and native C++ logs contain explicit success markers. The updated checker revalidated all three fresh final artifacts. | `host-tests-20260915-123606-289-269ae42e9f46469fa6f62169dd30e460/summary.json`; `host-tests-20260915-123606-289-269ae42e9f46469fa6f62169dd30e460/python-tests.log` | Passed |
| Release boot | Fresh Release Rotate / run ID `1cd41c739a9f4bf6a32a7b21f0c63150` | Build and launch the final Release artifact by manifest with verified portable firmware. | Firmware-hash-guarded 6-second DuckStation smoke reaches first frame 8 and scene sequences 120/240 with changing phases and failures=0. The longer 12-second smoke reaches 120/240/360/480/600 with guards=1 and peak 7 packets / 136 bytes. | `20260915-121533-239-Release-1cd41c739a9f4bf6a32a7b21f0c63150/manifest.json`; `controller-duck-probe/final-firmware-verified-release.log`; `controller-duck-probe/final-release.log` | Passed for guest diagnostics; pixels pending |
| Generated Core boot | Release Rotate CoreBoot / run ID `0cdf40dacd90411b8ad9b365ed9c1898` | Build with `-CoreBoot` against `core-boot-07/generated`, launch by manifest, read guest TTY. | `PS1_CORE_INITIALIZING`, `PS1_CORE_CONSTRUCTED`, `PS1_CORE_READY`, then `PS1_FIRST_FRAME_PRESENTED parity=0 frame=9` with no abort over 45 seconds. Host suite `host-tests-20260915-184830-250-234dca3d2dbf4f0187932a15f03395cf` adds the runtime clock test. | `20260915-184730-065-Release-0cdf40dacd90411b8ad9b365ed9c1898/duckstation-guest.log` | Passed |
| Authored cube_test load | Release Authored / run ID `2c0b0059cb8e465d9c09faac39188ab6` | Build through `scripts/build-authored.ps1`, launch by manifest, read guest TTY. | Embedded archive served four declared assets; scene, material and engine cube deserialized through Core; one camera, one drawable, one updateable; generated spin component rotated the cube at pi/2 rad/s over 43 seconds. | `20260915-213407-491-Release-500906b3366040e9afa26d79df51a3ce/duckstation-guest.log` | Passed |
| Authored cube_test render | Release Authored / run ID `162de7934ad4447cbb2081f6ed165f12` | Same build with GTE rendering; capture first-frame and 600/1200-update checkpoints. | Pose trz=640 h=208 from the authored camera; 2 or 4 triangles per frame, zero GTE flags, guards intact; 600 updates in 601 VBlanks between checkpoints; heap end constant. Host projection tests pass. Visual confirmation by Helena pending. | `20260916-010501-785-Release-162de7934ad4447cbb2081f6ed165f12/duckstation-guest.log` | Passed (visual pending) |
| Reset | Any verified artifact / pending | Perform five actual emulator reset operations or a documented console reset mechanism. | Cold boots are not reset observations. No five-reset evidence is claimed. | Pending | Pending |
| Retail hardware | Same hashed Release EXE / pending | Load through the available homebrew method and record console, region, loader, video, rotation, resets, and stress behavior. | No retail console was available for this task. | Pending | Pending |

The exact VRAM layout is the pinned PSYQo configuration: 320x240 progressive
NTSC, 15-bit RGB stored in 16-bit framebuffer storage, with display buffers at
Y=0 and Y=256. The corrected SDK ordering-table clear is included in the
official Static evidence; before the correction the target chain had no
reachable triangle packets.

## Authored cube-test subset checkpoint (2026-09-15)

The focused .NET integration suite passed 8/8 tests, including ambiguous and
nested target rejection and output alias protection. Fresh current-source
engine dependencies were used. Evidence under the external build root:

- `subset-root-verification-final/results/subset-fixed.trx`
- `cube-test-subset-final-verified/subset.helen`
- `cube-test-subset-final-verified/report.json`

The wrapper completed successfully using its exact freshly built DLL. All six
recorded loaded assembly hashes matched both their loaded and requested files.
The exported source snapshot SHA-256 is
`da429290f091802c8faec2ac34f21ed5c6da711aec0e0fa119da8d34121d1b97`.
It retains camera entity 1 and cube entity 16, with original spin data and
material/model references. Camera near/far values are 0.1/64; clear color is
cornflower blue; stencil clear is disabled and postprocessing is disabled.

The authoring scene changed externally during the session; the receipt identifies
the exact exported snapshot. Entity IDs in earlier inspection notes are not
used as fixed selectors. Scoped C# re-review passed after the camera decoding,
path validation, provenance, and selection fixes.

This verifies subset serialization only. Asset cooking, generated Core, PS1
scene loading, and target execution remain pending the architecture decision
in [GeneratedCorePrerequisite.md](GeneratedCorePrerequisite.md). No new PS1
executable was built or launched for this checkpoint.

## Evidence limits

### Frame-pacing follow-up (2026-09-15)

The user confirmed the rotating cube appears correctly in DuckStation, but
reported periodic dips to 57-58 FPS on a Ryzen 9800X3D / RTX 4090 host.
A GDB probe around `ramsyscall_printf` measured two guest VBlanks consumed
by each of six periodic render diagnostics. The cached `GPU::now()` value
does not update inside that call and must not be used to time it.

The same cadence regression probe sampled six blocks of 120 updates at
`GPU::flip` entry. Before the change it observed `[123,122,122,123,123,122]`
VBlanks and failed the one-VBlank-per-update assertion. With periodic
logging disabled by default it observed `[120,120,120,120,120,120]` and
passed. Startup render and first-presented diagnostics remained present.
The GPU completion fence and renderer were unchanged.

The fresh Release Rotate build and retail-memory validation passed:
`20260915-131022-961-Release-ddc2d563aad645b4b1cc4e014c40dfac/manifest.json`.
Its PS-X EXE SHA-256 is
`dddae83c06161b7190ef64ddb13be69efb9fe402293c8ca03e2a43172e60140d`.
Probe scripts and reports live under the external build root at
`controller-duck-probe/{pacing_probe.py,cadence_probe.py,pacing-printf-baseline.json,cadence-before.json,cadence-after.json}`.
This verifies guest cadence over 720 updates; it does not measure the host
display's long-term presentation rate. Historical periodic-log captures
above predate this change; reproducing them now requires
`-SdkDefine PS1_PERIODIC_DIAGNOSTICS`, except for CompletionStress.

GDB memory and packet-chain records prove projected coordinates, culling,
colors, DMA reachability, and ordering. They do not export framebuffer pixels.
The completion marker and fresh-VBlank lifecycle are observed through guest
diagnostics and the verified emulator configuration. Hardware timing,
framebuffer appearance, five actual resets, and disc boot remain outside the
evidence unless the pending rows acquire direct observations.

## Authored directional lighting (2026-09-16)

The cube_test scene's single directional light now drives per-vertex shading
on the GTE. The subset exporter retains `CubeTestSun` with its
`helengine.DirectionalLightComponent` alongside the camera and cube, the PS1
material cooker emits `Lit = true` for the `vertex-lit` lighting mode, and the
render manager reads `ObjectManager::get_DirectionalLights()` and builds the
GTE light and colour matrices. Object rotation folds into the light rows so
model normals stay in their authored local space, which is the arrangement the
hardware's single normal-colour pass expects.

Ambient is deliberately absent. The scene authors none, so faces turned away
from the sun go black rather than being lifted by a value nobody wrote.

Evidence, from
`20260916-135424-842-Release-5fbfa3b1e3cd4667847d71af1739295e` booted in
DuckStation under the verified portable configuration:

```
PS1_AUTHORED_MATERIAL id=Materials.rendering.cube_test.CubeTestSolid color=FFFFFF lit=1
PS1_AUTHORED_LIGHT count=1 l0=-1636,-3077,-2152 l1=0,0,0 l2=0,0,0 c0=4096,4096,4096 bg=0,0,0
PS1_AUTHORED_SHADE sampled=1 v0=858585 v1=858585 v2=858585      (frame 1)
PS1_AUTHORED_SHADE sampled=1 v0=4d4d4d v1=4d4d4d v2=4d4d4d      (frame 1200)
```

The light row was verified by hand against the authored sun. Its orientation
quaternion rotates the engine forward axis to a world direction of
`(0.3994, -0.7512, -0.5254)`; negating and flipping into GTE camera axes gives
`(-1636, -3077, -2152)` in 4.12, which is what the target reported.

The two shade samples were also verified by hand and are the load-bearing
evidence, because they are the hardware's own output rather than its input.
At frame 1 the visible face normal is `(0, 0, 1)`, whose dot with the
direction toward the light is `0.5254`; times the white base colour that is
`133`, and the target reported `0x85`. At frame 1200 the cube has turned
25.3 degrees, the same face normal becomes `(0.428, 0, 0.904)`, the dot falls
to `0.3041`, and `77` is expected; the target reported `0x4d`. One light, one
material, two orientations, two different measured brightnesses.

`Ps1AuthoredLightingTests.cpp` runs in the host suite and checks the light and
colour matrix construction, the per-object light matrix, normal conversion,
clamping and saturation, and parity against an independent reference for all
six cube face normals. It also checks the rejection paths: more than three
lights, a degenerate light direction, and non-uniform object scale.

This verifies the shading math and the values the GTE produced. It does not
export framebuffer pixels, so the on-screen appearance remains observed rather
than captured.

## Authored camera field of view (2026-09-16)

PS1 no longer invents its field of view. `ICamera` now carries `FieldOfView`
in radians, so the render manager reads it from the loaded camera and passes
it to `TryBuildPose`, which already rejects values it cannot project.

This reframes the PS1 picture, and deliberately so. The PS1 constant was
1.0471976 radians, 60 degrees, while the engine default is 45 degrees, the
value Dreamcast, the Xbox 360, DirectX 11 and Vulkan had each chosen
independently. PS1 was the outlier. The authored cube_test camera serializes
no field of view, so it takes the engine default and the cube now appears
larger, framed the same way as on every other platform.

The projection distance is the observable consequence, since
`h = 120 / tan(fov / 2)`:

| Field of view | Predicted h | Reported h |
|---|---|---|
| 60 degrees, the old constant | 207.8 | 208 |
| 45 degrees, the authored value | 289.7 | 290 |

From `20260916-182343-785-Release-203d04c1d1d2448a86c707fb4624b0e5`:

```
PS1_AUTHORED_FOV milliradians=785
PS1_AUTHORED_POSE trx=0 try=0 trz=640 h=290 ...
PS1_AUTHORED_LIGHT count=1 l0=-1636,-3077,-2152 c0=4096,4096,4096 bg=0,0,0
PS1_AUTHORED_SHADE sampled=1 v0=858585 v1=858585 v2=858585
```

785 milliradians is pi/4 read from the camera rather than compiled in. The
light and shade values are unchanged from the previous build, which is the
expected result: field of view moves the projection and must not touch
shading.

This build also bumps the PS1 engine revision from `d0d3ce7f` to `eeaa7a1e`,
which additionally brings a null guard in `Core.Draw` for scenes without a
render manager and two editor build fixes that speed up editor-app rebuilds.

`Ps1AuthoredProjection::DefaultFieldOfViewRadians` is renamed to
`TestFieldOfViewRadians` and is now used only by the host projection tests, so
their expected screen coordinates stay fixed and no production path can read a
field of view that the authored camera did not supply.

## Embedded codegen resolution (2026-09-17)

The editor now resolves csharpcodegen itself. The registry's `codegenToolPath`
is retired, and `EngineCodegenToolProvider` looks first for a `codegen`
directory beside the editor assembly, then builds the engine's
`engine/vendor/csharpcodegen` submodule on demand into
`HELENGINE_BUILD_WORKSPACE_ROOT`, keyed by the submodule commit.

Both paths were measured end to end against helengine `cd9b996d` with the
submodule at `c3c0521`, using the same subset scene. Each produced a working
328 KB PS-X EXE.

| Path | Step that fired | Wall time |
|---|---|---|
| Nothing beside the editor | on-demand submodule publish | 10 min 33 s |
| Codegen published beside the editor | pre-populated copy, no fallback | 9 min 44 s |

The on-demand publish succeeded with only `git` and `dotnet` on the headless
process's PATH, with no intervention. It wrote 273 files totalling 37 MB into
`<output>\workspace\codegen\<commit>\`.

That location is the finding. `build-authored.ps1` refuses to reuse an output
directory and derives the workspace root from it, so the root is new on every
run and the commit key can never match. The on-demand publish therefore recurs
on every PS1 build while the submodule sits unchanged. `%TEMP%\helengine-builds`
stayed empty, confirming the workspace root overrode it. This is recorded as a
follow-up against the engine: the cache belongs under an unconditional location
or its own variable, not under a root that pipelines are entitled to treat as
per-build scratch.

Neither run logged which path it took, so the step had to be inferred from
where files appeared. One provider log line naming the resolved tool would
replace that inference.

PS1 now publishes the generator itself, from the engine's pinned submodule by
default. A pinned build records `codegen_is_engine_pinned: true` and the
resolved `codegen_commit`, and completes in 9 min 10 s:

```
engine_version           1.0.0+cd9b996df2572d69f002800352013689cdae2946
codegen_root             ...\helengine\engine\vendor\csharpcodegen
codegen_is_engine_pinned True
codegen_commit           c3c0521cb5069cc19f9c7980d4a9af24551dde68
```

That build boots with `lit=1`, `milliradians=785`, `h=290`,
`l0=-1636,-3077,-2152` and `v0=858585`, matching the previous engine revision
exactly, so neither the engine merge nor the codegen bump changed PS1 output.
`c3c0521` is the fix for the RTTI capability-default regression this session's
`7d6fe36` introduced; PS1 was never affected because it opts into RTTI
explicitly.

### Forward check against csharpcodegen master (2026-09-17)

The `-CodegenRoot` override exists to test a generator change before the engine
pins it. Its first use: PS1 rebuilt through the full pipeline against
csharpcodegen master at `4f11363`, which routes generated delegate construction
through `he_cpp_bind_front` instead of naming `std::bind_front` directly.

The build succeeded and boots with output identical to the engine-pinned build
at `c3c0521`: `lit=1`, `milliradians=785`, `h=290`, `l0=-1636,-3077,-2152`,
`v0=858585`. The record shows `codegen_is_engine_pinned: false` with
`codegen_commit: 4f11363...`, so it cannot be confused with a pinned build.

PS1 does not exercise the changed path. The published tool carries the helper,
but the generated Core references neither `he_cpp_bind_front` nor
`std::bind_front` anywhere, because PS1 runs with `codegen-use-std-function`
false. So this check shows the change does not break PS1; it does not validate
the helper itself, which needs a target that emits delegates.

## Authored data drives rendering (2026-09-17)

The outstanding acceptance item was to alter authored data and confirm the
target follows, so that matching values cannot be coincidence. The authoring
project is never modified: `assets`, `settings` and `project.heproj` are copied
to a build directory, the copy is edited, and the pipeline runs against it with
`-ProjectRoot` pointing at the copy.

One value changed. The cube material's authored base colour went from
`#ffffffff` to `#3f7fbfff`, a same-length edit to the copy alone. Everything
else was held: same scene subset, same camera, same light, same geometry, same
engine and codegen commits.

Predicted from the front face's already-measured intensity of 0.5254:

| Channel | Authored | Expected |
|---|---|---|
| R | 0x3F, 63 | 33, 0x21 |
| G | 0x7F, 127 | 66, 0x42 |
| B | 0xBF, 191 | 100, 0x64 |

Reported by the target:

```
PS1_AUTHORED_MATERIAL id=Materials.rendering.cube_test.CubeTestSolid color=3F7FBF lit=1
PS1_AUTHORED_SHADE sampled=1 v0=644221 v1=644221 v2=644221
```

`644221` is B `0x64`, G `0x42`, R `0x21`. All three channels moved
independently and each landed on its predicted value. The white material could
not have shown this, because three equal channels are consistent with any
channel order; this run also confirms the GTE emits red in the low byte, which
is what the gouraud fragment assumes.

Taken with the two earlier checks, three authored inputs have now been varied
and tracked to the hardware: the cube's rotation, through the shade moving from
`0x85` to `0x4d` across frames; the camera's field of view, through `h` moving
from 208 to 290; and now the material's base colour. Camera position and light
direction remain verified by hand calculation against a single build rather
than by variation.

### A defect this found

The first attempt failed to compile, and not because of the colour:

```
error: 'he_cpp_bind_front' was not declared in this scope
```

`dotnet publish` leaves files it considers up to date, so publishing the
engine-pinned csharpcodegen over the master build from the earlier forward check
produced a directory with one commit's tool and the other's bundled headers.
Each build had passed on its own; only switching between them exposed it. The
wrapper now clears that directory first. Recorded because a mixed toolchain
directory fails in a way that looks like a code generation bug.

## What the whole cube_test scene needs (2026-09-17)

PS1 runs the camera, cube and sun subset of `cube_test`. This measures what the
unmodified scene needs, by running the real scene through the pipeline with no
subsetting and recording each failure in turn rather than predicting them.

Three blockers surfaced, in order.

**Project scripts that need C++ exceptions.** Codegen rejected
`SoftwarePathTracer.InitializeProgressive` with CPP1001, because PS1 sets
`codegen-use-exceptions` false and the method recovers from a thrown exception.
Across the project's runtime modules only two files use try/catch,
`SoftwarePathTracer.cs` and `SoftwarePathTracerComponent.cs`, and both belong to
a software path tracer that will never run on this hardware. Every other
`catch` is in a `.tools` or `.tests` module, which the wrapper already drops.
Excluding those two is enough.

**Script pruning is wrong for a whole scene.** The wrapper kept only classes
named by a scene component, which broke the C# build because those components
reference helpers no component names. `-RetainAllRuntimeScripts` keeps the
runtime codebase, and `-ExcludeScriptClasses` drops named classes with a check
that each one was actually found.

**Memory, which is the real blocker.** With both of the above resolved, the
build reaches the linker and fails there:

```
out/helengine_ps1.elf section `.rodata' will not fit in region `ram'
region `ram' overflowed by 6707352 bytes
```

The scene cooks to 36 files totalling 7.9 MB, against 2 MB of main RAM:

| Count | Size each | What |
|---|---|---|
| 30 | 262,254 bytes | imported textures, 512x512 at 8 bits plus header |
| 1 | 266,030 bytes | `demodiscbody.hefont` |
| 1 | 134,954 bytes | imported asset |
| 3 | under 15 KB | scene, model, material |

The 30 texture pages alone are 7.5 MB. The console has 2 MB of main RAM and
1 MB of VRAM, so this content is roughly four times over both budgets at once.

**This conclusion was wrong and is corrected below.** It read as though the
engine lacked the machinery. It does not. PS1 simply declared none of it.

## PS1 texture cooking (2026-09-18)

The 7.9 MB above was not what the scene costs. It was what a platform that
declares no texture cook capabilities costs.

`PlatformDefinition` takes an optional `assetCookCapabilities` array, and the
PS1 factory stopped before it. Nintendo DS declares three entries and gets its
textures downsampled and palettised at cook time; N64 declares one. PS1
declared none, so the editor emitted source assets unchanged: thirty 512x512
8-bit pages. The same shape as the field of view and the draw-call count, where
a platform that declared nothing received a silently unreasonable default.

PS1 now declares two builder-owned cook capabilities. Ordinary textures default
to Indexed8 at 64 pixels a side, font atlases to Indexed4 at 128. The GPU
samples only 4-bit and 8-bit paletted textures from a 256x256 page, so the
format capability advertises those two. Raw `Rgba32` values carried forward
by the shared importer are treated as source-image markers and converted to
the indexed PS1 defaults by the builder.

Almost no new code was needed. The resize and quantisation are the engine's own
`TextureAssetProcessor`, which the builder already had access to. PS1 adds a
settings serializer, a source decoder ported from N64, and a work-item executor
that resolves settings, source and destination.

Measured on the whole scene, same inputs as the failing build above:

| | Before | After |
|---|---|---|
| Cooked files | 36 | 6 |
| Cooked content | 7.9 MB | 0.02 MB |
| Font atlas | 262,260 bytes | 8,349 bytes |
| Link result | RAM overflow by 6,707,352 bytes | links |
| Static footprint | did not link | 475,288 bytes |
| Free heap | did not link | 1,490,792 bytes |

The whole scene now boots. It reads its 6-asset archive, opens the 14 KB scene
and then stops:

```
PS1_AUTHORED_ARCHIVE_READY bytes=23324 assets=6
PS1_AUTHORED_SCENE_LOADING id=cube_test
PS1_AUTHORED_ASSET_OPEN cooked/scenes/rendering/cube_test.hasset bytes=14054
PS1_CORE_FATAL Exception
```

That is the expected next wall rather than a regression. The scene carries
sprite, text, viewport, rounded-rect, FPS, interactable and blueprint-instance
components, and PS1 implements none of them. The failure text is bare because
the PS1 codegen profile sets `codegen-compact-native-exception-messages`, which
strips the detail; identifying the exact component needs a build with that
turned off.

At this 2026-09-18 cook checkpoint, the cooked `.hetex` atlas pages were not
staged into the embedded archive, and the manifest reported one cooked artifact
where it previously reported 36. The atlas-staging gap is since resolved: the
2026-09-22 authored UI archive contains both font atlases. Text rendering is
still unimplemented, and the manifest artifact-count discrepancy remains
unexplained.

## Workspace correction (2026-09-22)

PS1 uses the existing `C:\dev\helworks\helengine` checkout. The earlier
migration to a nested `dependencies/helengine` clone misunderstood the requested
layout and was reversed. Both the old sibling PS1 worktree and the unnecessary
nested clone have been removed; no PS1-specific engine checkout remains.

The builder, sample exporter and exporter test project defaults were verified
with MSBuild property evaluation to resolve to the shared engine checkout.
Its observed HEAD was `72a4fcbfd6b2fa6cc2c59a618c09eb4a49bf9637`; this is an
observation, not a pin. The shared checkout was not switched or modified.
No PS1 build or emulator run was performed for this layout correction, and
the compact-exception flag remains unchanged.

## Whole-scene PS1 rebuild (2026-09-22)

The rebuild used the shared engine checkout at `C:\dev\helworks\helengine`
(observed HEAD `72a4fcbfd6b2fa6cc2c59a618c09eb4a49bf9637`) and the unchanged
`cube_test` export. The authored package source contained six cooked assets;
the embedded archive was 18,428 bytes. Generated Core linked after the PS1
Makefile stopped adding SDK math entry points already emitted by the current
engine-pinned codegen. The resulting PS-X EXE was 438,272 bytes.

The temporary compact-exception diagnostic setting was restored after the
build. No emulator was launched for that compact build, so the first
unsupported scene component was still pending runtime capture.

## Whole-scene diagnostic boot (2026-09-22)

After the raw `Rgba32` source-marker conversion was added, a fresh authored
build against the same shared engine checkout was launched with the temporary
verbose native-exception setting. The embedded archive measured 213,344 bytes
with 42 declared assets; the PS-X EXE measured 675,840 bytes. DuckStation
`0.1-11894-gc66b2694d` using the verified portable OpenBIOS booted the artifact
and logged `PS1_AUTHORED_SCENE_READY` with one camera, one 3D drawable and nine
updateables, followed by `PS1_AUTHORED_FIRST_FRAME` and the sequence-1200
checkpoint. The log contains no `PS1_CORE_FATAL` or exception.

The exported scene retained Sprite, Text, Viewport, RoundedRect, FPS,
BlueprintInstance, DirectionalLight and the DemoDisc runtime components, so no
component throws first in this layout. The earlier wall was the generated
`dpad.png` texture work item carrying raw `Rgba32`, not scene-component
construction. Evidence: `20260922-diagnostic-runtime-3/editor-output/manifest.json`
and the adjacent `duckstation-20260922-155115-567.log`.

## PS1-only authored controls and pre-scaled icon upload (2026-09-22)

The shared control blueprint still emits cook work items for every platform
variant. The PS1 stager now keeps only the `generated/ps1` control textures,
while preserving each authored PS1 override's resolution. The unchanged
`cube_test` scene produced 11 declared archive assets (three imported PS1
controls, two fonts and their atlases, scene, material and model) and a 37,028
byte embedded archive. The imported control payloads were 1,842–1,895 bytes;
the previous all-variant package contained 36 imported payloads at 5,262 bytes
each.

The authored PS1 icon overrides are 26/27 pixels wide. PS1 upload now pads odd
indexed rows to complete VRAM words without changing the logical texture size.
DuckStation `0.1-11894-gc66b2694d` booted the resulting PS-X EXE and logged two
27x27 8-bit uploads, one 26x26 upload, the default font upload, scene-ready,
first-frame and cadence-600 markers. The log contains no `PS1_CORE_FATAL`,
exception or assertion. Evidence: native manifest
`C:\dev\helworks\builds\helengine-ps1\20260922-163735-294-Release-27694b231ae44009bd1f4e96f0ed2f56\manifest.json`
and the adjacent `duckstation.log` output.

## Authored PS1 sprite and rounded-shape rendering (2026-09-22)

The unchanged `cube_test` scene now traverses its ordered 2D camera queue and
submits indexed sprites and rounded rectangles in the same GPU frame chain as
the 3D scene. The render path uses three chords per rounded quarter-circle so
both authored bordered panels fit within the 96-triangle UI reservation. PS1
fractional alpha uses the hardware's fixed half-back/half-front blend; fully
transparent pixels are skipped. This is a 50% blend approximation for all
non-binary alpha values.

The build used shared engine revision
`72a4fcbfd6b2fa6cc2c59a618c09eb4a49bf9637` and the `cube_test` export. Its 11
declared assets produced a 37,028-byte embedded archive. The three imported
PS1 control textures remained 27x27, 27x27 and 26x26 pixels. Both default and
DemoDisc body font atlases are present in the archive; text rendering remains
unimplemented and the five visible text drawables are skipped.

DuckStation `0.1-11894-gc66b2694d` booted the PS-X EXE and reported
`sprites=3 shapes=2 skipped_text=5 submitted_sprites=3 submitted_triangles=98
failure=none`; the combined draw chain had 101 packets, 2,104 bytes high-water,
intact guards, and no fatal or abort. Rounded-corner arc samples use a constant
lookup instead of per-frame software sine/cosine calls. The optimized build
reports VBlank 1,251 / 652 missed at sequence 600 and VBlank 2,452 / 1,253
missed at sequence 1,200, about 29.4 scene updates per second in this
DuckStation configuration. The heap end remained `0x80096d78`. This run
does not establish real-hardware frame timing or visually verify framebuffer
pixels; no screenshot was captured.

| Measure | Value |
|---|---:|
| PS-X EXE | 468,992 bytes |
| PS-X EXE payload | 466,944 bytes |
| Static footprint | 498,464 bytes |
| Heap capacity | 1,467,616 bytes |
| Embedded authored archive | 37,028 bytes (11 declared assets) |
| UI packets | 98 triangles + 3 sprites (101 combined packets) |
| PS-X EXE SHA-256 | `75e2940fd755021a0de0d48399ba1dbc61965a9b4418e492b5ca17e497849bc7` |

Evidence: build manifest
`C:\dev\helworks\builds\helengine-ps1\20260922-193546-150-Release-9be13469f6b542dda60b2832a928722c\manifest.json`
and DuckStation log
`C:\dev\helworks\emus\ps1\duckstation-windows-x64-release\duckstation.log`.

## Authored sprite page-local UV correction (2026-09-22)

The UI packet builder already adds each texture's VRAM page-local origin to its
UVs. `Ps1RenderManager2D::DrawSprite` also added that origin, so the GPU sampled
the wrong part of the page for textures not placed at its top-left. The
page-aligned light swatch had U offset 0 and rendered; the two other 8-bit
textures had U offsets 28 and 24 texels, which were applied twice as 56 and 48.
The manager now leaves UVs texture-relative and the packet builder applies the
offset once.

The fresh PS1 build used the unchanged `cube_test` export (source SHA-256
`ca237b8a84abe3db34ad6ee10574ab9e2da1d48819b9083f561c458ff54a58d2`) and
shared `helengine` revision `72a4fcbfd6b2fa6cc2c59a618c09eb4a49bf9637`.
The linked PS-X EXE is 468,992 bytes, with a 498,464-byte static footprint and
1,467,616 bytes of heap. The host test suite completed with all required exit
codes at zero. The manifest-checked DuckStation boot after this correction
logged `PS1_AUTHORED_FIRST_FRAME`, `sprites=3`, `submitted_sprites=3`,
`submitted_triangles=98`, 101 total packets, intact guards and no fatal marker.
This confirms the corrected packets reached the frame chain, but not the
framebuffer's pixels; no screenshot was captured.

Evidence: build manifest
`C:\dev\helworks\builds\helengine-ps1\20260922-ui-fix-Release\editor-output\manifest.json`,
authored build record
`C:\dev\helworks\builds\helengine-ps1\20260922-ui-fix-Release\authored-build.json`,
and host summary
`C:\dev\helworks\builds\helengine-ps1\host-tests-20260922-201017-283-3976aad036b04557b6cf882f4b4c0e36\summary.json`,
and DuckStation log
`C:\dev\helworks\emus\ps1\duckstation-windows-x64-release\duckstation.log`.
