# PS1 v0.1 build and launch runbook

## Authored cube-test subset checkpoint

The .NET 9 utility serializes the existing scene's camera and cube subset
and writes a provenance report. It does not cook assets, generate Core, or
produce a PS1 executable. See [GeneratedCorePrerequisite.md](GeneratedCorePrerequisite.md)
for the runtime prerequisite discovered during implementation.

Build current editor dependencies into the external build directory first:

```powershell
dotnet build C:\dev\helworks\helengine\engine\helengine.editor\helengine.editor.csproj `
  --artifacts-path C:\dev\helworks\builds\helengine-ps1\current-editor-dependencies `
  -p:UseCommonOutputDirectory=false -p:RunAnalyzers=false

$engineBinaries = 'C:\dev\helworks\builds\helengine-ps1\current-editor-dependencies\bin\helengine.editor\debug'
.\scripts\generate-authored-fixture.ps1 `
  -ProjectRoot C:\dev\helprojs\demodisc `
  -OutputRoot C:\dev\helworks\builds\helengine-ps1\cube-test-subset `
  -EngineBinaryRoot $engineBinaries
```

The wrapper uses a fresh external build directory and runs that exact built
assembly. Output consists of `subset.helen` and `report.json`; the report records
the source snapshot hash, retained payloads, dependencies, decoded camera/spin,
and requested and loaded engine assembly paths and hashes. Use current engine
binaries containing the shared scene persistence walker. Outputs must be outside
the authoring project. The source scene is read without modification.

Run the focused integration tests against the same engine binaries:

```powershell
$env:HELENGINE_PS1_ENGINE_BINARY_ROOT = $engineBinaries
dotnet test .\tests\helengine.ps1.builder.tests\helengine.ps1.builder.tests.csproj `
  --artifacts-path C:\dev\helworks\builds\helengine-ps1\subset-tests `
  "-p:EngineBinaryRoot=$engineBinaries"
```

This repository builds a standalone PlayStation 1 console executable. The
artifact is a PS-X EXE (`helengine_ps1.ps-exe`), not a Windows `.exe`. Build
products, copied SDK sources, logs, manifests, and test output belong below
`C:\dev\helworks\builds\helengine-ps1`; they are not source files.

## Prerequisites and locked inputs

Use Windows PowerShell 5 or later, Git, and a running Docker daemon. Run the
commands from `C:\dev\helworks\helengine-ps1-impl`. The wrappers create a new
directory for each invocation, so do not reuse an older `out` directory when
recording evidence.

The dependency record is [toolchain.lock.json](../toolchain.lock.json):

| Input | Locked value |
| --- | --- |
| Nugget/PSYQo | `625da805c4a9bfd56dd9b2b17409805c0591dd42` |
| `third_party/uC-sdk` | `69e06871824e2d62069487a7426ded09090ceb69` |
| `third_party/psxlua` | `abed030e686b4e34987851bd0a028c93b1f73967` |
| `third_party/xmake-psx` | `e02b8cb6fdaf9258e74b80de806c8ac23222d41e` |
| Docker image | `ghcr.io/grumpycoders/pcsx-redux-build@sha256:4f99ff0dd972096280cd6faf27c4255cc2bcf932397f8f9276ee22b6b1a67555` |
| MIPS GCC | `16.2.0` |
| Binutils | `2.47.20260726` |
| GNU Make | `4.4.1` |
| host GCC | `15.2.0` |
| Python | `3.14.4` |

The tracked SDK correction is
`patches/nugget/0001-frame-completion.patch`. A successful manifest records
the lock-file and patch hashes as well as the copied-input hashes. A build
must stop before writing a manifest when the SDK pin, patch, Docker build, or
retail-memory check fails.

## Fresh builds

Run each configuration in its own fresh invocation. Task 4 evidence uses the
rotation mode; substitute another diagnostic mode only when documenting that
mode explicitly.

```powershell
.\scripts\build.ps1 -Configuration SmallDebug -DemoMode Rotate
.\scripts\build.ps1 -Configuration Release -DemoMode Rotate
# Use a separate fresh directory when checking completion diagnostics.
.\scripts\build.ps1 -Configuration Release -DemoMode CompletionStress
```

The command prints the absolute `manifest.json` path. Preserve that path with
the build log. The manifest identifies the source revision and dirty state,
configuration, mode, copied inputs, exact artifact SHA-256 values, and the
measured retail-memory result. `out\` contains the matching unstripped ELF,
PS-X EXE, and linker map.

The accepted diagnostic modes are:

| Mode | Purpose |
| --- | --- |
| `Clear` | Exercise startup, the two display buffers, completion, and first presentation. |
| `Static` | Inspect the six flat-colored cube faces, fixed poses, and winding rejection. |
| `Rotate` | Accumulate wrap-safe elapsed time and observe bounded rotation over a soak. |
| `DepthOrder` | Insert far and near diagnostic triangles in both insertion orders. |
| `InvalidPose` | Reject the deliberately invalid near-plane pose before queueing a cube. |
| `Capacity` | Prove bounded packet reservation fails before a write. |
| `CompletionStress` | Exercise a bounded set of larger opaque packets and completion diagnostics. |

## Host tests and memory validation

Normal demos emit startup and failure diagnostics only. Periodic BIOS text
output stalls the emulated console and distorts frame pacing. To capture
render statistics every 120 scene updates, explicitly enable them:

```powershell
.\scripts\build.ps1 -Configuration Release -DemoMode Rotate -SdkDefine PS1_PERIODIC_DIAGNOSTICS
```

`CompletionStress` retains periodic diagnostics automatically. Diagnostic
captures are unsuitable for measuring normal demo frame pacing.

Run the host smoke, renderer-policy, shared-ownership, hashing, runtime-clock,
and Python memory-fixture tests through the pinned image:

```powershell
.\scripts\test.ps1
```

Keep the returned `summary.json` and its referenced logs under the build root.
Each build invocation runs the memory checker against its ELF and PS-X EXE and
records the static footprint, heap start and end/capacity, the 64 KiB stack
reservation, and packet-context accounting in the manifest. The PSYQo display
rectangles are `(0,0)-(320,240)` and `(0,256)-(320,496)`; context storage
already counted in BSS is counted once in the recorded summary. Final Task 4
manifests and the host-test summary are listed in [Validation.md](Validation.md).

## DuckStation portable setup

The supplied DuckStation build is the runtime reference because the supplied
PCSX-Redux build does not implement GP0(1Fh), the ordered GPU completion marker
used by the patched SDK. Do not weaken that fence for emulator compatibility.

Make a versioned, isolated copy of the supplied DuckStation directory below
the build root. Place `portable.txt` beside the DuckStation executable, and
place the supplied OpenBIOS image at `bios\openbios.bin`. The OpenBIOS image
used by the controller evidence has SHA-256
`713ea2aed58606282b3ff5e91d77f8c5892ea36f5adcd279d2c7dd26604a625d`.
The launcher requires this exact SHA-256 before starting DuckStation. Do not
download or add a Sony BIOS.

The portable `settings.ini` must select the relative BIOS filename and the
native validation settings:

```ini
[BIOS]
PathNTSCU=openbios.bin
PathNTSCJ=openbios.bin
PathPAL=openbios.bin

