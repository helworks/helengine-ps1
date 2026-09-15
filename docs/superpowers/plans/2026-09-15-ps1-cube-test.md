# PS1 exported cube_test implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Delegate only within the user's authorization. Steps use checkbox syntax for tracking.

**Goal:** Load and render the camera and rotating cube from the existing demodisc cube_test scene on retail-spec PS1.

**Architecture:** Export a minimal staged subset through Helengine's asset pipeline and generated Core. Embed declared cooked files into the PS-X EXE, expose them as content streams, and adapt Core's camera/drawable submissions to the existing PSYQo renderer.

**Tech Stack:** C# exporter/builder, generated C++ Core, PSYQo/GTE, pinned MIPS Docker toolchain, DuckStation.

**Spec:** `docs/superpowers/specs/2026-09-15-ps1-cube-test-design.md`

## Global constraints

- Source fixture: `C:/dev/helprojs/demodisc/assets/scenes/rendering/cube_test.helen`.
- Preserve the source project. All staging, generated code, logs and artifacts go below `C:/dev/helworks/builds/helengine-ps1`.
- Work in `helengine-ps1-impl`; inspect its branch/status before changes. Do not merge or push implicitly.
- Keep the pinned SDK/toolchain and retail 2 MiB RAM / 64 KiB reserved stack.
- Keep the GPU completion marker and fresh-VBlank swap. Periodic BIOS logs stay opt-in.
- Camera, cube, opaque unlit material and existing cube rotation only; no camera controls, UI, lighting, physics, textures or disc filesystem.
- Read applicable AGENTS.md before touching another repository. Fix generation at its source; never patch generated C++ afterward.
- Use measured runtime memory and frame cadence, not emulator FPS alone, for acceptance.

## Task 1: Export the real minimal scene

**Files:** Create `tools/helengine.ps1.samplegen/` C# project, `scripts/generate-authored-fixture.ps1`, and `tests/helengine.ps1.builder.tests/CubeTestSubsetTests.cs`. Inspect existing generic packaging APIs and N64 sample generator before choosing their concrete call signatures.

**Boundary:** Consume the source project path; produce a staged source project, cooked manifest and final generated Core tree under an explicit output root. Keep the original scene identity. A machine-readable subset report records source hash, retained entities/components and excluded dependencies.

- [ ] Deserialize the actual scene and dependencies through the engine's readers. Record camera, mesh, material and script values, including any platform overrides. Compare with the authoring factory; use serialized values as authority.
- [ ] Write a behavioral test that loads the exported package and asserts one camera, one mesh, the original asset IDs and transform values, plus CubeTestSpinComponent. Assert no UI/light/control components or font/icon dependencies remain. Exercise real serialization rather than matching generated source text.
- [ ] Run that test before implementation; record the failure showing the missing subset/export behavior.
- [ ] Implement staging and explicit entity/component selection using typed scene data. Preserve references, ancestor transforms where needed, and material values. Perform filtering before dependency discovery and code generation.
- [ ] Reject ambiguous/missing camera or cube selection and unsupported retained components. Test those cases and verify the source scene hash is unchanged.
- [ ] Provide this command, with no editor UI dependency:

```powershell
.\scripts\generate-authored-fixture.ps1 -ProjectRoot C:\dev\helprojs\demodisc -OutputRoot C:\dev\helworks\builds\helengine-ps1\cube-test-export
```

- [ ] Commit the exporter and its focused tests after they pass.

## Task 2: Prove generated Core and embedded assets fit

**Files:** Modify `Makefile`, `scripts/build.ps1`; create `builder/helengine.ps1.builder.csproj`, `builder/PlayStation1PlatformAssetBuilder.cs`, `builder/PlayStation1ContentStager.cs`, `builder/PlayStation1MaterialCooker.cs`, `builder/PlayStation1SceneCompatibilityValidator.cs`, `scripts/embed-authored-assets.py`, and corresponding builder/packing tests.

**Boundary:** Consume the generic PlatformBuildRequest and manifest-declared cooked files. Produce the final generated Core inputs, a sorted asset table of relative path/offset/length, and embedded payload; return a normal build report. Follow N64's builder contract rather than creating another editor export protocol.

- [ ] Write failing packer tests for truncated input, duplicate/unsafe paths, missing declared files and nondeterministic ordering. Verify undeclared staging files cannot enter the package.
- [ ] Implement a versioned little-endian archive with checked offsets and lengths. Sort by normalized relative path and align payload ranges to four bytes. Include its bytes in the linked retail image.
- [ ] Add an authored build mode accepting generated Core and cooked manifest paths. Require the final `helengine_core_unity.cpp` and scene component registrations, not the earlier incomplete unity output. Missing inputs must fail, never select diagnostic mode.
- [ ] Link the selected generated Core for MIPS I with the existing SDK. Resolve platform/runtime dependencies in their owning source or generator. Inspect linker map and existing memory-check output before continuing.
- [ ] Record linked footprint and remaining heap budget. If the minimal runtime cannot fit retail RAM, report the measured blocker; do not silently switch to a bespoke native scene format.
- [ ] Run packer/builder tests and an authored link/memory check; commit the integration checkpoint.

