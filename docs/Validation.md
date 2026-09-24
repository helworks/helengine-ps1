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
first runtime texture was page-aligned at X=320; the other 8-bit textures had U
offsets 28 and 24 texels, which were applied twice as 56 and 48. The manager
now leaves UVs texture-relative and the packet builder applies the offset once.

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

## Authored sprite CLUT addressing correction (2026-09-22)

After the page-local UV correction, the user still saw only the light swatch.
The sprite packets were present, but their palette indices used the wrong VRAM
coordinates: PSYQo's `ClutIndex` stores X in 16-word units, while the upload
path passed raw word coordinates. The first two palette reservations were at
X=334 and X=604, neither aligned to the GPU's 16-word CLUT address step. The
allocator now aligns palette reservations and the draw path passes `clutX / 16`
to `ClutIndex`.

The regression test checks that a 27x27 8-bit texture at X=320 is followed by
an aligned palette at X=336 (encoded CLUT X=21), without overlap. The full PS1
host suite passed. A fresh build against the same shared `helengine` revision
booted the unchanged scene and logged textures at `(vram, clut)` coordinates
`(320,0; 336,0)`, `(592,0; 608,0)`, and `(912,0; 320,64)`. It then logged the
first frame, three submitted sprites, 98 UI triangles, intact guards and no
fatal marker. PS-X EXE size and RAM use remained 468,992 bytes, 498,464 bytes
static and 1,467,616 bytes heap. These logs verify VRAM setup and packet
submission, not framebuffer pixels; no screenshot was captured.

Evidence: build manifest
`C:\dev\helworks\builds\helengine-ps1\20260922-clut-fix-Release\editor-output\manifest.json`,
authored build record
`C:\dev\helworks\builds\helengine-ps1\20260922-clut-fix-Release\authored-build.json`,
host summary
`C:\dev\helworks\builds\helengine-ps1\host-tests-20260922-210649-463-79d56a4a520741cf8a182aac86f26bcc\summary.json`,
and DuckStation log
`C:\dev\helworks\emus\ps1\duckstation-windows-x64-release\duckstation.log`.

## Authored viewport fit on PS1 (2026-09-22)

After the CLUT correction, the user still saw only the light swatch. A
first-frame guest trace showed the instruction panel at `(24, 528)` and its
three sprite quads at Y=560–732 on a 320x240 display. Core's screen-bound
`ViewportComponent` fell back to its authored 1280x720 fixed size because the
PS1 render manager had not registered its native display. N64 already calls
`RenderManager3D::AddWindow(0, 320, 240)` before Core initialization; PS1 now
does the same.

With the same scene export and engine revision, the first-frame guest trace
showed the panel at `(6, 162)` with its component size changed from 345x150
to 86x38, and all three sprite quads moved into Y=170–214. The light swatch
moved from about `(190, 73)` to `(48, 48)`. The panel's authored 1.5 entity
scale remains in both traces. This establishes that Core fitted the reference
canvas into the native viewport; no framebuffer capture was made, so pixel
appearance still needs a direct visual check.

A clean rebuild from commit `695002d` (temporary tracing removed) used
`helengine` revision `72a4fcbfd6b2fa6cc2c59a618c09eb4a49bf9637` and the unchanged
scene export SHA-256
`ca237b8a84abe3db34ad6ee10574ab9e2da1d48819b9083f561c458ff54a58d2`.
It booted in DuckStation with an 11-asset, 37,028-byte archive, one complete
first frame, 3 submitted sprites, 98 submitted UI triangles, 101 total
packets, intact guards, and no fatal marker. Five text drawables are still
skipped. The PS-X EXE stayed at 468,992 bytes, static footprint at 498,464
bytes and heap capacity at 1,467,616 bytes. The host suite reported zero for
every required check; its negative stream fixture returned the expected 73.

Evidence: pre-fix guest log
`C:\dev\helworks\builds\helengine-ps1\20260922-ui-trace-Release\editor-output\guest-first-frame.log`,
post-fix guest log
`C:\dev\helworks\builds\helengine-ps1\20260922-ui-viewport-Release\editor-output\guest-first-frame.log`,
and clean manifest
`C:\dev\helworks\builds\helengine-ps1\20260922-ui-viewport-final-Release\editor-output\manifest.json`
(PS-X EXE SHA-256
`d94e29b715186828113fe8eeba32ae3cd2b782f0346372a346fe31325a596e14`),
host summary
`C:\dev\helworks\builds\helengine-ps1\host-tests-20260922-215047-874-77de82768eac437c812b928170b99fd8\summary.json`,
and clean DuckStation guest log
`C:\dev\helworks\builds\helengine-ps1\20260922-ui-viewport-final-Release\editor-output\guest-first-frame.log`.

## Authored font glyphs on PS1 (2026-09-23)

