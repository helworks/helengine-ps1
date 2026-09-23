# Cross-Platform CPU Profiler Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking. Helena requested one PS1 agent, so execute natively without subagents or an independent reviewer unless she explicitly opts in.

**Goal:** Produce detailed 600-frame CPU timing reports for PS1, using Core probes that N64 can adopt later.

**Architecture:** A nullable profiling sink in shared Core captures fixed phase durations and individual updateables. A fixed-capacity PS1 collector and native adapter aggregate Core and renderer timings, then print a key/value report outside sampled frames. A PS1 build switch enables the sink and records that choice in the manifest.

**Tech Stack:** C#/.NET 9 Core and xUnit, csharpcodegen generated C++, PS1 C++20/PSYQo, PowerShell build scripts, DuckStation guest logs.

**Spec:** `docs/superpowers/specs/2026-09-23-cross-platform-cpu-profiler-design.md` (PS1 repository, commit `7782748`).

## Global Constraints

- Work directly on PS1 `main`; do not create a branch or worktree. Keep shell working directories inside `C:\dev\helworks\helengine-ps1` and use absolute paths for external inputs.
- Shared Core lives in `C:\dev\helworks\helengine`; do not switch, reset, clone, or silently repin it. Record its revision and dirty state before builds. Stage only profiler files when committing there; it currently has unrelated changes.
- Do not edit the N64 repository. Its owner can consume the shared contract in a later change.
- Keep timed frames free of profiler logging and heap allocation. Default PS1 Release has no sink, and the FPS overlay remains unchanged.
- PS1 HSync timing has a 64-microsecond quantum. The log calls CPU Draw submission time CPU time, never GPU raster time.
- Put artifacts under `C:\dev\helworks\builds\helengine-ps1`; use a fresh native object cache for each comparison. Do not stage the existing unrelated `scripts/build.ps1` modifications wholesale without reviewing them.
- Before launching DuckStation, honor the standing user authorization to launch through the shell only. Read logs; ask Helena for visual judgment. Do not use Windows computer-use or screen capture.

## Review Focus

Each case below has a named behavior test in its owning task.

1. A backwards timestamp discards the current window with a clock marker, never an enormous unsigned duration (Tasks 1 and 3).
2. Two instances with the same type name and owner retain separate rows, keyed by object identity (Tasks 2 and 3).
3. An updateable inserted, removed, or reordered mid-window discards mixed samples and reports a reset (Tasks 2 and 3).
4. More updateables than the PS1 fixed capacity increment `dropped` instead of corrupting adjacent storage (Task 3).
5. Default Release performs no timestamp calls and its manifest says profiling is off; a profile build records profiling on (Tasks 1 and 4).

## File map

| Area | File | Responsibility |
|---|---|---|
| Shared Core | `C:\dev\helworks\helengine\engine\helengine.core\diagnostics\IRuntimeCpuProfileSink.cs` | Host clock, stage/component records, type legend, clock fault contract |
| Shared Core | `C:\dev\helworks\helengine\engine\helengine.core\diagnostics\RuntimeCpuProfileStage.cs` | Stable numeric Core stage IDs |
| Shared Core | `...\helengine.core\CoreInitializationOptions.cs`, `...\helengine.core\Core.cs`, `...\helengine.core\managers\ObjectManager.cs` | Optional sink and timed Core call sites |
| Shared test | `C:\dev\helworks\helengine\engine\helengine.editor.tests\RuntimeCpuProfileProbeTests.cs` | Deterministic clock, stage, identity, disabled-path tests |
| PS1 | `src/platform/ps1/profiling/Ps1CpuProfileWindow.hpp/.cpp` | Bounded integer accumulators, membership/reset tracking, ms formatting |
| PS1 | `src/platform/ps1/profiling/Ps1CpuProfileSink.hpp/.cpp` | Generated Core interface adapter, HSync clock, type legend, report emission |
| PS1 | `src/platform/ps1/Ps1AuthoredScene.cpp/.hpp`, `Ps1RenderManager3D.cpp/.hpp`, `Ps1RenderManager2D.cpp/.hpp` | Frame and render-phase boundaries |
| PS1 | `Makefile`, `scripts/build.ps1` | Explicit `-CpuProfile` build mode and manifest field |
| PS1 test | `tests/host/Ps1CpuProfileWindowTests.cpp`, `scripts/test.ps1` | Pure host tests and suite registration |
| Evidence | `docs/Build.md`, `docs/Validation.md` | Repeatable commands, measured on/off cost, guest report |

