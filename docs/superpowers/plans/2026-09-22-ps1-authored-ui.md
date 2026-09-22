# PS1 Authored UI Rendering Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Render authored PS1 sprites and rounded-rectangle UI from the active scene camera through the existing PS1 frame submission.

**Architecture:** Keep the shared Core checkout and its revision fixed. Add pure PS1 screen-space and rounded-shape geometry helpers, reserve bounded primitive storage in the PS1 frame contexts, then make `Ps1RenderManager2D` visit the active camera queue and submit those primitives in the same ordering table as the 3D scene. The first slice covers indexed sprites and rounded rectangles; text waits for cooked font atlases to be staged, and controller interaction is separate.

**Tech Stack:** C++20, PSYQo GPU primitives and ordering tables, existing generated Core interfaces, Docker-based host C++ checks, authored PS1 build scripts, DuckStation.

**Spec:** `docs/superpowers/specs/2026-09-22-ps1-ui-design.md`

## Global Constraints

- Work directly on `main` in `C:\dev\helworks\helengine-ps1`; do not create a branch or worktree.
- Build against `C:\dev\helworks\helengine` without switching or resetting its revision.
- Keep shell working directories inside the PS1 repository and write build artifacts under `C:\dev\helworks\builds\helengine-ps1`.
- Keep PS1 control images scoped to PS1 and at their authored 26/27-pixel sizes.
- Preserve the existing double-buffered frame-context completion ownership and guard checks.
- Submit 3D and 2D work through one GPU frame chain at the existing 320x240 display mode.
- Text and `InteractableComponent` behavior remain outside this implementation slice.

## Review Focus

- **Authored transform and viewport fitting:** UI must apply the entity world transform and viewport scaling once, then map logical pixels to the selected pixel viewport. Pin origin, scale, rotation, and viewport offset in transform tests.
- **Texture page and UV edges:** odd logical widths, source rectangles on texture boundaries, and 4-bit versus 8-bit indexed page addressing must stay inside PS1 texture-page limits. Pin source-to-UV conversion and reject unsupported dimensions before queuing.
- **2D-over-3D ordering:** the camera's 2D render order must be preserved and all UI must overlay the 3D pass. Pin the order-table bucket policy and draw sequence.
- **Rounded geometry and bounds:** zero radius, radius clamping, each partial-corner combination, borders, and small shapes must not create inverted or degenerate geometry. Pin geometry counts and representative vertices.
- **Capacity and frame lifetime:** a full 2D queue must reject the frame before writing past storage; queued fragments must remain alive until PSYQo releases that display parity. Pin exact-capacity and overflow behavior.

---

### Task 1: Add tested PS1 screen-space and rounded-rectangle geometry

**Files:**
- Create: `src/platform/ps1/rendering/Ps1AuthoredUiGeometry.hpp`
- Test: `tests/host/Ps1AuthoredUiGeometryTests.cpp`
- Modify: `scripts/test.ps1`

**Interfaces:**
- Consumes: plain numeric world position, scale, rotation, dimensions, normalized source rectangle, pixel viewport and corner-bit inputs. Keep the helper independent of generated Core headers so the host test runner can compile it directly; `Ps1RenderManager2D` converts Core drawable values into these inputs.
- Produces: a header-only helper with PS1-owned POD types `Ps1ScreenRect`, `Ps1UiPoint`, `Ps1UiSize`, `Ps1UiRectF`, `Ps1UiVertex`, and `Ps1UiTriangle`; `TryBuildSpriteQuad(...)`, `TryBuildRoundedRectTriangles(...)`, and `TryMapRenderOrder(uint8_t renderOrder, uint16_t& bucket)`; each `Try...` returns `false` for invalid dimensions or out-of-range geometry and never allocates. The geometry APIs receive already-resolved screen-space position/scale and apply only the selected viewport's pixel origin.