[GPU]
Renderer=Software
ResolutionScale=1
PGXPEnable=false

[Console]
Enable8MBRAM=false

[CPU]
OverclockEnable=false
```

The relative filename is intentional. An absolute BIOS path is combined with
DuckStation's BIOS root and fails silently in this setup. Keep the portable
directory and its logs isolated from the original distribution.

## Manifest-based launch

Pass the manifest produced by the build and the executable from the isolated
portable directory:

```powershell
.\scripts\launch_in_emulator.ps1 `
  -Manifest C:\absolute\path\to\manifest.json `
  -EmulatorPath C:\absolute\path\to\portable\duckstation-qt-x64-ReleaseLTCG.exe
```

The launcher verifies every artifact path and SHA-256 from the manifest, then
requires `portable.txt`, `settings.ini`, `bios\openbios.bin`, and the native
settings above beside the specified DuckStation executable. It launches with
the verified PS-X EXE as a positional argument using `-batch -nogui
-earlyconsole`, from the portable directory, and records the executable,
emulator, settings, BIOS, manifest hashes, arguments, process ID, and stdout
and stderr log paths in `last-launch.json` beside the manifest. It never
selects a newest artifact and never falls back to a stale executable.

Do not capture screenshots of the user's windows. Guest logs, manifest hashes,
debugger memory, and permitted emulator exports are the evidence sources.

## Debugger attach

For a debugger run, enable the verified DuckStation GDB settings in the
portable `settings.ini`:

```ini
[Debug]
EnableGDBServer=true
GDBServerPort=23456
```

Connect to `127.0.0.1:23456` only after the launcher records a running process.
The installed build was checked with the following remote-serial-protocol
operations: `?` for the stop state, `mADDR,LENGTH` for memory reads,
`Z0,ADDR,4` and `z0,ADDR,4` for execution breakpoints, `c` to continue, and
byte `03` to interrupt. Resolve allocator and diagnostic addresses from the
current ELF/map; never reuse addresses from an older build. Remove all
breakpoints and stop the process after collecting evidence.

## Outputs and repeat steps

Build and test runs create timestamped directories under
`C:\dev\helworks\builds\helengine-ps1`. The launch wrapper writes logs and
`last-launch.json` beside the selected manifest. Preserve those paths in the
acceptance table. To repeat a clean emulator boot, stop the process ID from
`last-launch.json` and invoke the same explicit manifest command again. A
cold emulator launch is not a reset observation; five actual reset results
and retail hardware timing remain pending unless separately evidenced.

The emulator evidence currently verifies guest markers, packet chains, GDB
memory, and allocator/stack diagnostics. It does not prove framebuffer pixel
appearance. See [Validation.md](Validation.md) for the acceptance table and
the exact pending limits.