The prior first frame visited five text drawables but skipped all five. It
submitted 3 textured sprite quads and 98 UI triangles in 101 packets, using
2,104 bytes of packet storage. The new font path uses the cooked default font
atlas and Core glyph metrics, preserving the ordered sprite/shape/text queue.
It also increases the textured reservation from 24 to 128 quads and the frame
packet budget from 8,192 to 16,384 bytes.

With the same scene export (SHA-256
`ca237b8a84abe3db34ad6ee10574ab9e2da1d48819b9083f561c458ff54a58d2`)
and engine revision `72a4fcbfd6b2fa6cc2c59a618c09eb4a49bf9637`, the first
frame visited 5 text drawables, staged 82 glyph quads, skipped 0 texts, and
submitted 85 textured quads and 262 UI triangles in 265 packets. Packet
high-water rose from 2,104 to 7,352 bytes, with intact guards and no failure
marker. The PS-X EXE grew from 468,992 to 475,136 bytes; static RAM grew from
498,464 to 521,568 bytes, leaving 1,444,512 bytes of heap instead of
1,467,616. The host suite passed all required checks; the stream fixture's
negative exit code 73 was expected. These measurements show glyph packet
submission, while visual font appearance remains for a user check.

The first build attempt stopped during staged C# compilation because the
new menu-only `FooterIdentityMarqueeComponent` uses a viewport constant absent
from the pinned engine. The successful build excluded that class; `cube_test`
does not reference it. The shared engine revision was not changed.

Evidence: build manifest
`C:\dev\helworks\builds\helengine-ps1\20260923-ui-font-menu-excluded-Release\editor-output\manifest.json`,
guest log
`C:\dev\helworks\builds\helengine-ps1\20260923-ui-font-menu-excluded-Release\editor-output\guest-first-frame.log`,
and host summary
`C:\dev\helworks\builds\helengine-ps1\host-tests-20260923-095853-889-93e82d4554dd45ca95d9926020fd4d46\summary.json`.

## Native font atlas and glyph bounds (2026-09-23)

The first font build cooked the default 256x128 atlas at 128x64 while retaining
the original glyph metrics. Its generic sprite UV conversion could also include
an adjacent texel for a small glyph. The PS1 font cook now retains the 256x128
atlas, glyph quads use exact source texel bounds, and texture uploads reserve a
single horizontal GPU page when possible. The page rule placed this atlas at
VRAM X=896 instead of X=864, where it had crossed a page and rejected all text.

The rebuilt scene uploaded the font at 256x128, staged 82 glyph quads, skipped
0 of 5 text drawables, and submitted 85 textured quads and 262 UI triangles on
the first frame, with intact packet guards and no failure marker. The 600-frame
cadence used 1,851 VBlanks versus 3,048 for the earlier 128x64 build, or about
19.4 versus 11.8 frames per second at 60 Hz. The PS-X EXE grew from 475,136 to
512,000 bytes; static RAM grew from 521,568 to 558,432 bytes, leaving
1,407,648 bytes of heap versus 1,444,512. The host suite and focused font cook
test passed. No framebuffer capture was made, so the glyph appearance still
needs a user visual check.

Evidence: rebuilt manifest
`C:\dev\helworks\builds\helengine-ps1\20260923-105323-722-Release-f5beb5520b7b45d59e4052f31c054408\manifest.json`,
guest log
`C:\dev\helworks\builds\helengine-ps1\20260923-105323-722-Release-f5beb5520b7b45d59e4052f31c054408\guest-font-native-atlas.log`,
earlier cadence log
`C:\dev\helworks\builds\helengine-ps1\20260923-102551-055-Release-920901ae34e849ac8315d6ef8327d925\guest-timing.log`,
and host summary
`C:\dev\helworks\builds\helengine-ps1\host-tests-20260923-105114-210-b5da0b99b4ad433799dfbebfeda44e4f\summary.json`.

## Shared MSD FPS scale and PS1 glyph sampling (2026-09-23)

The original cooked `cube_test` FPS component used a `2.0` font scale on PS1.
Moving the authored override from `platform:n64` to the `sd/msd` group made the
PS1 cook resolve `1.0`; the 9,147-byte cooked scene changed only the two bytes
of that float (`00 00 00 40` to `00 00 80 3F`). A first-frame UI trace before
this change placed the two FPS rows at Y=6 and Y=34 while the Light row began
at Y=43.5. The user confirmed that the new FPS text matched the other text
sizes, but all glyphs still lost pixels at their right and bottom edges.

The PS1 glyph quad had mapped its far vertices to `source + size - 1`, although
its screen vertices mark the exclusive destination edge. The host geometry test
now models the PS1's half-texel UV start and checks every source column and row
of a 5x9 glyph at 1:1 size. It failed against the old UVs and passed with the
exclusive source edge. The user visually confirmed that the resulting glyphs
look correct. The full host suite passed with required exit codes zero; its
negative stream fixture returned the expected 73.