---

### Task 1: Shared Core sink contract and phase probes

**Files:** Create `C:\dev\helworks\helengine\engine\helengine.core\diagnostics\IRuntimeCpuProfileSink.cs` and `RuntimeCpuProfileStage.cs`; modify `CoreInitializationOptions.cs` and `Core.cs`; create `C:\dev\helworks\helengine\engine\helengine.editor.tests\RuntimeCpuProfileProbeTests.cs`.

**Interfaces:** Produce `CoreInitializationOptions.CpuProfileSink`, `IRuntimeCpuProfileSink.ReadMicroseconds()`, `RecordStage(RuntimeCpuProfileStage, ulong)`, `RecordUpdateable(IUpdateable, int, uint, uint, ulong)`, `RegisterType(uint, string)`, and `ClockFault()`. Expose an internal `Core.CpuProfileSink` getter for `ObjectManager` in Task 2. Use enum members `EarlyInput`, `FrameCounters`, `ObjectManagerUpdate`, `Audio`, `Physics`, `LateInput`, `PointerInteraction`, `FrameBoundary`, and `RenderManager3D` with explicit byte values 1-9.

- [ ] **1.1 Write behavior tests before implementation.** In `RuntimeCpuProfileProbeTests.cs`, define `CreateInitializedCore` using `FakeContentStreamSource`, `TestRenderManager3D`, `TestRenderManager2D`, `TestInputBackend`, and `TestPlatformInfo.Shared` from the existing test helpers. Define `RecordingSink` with a timestamp counter incremented by 64 on each read. Initialize Core with `CpuProfileSink = sink`, call `Update(1.0 / 60.0)`, `CompleteFrameBoundary()`, and `Draw()`, and assert one `ObjectManagerUpdate` and one `RenderManager3D` record with nonnegative durations. Also create Core with no sink, call the same methods, and assert normal operation and that `options.CpuProfileSink` remains null. Use `CoreTimingTests.CreateCore` as the initialization model, not source-substring assertions.

```csharp
var sink = new RecordingSink(stepMicroseconds: 64);
var core = CreateInitializedCore(new CoreInitializationOptions { CpuProfileSink = sink });
core.Update(1.0 / 60.0);
core.CompleteFrameBoundary();
core.Draw();
Assert.Contains(sink.Stages, entry => entry.Stage == RuntimeCpuProfileStage.ObjectManagerUpdate);
Assert.Contains(sink.Stages, entry => entry.Stage == RuntimeCpuProfileStage.RenderManager3D);
```

- [ ] **1.2 Confirm failure, then add the interface and enum.** Run `& 'C:\Program Files\dotnet\dotnet.exe' test 'C:\dev\helworks\helengine\engine\helengine.editor.tests\helengine.editor.tests.csproj' --filter FullyQualifiedName~RuntimeCpuProfileProbeTests --no-restore`; it should fail to compile before the contract exists. Add nullable `CpuProfileSink` to options and retain it on Core.

```csharp
public interface IRuntimeCpuProfileSink {
    ulong ReadMicroseconds();
    void RecordStage(RuntimeCpuProfileStage stage, ulong elapsedMicroseconds);
    void RecordUpdateable(IUpdateable item, int index, uint typeHash, uint ownerSceneEntityId, ulong elapsedMicroseconds);
    void RegisterType(uint typeHash, string typeName);
    void ClockFault();
}
```

- [ ] **1.3 Add explicit Core probes and rerun the filtered test.** Surround existing phase calls in `AdvanceUpdate`, `CompleteFrameBoundary`, and `Draw` with two timestamp calls only when the sink is non-null. Avoid delegate/action wrappers that allocate each frame. On `end < start`, call `ClockFault()` and omit that sample. Keep `SetHostFrameTimings` and overlay logic unchanged.

