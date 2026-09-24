# Sparse Scene Reference Binding Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (- [ ]) syntax for tracking.

**Goal:** Bind authored demodisc scene references once during load, with temporary memory proportional only to referenced relationships, and remove fixed-scene searches from Update.

**Architecture:** Runtime deserializers report decoded SceneEntityReference objects to a load-scoped fixup list. After all roots exist, the loader walks the scene once, fills runtime-only Entity pointers, checks missing or ambiguous targets, and releases the list before initialization. Demodisc factories persist the fixed relationships and scripts consume the bound pointers.

**Tech Stack:** C#/.NET 9, helengine scene serialization and C++ player-deserializer generator, demodisc editor authoring, PS1 native build.

**Spec:** docs/superpowers/specs/2026-09-24-sparse-scene-reference-binding-design.md

## Global Constraints

- Work directly on main; no branches or worktrees. Keep command workdir at C:\dev\helworks\helengine-ps1 and use absolute paths for external projects.
- Shared Core is C:\dev\helworks\helengine. Do not create or switch an engine checkout; record its revision before validation.
- Preserve unrelated dirty files in demodisc and PS1. Stage explicit paths for each commit.
- Dynamic gameplay and max-objects benchmark creation remain outside this migration.
- Do not retain an all-entity runtime index. Release the sparse fixup list before the first Update; measure PS1 peak and retained RAM.
- Ask before launching DuckStation or capturing the screen. Use logs and native build reports first.
- Use one primary agent unless the user explicitly opts into delegation. An independent review needs separate user approval.

## Review Focus

- A reference to a later root must resolve before either root initializes. Task 3 tests this.
- Two component properties may reference one entity; both must resolve, and a duplicate target ID must fail. Tasks 1 and 3 test this.
- Null/zero references stay optional; a nonzero missing ID must fail with its owner type and ID. Tasks 1 and 3 test this.
- Native generated deserializers, including nested or array references, must register fixups just like reflected desktop decoding. Task 2 tests this.
- Two instances of one Blueprint must have unique scene IDs after expansion and bind their own targets. Task 3 tests this.

---

### Task 1: Runtime-only reference result and sparse fixup list

**Files:**
- Modify: C:\dev\helworks\helengine\engine\helengine.core\assets\raw\scene\SceneEntityReference.cs
- Create: C:\dev\helworks\helengine\engine\helengine.core\scene\runtime\RuntimeSceneReferenceFixups.cs
- Test: C:\dev\helworks\helengine\engine\helengine.editor.tests\serialization\scene\RuntimeSceneReferenceFixupsTests.cs

**Interfaces:**
- Produces: SceneEntityReference.ResolvedEntity (public getter, internal setter, ignored by persistence).
- Produces: RuntimeSceneReferenceFixups.Track(SceneEntityReference reference, string componentTypeId), Bind(IReadOnlyList<Entity> roots), Clear().

- [ ] **Step 1: Write failing behavioral tests.** Construct two entities with SceneEntityRuntimeIdComponent IDs 10 and 20 and two references to ID 20. Track both, bind roots, and assert both ResolvedEntity values are the same entity. Cover null/zero, missing ID 99, and two entities with requested ID 20. Assert diagnostics include the component type and numeric ID.

~~~csharp
var target = new Entity(core);
target.InitComponents();
target.InitChildren();
target.AddComponent(new SceneEntityRuntimeIdComponent { SceneEntityId = 20u });
var first = new SceneEntityReference { EntityId = 20u };
var second = new SceneEntityReference { EntityId = 20u };
var fixups = new RuntimeSceneReferenceFixups();
fixups.Track(first, "Demo.TestComponent");
fixups.Track(second, "Demo.OtherComponent");
fixups.Bind(new[] { target });
Assert.Same(target, first.ResolvedEntity);
Assert.Same(target, second.ResolvedEntity);
~~~

- [ ] **Step 2: Run the focused test to confirm the missing API fails.**

~~~powershell
& "C:\Program Files\dotnet\dotnet.exe" test C:\dev\helworks\helengine\engine\helengine.editor.tests\helengine.editor.tests.csproj --filter FullyQualifiedName~RuntimeSceneReferenceFixupsTests -v minimal
~~~

