# PS1 Static Main Menu Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Boot PS1 into the complete, static initial demodisc main-menu screen from its committed scene without changing demodisc.

**Architecture:** A menu-specific exporter removes behavior components from a pinned committed scene while preserving visual data and platform overrides. A PS1 wrapper extracts the binary Git blob into a fresh build directory, stages the current project assets, and runs the existing one-scene build. The native boot accepts a camera-only scene; packet capacity changes only if measured failure requires it.

**Tech Stack:** C#/.NET 9, Helengine binary scene serialization, PowerShell 5.1, Git, PS1 C++/psyqo, xUnit, Docker toolchain, DuckStation.

**Spec:** `docs/superpowers/specs/2026-09-24-ps1-static-main-menu-design.md`

## Global Constraints

- Work on `main` in `C:\dev\helworks\helengine-ps1`; no branch or worktree. Every shell command runs with that working directory.
- Read demodisc at `C:\dev\helprojs\demodisc` and the shared engine at `C:\dev\helworks\helengine`; do not modify, switch, or reset either checkout. Use absolute paths for external inputs and outputs.
- Pin menu scene revision `5cc124eec06b8db729b2f9b15d99d26cb1ca8cc3`, blob `7bdb310b5169ebf2c68a394a850f9eacf13cda6a`, and serialized scene ID `Scenes/DemoDiscMainMenu.helen`. The staged relative path is `scenes/DemoDiscMainMenu.helen`.
- Keep all build output and logs under `C:\dev\helworks\builds\helengine-ps1`. Record the actual shared engine revision and current staged asset hashes.
- Preserve preexisting PS1 changes to `scripts/build.ps1` and untracked samplegen `bin/` and `obj/`; stage explicit paths for each commit.
- The milestone has no navigation, input, animation, scroll, marquee, transition, or additional scene. Keep the authored visual hierarchy and `sd/msd` resolution.
- Ask before launching DuckStation or capturing the screen. An independent review requires separate user approval.

## Review Focus

- PowerShell must copy the committed `HELE` blob byte-for-byte; its text pipeline must never rewrite binary bytes. Task 2 checks Git blob and SHA-256 identities after extraction.
- A behavior component added only on the `sd/msd` scope must not survive packaging. Task 1 exercises scope-added filtering and Task 3 inspects the packaged scene.
- An unknown component must fail with entity, type, and scope rather than vanish silently. Task 1 tests base and scope-added unknowns.
- A scene with zero project-script components must build while the cube scene still retains its spin script. Task 2 checks both report-driven pruning paths; Task 3 builds the menu.
- A camera-only scene must submit 2D frames instead of dying at the old drawable assertion. Task 3 checks scene-ready and nonzero UI counters in guest logs; its unchanged camera-count guard still rejects a missing camera.

---

### Task 1: Static-menu scene exporter

**Files:**
- Create: `tools/helengine.ps1.samplegen/Ps1StaticMenuExporter.cs`
- Modify: `tools/helengine.ps1.samplegen/Program.cs`
- Create: `tests/helengine.ps1.builder.tests/Ps1StaticMenuExporterTests.cs`

**Interfaces:**
- Produces: `Ps1StaticMenuExporter.Export(string sourceScenePath, string outputRootPath, string sourceRevision, string sourceBlobId) : string`, writing `scene.helen` and `report.json` beside it.
- The CLI takes `--static-menu` followed by source scene path, output root, source revision, and source blob ID; the build wrapper supplies the pinned metadata.
- The report retains `sceneId`, `authoringAssetId`, `sourceSceneHash`, `outputSceneHash`, `sourceRevision`, `sourceBlobId`, `retainedComponents`, component counts/removals, and scope paths. `retainedComponents` includes base and scope-added visual types for `build-authored.ps1` pruning.

- [ ] **Step 1: Write model-level xUnit cases.** Construct a `SceneAsset` with a camera root and a disabled child panel. Put `helengine.TextComponent` and `DemoDisc.menu.MenuComponent` in base records, and `helengine.SpriteComponent` plus `DemoDisc.menu.MenuItemComponent` in an `sd/msd` added-component override. Serialize it with `Ps1SceneSerialization.Write`, export, deserialize, and assert unchanged entity IDs, enabled flags, transforms, parent/child structure, visual payload bytes, and override scopes. Assert both behavior records are absent and the report lists their removal. Add separate base and override unknown-type cases; assert the error names the entity, type, and scope. Add malformed scene and wrong scene-ID cases. This tests the scene model, not source text.

```csharp
string scenePath = new Ps1StaticMenuExporter().Export(sourcePath, outputRoot, revision, blobId);
SceneAsset exported = Ps1SceneSerialization.Deserialize(File.ReadAllBytes(scenePath));
Assert.Equal(source.RootEntities[0].Id, exported.RootEntities[0].Id);
Assert.Equal(source.RootEntities[0].Children[0].Enabled, exported.RootEntities[0].Children[0].Enabled);
Assert.DoesNotContain(exported.RootEntities[0].Components, c => c.ComponentTypeId == "DemoDisc.menu.MenuComponent");
Assert.Contains(exported.RootEntities[0].PlatformComponentOverrides[0].AddedComponents,
    added => added.Component.ComponentTypeId == "helengine.SpriteComponent");
```