```csharp
IRuntimeCpuProfileSink sink = CpuProfileSink;
ulong begin = sink == null ? 0UL : sink.ReadMicroseconds();
ObjectManager.Update();
if (sink != null) {
    ulong end = sink.ReadMicroseconds();
    if (end < begin) sink.ClockFault();
    else sink.RecordStage(RuntimeCpuProfileStage.ObjectManagerUpdate, end - begin);
}
```

- [ ] **1.4 Verify and commit only the five shared-Core/test files.** Run the filtered xUnit test and a managed `dotnet build` of `helengine.core.csproj` with the full dotnet path. Confirm no unrelated shared-engine files entered the staged diff. Commit `feat(core): expose opt-in CPU phase probes` in the existing shared checkout, without changing its branch or other agents' files.

### Task 2: Shared per-updateable timing and readable type registration

**Files:** Modify `C:\dev\helworks\helengine\engine\helengine.core\managers\ObjectManager.cs` and `...\helengine.editor.tests\RuntimeCpuProfileProbeTests.cs`.

**Interfaces:** Consume Task 1 sink. `RegisterForUpdate` registers the type name once per type for a profiling session, outside the timed update loop. `Update()` records each `IUpdateable` using the existing diagnostic hash and owner fields, with reference identity and ordered index.

- [ ] **2.1 Add behavioral tests for two component instances and list mutation.** Define `ProfileProbeComponent : UpdateComponent` in the test file. Create two distinct instances on entities, register them through the existing entity lifecycle, call `core.Update`, and assert two matching records with different `Item` references and the same type name. Insert or remove one component between updates and assert the event stream exposes the changed order/identity to the collector. Add a fake clock sequence containing a backward pair and assert `ClockFault()` fires without a wrapped duration.

```csharp
Assert.Equal(2, sink.Updateables.Count(entry => entry.Item is ProfileProbeComponent));
var probeRows = sink.Updateables.Where(entry => entry.Item is ProfileProbeComponent).ToArray();
Assert.NotSame(probeRows[0].Item, probeRows[1].Item);
Assert.Contains(sink.TypeNames, entry => entry.Name == nameof(ProfileProbeComponent));
Assert.DoesNotContain(sink.Updateables, entry => entry.ElapsedMicroseconds > 1000000UL);
```

- [ ] **2.2 Run the filtered tests to see the missing callbacks, then implement.** Reuse `ResolveStableTypeNameHash(item)` and `ResolveUpdateableOwnerSceneEntityId(item)` already used by crash breadcrumbs; do not add a new per-frame reflection path. At registration, send `item.GetType().Name` once per type to the sink, retaining a per-manager dictionary keyed by `Type`. Around `item.Update()` use `ReadMicroseconds()` before and after, with the same rollback rule as Task 1.

```csharp
ulong start = sink == null ? 0UL : sink.ReadMicroseconds();
item.Update();
if (sink != null) {
    ulong end = sink.ReadMicroseconds();
    if (end < start) sink.ClockFault();
    else sink.RecordUpdateable(item, i, LastUpdateableDiagnosticTypeHash,
        LastUpdateableDiagnosticOwnerSceneEntityId, end - start);
}
```

- [ ] **2.3 Run the filtered xUnit test and full `helengine.editor.tests` project test, then commit only ObjectManager and the profiler test file.** Commit `feat(core): time updateable components for native hosts`. Record shared Core HEAD afterward; do not reset the checkout.

### Task 3: PS1 bounded collector and host tests

**Files:** Create `src/platform/ps1/profiling/Ps1CpuProfileWindow.hpp/.cpp` and `tests/host/Ps1CpuProfileWindowTests.cpp`; modify `scripts/test.ps1`.