- [ ] **Step 3: Implement one list of only requested references.** Keep each reference, owner type, and matched flag. Bind by recursively walking roots and comparing every nonzero entity.SceneEntityRuntimeId with tracked IDs. Detect a second entity for a requested ID; after the walk, reject unmatched requests. Clear the list in a finally path. Put ScenePersistenceIgnore on the runtime pointer.

~~~csharp
[ScenePersistenceIgnore]
public Entity ResolvedEntity { get; internal set; }

public void Track(SceneEntityReference reference, string componentTypeId) {
    if (reference != null && reference.EntityId != 0u) {
        Requests.Add(new Request(reference, componentTypeId));
    }
}
~~~

- [ ] **Step 4: Run the focused test; expect all cases to pass.** Commit only these Core files with feat(core): bind sparse scene references.

### Task 2: Register references from reflected and generated native decoding

**Files:**
- Modify: C:\dev\helworks\helengine\engine\helengine.core\serialization\EngineBinaryReader.cs
- Modify: C:\dev\helworks\helengine\engine\helengine.core\scene\runtime\IRuntimeComponentDeserializer.cs
- Modify: C:\dev\helworks\helengine\engine\helengine.core\scene\runtime\AutomaticScriptComponentRuntimeDeserializer.cs
- Modify: C:\dev\helworks\helengine\engine\helengine.editor\managers\project\ScriptComponentPlayerDeserializerGenerator.cs
- Test: C:\dev\helworks\helengine\engine\helengine.editor.tests\managers\project\ScriptComponentPlayerDeserializerGeneratorTests.cs
- Test: C:\dev\helworks\helengine\engine\helengine.editor.tests\serialization\scene\RuntimeSceneLoadServiceTests.cs

**Interfaces:**
- Consumes: RuntimeSceneReferenceFixups.Track from Task 1.
- Produces: IRuntimeComponentDeserializer.Deserialize(record, referenceResolver, fixups) and EngineBinaryReader.SceneReferenceFixups plus SceneReferenceOwnerTypeId.

- [ ] **Step 1: Add a reflected round-trip test.** Serialize a test component with a SceneEntityReference, deserialize its packaged record with a fixup list, and assert the list receives exactly that nonzero reference. Repeat for a null property. Do not inspect a source file.
- [ ] **Step 2: Add generator tests for a component with one direct reference and a nested/array reference.** Generate native source in memory and assert its reader attaches the passed fixup sink before member reads; compile the generated source in the existing generated-Core test path.
- [ ] **Step 3: Run the two focused test classes; expect failures for the absent sink argument and generated reader setup.**

~~~powershell
& "C:\Program Files\dotnet\dotnet.exe" test C:\dev\helworks\helengine\engine\helengine.editor.tests\helengine.editor.tests.csproj --filter "FullyQualifiedName~ScriptComponentPlayerDeserializerGeneratorTests|FullyQualifiedName~RuntimeSceneLoadServiceTests" -v minimal
~~~

- [ ] **Step 4: Thread fixups as an explicit deserializer argument.** Set the optional reader sink and owner type before decoding any member in both the reflected deserializer and the C++ source emitted by ScriptComponentPlayerDeserializerGenerator. ReadSceneEntityReference registers its returned object. The same reader flows through nested and array member reads. Keep the binary payload format unchanged.

~~~csharp
Component Deserialize(
    SceneComponentAssetRecord record,
    RuntimeSceneAssetReferenceResolver referenceResolver,
    RuntimeSceneReferenceFixups fixups);
~~~

- [ ] **Step 5: Run both focused classes and the existing generated-deserializer tests; expect pass.** Commit explicit Core/editor files with feat(core): collect decoded scene reference fixups.

### Task 3: Bind before hierarchy initialization in both load paths

**Files:**
- Modify: C:\dev\helworks\helengine\engine\helengine.core\scene\runtime\RuntimeSceneLoadService.cs
- Modify: C:\dev\helworks\helengine\engine\helengine.core\scene\runtime\RuntimeSceneLoadOperation.cs
- Test: C:\dev\helworks\helengine\engine\helengine.editor.tests\serialization\scene\RuntimeSceneLoadServiceTests.cs
- Test: C:\dev\helworks\helengine\engine\helengine.editor.tests\managers\project\BlueprintPackagedSceneExpansionTests.cs