- [ ] **Step 2: Run the focused tests against editor binaries from the current shared engine; expect failure for the missing exporter.** Build editor dependencies into a fresh external artifacts directory if no matching engine binaries exist.

```powershell
& 'C:\Program Files\dotnet\dotnet.exe' test .\tests\helengine.ps1.builder.tests\helengine.ps1.builder.tests.csproj `
  --filter FullyQualifiedName~Ps1StaticMenuExporterTests `
  "-p:HelengineRoot=C:\dev\helworks\helengine" "-p:EngineBinaryRoot=$engineBinaries" -v minimal
```

- [ ] **Step 3: Implement an explicit component policy.** Preserve only `helengine.CameraComponent`, `ViewportComponent`, `LayoutComponent`, `ClipRectComponent`, `SpriteComponent`, `TextComponent`, and `RoundedRectComponent`. Remove `helengine.AnimationPlayerComponent`, `ScrollComponent`, and `DemoDisc.menu.MenuComponent`, `MenuItemComponent`, `MenuPanelComponent`, `MenuBackgroundMotionComponent`, and `FooterIdentityMarqueeComponent`. Walk every root and child; filter `Components` and every `PlatformComponentOverrides[].AddedComponents`, preserving removed keys, existence/transform overrides, component keys, payloads, and order for retained records. Reindex base components. Fail an unclassified type and verify no forbidden type remains before serialization. Serialize natively and write the report with both input and output SHA-256.

```csharp
foreach (SceneEntityAsset entity in Enumerate(scene.RootEntities)) {
    entity.Components = Filter(entity.Components, entity.Name, "Common");
    foreach (SceneEntityPlatformComponentOverrideAsset item in entity.PlatformComponentOverrides) {
        item.AddedComponents = FilterAdded(item.AddedComponents, entity.Name,
            SceneOverrideScopePath.Format(item.Scope));
    }
}
```

- [ ] **Step 4: Run the focused tests and existing `CubeTestSubsetTests`; expect pass.** Commit only these three files with `feat(ps1): export static authored menu scene`.

### Task 2: Reproducible build-local menu input and script pruning

**Files:**
- Create: `scripts/build-static-menu.ps1`
- Modify: `scripts/build-authored.ps1`
- Modify: `docs/Build.md`

**Interfaces:**
- Produces: `build-static-menu.ps1` parameters `-ProjectRoot`, `-EngineSourceRoot`, `-EditorAppPath`, `-OutputRoot`, and optional `-ExportOnly` (which does not require `-EditorAppPath`).
- Writes `source/DemoDiscMainMenu.helen`, `export/scene.helen`, and `export/report.json` beneath the fresh output root. A full run also writes `authored/authored-build.json` and `authored/staged-input-hashes.json`; `-ExportOnly` stops before the authored build.
- Consumes the Task 1 CLI and passes `-SceneRelativePath scenes/DemoDiscMainMenu.helen -SceneId Scenes/DemoDiscMainMenu.helen` to `build-authored.ps1`.

- [ ] **Step 1: Add an export-only smoke check before the wrapper exists.** Use a fresh output root and assert the command fails because `build-static-menu.ps1` does not exist. Capture demodisc `git status --porcelain` before the check for later comparison.

```powershell
$root = 'C:\dev\helworks\builds\helengine-ps1\static-menu-export-probe'
& .\scripts\build-static-menu.ps1 -ProjectRoot C:\dev\helprojs\demodisc `
  -EngineSourceRoot C:\dev\helworks\helengine -OutputRoot $root -ExportOnly
```

- [ ] **Step 2: Write the wrapper with binary-safe Git extraction.** Resolve `revision:assets/scenes/DemoDiscMainMenu.helen` and require the pinned blob ID. Use `System.Diagnostics.Process` with `RedirectStandardOutput=true` and copy `StandardOutput.BaseStream` to the source file; wait and check exit code. Verify `git hash-object` on the extracted file equals the pinned blob and the first bytes are `HELE`. Build/run samplegen with the current engine binaries and Task 1 CLI, then call `build-authored.ps1` only when `-ExportOnly` is absent. Do not use PowerShell `>` or `Out-File` for the blob.

```powershell
$copy = [IO.File]::Create($sourcePath)
try { $gitProcess.StandardOutput.BaseStream.CopyTo($copy) } finally { $copy.Dispose() }
$gitProcess.WaitForExit()
if ($gitProcess.ExitCode -ne 0) { throw 'Pinned menu blob extraction failed.' }
if ((& git -C $projectRoot hash-object $sourcePath).Trim() -ne $pinnedBlob) {
    throw 'Pinned menu blob changed during extraction.'
}
```

- [ ] **Step 3: Generalize project-script pruning.** Delete the hard failure for zero retained script classes in `build-authored.ps1`; leave the existing per-class presence check for nonzero lists and the `-RetainAllRuntimeScripts` behavior. Hash every staged `assets` and `settings` file into a sorted `staged-input-hashes.json`, and include that path in `authored-build.json`. The export report provides the real menu scene ID and authoring asset ID; the wrapper never invents either.

```powershell
$stagedInputs = @(Get-ChildItem -LiteralPath (Join-Path $stagedProject 'assets') -Recurse -File) +
                @(Get-ChildItem -LiteralPath (Join-Path $stagedProject 'settings') -Recurse -File)
