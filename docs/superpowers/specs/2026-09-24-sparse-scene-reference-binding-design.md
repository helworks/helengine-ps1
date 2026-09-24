# Sparse runtime scene-reference binding

## Intent and scope

Fixed demodisc scene and UI relationships should be authored and serialized. Runtime components should receive usable references after scene load, without scanning the scene graph during Update or constructing fixed UI at runtime. Dynamic gameplay objects and the max-objects benchmark are outside this change. The PS1's peak and retained RAM are constraints, so runtime binding must not retain an index of every entity.

This design applies to the shared helengine scene loader and to demodisc authoring. PS1 is the first native integration and measurement target.

## Current behavior

The editor's `SceneEntityReferenceTable` assigns and resolves stable scene IDs. A serialized `SceneEntityReference` contains only `EntityId`. `RuntimeSceneLoadService` creates each entity and attaches `SceneEntityRuntimeIdComponent`, while `EngineBinaryReader.ReadSceneEntityReference` only reconstructs the ID holder. Runtime component deserialization has no binding phase. Consequently, demodisc scripts repeatedly or once-after-load scan `ObjectManager.Entities` or child trees to recover objects that authoring already identifies.

The standard rendering UI root illustrates a separate error: `DemoDiscReturnToMenuComponent` defaults to pointer interaction, but `DemoDiscSceneUiKitFactory` adds no sibling `InteractableComponent`. Its `Update` retries the unsuccessful component search every frame. The previously added debug-label reference is not yet present in the committed `cube_test.helen`, so that scene uses the one-time compatibility search until regenerated.

## Approaches considered

1. **Selected: sparse load-time fixups.** Collect only deserialized, nonzero `SceneEntityReference` instances. After all entities exist, traverse the scene once, match IDs requested by those references, and fill their runtime-only entity pointers. Discard the temporary request list before the first Update. Retained binding memory grows with the number of referenced relationships, not all entities.
2. **Full runtime ID table.** Lookup is simple, but retaining an entry for every authored entity spends PS1 RAM on IDs that no component references. Rejected.
3. **Per-component search.** Requires no Core change, but duplicates lifecycle and lookup code across demodisc and can repeat searches each frame. Rejected as the general solution; a single same-owner component lookup during initialization remains appropriate where no cross-entity reference exists.

## Binding flow

An explicit load-scope reference sink receives every nonzero `SceneEntityReference` decoded by both reflected and generated native component deserializers. It records only these reference objects and their IDs. There is no process-global registry. The existing `EntityId` remains the only serialized representation; the runtime-only resolved `Entity` property is excluded from persistence.

After the last root entity has been materialized, and before hierarchy initialization can start component Updates, the load operation walks its root trees once. It compares each nonzero entity ID against the requested IDs and sets all corresponding references, including multiple references to one entity. The loader compares each entity ID with the pending reference list, marking matched requests. This one-time linear matching uses no additional index and must not build or retain an all-entity dictionary. It rejects duplicate entities for a requested ID and fails load with the referring component and ID when a required target is absent. A null or zero reference remains optional and unresolved. The temporary request list is released before completing the load; unloading the scene releases its components and their resolved pointers. References are scene-local. Blueprint expansion must assign scene-unique runtime IDs before binding; tests cover repeated instances of one Blueprint.

Both `RuntimeSceneLoadService.Load` and incremental `RuntimeSceneLoadOperation.Advance` must run the same binding step. The finalization order is: materialize roots, prepare assets, bind references, initialize hierarchy, publish loaded scene. This order also addresses components whose targets are later roots or children.

## Demodisc migration

Factories and scene files should serialize references for fixed cross-entity relationships. Existing ID-bearing components such as follow cameras, menu background motion, loading-screen rectangles, and the debug label should read the bound object rather than search `ObjectManager.Entities`. Components that currently find fixed targets by name, role, render order, or child position should receive authored references where the factory already knows the target. Same-owner `InteractableComponent`, `TextComponent`, or similar dependencies may bind once in `ComponentInitialized`; missing required dependencies should fail clearly instead of retrying in `Update`.

Specifically, author `AllowPointerReturn = false` on non-clickable scene UI roots and remove the per-frame unsuccessful sibling search. Keep pointer binding on actual authored buttons. Regenerate affected scene assets, including `cube_test.helen`, so the serialized files carry the new references. Preserve unrelated dirty demodisc edits and do not regenerate every scene indiscriminately.

Runtime creation of benchmark cubes and gameplay objects remains. The physics-bounds wireframe diagnostic is evaluated separately because its visuals represent live debug state, not fixed authored UI.

## Verification

Tests exercise backward and forward references across roots and children, multiple references to one target, optional null references, missing and duplicate IDs, unload/reload, reflected desktop decoding, and generated native decoding. A PS1 authored build must link and boot the migrated cube scene. Compare its payload size, static size, heap capacity, sampled/peak heap use, and Update timing against the existing baseline. Confirm the temporary fixup list is gone before frame execution and that reference resolution and missing-sibling probes do not appear in per-frame profiles. Visual validation uses existing emulator practice and user observation; do not launch or capture an emulator without the user's request.