**Interfaces:**
- Consumes: three-argument Deserialize and RuntimeSceneReferenceFixups.Bind/Clear.
- Produces: every authored reference bound before ComponentInitialized in direct and incremental loads.

- [ ] **Step 1: Add tests with a target in a later root and a component that reads ResolvedEntity during ComponentInitialized.** Exercise Load and CreateTrackedLoadOperation.Advance. Add repeated Blueprint instance coverage using the existing blueprint expansion fixture. Check missing and duplicate requested IDs abort load before initialization.
- [ ] **Step 2: Run RuntimeSceneLoadServiceTests and BlueprintPackagedSceneExpansionTests; expect the forward-reference cases to fail.**
- [ ] **Step 3: Give each load operation its own fixup list.** Pass it through LoadComponent to Deserialize. For direct Load and incremental Advance, complete all root materialization and asset preparation, call Bind, clear the list in finally, then initialize all roots. Do not publish RuntimeSceneLoadResult on bind failure.

~~~csharp
for (int index = 0; index < roots.Count; index++) {
    meshPreparationService.Prepare(roots[index], TrackPreparedModel);
}
try {
    fixups.Bind(roots);
} finally {
    fixups.Clear();
}
for (int index = 0; index < roots.Count; index++) {
    roots[index].InitializeHierarchy();
}
~~~

- [ ] **Step 4: Run the focused scene-load and blueprint tests, then the full helengine.editor.tests project.** Commit Core files and tests with feat(core): bind scene references before initialization.

### Task 4: Remove the known per-frame miss from fixed demodisc UI

**Files:**
- Modify: C:\dev\helprojs\demodisc\assets\codebase\menu\DemoDiscReturnToMenuComponent.cs
- Modify: C:\dev\helprojs\demodisc\assets\codebase\DemoDiscEditorTools\rendering\DemoDiscSceneUiKitFactory.cs
- Modify: C:\dev\helprojs\demodisc\assets\codebase\DemoDiscEditorTools\game\GameSceneFactory.cs
- Test: C:\dev\helprojs\demodisc\assets\codebase\DemoDiscEditorTools.tests\rendering\DemoDiscSceneUiKitFactoryTests.cs

**Interfaces:**
- Consumes: no new Core API.
- Produces: non-clickable roots have AllowPointerReturn=false; clickable hosts bind once after all components attach.

- [ ] **Step 1: Add a factory behavior test.** Create standard UI and assert its return component has AllowPointerReturn=false; create a real clickable return host and assert its component still allows pointer input. Exercise one click event and assert it requests one scene transition.
- [ ] **Step 2: Run the generated demodisc editor test project filter; expect failure.**

~~~powershell
& "C:\Program Files\dotnet\dotnet.exe" test C:\dev\helprojs\demodisc\cache\generated_code\projects\DemoDiscEditorTools.tests\DemoDiscEditorTools.tests.csproj --filter FullyQualifiedName~DemoDiscSceneUiKitFactoryTests -v minimal
~~~

- [ ] **Step 3: Author false on fixed non-clickable roots.** Move TryBindInteractable out of Update and perform it in ComponentInitialized only when AllowPointerReturn is true. Require InteractableComponent on clickable hosts; keep unsubscribe in ComponentRemoved/Dispose.

~~~csharp
entity.AddComponent(new DemoDiscReturnToMenuComponent {
    AllowPointerReturn = false
});
~~~

- [ ] **Step 4: Rebuild the shared editor app, regenerate the demodisc generated test project against its new Core, and rerun the focused test.** Stage only the named demodisc files and commit fix(demodisc): stop retrying absent return interactable.

### Task 5: Migrate fixed rendering and menu references

