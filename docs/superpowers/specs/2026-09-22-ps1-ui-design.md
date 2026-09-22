# PS1 Authored UI Rendering Design

## Goal

Render the authored scene's 2D UI on PlayStation 1 through the existing Core scene and camera queues. The first milestone supports textured sprites and rounded-rectangle UI shapes; text and controller interaction follow as separate milestones.

## Context and constraints

- Work directly on `main` in `C:\dev\helworks\helengine-ps1`; use the shared engine checkout at `C:\dev\helworks\helengine` without changing its revision.
- Preserve authored PS1 texture sizes. PS1 control icons are already scaled to 26/27 pixels and must be uploaded at those dimensions.
- `Core::Draw()` invokes the platform 3D renderer. The PS1 2D manager can upload indexed textures, but its drawable methods fail and it does not currently traverse a camera's 2D queue.
- `CameraComponent` owns ordered 2D and 3D queues. Drawables carry entity transforms and render-order values.
- Font atlas artifacts currently are not staged into the embedded PS1 archive. Text rendering must wait until the PS1 builder stages and declares those cooked atlas pages.
- Keep PS1's single GPU frame submission and fixed 320x240 display mode.

## First milestone

`Ps1RenderManager3D::Draw()` will visit the selected camera's 3D queue, then ask `Ps1RenderManager2D` to visit that camera's ordered 2D queue before the authored frame is submitted. The 2D manager will queue textured quads for `ISpriteDrawable2D` and flat-color rounded rectangles for `IRoundedRectDrawable2D` into the existing PS1 frame context. This keeps all primitives under the same ordering table and presentation fence.

The screen-space conversion will use the drawable entity's resolved world transform and the camera's resolved pixel viewport. It will preserve authored width, height, rotation, source rectangle, render order, layer visibility, and color tint. Textures use the already uploaded PS1 indexed pages, including odd-width rows padded for VRAM word transfers. Rounded rectangles will use deterministic PS1 geometry with authored corner selection, radius, fill, border color, and border thickness. Shape rendering will not require a cooked texture.

The initial supported path is the main display camera at 320x240. Multiple cameras, off-screen render targets, post-processing, live texture updates, and text are outside this milestone. If the active camera requests an unsupported target or the scene exceeds fixed packet capacity, the renderer reports an explicit PS1 diagnostic and rejects the frame rather than silently dropping UI.

## Follow-on milestones

Text will consume `ITextDrawable2D` and `FontAsset` glyph metrics after the builder stages each cooked font atlas page into the embedded archive. It will preserve authored font scale, wrapping, alignment, tint, outline, and shadow only where the PS1 primitive budget can support them. The current missing-atlas packaging gap must be fixed and measured before the text renderer is enabled.

Controller-backed `InteractableComponent` behavior is a later input milestone. `Ps1InputBackend::CaptureFrame()` currently returns an empty frame, so pointer mapping, focus, and button activation are not part of visual rendering.

## Runtime behavior and limits

- Use only PS1-scoped, pre-scaled scene assets selected by the authored platform groups.
- Map 2D drawables through the active camera's layer mask and render queue ordering.
- Preserve 2D-over-3D composition by submitting the combined command list once per frame.
- Bound sprite and shape packet use before submission; maintain frame guards and the existing double-buffer completion ownership.
- Preserve binary transparency from indexed palettes. For fractional UI alpha, use the PS1 GPU's fixed half-back/half-front blend mode; this approximates each non-binary alpha value as 50%. Unsupported texture depth, camera target, or primitive counts must produce a specific diagnostic.
- Add low-frequency counters for visited sprites/shapes, submitted primitives, and rejection reason so emulator evidence can verify what reached the GPU.

## Verification

Use pure host-side coverage for the screen transform, UV/source rectangle conversion, rounded-rectangle geometry, and capacity rejection. Build the unchanged `cube_test` scene through the authored PS1 pipeline, confirm its staged controls remain at their authored 26/27-pixel sizes, then boot the resulting PS-X EXE in DuckStation and inspect the visible instruction UI plus renderer counters. Record archive size, executable size, staged PS1 UI assets, and emulator markers in `docs/Validation.md`.

## Risks

- Existing PS1 frame contexts reserve storage primarily for 3D triangles; 2D packets need an explicit bounded allocation strategy that does not overwrite fragment guards or starve 3D geometry.
- `RoundedRectComponent` supports borders, partial rounded corners, and rotation. Geometry must match the shared component's coordinate conventions closely enough that existing authored overlays remain recognizable.
- Scene transforms are authored in logical pixel coordinates and may be modified by `ViewportComponent`; conversion must avoid applying viewport scaling a second time.
- The 8-bit indexed VRAM layout has a maximum texture width and page-placement constraints. The authored, already-scaled controls fit; unsupported dimensions must fail with a clear marker.
- Staging font atlases will increase archive and VRAM use, so the text milestone needs measured budgets before enabling every glyph page.