The user then reported that `Update FPS` touched the visible top edge. The
FPS component's common top padding is 6 pixels. A new `sd/msd` override raises
that padding to 16 pixels and leaves PSP at 6. Regenerating `cube_test` changed
the authored scene from 50,668 (scale-only) to 50,679 bytes. Comparing the two PS1 cooked
scenes showed both at 9,147 bytes, with exactly one changed byte in the FPS
padding (`06` to `10` at offset 2996); the scale remained `1.0`. The final
PS-X EXE is 512,000 bytes, with a 558,432-byte static footprint and 1,407,648
bytes of heap, unchanged from the earlier native-atlas build. The user
confirmed the final DuckStation build gives `Update FPS` a comfortable top margin.
The user also reports about 19.8 FPS and `0.0 ms` render time in that build;
the timing source and 2D workload are under investigation.

Evidence: final scene export SHA-256
`fa2a9b72ae0760d95b9968946f910bbdac69ca0504a6dc88eb0dd6aeb86ee6f6`,
final build manifest
`C:\dev\helworks\helengine-ps1\out\msd-padding-build-20260923\editor-output\manifest.json`,
prior glyph-fix manifest
`C:\dev\helworks\builds\helengine-ps1\20260923-120325-857-Release-0f6228ba83704669b6fc02784269d21a\manifest.json`,
host summary
`C:\dev\helworks\builds\helengine-ps1\host-tests-20260923-120438-614-c791ad64cc58436fb2f6588b65d17e4c\summary.json`,
and final launch record
`C:\dev\helworks\helengine-ps1\out\msd-padding-build-20260923\editor-output\last-launch.json`.

## PS1 UI frame timing and layout cost (2026-09-23)

The Core render stopwatch had shown `0.0 ms` because PSYQo's `GPU::now()`
advances at page-flip callbacks, not while `Core::Draw` runs. The PS1 runtime
clock now extends the hardware HSync counter at 64 microseconds per tick. The
user's on-screen Render value changed from `0.0 ms` to a measured `~30 ms`.
A PS1-only overlay row also shows average Update time; it does not change the
shared engine checkout.

A 600-frame profile before the final text-layout change measured Update
3.718 ms, frame boundary 0.192 ms, Draw 30.079 ms, and 1,802 VBlanks over the
steady 600-frame interval (19.98 FPS at 60 Hz). The PS1 now avoids rotation
math for unrotated UI, caches cooked glyph metrics and atlas texel bounds,
reuses rounded panel geometry, and skips the width-measurement pass for
left-aligned text. The latter test first failed at 8 glyph lookups for a
4-glyph label, then passed at 4 with identical positions. The final steady
600-frame interval used 1,243 VBlanks (28.96 FPS at 60 Hz), with Update
3.673 ms, frame boundary 0.193 ms, Draw 28.358 ms, and between-frame wait
2.645 ms. The first 600-frame interval used 1,293 VBlanks and includes the
57-VBlank startup frame. The user reported 28.7–28.8 FPS, Update 3.7 ms, and
Render 28.0 ms on the final build; they had confirmed correct text and panel
corners on the immediately preceding candidate.

The final PS-X EXE is 516,096 bytes versus 512,000 before this work. Static
RAM is 562,592 versus 558,432 bytes, leaving 1,403,488 versus 1,407,648
bytes of heap. The full host suite passed: all required exit codes were zero,
and the negative stream fixture returned its expected 73.

Evidence: final build manifest
`C:\dev\helworks\builds\helengine-ps1\20260923-132245-065-Release-6c694050d7b841c58db38ca76d80f0bc\manifest.json`,
preserved guest log
`C:\dev\helworks\builds\helengine-ps1\20260923-132245-065-Release-6c694050d7b841c58db38ca76d80f0bc\guest-ui-perf.log`,
and host summary
`C:\dev\helworks\builds\helengine-ps1\host-tests-20260923-132118-037-1d1e53443e5544d5a786be7ca0876e3c\summary.json`.

## NTSC cadence and cross-frame glyph cache (2026-09-23)

DuckStation's FPS counter counts new game frames while VPS counts emulated video
refreshes. The visible 28.91 FPS / 59.82 VPS therefore meant the emulator was
running at NTSC video speed while the game missed some two-VBlank deadlines.
At 59.82 VPS, the two-VBlank target is 29.91 FPS, not 30.00. The prior steady
600-frame interval used 1,243 VBlanks rather than 1,200.

Temporary diagnostics isolated the recurring miss: 40 of 41 third-VBlank
frames in the second 600-frame interval followed an Update over 4.5 ms.
Normal Update averaged 3.583 ms; the 40 slow Updates averaged 4.928 ms.
The latter plus Draw 28.356 ms and frame boundary 0.193 ms reached 33.477 ms,
just beyond the two-VBlank budget. The 40 slow Updates align with the FPS
component's 0.5-second refresh interval. Sending the PS1 overlay text rows
after Draw did not coincide
with those missed frames. These counters were
temporary and removed after the diagnosis.