- [x] **Step 1: Add the host test file and compile it through the existing Docker host-test runner.** Cover a centered 320x240 viewport, an offset half-size viewport, a 26x27 sprite, a rotated quad, zero-size rejection, rounded rectangles with zero radius, clamped radius, and one rounded corner, plus `TryMapRenderOrder` preserving increasing order. Initially declare the helper calls in the test so the compile fails because the header does not exist.
- [x] **Step 2: Run the focused test compile and confirm the expected missing-header failure.** Run `scripts/test.ps1` after wiring only this test into its host compile/run sequence; record the first compile failure as the red result.
- [x] **Step 3: Implement the header-only geometry.** Convert pixel-space top-left and dimensions to four integer GPU vertices after entity scale and rotation; map normalized source rectangles to 0..255 UV coordinates; build fill and optional border triangles with fixed segment counts and per-corner selection; validate positive size, finite transforms, and screen-coordinate bounds before returning success.
- [x] **Step 4: Run the geometry test executable and confirm all cases pass.** Keep test expectations independent of helper internals by checking resulting screen corners, UV endpoints, corner placement, triangle count, and rejection results.
- [x] **Step 5: Commit the geometry helper and host test.** Use `feat(ps1): add authored UI geometry helpers`.

### Task 2: Reserve bounded sprite and shape primitives in frame contexts

**Files:**
- Modify: `src/platform/ps1/rendering/Ps1FrameContext.hpp`
- Modify: `src/platform/ps1/rendering/Ps1FrameContext.cpp`
- Modify: `src/platform/ps1/rendering/Ps1PacketBudget.hpp`
- Test: `tests/host/Ps1RenderPolicyTests.cpp`

**Interfaces:**
- Consumes: `Ps1UiVertex` and geometry from Task 1; PSYQo `TexturedTriangle` and `Triangle` fragment types.
- Produces: `TryReserveTexturedTriangle()`, `TryReserveUiTriangle()`, and matching `Insert(..., bucket)` overloads; counters expose total and per-kind reservations for diagnostics.

- [x] **Step 1: Add packet-budget tests for exact capacity and one-over-capacity.** Assert each successful reservation consumes the expected bytes and a further reservation returns `nullptr` without changing guards or counters; assert `Cancel()` resets reservations and leaves the next `Begin()` usable.
- [ ] **Step 2: Run the focused host policy executable and confirm the new reservation assertions fail against current capacity accounting.** Use the `scripts/test.ps1` host C++ path and capture the failing assertion.
- [x] **Step 3: Implement explicit capacities and guarded storage for flat UI triangles and textured triangles.** Keep fragment arrays inside each parity-owned `Ps1FrameContext`, include their worst-case bytes in `PacketBudgetBytes`, validate slot ownership in insert methods, and count them in `UsedPackets()` and `HighWaterBytes()`.
- [ ] **Step 4: Run the host policy executable and confirm exact-capacity, overflow, cancel, and guard cases pass.** Also compile the context against PSYQo's primitive static sizes in the native PS1 build.
- [ ] **Step 5: Commit the capacity change.** Use `feat(ps1): reserve bounded UI frame primitives`.

### Task 3: Queue sprite and rounded-rectangle drawables from the camera's 2D list

**Files:**
- Modify: `src/platform/ps1/Ps1RenderManager2D.hpp`
- Modify: `src/platform/ps1/Ps1RenderManager2D.cpp`
- Modify: `src/platform/ps1/Ps1RenderManager3D.hpp`
- Modify: `src/platform/ps1/Ps1RenderManager3D.cpp`
- Modify: `src/platform/ps1/Ps1AuthoredScene.cpp`
- Modify: `src/platform/ps1/rendering/Ps1RasterRenderer.hpp`
- Modify: `src/platform/ps1/rendering/Ps1RasterRenderer.cpp`
- Modify: `src/platform/ps1/rendering/Ps1FrameContext.hpp`
- Modify: `src/platform/ps1/rendering/Ps1FrameContext.cpp`

