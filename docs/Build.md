# PS1 v0.1 build and launch runbook

## Workspace layout

Work directly on `main` in `C:\dev\helworks\helengine-ps1`; do not create
PS1 branches, worktrees or separate engine checkouts. PS1 uses the existing
engine checkout at `C:\dev\helworks\helengine`. The builder, sample exporter
and exporter tests default to that sibling path. Pass that same path as
`-EngineSourceRoot` to the authored build wrapper and build the editor from it.
Record the actual engine revision used for each build. Do not change the shared
engine checkout's branch or revision as part of PS1 setup.

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
commands from `C:\dev\helworks\helengine-ps1`. The wrappers create a new
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

## Authored cube_test build

The authored build embeds the exported camera-and-cube subset and drives it
through generated Core. It runs the engine's headless build graph with the PS1
platform builder in `builder/`, then the same pinned native toolchain as the
diagnostic builds. Inputs: the demodisc project (never modified), the Task 1
subset written by `scripts/generate-authored-fixture.ps1` with its
`report.json`, the engine checkout, and an editor app built from that checkout.

```powershell
.\scripts\build-authored.ps1 `
  -ProjectRoot C:\dev\helprojs\demodisc `
  -SubsetScenePath C:\dev\helworks\builds\helengine-ps1\cube-test-subset-fov\subset.helen `
  -EngineSourceRoot C:\dev\helworks\helengine `
  -EditorAppPath <editor-app-build>\bin\helengine.editor.app\debug\helengine.editor.app.dll `
  -OutputRoot C:\dev\helworks\builds\helengine-ps1\<fresh-directory>
```

The generator comes from the engine's own `engine/vendor/csharpcodegen`
submodule, so PS1 transpiles with the codegen that engine commit pins rather
than one chosen here. Initialise it first with
`git -C <engine> submodule update --init --recursive`. The wrapper publishes it
into a `codegen` directory beside the editor assembly, which is the first place
the editor looks, and records the resolved commit in `authored-build.json` as
`codegen_commit`.

`-CodegenRoot` still exists and overrides that pin, for bisecting a generator
change before it lands. `authored-build.json` records
`codegen_is_engine_pinned: false` when it is used, so an overridden build cannot
be mistaken for a pinned one. Prefer bumping the submodule in the engine.

Letting the editor resolve codegen on its own also works: it builds the
submodule on demand into `HELENGINE_BUILD_WORKSPACE_ROOT`. This wrapper points
that root at a fresh per-build directory, so the on-demand cache never hits and
republishes roughly 37 MB every run, measured at about 49 seconds. Publishing it
ourselves costs about 3 seconds.

The wrapper stages a copy of the project's `assets` and `settings`, replaces
the scene with the subset, prunes project code to the retained script
components (recorded in `code-prune-report.json`), writes a build-local
`user_settings/build_config.json` and platform registry, and invokes the
editor with `--build ps1 --build-profile release`. The editor regenerates Core
and the gameplay module, cooks the scene, material and engine cube, stages the
declared files, and calls the builder, which writes the scene catalog and runs:

```powershell
.\scripts\build.ps1 -Configuration Release -Authored `
  -GeneratedCoreRoot <workspace>\generated-core `
  -AuthoredContentRoot <workspace>\builder\ps1-authored-content `
  -AuthoredDeclarations <workspace>\builder\ps1-authored-declarations.json
```

`scripts/embed-authored-assets.py` packs the declared files into a sorted,
four-byte-aligned little-endian archive that the Makefile links into `.rodata`
through an `incbin` stub, and the retail memory check accounts for it. The
executable, link map and manifest are copied into the editor output directory
with `native-build-run-root.txt` naming the build run.

## Static main-menu export and build

`scripts/build-static-menu.ps1` extracts the committed demodisc menu scene from
revision `5cc124eec06b8db729b2f9b15d99d26cb1ca8cc3` and verifies its Git blob
ID before exporting it. The extraction streams Git's binary output directly to
disk, preserving the serialized `HELE` bytes. The exporter writes
`source/DemoDiscMainMenu.helen`, `export/scene.helen`, and
`export/report.json` beneath the fresh output directory. `-ExportOnly` stops
after that step and does not require an editor assembly:

```powershell
.\scripts\build-static-menu.ps1 `
  -ProjectRoot C:\dev\helprojs\demodisc `
  -EngineSourceRoot C:\dev\helworks\helengine `
  -OutputRoot C:\dev\helworks\builds\helengine-ps1\static-menu-export-check `
  -ExportOnly
```

For a full PS1 build, pass the editor app built from the same shared engine
checkout and copied or built beneath the PS1 build root. The authored build
publishes codegen beside the editor DLL, so the wrappers reject editor paths
outside that root before creating output. The wrapper stages the exported scene at
`scenes/DemoDiscMainMenu.helen`, selecting scene ID
`Scenes/DemoDiscMainMenu.helen`, then calls `build-authored.ps1`:

```powershell
.\scripts\build-static-menu.ps1 `
  -ProjectRoot C:\dev\helprojs\demodisc `
  -EngineSourceRoot C:\dev\helworks\helengine `
  -EditorAppPath C:\dev\helworks\builds\helengine-ps1\<editor-build>\helengine.editor.app.dll `
  -OutputRoot C:\dev\helworks\builds\helengine-ps1\static-menu-build
```