Cooked font metrics are immutable, so the PS1 glyph cache now persists across
frames and is invalidated when a runtime texture is released. The host test
first failed without the invalidation method, then passed with cross-frame
reuse and refetch after invalidation. Precomputing each text pass's GPU color
and blend state avoids repeated conversions per glyph. In the final native
build, steady Draw averaged 25.674 ms versus 28.358 ms before these changes.
The second 600-frame interval used 1,202 VBlanks versus 1,243, equivalent to
29.86 versus 28.88 FPS at the measured 59.82 VPS. Update averaged 3.673 ms
and frame boundary 0.192 ms. The user saw 29.7 FPS, Update 3.7 ms,
and Render 25.3 ms in the game, while DuckStation showed 29.91 FPS and
59.82 VPS. The PS-X EXE remained 516,096 bytes, static RAM
562,592 bytes, and heap capacity 1,403,488 bytes. The full host suite passed
all required checks; its negative stream fixture returned the expected 73.

Evidence: final build manifest
`C:\dev\helworks\builds\helengine-ps1\20260923-135441-237-Release-930e8750263943d2b5867dafa7f98406\manifest.json`,
final guest log
`C:\dev\helworks\builds\helengine-ps1\20260923-135441-237-Release-930e8750263943d2b5867dafa7f98406\guest-glyph-cache.log`,
Update-cost diagnostic log
`C:\dev\helworks\builds\helengine-ps1\20260923-134453-706-Release-55b68ed8c4bc456f8c3cfa7cb378a1c3\guest-update-cost.log`,
and host summary
`C:\dev\helworks\builds\helengine-ps1\host-tests-20260923-135357-517-4fbbbb5c42364441839074ebca71f36d\summary.json`.
## Authored scene at one frame per NTSC VBlank (2026-09-23)

With the unchanged `cube_test` authored archive, the prior Release build averaged
3.673 ms in Update and 25.674 ms in Draw. Its second 600-frame interval used
1,202 VBlanks, or 29.86 game FPS at DuckStation's measured 59.82 VPS.
Temporary UI profiling found 12.9 ms per frame in text layout and glyph quad
construction. Retaining each text component's quads, pointing the ordered UI
queue at those retained quads, and changing only glyph UVs when new characters
have identical metrics removed most of that repeated work. A shape-quad cache
was discarded after it worsened Draw from 15.1 to 16.0 ms.

Release builds of the authored path now compile generated Core and the frame
renderers with `-O2`; diagnostic and non-authored builds keep their existing
flags. Measured in the final DuckStation build, the second 600-frame profile
averaged Update 2.984 ms, boundary 0.192 ms, and Draw 9.874 ms. The clean
frame-1800-to-2400 interval used exactly 600 VBlanks for 600 game frames,
equivalent to 59.82 game FPS at 59.82 VPS. The startup interval still includes
a 54-VBlank first frame, and the 600/1200 diagnostic printouts disturb nearby
frames; neither is included in the clean interval. The first-frame UI report
still lists 3 sprites, 2 shapes, 5 texts, 82 glyph quads and no UI failure.
The user confirmed that the panel, text and shapes remain visually correct on
the final build, and that both the game and DuckStation FPS/VPS readings
settle near 59.8.

The throughput flags increased the PS-X EXE from 516,096 to 790,528 bytes and
static RAM from 562,592 to 839,080 bytes. Heap capacity decreased from
1,403,488 to 1,127,000 bytes, remaining above 1.1 MB. The full host suite
passed with every required exit code zero and the negative stream fixture at
its expected 73.

Evidence: final build manifest
`C:\dev\helworks\builds\helengine-ps1\20260923-175104-398-Release-3074997bf2f8488bb7d8d14e09868223\manifest.json`,
preserved guest log
`C:\dev\helworks\builds\helengine-ps1\20260923-175104-398-Release-3074997bf2f8488bb7d8d14e09868223\guest-steady-600.log`,
and host summary
`C:\dev\helworks\builds\helengine-ps1\host-tests-20260923-174952-451-3275b2f3dcc84a83b529b2796f9bad11\summary.json`.

## Authored UI textured quads (2026-09-23)

The unchanged `cube_test` UI previously emitted two textured triangle packets
per sprite or glyph. A PS1 textured quad now carries the same four vertices in
one packet. The first-frame draw packet count fell from 265 to 180, while
the UI report still counted 3 sprites, 2 shapes, 5 texts, and 82 glyph quads
with no failure. The second 600-frame average changed from Update 2.984 ms,
frame boundary 0.192 ms, and Draw 9.874 ms to Update 2.926 ms, boundary
0.188 ms, and Draw 9.337 ms. The clean 1800–2400 interval still used exactly
600 VBlanks for 600 frames. The user confirmed the fonts, sprites, and panels
look the same and read Render near 9.3 ms on screen.