**Files:**
- Modify: C:\dev\helprojs\demodisc\assets\codebase\rendering\DemoDiscDebugSceneLabelComponent.cs
- Modify: C:\dev\helprojs\demodisc\assets\codebase\rendering\DemoDiscLightToggleComponent.cs
- Modify: C:\dev\helprojs\demodisc\assets\codebase\rendering\DemoFollowCameraComponent.cs
- Modify: C:\dev\helprojs\demodisc\assets\codebase\menu\MenuBackgroundMotionComponent.cs
- Modify: C:\dev\helprojs\demodisc\assets\codebase\menu\SceneLoadingScreenComponent.cs
- Modify: C:\dev\helprojs\demodisc\assets\codebase\menu\HelenOfCodeSplashComponent.cs
- Modify: C:\dev\helprojs\demodisc\assets\codebase\DemoDiscEditorTools\rendering\DemoDiscLightIndicatorOverlayFactory.cs
- Modify: C:\dev\helprojs\demodisc\assets\codebase\DemoDiscEditorTools\menu\SceneLoadingScreenFactory.cs
- Modify: C:\dev\helprojs\demodisc\assets\codebase\DemoDiscEditorTools\menu\HelenOfCodeSplashSceneFactory.cs
- Modify: C:\dev\helprojs\demodisc\assets\codebase\DemoDiscEditorTools\menu\DemoDiscStandardMainMenuSceneFactory.cs
- Test: C:\dev\helprojs\demodisc\assets\codebase\DemoDiscEditorTools.tests\rendering\FixedSceneReferenceAuthoringTests.cs

**Interfaces:**
- Consumes: SceneEntityReference.ResolvedEntity from Task 1.
- Produces: authored IDs for fixed targets and no ObjectManager.Entities lookup in these components' Update paths.

- [ ] **Step 1: Add behavioral authoring tests.** Build a scene UI and menu fixtures through their factories; save/reload or package them, and assert required SceneEntityReference IDs are nonzero and refer to the intended swatch, light, sprite, camera target, and rectangles. Avoid source-substring tests.
- [ ] **Step 2: Run FixedSceneReferenceAuthoringTests; expect missing authored links to fail.**
- [ ] **Step 3: Give fixed targets SceneEntityReference properties at authoring time.** Factories already hold the created entities; use their EntitySaveComponent.EntityId after allocation. The light toggle holds an array of authored light-entity references plus one swatch reference. In components, take ResolvedEntity in ComponentInitialized, bind the needed component once, and fail for a missing required target. Remove the label's render-order fallback only after the scene file is regenerated in Task 7.

~~~csharp
Entity target = TargetEntityReference?.ResolvedEntity
    ?? throw new InvalidOperationException("Authored target entity reference is missing.");
~~~

- [ ] **Step 4: Run the focused demodisc test filter and compile the runtime scripts through the editor build.** Commit only the relevant demodisc source and tests with feat(demodisc): author fixed rendering and menu links.

### Task 6: Migrate fixed Tilt Trial and remaining UI lookup paths

**Files:**
- Modify: C:\dev\helprojs\demodisc\assets\codebase\TiltPlay\DemoTiltFollowCameraComponent.cs
- Modify: C:\dev\helprojs\demodisc\assets\codebase\TiltPlay\DemoTiltSpeedTextComponent.cs
- Modify: C:\dev\helprojs\demodisc\assets\codebase\TiltPlay\DemoTiltStageComponent.cs
- Modify: C:\dev\helprojs\demodisc\assets\codebase\TiltPlay\TiltPlayMenuComponent.cs
- Modify: C:\dev\helprojs\demodisc\assets\codebase\TiltPlay\TiltTrialSessionComponent.cs
- Modify: C:\dev\helprojs\demodisc\assets\codebase\DemoDiscEditorTools\game\GameSceneFactory.cs
- Test: C:\dev\helprojs\demodisc\assets\codebase\TiltPlay.tests\TiltTrialSessionComponentTests.cs

**Interfaces:**
- Consumes: SceneEntityReference.ResolvedEntity.
- Produces: direct fixed camera, player, goal, panel, and label links; dynamic gameplay state remains dynamic.

- [ ] **Step 1: Add runtime tests with renamed/reordered authored children.** Confirm the same target entities are used via serialized IDs. Add one missing-target test that fails at load instead of silently retrying during Update.
- [ ] **Step 2: Run TiltTrialSessionComponentTests; expect renamed/reordered fixture to fail before migration.**
- [ ] **Step 3: Author fixed references in GameSceneFactory and other owning factories.** Replace name/role/child-position searches for fixed entities with ResolvedEntity, while keeping runtime discovery of genuinely spawned gameplay objects and hit testing.