**Interfaces:** Produce a generated-Core-independent collector with `BeginFrame(sequence, vblank, sceneId)`, `AddStage(stageId, elapsedUs)`, `AddComponent(identity, index, hash, owner, elapsedUs)`, `EndFrame(sequence, vblank)`, `ClockFault()`, and `Reset(reason)`. Expose `TryFormatMilliseconds(totalUs, calls, char (&out)[16])`. Use fixed arrays, 128 component rows, 32 stage rows, and 64 type legends; no heap allocation per frame. A 600-frame window becomes printable only after `EndFrame`.

- [ ] **3.1 Write failing pure C++ tests.** Feed 600 synthetic frames with two instances sharing type/owner and 64-us stage samples. Assert distinct rows and `0.064` ms average. Change one identity/order at frame 300 and assert a reset marker and no mixed 600-frame report. Feed 129 distinct identities and assert `dropped == 1` with intact guards. Feed a clock fault and assert a discarded window, not unsigned wrap. Test `TryFormatMilliseconds(1638400, 600)` returns `2.730`.

```cpp
char milliseconds[16]{};
if (!Ps1CpuProfileWindow::TryFormatMilliseconds(1638400u, 600u, milliseconds)) return 1;
if (std::strcmp(milliseconds, "2.730") != 0) return 2;
```

- [ ] **3.2 Register the test in `scripts/test.ps1` and run the suite to confirm it fails before implementation.** Follow the existing `Ps1RuntimeClockTests.cpp` Docker compile/run pattern and add explicit compile/run exit codes to `summary.json`.
- [ ] **3.3 Implement the bounded collector and run the full host suite.** Keep integer totals (`uint64_t`) and format with integer division; detect overflow, sequence gaps, frame after report, and identity-order changes. Do not print from `BeginFrame`, `AddStage`, `AddComponent`, or `EndFrame`; expose a read-only report view for the adapter. The expected stream-negative fixture exit remains 73.
- [ ] **3.4 Commit only collector, test, and test-runner changes** as `feat(ps1): collect bounded CPU profile windows`.

### Task 4: PS1 adapter, render probes, and build switch

**Files:** Create `src/platform/ps1/profiling/Ps1CpuProfileSink.hpp/.cpp`; modify `src/platform/ps1/Ps1AuthoredScene.cpp/.hpp`, `Ps1RenderManager3D.cpp/.hpp`, `Ps1RenderManager2D.cpp/.hpp`, `Makefile`, and `scripts/build.ps1`.

**Interfaces:** Consume the generated `IRuntimeCpuProfileSink` from Tasks 1-2 and Task 3 window. `-CpuProfile` is valid only with `-Authored -Configuration Release`; it sets `CPU_PROFILE=1`, includes the adapter object, and writes `cpu_profile: true` to the manifest. Default builds write `false`. Native stages use IDs for `camera_setup`, `mesh_3d`, `ui_queue_visit`, `ui_packet_flush`, and `submit`.

- [ ] **4.1 Add build-mode tests before changing the script.** Run `powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts\build.ps1 -Configuration Release -CpuProfile` before editing and record the expected unknown-parameter failure. After the change, the same command must exit nonzero with `CpuProfile requires authored Release.` before invoking Docker. Check an authored manifest's `cpu_profile` Boolean in Task 5, not a source substring. Preserve the existing dirty native-cache work in `scripts/build.ps1` and stage only the profiler hunk.
- [ ] **4.2 Wire the build switch.** Add `[switch] $CpuProfile`, reject invalid combinations, include it in the native cache fingerprint and manifest, and pass `CPU_PROFILE=$([int][bool]$CpuProfile)` to make. In Makefile, add `CPU_PROFILE ?= 0`, include `Ps1CpuProfileSink.cpp` and `Ps1CpuProfileWindow.cpp` only for authored profile builds, and define `PS1_CPU_PROFILE=1` only there. Keep `-O3` identical for profile and comparison builds.

```powershell
if ($CpuProfile -and (-not $Authored -or $Configuration -ne 'Release')) {
    throw 'CpuProfile requires authored Release.'
}
"CPU_PROFILE=$([int][bool]$CpuProfile)"  # element before the final all target in $dockerArgs
cpu_profile = [bool]$CpuProfile
```