The quad fragment is 40 bytes versus 64 bytes for two triangle fragments.
With capacity for 128 textured quads, the fixed packet allocation decreased
from 11,920 to 8,848 bytes, leaving room for 376 scene triangles instead of
223 under the same 16 KB packet budget. The PS-X EXE remained 790,528 bytes;
static RAM was 839,104 bytes and heap capacity 1,126,976 bytes. The full host
suite passed.

Evidence: build manifest
`C:\dev\helworks\builds\helengine-ps1\20260923-185010-355-Release-280eb3481e9b498cbfcdffee3d033c38\manifest.json`,
guest log `C:\dev\helworks\helengine-ps1\out\quad-trial-guest.log`, and host
summary
`C:\dev\helworks\builds\helengine-ps1\host-tests-20260923-184924-654-0f59df19cbe346c19f42ab15b37e20d1\summary.json`.

## Stable 2D queue type validation (2026-09-23)

The `cube_test` render list contains 3 sprites, 2 rounded rectangles and 5
text drawables. Profiling the quad build attributed 4.167 ms per frame to
visiting that list, including roughly 1 ms outside the draw methods. On PS1,
the visitor had repeated up to three RTTI checks for every drawable each
frame. The render list already increments a version when its membership
changes. PS1 now validates concrete drawable types on the first visit for a
queue version, and reuses that result until the version changes. It rejects a
version change during traversal.

With the unchanged scene and restored, clean generated Core, the second
600-frame average changed from Update 2.926 ms and Draw 9.337 ms to Update
2.924 ms and Draw 8.382 ms. The first-frame packet count stayed 180; the UI
report still lists 3 sprites, 2 shapes, 5 texts, 82 glyph quads and no failure.
The clean 1800–2400 interval used 600 VBlanks for 600 game frames. The host
suite passed, including the expected negative stream fixture exit 73.
A follow-up trial retained each glyph's 36-byte GPU primitive alongside its
cached quad. It worsened Draw from 8.382 to 8.833 ms and raised the steady
heap end from `0x800fb920` to `0x80105c20` (41,728 bytes). That trial was
discarded; the faster queue-validation build remains current.

Evidence: build manifest
`C:\dev\helworks\builds\helengine-ps1\20260923-191739-649-Release-23ad068bb5c7470d9a170197614d4792\manifest.json`,
guest log `C:\dev\helworks\helengine-ps1\out\validated-ui-queue-guest.log`,
and host summary
`C:\dev\helworks\builds\helengine-ps1\host-tests-20260923-191714-027-a91a2111e13c468e8f410cd2360d112d\summary.json`.

## Update phase profile (2026-09-23)

Temporary timing probes in the generated Core, rebuilt in an isolated native
object cache, measured the second 600-frame interval at Update 2.984 ms.
`ObjectManager::Update` accounted for 2.792 ms and pointer interaction for
0.116 ms; early input, FPS frame counting, audio, physics, and late input each
rounded to 0–1 us in this scene. The probes add about 0.06 ms versus the
unmodified Update baseline of 2.924 ms, so individual figures are approximate.

Per-updateable probes found 0.768 ms for the orbit camera (entity 8), 0.704 ms
for cube spin (entity 27), 0.448 ms for the debug scene label and 0.270 ms for
return-to-menu (both entity 17). The remaining five updateables together used
about 0.25 ms. The entity/component mapping comes from the scoped PS1 scene
package and its serialized component order. A separate diagnostic experiment
reduced Update from 2.926 to 2.540 ms by skipping per-component diagnostic
owner/type lookups; it was restored after measurement, since changing generated
Core without changing its source would leave the build hard to reproduce.

Evidence: phase-profile guest log
`C:\dev\helworks\helengine-ps1\out\update-phase-profile-guest.log`, per-updateable
guest log `C:\dev\helworks\helengine-ps1\out\updateable-profile-guest.log`, and
per-updateable build manifest
`C:\dev\helworks\builds\helengine-ps1\20260923-194036-265-Release-2753610ec5374688a704b1bb271f92f2\manifest.json`.

## Frame-context reuse of cached text quads (2026-09-23)

Text layout already retains glyph quads, but each frame still converted every
quad to a PS1 GPU packet. The two frame contexts now remember the cached quad
address and its revision at each textured packet slot. When the same quad
occupies that slot again, the renderer reuses the GPU primitive; changed glyph
UVs, a rebuilt text cache, a different quad order, and dynamic sprites rebuild
it. The ordering-table insertion and frame statistics still run every frame.

On the unchanged `cube_test` scene, the second 600-frame average changed from
Update 2.924 ms, boundary 0.187 ms, Draw 8.382 ms to Update 2.924 ms,
boundary 0.187 ms, Draw 7.867 ms. Combined Update + Draw fell from 11.306
to 10.791 ms. The clean 1800-to-2400 interval used 600 VBlanks for 600 game
frames. First-frame packet count remained 180, with 3 sprites, 2 shapes,
5 texts, 82 glyph quads and no failure. The user confirmed that UI placement
and glyphs remained correct and the FPS/Update/Render numbers kept changing.