$hashRows = @($stagedInputs | Sort-Object FullName | ForEach-Object {
    [ordered]@{ path = $_.FullName.Substring($stagedProject.Length + 1).Replace('\','/');
                sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant() }
})
```

- [ ] **Step 4: Run `-ExportOnly` and check the raw blob ID, report scene ID, behavior-component removals, preserved `sd/msd` scopes, and demodisc status equality.** Exercise an invalid project root and confirm a clear error. Check the existing cube subset report still retains its spin class. Run focused .NET exporter tests and `git diff --check`. Document the exact wrapper command in `docs/Build.md`; commit explicit files with `build(ps1): stage pinned static menu locally`.

### Task 3: Camera-only boot, native build, and measured acceptance

**Files:**
- Modify: `src/platform/ps1/Ps1AuthoredScene.cpp`
- Conditionally modify after measurement: `src/platform/ps1/rendering/Ps1PacketBudget.hpp`
- Modify: `docs/Validation.md`
- Keep untouched: the already dirty `scripts/build.ps1`

**Interfaces:**
- Consumes Task 2's authored build output and its generated Core/archive. Produces a retail PS-X EXE and manifest for the static menu.
- Startup accepts one or more cameras and zero or more 3D drawables. A missing camera still fails. The 2D renderer supplies nonzero UI counters on a successful menu frame.

- [ ] **Step 1: Build an editor app from the actual shared engine revision into a fresh external artifacts directory.** Record `git -C C:\dev\helworks\helengine rev-parse HEAD` and the app DLL path; do not change that checkout. Run the Task 2 wrapper without `-ExportOnly` to produce the authored build. Diagnose any build failure from logs before changing packet capacity.

```powershell
& 'C:\Program Files\dotnet\dotnet.exe' build C:\dev\helworks\helengine\helengine.ui\helengine.editor.app\helengine.editor.app.csproj `
  -c Debug --artifacts-path C:\dev\helworks\builds\helengine-ps1\static-menu-editor-app -v minimal
$editorApp = 'C:\dev\helworks\builds\helengine-ps1\static-menu-editor-app\bin\helengine.editor.app\debug\helengine.editor.app.dll'
$menuRoot = 'C:\dev\helworks\builds\helengine-ps1\static-menu-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff')
& .\scripts\build-static-menu.ps1 -ProjectRoot C:\dev\helprojs\demodisc `
  -EngineSourceRoot C:\dev\helworks\helengine `
  -EditorAppPath $editorApp -OutputRoot $menuRoot
```

- [ ] **Step 2: Permit camera-only scenes in the PS1 boot check.** Change the post-load assertion to fail only when `cameraCount == 0`. Remove cube-specific wording from the camera-only diagnostics while preserving the numeric count fields. The existing `Ps1RenderManager3D::Draw` already visits an empty mesh queue and calls `Render2D.DrawQueue(camera)`; do not add a second render path.

```cpp
if (cameraCount == 0) {
    he_cpp_custom::Fail("Authored scene registered no camera");
}
```

- [ ] **Step 3: Build the menu and inspect output before any emulator launch.** Check exporter/staged/packaged component inventories, source and dependency hashes, engine revision, native manifest, retail RAM figures, and linker map. Run `scripts/test.ps1` with the shared engine's codegen root. If capacity fails at runtime, read packet/failure/high-water markers first; raise only the exhausted `UiTriangles` or textured-quad capacity by measured demand plus small headroom, then repeat native build and RAM check. Keep packet-budget and boot changes in separate `fix(ps1)` commits when both are needed.

```powershell
& .\scripts\test.ps1 -CodegenRuntimeRoot C:\dev\helworks\helengine\engine\vendor\csharpcodegen
git diff --check
```

- [ ] **Step 4: Ask before launching DuckStation.** With approval, use `scripts/launch_in_emulator.ps1 -Manifest $menuManifest -EmulatorPath $approvedDuckStationPath -ShowWindow`, where `$menuManifest` comes from the fresh authored build and `$approvedDuckStationPath` is the installed path confirmed for this launch. Read guest logs for scene-ready, repeated frames, cameras>0, drawables=0, nonzero UI counts, packet guards intact, no `PS1_CORE_FATAL` and no UI failure; record packet high-water and heap endpoint. Ask Helena to confirm the logo, full panels/text, selected row, clipping, colors, and placement. Do not capture the screen without separate approval.

- [ ] **Step 5: Run a `cube_test` regression authored build if the shared boot or packet budget changed, and compare its markers to the last known build.** Record both successful and failed measured attempts, native memory figures, guest evidence paths, visual feedback, actual engine revision, and any limitations in `docs/Validation.md`. Commit the evidence with `docs(ps1): validate static main menu` after the implementation commits.