**Interfaces:**
- Consumes: Task 1 geometry and Task 2 frame reservations; generated `IRenderVisitor2D`, `IRenderQueue2D`, `ISpriteDrawable2D`, `IRoundedRectDrawable2D`, `Ps1RuntimeTexture`, `ICamera`, and `RenderTarget`.
- Produces: `Ps1RenderManager2D::DrawQueue(ICamera*)`, visitor dispatch for sprites and rounded rectangles, and raster submission methods for flat and textured UI fragments. `Ps1RenderManager3D::Draw()` owns queue traversal order: 3D queue first, then 2D queue, then one submission.

- [x] **Step 1: Extend host policy coverage for order-table placement.** Pin that camera queue order maps to increasing UI draw order and that the selected UI depth band overlays all 3D buckets. Layer filtering remains the existing `IRenderQueue2D::VisitOrdered` contract; do not duplicate it in PS1 code.
- [ ] **Step 2: Run the focused host policy executable and confirm the expected ordering assertions fail.** Keep the failure to wrong/missing UI bucket behavior.
- [x] **Step 3: Implement sprite visitor dispatch.** Resolve the drawable world transform and active camera pixel viewport once; reject non-main render targets and non-Indexed4/Indexed8 textures; use texture runtime VRAM/CLUT coordinates and the logical texture size for UV/page calculation; reserve and insert the two textured triangles while preserving tint, binary transparency, and PS1 half-blend alpha.
- [x] **Step 4: Implement rounded-rectangle visitor dispatch.** Read all shape fields, transform generated fill and border vertices to screen space, reserve each triangle before insertion, preserve selected corners and radius, and record an explicit capacity rejection if any reservation fails. Three chords per rounded corner keep the authored panels within the bounded packet reservation; constant arc samples avoid per-frame software trig for rounded points.
- [x] **Step 5: Connect the ordered camera 2D queue to the shared authored frame.** Pass the existing `Ps1RenderManager2D` into `Ps1RenderManager3D`; after visiting the selected camera's 3D queue call `camera->get_RenderQueue2D()->VisitOrdered(m_render2D)`; submit only after both queues complete. Remove the three unconditional `Fail(...)` implementations and replace unsupported cases with stable diagnostic markers and a rejected-frame result.
- [x] **Step 6: Run focused host geometry/policy tests, build the PS1 native target, and confirm that the authored scene reaches frame submission with nonzero sprite/shape counts.** DuckStation reports 3 sprites, 2 shapes, 98 submitted UI triangles, intact guards, and no frame failure.
- [x] **Step 7: Commit the queue integration.** Use `feat(ps1): render authored sprites and shapes`.

### Task 4: Build, boot, measure, and document the first UI slice

**Files:**
- Modify: `docs/Validation.md`
- Modify: `docs/Build.md` only if the command or group behavior changed during integration.

**Interfaces:**
- Consumes: the `cube_test` scene export, authored PS1 build pipeline, native PS-X EXE, and DuckStation profile already used for PS1 validation.
- Produces: a measured validation entry naming the shared engine revision, declared archive assets and bytes, executable bytes, UI renderer counters, and emulator result.

- [x] **Step 1: Export the unchanged `cube_test` scene and run the full authored PS1 build against `C:\dev\helworks\helengine`.** The final native build used the generated Core and authored assets from the full pipeline.
- [x] **Step 2: Inspect the generated manifest and embedded asset declarations.** Confirmed 11 assets, three PS1 control textures at 26/27-pixel dimensions, and measured archive and executable sizes.
- [x] **Step 3: Launch the PS-X EXE in visible DuckStation using the user's existing emulator-launch authorization.** The guest is running in a visible window; no framebuffer screenshot was captured.
- [x] **Step 4: Read the emulator log and confirm scene-ready, first-frame, UI visited/submitted counters, and cadence markers, with no fatal, exception, guard, or capacity failures.** The VBlank cadence is slower than the prior 3D-only result and is recorded in `docs/Validation.md`.
- [x] **Step 5: Add the measured result to `docs/Validation.md`, run `git diff --check`, and commit the evidence.** Use `docs(ps1): record authored UI rendering boot`.

## Handoff

This plan is for native execution by the current PS1 agent, in the existing `helengine-ps1` repository on `main`. Text rendering, font atlas staging, and interactable/controller input require later plans after this visual slice boots.