The PS-X EXE grew from 790,528 to 792,576 bytes and static RAM from 839,104
to 843,200 bytes. Heap capacity decreased from 1,126,976 to 1,122,880
bytes. The full host suite passed, including the expected negative stream
fixture exit 73. Generated Core was restored to the unprofiled baseline and
rebuilt with a fresh native object cache.

Evidence: build manifest
`C:\dev\helworks\builds\helengine-ps1\20260923-195221-925-Release-cc45522dd83946cbbe74dc371c76431e\manifest.json`,
guest log `C:\dev\helworks\helengine-ps1\out\frame-context-quad-reuse-guest.log`,
and host summary
`C:\dev\helworks\builds\helengine-ps1\host-tests-20260923-195119-527-a4df173789354133b9a5928d25f7bcd5\summary.json`.

## Authored Release `-O3` comparison (2026-09-23)

With unchanged `cube_test` content and the frame-context glyph packet cache,
raising the generated Core and PS1 frame renderers from `-O2` to `-O3`
changed the second 600-frame average from Update 2.924 ms, boundary
0.187 ms, Draw 7.867 ms to Update 2.727 ms, boundary 0.131 ms, Draw
7.676 ms. Combined Update + Draw fell from 10.791 to 10.403 ms. The clean
1800-to-2400 interval still used 600 VBlanks for 600 game frames. The UI
report retained 3 sprites, 2 shapes, 5 texts, 82 glyph quads and no failure.
The user confirmed the UI, glyphs and cube motion are visually correct.

The PS-X EXE grew from 792,576 to 817,152 bytes and static RAM from
843,200 to 867,776 bytes. Heap capacity fell from 1,122,880 to 1,098,304
bytes. The authored native build completed with a fresh object cache.
The host suite had passed for the same source before this Makefile-only
change; this flag comparison was checked with the native build and guest run.

Evidence: build manifest
`C:\dev\helworks\builds\helengine-ps1\20260923-200639-911-Release-610c4096277043008e40f614e3d10e64\manifest.json`
and guest log `C:\dev\helworks\helengine-ps1\out\o3-trial-guest.log`.

## Cross-platform CPU profiler identity build (2026-09-24)

The shared Core sink now receives the concrete updateable instance during registration. The PS1 adapter resolves native RTTI once at registration, caches the instance identity and stable name hash in bounded storage, and performs only the cached identity lookup in each timed sample. The shared-engine change was built from `15fa776319b9a263bfcb491eafd1d49a420bb0c7`; the PS1 identity adapter is commit `a74ee07`.

The strict matched authored Release pair used the same 3,016 copied inputs with zero SHA-256 mismatches:

- Profile off: `C:\dev\helworks\builds\helengine-ps1\20260924-000229-118-Release-4b4b8ebc714646cf9bd3839c3b5b0637\manifest.json` — PS-X 821,248 bytes, static 871,872 bytes, heap 1,094,208 bytes.
- Profile on: `C:\dev\helworks\builds\helengine-ps1\20260923-235426-100-Release-90362d95fa1f49808b6ef4cc1938ca9c\manifest.json` — PS-X 827,392 bytes, static 878,024 bytes, heap 1,088,056 bytes.
- The profiler delta is +6,144 PS-X bytes, +6,152 static bytes, and -6,152 heap bytes.

The profile-on guest window reported 600 frames (601–1200), Update 2.971 ms, boundary 0.171 ms, Draw 7.864 ms and between 6.179 ms, with dropped=0, resets=0 and clock_faults=0. It emitted concrete component rows for `DemoDiscOrbitCameraComponent`, `CubeTestSpinComponent`, `ViewportComponent`, `FPSComponent`, `DemoDiscReturnToMenuComponent`, `DemoDiscLightToggleComponent` and `DemoDiscDebugSceneLabelComponent`; repeated `ViewportComponent` rows retained distinct owner IDs.

The matched profile-off guest averaged Update 2.727 ms, boundary 0.137 ms and Draw 7.670 ms at sequence 1200 (the earlier 600-frame sample was 2.709/0.167/7.702 ms). The profile-on observer cost in this pair was therefore +0.244 ms Update, +0.034 ms boundary and +0.194 ms Draw (+0.438 ms Update+Draw, +0.472 ms including boundary). The off cadence advanced VBlank 654 to 1256 over the 600-frame observation; a later profile-on window advanced 1310 to 1912 for 600 frames (603 VBlanks). The clean steady off interval used for the baseline was 600 VBlanks for 600 frames. These cadence readings include the emulator's fixed-rate scheduling and are reported as evidence rather than a GPU timing measurement.

The periodic report itself causes a visible hitch in DuckStation. It prints 25 BIOS TTY lines (about 2.6 KB) synchronously before the next sampled frame. The first completed window ended at VBlank 1269 and the next began at 1310 (41 VBlanks); subsequent report gaps were 30 VBlanks (1912 to 1942 and 2562 to 2592). This output pause is excluded from the measured 600-frame CPU averages but remains an observer effect during play. The ordinary unprofiled build emits no `HE_CPU_*` lines.