The full run writes `authored/authored-build.json` and
`authored/staged-input-hashes.json` alongside the exported input. The hash file
lists every final staged `assets` and `settings` file by relative path and
SHA-256. The authored build report records that hash file and the actual engine
revision. Script pruning follows the export report, so a visual-only scene with
no retained project scripts builds with zero project scripts while scenes with
script components still require each referenced class to exist.

Before native compilation, the PS1 builder validates
`generated-core/cpp-conversion-report.json`. It requires the v2 schema,
consistent diagnostic metadata and counters, recognized severities, and zero
conversion errors. A missing or malformed report, metadata mismatch, unknown
severity, or any conversion error stops the build. Authored builds retain
codegen's default fail-on-error behavior.

Authored mode never falls back to the diagnostic cube: a missing catalog,
undeclared asset, unsupported material, unsafe pose or packet failure stops
the boot with a `PS1_*` marker. Periodic frame diagnostics are opt-in through
`-SdkDefine PS1_PERIODIC_DIAGNOSTICS`; the default build reports the first
frame and one cadence checkpoint after 600 updates.

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
the verified PS-X EXE as a positional argument from the portable directory.
Unattended runs use `-batch -nogui -earlyconsole`; add `-ShowWindow` for the
full DuckStation GUI, which omits `-nogui`. Set `[Display] ShowFPS = true` in
the portable `settings.ini` to show DuckStation's own FPS and VPS counters.
The launcher records the executable,
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

## Whole authored scene on the override tree

Since 2026-09-19 the demodisc authors platform overrides on scope paths
(Common, platform groups, platforms) and the engine packager resolves them
against the staged project's `settings/platform-groups.json`. The whole
`cube_test` scene therefore builds for PS1 without any PS1-specific
authoring. Since 2026-09-21 the groups include `hd`, `sd`, and
`sd/dual-screen`. PS1 is a member of `sd`, so it receives the shared content
plus the single-screen presentation: camera, sun, cube, instruction overlay,
console camera/light blueprint and standard UI kit. The dual-screen
bottom-screen presentation and its chrome remain scoped to `sd/dual-screen`
and do not enter PS1. The full export report resolves the PS1 scene from
`common` and `sd`; the dual-screen presentation is filtered out.
Export the scene unchanged with `helengine.ps1.samplegen --full <scene>
<output-root>` (the report lists the authored scope paths) and build with:

```powershell
.\scripts\build-authored.ps1 `
  -ProjectRoot C:\dev\helprojs\demodisc `
  -SubsetScenePath <export-root>\scene.helen `
  -EngineSourceRoot C:\dev\helworks\helengine `
  -EditorAppPath <editor-app-build>\bin\helengine.editor.app\debug\helengine.editor.app.dll `
  -OutputRoot C:\dev\helworks\builds\helengine-ps1\<fresh-directory> `
  -RetainAllRuntimeScripts `
  -ExcludeScriptClasses @('SoftwareBvh','SoftwareMaterialKind','SoftwareModelComponent','SoftwarePathTracer','SoftwarePathTracerComponent','SoftwareScatteringMaterial','SoftwareSpecularScattering','SoftwareTraceScene','FooterIdentityMarqueeComponent')
```

`-RetainAllRuntimeScripts` is required because the component pruner keeps
only the classes the scene names, and `DemoDiscLightToggleComponent` now
depends on `DemoDiscDirectionalLightToggleState`, a helper the pruner does
not follow. The excluded files are the software path tracer, which uses
exception recovery the PS1 runtime profile rejects (`CPP1001`); they form a
closed family with no other runtime references. Pass the exclusions as a
PowerShell array through `-Command`; `-File` hands the list over as one
comma-joined string and the script rejects it.
`FooterIdentityMarqueeComponent` belongs to the menu, which `cube_test` does
not load. Keep it excluded from this scene build; the shared engine checkout
may change between builds, so record its actual revision in the build evidence.
For detailed CPU timing during a PS1 authored Release build, pass `-CpuProfile`
to `scripts/build.ps1` after the authored project has generated Core. The switch
is valid only with `-Authored -Configuration Release`; use a separate
`-NativeObjectCacheRoot` for profile-on and profile-off measurements. The guest
writes one `HE_CPU_WINDOW` report per 600 sampled frames, followed by phase and
component rows. The profile observes CPU submission work and does not measure
the GPU rasterizer.

As of csharpcodegen `46b6371` (engine revision `881f9bf0`) the generator's
freestanding math runtime defines `sin`, `cos`, `sqrt` and friends as global
symbols. The PS1 Makefile leaves those global wrappers to generated Core and
does not also link the duplicate SDK wrappers or the old `Ps1Math.cpp`
shim; the remaining fdlibm internals are still linked for the generated
implementation. This keeps the build on the engine checkout's codegen submodule,
so no `-CodegenRoot` override is required. The native link
still reports the existing soft/hard-float ABI warnings from psyqo and the
authored archive; they do not prevent the PS-X EXE from linking. Build
`authored-groups-10` (engine `dd43413f`) is the earlier reference: 620 KB
`ps-exe`, scene entity names in the ELF confirm one `CubeTestSun`, one
`CubeTestCube`, one `CubeTestCamera` and no DS screen cameras.