~~~csharp
Entity player = PlayerEntityReference?.ResolvedEntity
    ?? throw new InvalidOperationException("Tilt Trial player reference is missing.");
~~~

- [ ] **Step 4: Run the focused Tilt Trial tests and compile the generated demodisc projects.** Commit only these source/tests with feat(demodisc): bind fixed Tilt Trial scene links.

### Task 7: Regenerate selected scene data and validate PS1 memory and Update

**Files:**
- Modify: C:\dev\helprojs\demodisc\assets\scenes\rendering\cube_test.helen
- Modify: only other scene files whose migrated factories actually add or change references
- Modify: C:\dev\helworks\helengine-ps1\docs\Validation.md

**Interfaces:**
- Consumes: Tasks 1-6.
- Produces: serialized references in authored scenes and measured native PS1 evidence.

- [ ] **Step 1: Record revisions, dirty paths, and baseline metrics.** Use git -C for shared Core and demodisc while keeping shell cwd in the PS1 repo. Record the existing normal Release Update, heap, payload, and link-size values from Validation.md.
- [ ] **Step 2: Generate scenes in a temporary staged project under the PS1 build-output root.** Copy demodisc to C:\dev\helworks\builds\helengine-ps1\sparse-reference-authoring\project without its cache, output, or .git directories; run the editor command below against that staged project. Copy back only scene files proven changed by this migration. Compare each binary's asset graph and component references before replacing its source file. Never reset unrelated dirty files.

~~~powershell
& "C:\Program Files\dotnet\dotnet.exe" C:\dev\helworks\helengine\helengine.ui\helengine.editor.app\bin\Debug\net9.0-windows\helengine.editor.app.dll --project C:\dev\helworks\builds\helengine-ps1\sparse-reference-authoring\project\project.heproj --editor-command menu.generate-rendering-scenes
~~~
- [ ] **Step 3: Export cube_test unchanged from its regenerated source scene.**

~~~powershell
& "C:\Program Files\dotnet\dotnet.exe" run --project C:\dev\helworks\helengine-ps1\tools\helengine.ps1.samplegen\helengine.ps1.samplegen.csproj -- --full C:\dev\helprojs\demodisc\assets\scenes\rendering\cube_test.helen C:\dev\helworks\builds\helengine-ps1\sparse-references-export
~~~

- [ ] **Step 4: Build the authored PS1 scene with C:\dev\helworks\helengine.** Verify the selected output directory does not exist, then run the documented whole-scene invocation and preserve logs, ELF/map, and manifest.

~~~powershell
.\scripts\build-authored.ps1 -ProjectRoot C:\dev\helprojs\demodisc -SubsetScenePath C:\dev\helworks\builds\helengine-ps1\sparse-references-export\scene.helen -EngineSourceRoot C:\dev\helworks\helengine -EditorAppPath C:\dev\helworks\helengine\helengine.ui\helengine.editor.app\bin\Debug\net9.0-windows\helengine.editor.app.dll -OutputRoot C:\dev\helworks\builds\helengine-ps1\sparse-references-20260924 -RetainAllRuntimeScripts -ExcludeScriptClasses @('SoftwareBvh','SoftwareMaterialKind','SoftwareModelComponent','SoftwarePathTracer','SoftwarePathTracerComponent','SoftwareScatteringMaterial','SoftwareSpecularScattering','SoftwareTraceScene','FooterIdentityMarqueeComponent')
~~~
- [ ] **Step 5: Check native generated deserializer compilation, boot logs, profile output, and memory report.** Compare before/after Update, Render, payload, static, free heap, and sampled peak heap with numeric values. Do not infer framebuffer correctness from logs. Ask before a new emulator launch.
- [ ] **Step 5a: Record the physics-bounds debug wireframe classification.** Its live visualization remains dynamic diagnostic content; note this evidence in Validation.md so it is not mistaken for fixed UI generation.
- [ ] **Step 6: Run PS1 builder tests and inspect git diffs in all three repositories.** Update Validation.md with exact results and limits; commit only migrated scene files in demodisc and the evidence doc in PS1 with scoped messages.