The full host suite passed in `C:\dev\helworks\builds\helengine-ps1\host-tests-20260924-000042-890-abbbf3854ad04daa957eb851ee81a769\summary.json`: all compile/run exit codes were 0, the expected embedded-stream negative fixture returned 73, and Python checks returned 0. The final guest evidence is `C:\dev\helworks\helengine-ps1\out\cpu-profile-identity-final-guest.log`.

Helena confirmed that the UI, text and cube motion remain visually correct in the final profiled DuckStation build.

## Authored UI subphases and sprite geometry cache (2026-09-24)

The opt-in PS1 CPU profile now separates text, sprite and rounded-rect visits, sprite geometry, and textured versus shape packet runs. The stage samples nest inside `ui_queue_visit` and `ui_packet_flush`; they must not be added to their parent totals. Measuring packet runs instead of every packet kept the observer cost of the new stages near 0.12 ms of Draw in this scene. Before optimization, a 600-frame profiled window measured `ui_queue_visit` 3.401 ms, `ui_sprite_draw` 1.146 ms, `ui_sprite_geometry` 1.105 ms, `ui_text_draw` 1.332 ms, `ui_shape_draw` 0.810 ms, and `ui_packet_flush` 2.378 ms (textured packets 1.348 ms, shape packets 1.025 ms). Sprite geometry was the largest individual visit cost. PS1 now retains the four screen-space vertices for a sprite while its drawable, texture, transform, size, source rectangle, texture dimensions and viewport remain unchanged. A changed input rebuilds the quad. The cache has eight fixed slots and does not allocate per frame. Textured packet insertion also recognizes the just-reserved slot without address division, while preserving the full ownership check for other insertion orders.

After the cache, the comparable profiled window measured Draw 7.181 ms versus 8.014 ms before, `ui_queue_visit` 2.561 ms versus 3.401 ms, `ui_sprite_draw` 0.261 ms versus 1.146 ms, and `ui_sprite_geometry` 0.223 ms versus 1.105 ms. `ui_packet_flush` remained 2.378 ms. The profile window reported dropped=0, resets=0 and clock_faults=0.

In normal Release builds with profiling off and the same generated Core and authored content, the 600-frame sequence-1200 averages changed from Update 2.727 ms and Draw 7.670 ms to Update 2.728 ms and Draw 6.790 ms. Draw improved by 0.880 ms; Update + Draw fell from 10.397 to 9.518 ms. The final 600-frame steady interval used 600 VBlanks. At sequence 1200 the UI still reported 3 sprites, 2 shapes, 5 texts, 100 glyph quads, 103 submitted sprites, 298 triangles and failure=none. The ordinary build emitted no `HE_CPU_*` lines. Helena confirmed its sprites, text, shapes and cube motion remained visually correct.

The normal PS-X payload grew from 821,248 to 823,296 bytes; static RAM grew from 871,872 to 873,920 bytes and heap capacity fell from 1,094,208 to 1,092,160 bytes. The copied-input comparison found only six intended PS1 source paths (five modified and the new cache header); generated Core and authored content inputs were unchanged. The full host suite passed, including the expected negative embedded-stream fixture exit 73.

Evidence: normal baseline manifest `C:\dev\helworks\builds\helengine-ps1\20260924-000229-118-Release-4b4b8ebc714646cf9bd3839c3b5b0637\manifest.json`, final normal manifest `C:\dev\helworks\builds\helengine-ps1\20260924-112112-256-Release-e185f973c9ba4e2eb58e1ae4f73facdf\manifest.json`, profiled pre-cache manifest `C:\dev\helworks\builds\helengine-ps1\20260924-111152-730-Release-8b3d558dd1984a5f819b0c6a40699ca5\manifest.json`, final profiled manifest `C:\dev\helworks\builds\helengine-ps1\20260924-111829-675-Release-5b0f16a3ce25408fa3bdb787c7df38ca\manifest.json`, guest logs `out\ui-sprite-geometry-profile-guest.log`, `out\ui-sprite-cache-guest.log`, `out\ui-sprite-cache-off-guest.log`, and host summary `C:\dev\helworks\builds\helengine-ps1\host-tests-20260924-112833-766-7c6496aaaf3240619e96d68591920407\summary.json`.

## Scene-label Update lookup (2026-09-24)

The DemoDisc scene-label factory already creates the label entity, but its runtime component did not persist that identity. `DemoDiscDebugSceneLabelComponent` recursively searched the UI descendants for a text component with render order 7 on every Update, including Release where the label remains hidden. Demodisc commit `31c66ac` makes the factory author a `SceneEntityReference` and resolves the runtime entity once. Existing scene files without the new field use one compatibility lookup after loading, then retain the resolved entity. The `cube_test.helen` source scene was exported unchanged for this measurement, so this run exercises that compatibility path rather than a regenerated authored ID.