- [ ] **4.3 Implement the adapter and frame lifecycle.** `Ps1CpuProfileSink` implements the generated interface, forwards `ReadMicroseconds()` to `he_cpp_custom::MonotonicMicroseconds()`, copies first-seen names into bounded storage, and forwards numeric samples to `Ps1CpuProfileWindow`. Construct it before `Core.Initialize`; assign `m_options->set_CpuProfileSink(&m_cpuProfileSink)` only under `PS1_CPU_PROFILE`. After the 600-frame warmup, call `BeginFrame`/`EndFrame` around the existing frame path, print a finished report after `m_lastFrameEndUs`, and exclude the next frame. A scene/list identity change resets the window. Keep existing overlay and phase log output.
- [ ] **4.4 Add renderer measurements at real boundaries.** In `Ps1RenderManager3D::Draw`, time setup, queue visit, and submit; in `Ps1RenderManager2D::DrawQueue`, time `VisitOrdered` and `FlushPendingInVisitOrder`. Compile those calls out under `#if defined(PS1_CPU_PROFILE)` in default builds. Never label packet submission as GPU raster time. Run the full host suite and inspect the shared interface signatures. The full regenerated-Core/native ABI check is Task 5, where any mismatch gets a separate focused fix commit.
- [ ] **4.5 Commit PS1 adapter, renderer, and build-switch files** as `feat(ps1): emit opt-in CPU profile reports` after the host suite and build-switch checks pass. Task 5 regenerates Core and is the native-link acceptance gate; resolve any adapter ABI mismatch there in a separate focused commit. Do not stage unrelated generated output or pre-existing samplegen `bin/obj`.

### Task 5: Matched builds, guest report, and measured documentation

**Files:** Modify `docs/Build.md` and `docs/Validation.md`. All other outputs stay in `C:\dev\helworks\builds\helengine-ps1`.

**Interfaces:** Produce one profiled and one unprofiled manifest from identical scene, generated Core, and toolchain settings, plus a preserved guest log with `HE_CPU_WINDOW`, stage/component rows, and reset/drop status.

- [ ] **5.1 Capture shared-engine HEAD/dirty state and build the editor app from that exact checkout.** Run the commands below with shell working directory in the PS1 repo. Check the editor DLL exists, and do not change the shared checkout while exporting/cooking.

```powershell
$buildBase = 'C:\dev\helworks\builds\helengine-ps1'
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss-fff'
$editorArtifacts = Join-Path $buildBase "cpu-profile-editor-$stamp"
$engineRevision = git -c safe.directory=C:/dev/helworks/helengine -C 'C:\dev\helworks\helengine' rev-parse HEAD
$engineDirty = git -c safe.directory=C:/dev/helworks/helengine -C 'C:\dev\helworks\helengine' status --short
& 'C:\Program Files\dotnet\dotnet.exe' build 'C:\dev\helworks\helengine\helengine.ui\helengine.editor.app\helengine.editor.app.csproj' -c Debug --artifacts-path $editorArtifacts
$editorApp = Join-Path $editorArtifacts 'bin\helengine.editor.app\debug\helengine.editor.app.dll'
if (-not (Test-Path -LiteralPath $editorApp -PathType Leaf)) { throw 'Editor DLL missing' }
```

- [ ] **5.2 Export the unchanged scene and regenerate one authored workspace.** Use one export for both native builds. Build samplegen into the external artifact root to avoid creating new `bin/obj` files inside the PS1 repo. Invoke `build-authored.ps1` directly in the current PowerShell process so its string-array exclusions remain separate arguments. Read `authored-build.json` and assert its generated Core, declarations, and content directory exist.

