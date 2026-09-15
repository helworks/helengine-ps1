# PS1 exported cube_test: minimal authored scene

## Approved scope

Use the existing `C:/dev/helprojs/demodisc/assets/scenes/rendering/cube_test.helen`.
The first milestone contains its camera and cube only. Preserve the cube's
material and existing time-driven rotation. Exclude lighting, UI, overlays,
menu navigation, and camera controls from this export. Do not modify the
shared demodisc scene to make the PS1 milestone work.

## Proposed integration

Follow the N64 authored-scene architecture: the generic exporter produces
cooked assets and generated Core; the PS1 builder packages them; native
adapters supply content streams and rendering to Core. Use the actual scene
loader and generated cube rotation component. Do not substitute the built-in
PS1 cube or reimplement that component in native code.

For this milestone embed the small cooked package in the PS-X EXE and expose
its declared files through read-only memory streams. This proves exported
scene loading without introducing CD filesystem and disc-image boot work.
Account for the embedded data in the existing retail-memory check.

The camera remains at its authored pose. Read its projection, layer mask and
clear color from the exported data. Read the cube mesh, material base color,
and transform from loaded runtime objects. The current factory describes a
camera at (0,0,5), a cube at the origin and Y rotation at pi/2 radians/second;
these are expectations to compare with the current serialized asset, not
replacement constants. Deserialize the current asset before freezing the
fixture; the factory is not proof of the asset's current contents.

## Export boundary

Create a staging copy below `C:/dev/helworks/builds/helengine-ps1` and select
the actual camera and cube entities through supported scene serialization.
Retain CameraComponent, MeshComponent, and CubeTestSpinComponent. Preserve
their serialized values and referenced model/material identities. Explicitly
report removed entities/components and prune their asset/code dependencies
before packaging and generated-component discovery. Exclude the camera's
orbit-control component. Reject unexpected dependencies remaining in the
subset rather than silently ignoring them at runtime.

Render the retained material as opaque unlit base color for this milestone.
The PS1 material cooker must declare this supported material subset and reject
textures, transparency or effects it cannot represent. Do not add lighting to
approximate an unsupported material. Inspect the actual material first and
report incompatibility if it does not support this export interpretation.

## Rendering and memory

Keep 320x240 NTSC, two persistent packet contexts, bounded packet allocation,
ordering-table rendering, GPU completion marker, and fresh-VBlank swap.
Keep periodic BIOS logging opt-in; its removal was verified by 720 updates in
720 VBlanks and user-confirmed stable 60 FPS at commit `caae04c`.

Convert authored object-to-camera transforms and vertices to the GTE numeric
representation at the renderer boundary. Document the engine-to-PS1 axes,
winding, units, matrix precision, and camera projection conversion. Derive
safe bounds from this exported mesh and fixed camera over its full rotation.
Do not reuse the built-in cube's +/-64 bounds for exported geometry.

This is a constrained scene milestone, not an arbitrary-scene renderer.
Prove that this scene stays inside the safe projection/depth range. Reject
unsupported poses before packet submission. General clipping and movable
cameras remain a later milestone; if this exact authored scene needs clipping,
stop at that compatibility checkpoint rather than changing its camera.

Check generated Core linkage, soft-float/runtime dependencies, embedded
payload, heap, and reserved stack against retail 2 MiB RAM early. Measure
allocations after scene loading and require no steady-state frame allocations.
Never enable emulator extra RAM or overclock to pass acceptance.

## Acceptance

- A repeatable command exports the selected subset of the existing scene and
  builds a manifest with input and output hashes.
- Core loads the original scene identity and registers one camera and one
  drawable; generated CubeTestSpinComponent drives rotation.
- Altering the staged camera/transform/material changes the rendered output,
  demonstrating that the loaded scene supplies those values.
- Missing assets, corrupt/truncated payloads, unsupported components and
  packet/memory overflow fail explicitly; authored mode never falls back to
  the diagnostic cube.
- Target probes verify draw packets, transforms, bounds, guards and cadence.
  The user verifies appearance in DuckStation under Helena's main account.
- The original cube diagnostic and completion-stress modes remain available.

## References

- `helengine-n64-impl/docs/AuthoredSceneBuild.md`
- `helengine-n64-impl/builder/Nintendo64PlatformAssetBuilder.cs`
- `helengine-n64-impl/src/platform/n64/Nintendo64RuntimeHost.cpp`
- `helengine/engine/helengine.files/assets/SceneAssetPayloadSerializer.cs`
- `demodisc/assets/codebase/rendering.tools/CubeTestSceneFactory.cs`
- `demodisc/assets/codebase/rendering/CubeTestSpinComponent.cs`