In matched normal Release builds with profiling off, the 600-frame sequence-1200 average changed from Update 2.728 ms, boundary 0.137 ms, Draw 6.790 ms to Update 2.280 ms, boundary 0.190 ms, Draw 6.784 ms. Update improved by 0.448 ms; Update + Draw fell from 9.518 to 9.064 ms. The final interval still used 600 VBlanks for 600 frames. UI counters stayed at 3 sprites, 2 shapes, 5 texts, 100 glyph quads, 103 submitted sprites and 298 triangles, with failure=none. The PS-X payload, static footprint and heap capacity were unchanged at 823,296, 873,920 and 1,092,160 bytes respectively; the sampled heap end was also unchanged at `0x80105428`. The copied-input comparison found six changed inputs: the cooked scene record and five generated files for the updated component.

The DemoDisc editor test project compiled and its selected test passed. The authored PS1 build exited successfully, generated the updated component and deserializer, and booted through sequence 1200 without a fatal log marker. The newly opened DuckStation process is recorded in `last-launch.json`; this run did not use the CPU profiler.

Evidence: prior manifest `C:\dev\helworks\builds\helengine-ps1\20260924-112112-256-Release-e185f973c9ba4e2eb58e1ae4f73facdf\manifest.json`, new manifest `C:\dev\helworks\builds\helengine-ps1\label-reference-authored-20260924\editor-output\manifest.json`, guest log `out\label-reference-guest.log`, and selected editor test log `out\label-reference-test.stdout.log`.

## Sparse authored scene references (2026-09-24)

The shared Core now collects only referenced scene IDs while decoding, binds them
after the complete scene has loaded and before component initialization, and
releases the temporary requests. DemoDisc now persists fixed UI, camera, stage,
and menu references in its authored scenes. The new `cube_test` export kept the
same 23 entities; its scene file grew from 50,679 to 50,826 bytes. The PS1
builder omits 10 unused generated Batch2D sources after checking for external
references, while the PS1 renderer continues to use its own packet path.

The authored Release build completed with editor and native exit codes 0. The
PS-X payload was 831,488 bytes, static RAM 882,136 bytes, and heap capacity
1,083,944 bytes. Against the preceding scene-label build (823,296 / 873,920 /
1,092,160 bytes), those values changed by +8,192 / +8,216 / -8,216 bytes.
The embedded archive contained 11 entries and 73,912 bytes. This build used
the shared engine checkout at `0d2d1f7ebc21396d26a3a0b08386aef5d7140132`
with the subsequent Core ownership and fatal-only batch-writer edits present
as dirty source (later committed as `ea1f6218` and `29bf352a`); codegen was
`941881c8bb7c28459cf0f7b73a0d48b37dbe2925`. The exported scene and
replaced scene hashes matched at
`622b06a1dd55cba8556c24913d65f7a23a74988643fe89b6bfad9e8862564e05`.

Focused shared-Core tests passed 77/77, PS1 builder tests 23/23, and focused
DemoDisc editor tests 7/7. The PS1 host suite passed, including the expected
negative embedded-stream exit 73. The full editor suite was stopped after
unrelated failures, including an older version-24 editor asset while the
current engine requires version 25.

DuckStation booted `cube_test`, reached frame 1200 without `PS1_CORE_FATAL`,
and reported the expected 1 camera, 1 drawable, 9 updateables, 3 sprites,
2 shapes, 5 texts, and 100 glyph quads at that frame. The steady 600-frame
interval used 600 VBlanks. Its sequence-1200 average was Update 2.043 ms,
boundary 0.320 ms, Draw 7.178 ms, and Update + Draw 9.221 ms. The prior
scene-label run measured 2.280 / 0.190 / 6.784 / 9.064 ms respectively;
this is an observational comparison because the scene was regenerated and
the shared engine checkout advanced. The sampled heap end changed from
`0x80105428` to `0x80107498`; after accounting for the 8,216-byte static
growth, the sampled dynamic heap use increased by 88 bytes. Helena confirmed
that the UI and cube still look correct and read about 2.0 ms Update and
6.7 ms Render on the live panel.

Evidence: authored report
`C:\dev\helworks\builds\helengine-ps1\sparse-references-20260924-r3\authored-build.json`,
native manifest
`C:\dev\helworks\builds\helengine-ps1\20260924-152112-095-Release-8744848aa53c449fb808c948a8969beb\manifest.json`,
and host summary
`C:\dev\helworks\builds\helengine-ps1\host-tests-20260924-142549-655-3f64f317ef2c41b8ba03db3330ed1b04\summary.json`.
The launch record and copied guest log are
`C:\dev\helworks\builds\helengine-ps1\sparse-references-20260924-r3\editor-output\last-launch.json`
and `C:\dev\helworks\builds\helengine-ps1\sparse-references-20260924-r3\editor-output\duckstation-guest-snapshot-20260924.log`.
