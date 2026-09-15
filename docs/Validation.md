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
| Reset | Any verified artifact / pending | Perform five actual emulator reset operations or a documented console reset mechanism. | Cold boots are not reset observations. No five-reset evidence is claimed. | Pending | Pending |
| Retail hardware | Same hashed Release EXE / pending | Load through the available homebrew method and record console, region, loader, video, rotation, resets, and stress behavior. | No retail console was available for this task. | Pending | Pending |

The exact VRAM layout is the pinned PSYQo configuration: 320x240 progressive
NTSC, 15-bit RGB stored in 16-bit framebuffer storage, with display buffers at
Y=0 and Y=256. The corrected SDK ordering-table clear is included in the
official Static evidence; before the correction the target chain had no
reachable triangle packets.

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