```powershell
$samplegenArtifacts = Join-Path $buildBase "cpu-profile-samplegen-$stamp"
$exportRoot = Join-Path $buildBase "cpu-profile-export-$stamp"
$authoredRoot = Join-Path $buildBase "cpu-profile-authored-$stamp"
& 'C:\Program Files\dotnet\dotnet.exe' build 'tools\helengine.ps1.samplegen\helengine.ps1.samplegen.csproj' -c Debug --artifacts-path $samplegenArtifacts
$samplegenDll = Join-Path $samplegenArtifacts 'bin\helengine.ps1.samplegen\debug\helengine.ps1.samplegen.dll'
& 'C:\Program Files\dotnet\dotnet.exe' $samplegenDll --full 'C:\dev\helprojs\demodisc\assets\scenes\rendering\cube_test.helen' $exportRoot
$excluded = @('SoftwareBvh','SoftwareMaterialKind','SoftwareModelComponent','SoftwarePathTracer','SoftwarePathTracerComponent','SoftwareScatteringMaterial','SoftwareSpecularScattering','SoftwareTraceScene','FooterIdentityMarqueeComponent')
& .\scripts\build-authored.ps1 -ProjectRoot 'C:\dev\helprojs\demodisc' -SubsetScenePath (Join-Path $exportRoot 'scene.helen') -EngineSourceRoot 'C:\dev\helworks\helengine' -EditorAppPath $editorApp -OutputRoot $authoredRoot -RetainAllRuntimeScripts -ExcludeScriptClasses $excluded
$authored = Get-Content (Join-Path $authoredRoot 'authored-build.json') -Raw | ConvertFrom-Json
$generatedCore = $authored.generated_core_root
$declarations = (Get-ChildItem $authored.workspace_root -Filter ps1-authored-declarations.json -File -Recurse | Select-Object -First 1).FullName
$contentRoot = Join-Path (Split-Path -Parent $declarations) 'ps1-authored-content'
foreach ($inputPath in @($generatedCore,$declarations,$contentRoot)) { if (-not (Test-Path -LiteralPath $inputPath)) { throw "Missing authored input: $inputPath" } }
```

- [ ] **5.3 Build profile off/on with fresh separate native object caches and the same generated Core/content/declarations.** Capture the two printed manifest paths. Check `cpu_profile` is false/true, compare copied input hashes except the build-mode flag, and check both retail-memory summaries succeed. Do not reuse a native cache from an earlier generated-Core experiment.

```powershell
$baselineCache = Join-Path $buildBase "cpu-profile-cache-off-$stamp"
$profileCache = Join-Path $buildBase "cpu-profile-cache-on-$stamp"
$baselineManifest = & .\scripts\build.ps1 -Configuration Release -Authored -NativeObjectCacheRoot $baselineCache -GeneratedCoreRoot $generatedCore -AuthoredContentRoot $contentRoot -AuthoredDeclarations $declarations
$profileManifest = & .\scripts\build.ps1 -Configuration Release -Authored -CpuProfile -NativeObjectCacheRoot $profileCache -GeneratedCoreRoot $generatedCore -AuthoredContentRoot $contentRoot -AuthoredDeclarations $declarations
```
- [ ] **5.4 Launch the profile manifest through `scripts\launch_in_emulator.ps1 -ShowWindow` using the known DuckStation path.** Preserve its guest log. Confirm 600-frame report completeness, named updateables, render substages, clock quantum, VBlank cadence, and `dropped=0 resets=0` for a steady unchanged scene. Ask Helena whether UI and cube motion remain correct; read logs rather than capturing the screen. Run the unprofiled manifest under the same emulator settings and compare repeated steady intervals; keep logging frames out of measured windows.
- [ ] **5.5 Record actual before/after values and commit docs.** `docs/Validation.md` must show Update, Draw, boundary, between, cadence, EXE/static RAM/heap, profiler observer cost, build manifests, and guest log paths. `docs/Build.md` must show the exact `-CpuProfile` invocation and clarify CPU submission versus GPU work. Run `git diff --check`, verify the host suite summary's required exit codes (negative stream fixture 73), and commit `docs(ps1): document CPU profiling measurements`.

## Execution note

This plan is for native execution by the current PS1 agent; it does not authorize subagents, a fresh independent review session, or changes to the N64 repository. Before implementation, recheck shared-engine status because other agents modify that checkout. If their edits overlap these exact Core files, reconcile rather than reset or overwrite them. A shared-engine sandbox write may require an escalation; if auto-review rejects it, complete PS1-local preparatory work and report the rejected action and reason.