## Task 3: Load through Core

**Files:** Create `src/platform/ps1/Ps1RuntimeScene.hpp/.cpp`, `Ps1ContentStreamSource.hpp/.cpp`, `Ps1SceneBootstrap.hpp/.cpp`, and the minimal Core-required input/2D adapters. Modify `Ps1BootHost.hpp/.cpp` to select authored or diagnostic root scene at build time.

**Boundary:** The asset table supplies bounded read-only streams implementing the actual generated content interfaces. The runtime scene owns Core and adapters; PSYQo retains GPU and presentation ownership.

- [ ] Write stream tests for seek/read boundaries, zero-length reads, missing paths and truncated assets; run and observe failures first.
- [ ] Implement streams with independent cursors and no writable access to the embedded image. Reject integer overflow and out-of-range reads without reading adjacent assets.
- [ ] Initialize Core with required services and a catalog built from the manifest. Load the recorded startup scene through SceneManager and commit loading at the supported frame boundary.
- [ ] Validate one loaded camera and one drawable. Assert unsupported draws/services explicitly; empty 2D/input adapters must not hide retained UI/control dependencies.
- [ ] Advance Core with elapsed PS1 time using a bounded resume delta. Execute the generated spin component, preserving its authored angular speed; do not use the v0.1 native rotation clock to animate this cube.
- [ ] Emit startup-only scene ID, object counts and heap/guard diagnostics. Capture a target boot trace proving load completion and changing loaded object transforms.
- [ ] Test corrupt/missing assets and verify explicit startup failures; commit the scene-loading checkpoint.

## Task 4: Render the loaded camera and cube

**Files:** Create `src/platform/ps1/rendering/Ps1RenderManager3D.hpp/.cpp`, `Ps1RuntimeModel.hpp/.cpp`, `Ps1RuntimeMaterial.hpp/.cpp`, `Ps1AuthoredProjection.hpp/.cpp`; modify `Ps1RasterRenderer.hpp/.cpp`; add authored projection/packet tests to `scripts/test.ps1`.

**Boundary:** Consume Core's actual camera/model/material objects and world matrices. Convert to bounded GTE inputs and emit packets through the existing frame contexts. Keep the hardcoded cube path diagnostic-only.

- [ ] Write failing numeric tests with independently calculated projected coordinates for identity and a quarter-turn, translated camera, non-unit scale, reversed winding and layer mismatch. Include fixed-point overflow and out-of-range depth failures.
- [ ] Document and implement coordinate handedness, winding, world-unit scale, matrix quantization and projection conversion from the loaded camera. Derive bounds from the loaded vertices over the supported Y rotation.
- [ ] Implement opaque unlit material base color and indexed triangle submission. Use one context per frame and the existing guarded packet allocator/ordering table.
- [ ] Reject an unsafe pose before submitting packets. Test capacity exhaustion leaves guards intact. Do not copy the built-in cube radius as an authored-mesh bound.
- [ ] Run targeted host tests and fresh authored build. Inspect target triangle packets and loaded transforms at multiple rotations.
- [ ] Modify only the staged fixture's camera position, cube transform and color in separate export runs. Verify corresponding packet changes, proving the runtime uses authored data.
- [ ] Commit the authored rendering checkpoint after tests pass.

## Task 5: Verify and deliver

**Files:** Update `docs/Build.md` and `docs/Validation.md`. Keep probe code/results and artifacts below the external build root.

- [ ] Build fresh Release authored and diagnostic artifacts; run the existing retail-memory checker on each.
- [ ] Verify first authored draw, material color, clear color and generated-script rotation. Record source/package/EXE hashes with the command used.
- [ ] Repeat the 720-update guest VBlank cadence probe after startup. Require one VBlank per update for this minimal scene; investigate any deficit without weakening synchronization.
- [ ] Run an allocator/stack/packet-guard probe after scene load. Require no per-frame allocations and stable heap high-water across a rotation cycle and debugger pause/resume.
- [ ] Run the existing completion-stress mode to check the retained presentation path. Keep its diagnostic logging separate from the cadence measurement.
- [ ] Launch the verified authored EXE using `C:/dev/helworks/emus/ps1/duckstation-windows-x64-release/duckstation-qt-x64-ReleaseLTCG.exe` under Helena, with native settings and verified BIOS. Do not take screenshots without permission.
- [ ] Obtain the user's visual confirmation. Record physical hardware and disc boot as untested.
- [ ] Commit documentation and report the runnable artifact, measured cadence, remaining limits and commit IDs.
