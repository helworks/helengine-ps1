# PlayStation 1 Rendering Optimization

**The largest gains come from controlling what reaches the renderer, then making the surviving work cheap.** For a new native PS1 renderer, prioritize conservative visibility rejection, vertex reuse, compact GPU packets, CPU/GPU overlap, and a bounded pixel workload. Assembly scheduling becomes valuable after these architectural decisions are measured.

This report covers native PlayStation hardware, historical engine techniques, inspectable homebrew implementations, and a practical optimization sequence. It distinguishes documented hardware behavior, developer accounts, source-code observations, and proposed experiments. Repository and documentation status was checked on 15 September 2026. No new console benchmarks were performed.

## 1. Optimization priorities

| Priority | Change | Main cost reduced | First measurement |
|---|---|---|---|
| 1 | Reject rooms, cells, and objects before vertex work | CPU, GTE, packets, pixels | Visible objects / candidate objects |
| 2 | Transform shared positions once per visible batch | Repeated geometry work | Transformed vertices / unique vertices |
| 3 | Build compact packets in reusable frame arenas | CPU stores and submission overhead | Packet bytes and CPU build time |
| 4 | Overlap CPU preparation with prior GPU work | Serialized frame time | CPU, DMA, and drawing timeline |
| 5 | Reduce overdraw, texture footprint, and excess detail | GPU rasterization and memory traffic | Drawing time at fixed camera |
| 6 | Specialize and schedule measured hot loops | CPU stalls and instruction overhead | Cycles per surviving primitive |

This is a recommended investigation order, not a promise that every scene has the same bottleneck. A sprite-heavy game can start at fill cost; a densely instanced scene may spend most of its time preparing object matrices. Avoid selecting optimizations from triangle count alone.

The PS1 divides the job among CPU code, the GTE geometry coprocessor, DMA, and the GPU. The GPU accepts screen-space drawing commands rather than meshes; application code determines visibility and primitive order. The bare-metal cube and DMA examples expose these boundaries directly.[^9][^11]

**Recommended foundation:** use spicyjpeg's self-contained examples to understand the hardware interface; consider PSYQo for a C++ runtime; study psxsplash for world visibility and renderer integration. Their own source and license files make them more actionable starting points than an unattributed collection of retail-game code.[^8][^15][^19][^27]

The report's source catalogue includes exact paths and pinned revisions for the three repositories inspected locally. It also includes PSn00bSDK, Tundra, and hardware tests, with the qualifications that affect their use.

<!--page-->
## 2. Hardware budgets and data representation

Main RAM is 2 MiB. VRAM is a separate 1 MiB shared by framebuffers, texture images, and palettes. The CPU has a 4 KiB instruction cache and a 1 KiB scratchpad; ordinary application data does not benefit from a conventional data cache. These are separate pools, not a single 3 MiB heap.[^1]

The following framebuffer figures are calculated for two complete 16-bit storage surfaces, without texture-page alignment or other VRAM allocations:

| Render size | Pixels / frame | Two framebuffers | VRAM left before other allocations |
|---|---:|---:|---:|
| 256 x 240 | 61,440 | 240 KiB | 784 KiB |
| 320 x 240 | 76,800 | 300 KiB | 724 KiB |
| 512 x 240 | 122,880 | 480 KiB | 544 KiB |

Moving from 320 to 512 pixels wide at the same height increases framebuffer pixels by 60% and consumes another 180 KiB for double buffering. This is arithmetic, not a predicted 60% change in total frame time. The geometry workload might stay constant, and rasterization depends on primitive coverage and rendering mode.

**Recommendation:** start a general 3D engine at 320 x 240, then compare 256-wide and 512-wide variants using the same camera and content. Choose the output mode from measured visual benefit and available headroom. Treat PAL and NTSC pacing as explicit configurations rather than assuming a fixed 60 Hz presentation rate.

The GTE uses several numerical formats: matrix elements commonly have 12 fractional bits, screen offsets have 16, and projected screen coordinates become integers. Do not impose one fixed-point format on every quantity. Its depth FIFO saturates, so it cannot replace a signed camera-space coordinate for clipping decisions.[^3]

Use local mesh or cell coordinates that fit comfortably in the transform representation. Compute camera-relative translations in wider arithmetic before narrowing. At export time, check coordinate range, winding, UV range, index bounds, and primitive bounds. A cheap runtime works best when invalid assets cannot enter it.

For storage, keep packed positions separate from per-corner UVs and colors when that saves repeated transforms across texture seams. A split UV does not necessarily require a second transformed position. For runtime processing, use small visible batches with contiguous arrays rather than a graph of individually allocated face objects.

The CPU's load-delay and branch-delay rules matter in hand-written loops. A compiler flag cannot make an unsafe GTE access sequence correct. Inspect the generated MIPS code for unexpected divisions, memory traffic, and calls inside the face loop before replacing that loop with assembly.[^2]

<!--page-->
## 3. Historical techniques and their transferable lessons

### Crash Bandicoot: constrain the camera, move work offline

Andy Gavin's first-person account describes branching camera rails, precomputed visibility and polygon ordering, and environmental barriers that limited the visible workload. It also describes storing compressed per-frame vertex animation instead of reproducing the authoring rig at runtime. The reported visible-polygon targets were specific to Crash's pipeline, not universal PS1 limits.[^22]

**Application:** for a rail shooter, fixed-camera adventure, or guided platformer, export conservative visible-cell or visible-face lists for camera intervals. Precompute static draw order where it remains valid, then merge dynamic characters and effects into that order. Store visibility changes compactly if adjacent camera samples are similar.

Account for every allowed camera offset, shake, zoom, and branch transition. A visibility set calculated from one exact viewpoint can hide geometry that becomes visible between samples. Dynamic occluders need their own treatment. Precomputed lists save visibility and sorting work; their surviving vertices and pixels still require processing.

The animation tradeoff is equally useful: compare compressed vertex frames, rigid parts, and skeletal animation per asset. Vertex frames remove skinning work but increase animation storage and decoding demand. Export the representation that meets the combined CPU and memory budget rather than enforcing a single character format.

### Spyro: visibility-aware level art and separate distance representations

Insomniac artists Craig Stitt and John Fiorito describe building solid mountains, walls, cliffs, and buildings to hide distant geometry while preserving an open feeling. Their account also explains coordinating base textures and vertex colors. Independently, the Spyro-Scope reverse-engineering tool exposes distinct near high-detail and far low-detail meshes in the games' data.[^23][^24]

**Application:** build an inexpensive distant shell for large environments, with a near representation that replaces it as the player approaches. Test untextured vertex-colored geometry for the distant shell when it preserves the silhouette. Use mountains and building masses as real visibility boundaries, not just visual decoration.

LOD transitions must remain compatible with draw ordering and occlusion. Add hysteresis to prevent repeated switching near a threshold. Preserve shared boundaries, or use deliberate overlap/skirt geometry where suitable, so the two representations do not expose cracks. A distant shell should not be drawn underneath an expensive near surface without measuring the extra fill.

### The broader engine lesson

The useful historical pattern is specialization: camera restrictions, level structure, animation representation, and art style can all remove work before it becomes an instruction-level problem. A general-purpose engine should offer these strategies as asset or scene options rather than requiring every game to pay for every feature.

No verified, permissively licensed original Crash or Spyro retail renderer is established by these sources. The historical evidence supports the techniques; the homebrew repositories provide inspectable implementation starting points.

<!--page-->
## 4. Visibility before geometry

**Recommended sequence:** determine candidate regions; test region bounds; select LOD; test object bounds; transform surviving geometry; reject faces; emit packets. Coarse rejection must be conservative: drawing extra geometry is preferable to incorrectly hiding a visible surface.

| World structure | Starting strategy | Main limitation |
|---|---|---|
| Rooms and corridors | Room adjacency plus portals | Incorrect portals can hide required rooms |
| Terrain and open environments | Cells or a BVH plus frustum tests | A frustum test does not establish occlusion |
| Fixed or rail cameras | Precomputed conservative visibility | Camera freedom expands the stored sets |
| Small arena | Object spheres/AABBs and distance LOD | A complex world structure may cost more than it saves |

psxsplash provides a concrete room/portal path in `src/renderer.cpp`, including projected portal rectangles and cell tests. Its separate `src/bvh.cpp` uses iterative traversal to produce triangle references from visible nodes. These are useful native examples of reducing the set that reaches the triangle processor.[^25][^26]

**Implementation recommendation:** split static geometry spatially at export time. Store compact bounds and contiguous face ranges per cell. Start with object-level bounding spheres or AABBs and add finer hierarchy only if the saved transforms exceed traversal overhead. Track how many objects were rejected before their matrices were prepared.

For a portal system, intersect each portal's projected region with the accumulated region from earlier portals. Include numerical margins. Handle portals crossing the near plane conservatively. When a room can be reached through several paths, visibility bookkeeping must preserve the union of relevant regions or safely revisit the room; a single visited bit can otherwise discard a wider later view.

For all hierarchy implementations, define overflow behavior. A full traversal stack or visible-reference array must produce a visible diagnostic and a controlled fallback. Quietly truncating the list produces scene-dependent missing geometry that resembles a clipping bug.

Do not confuse frustum rejection with hidden-surface removal. A building behind another building remains inside the camera frustum. To avoid drawing it, add an appropriate conservative occlusion mechanism, a precomputed visibility set, or scene structure that establishes it cannot contribute.

Backface rejection remains valuable after projection, but it is late: the transform cost has already been paid. Use the sign appropriate to the engine's winding and screen-coordinate convention; validate with an asymmetric test mesh. For two-sided content such as leaves, choose the material's behavior explicitly rather than disabling rejection globally.

Measure tiny-face rejection separately. Removing subpixel or nearly degenerate distant faces can reduce setup cost, but it can also erase thin silhouettes and wires. Prefer an authored LOD for important distant shapes. The acceptance criterion is stable output along a moving camera path, not just one matching screenshot.

<!--page-->
## 5. GTE throughput and vertex reuse

The documented kernel costs are 15 cycles for `RTPS` and 23 for `RTPT`; three single transforms therefore occupy 45 kernel cycles versus 23 for the triple command. `NCLIP` takes 8, `AVSZ3` 5, and `AVSZ4` 6. These figures exclude surrounding loads, stores, setup, and pipeline effects.[^3]

The approximately 49% reduction from 45 to 23 applies only to that transform-kernel portion. It is not a frame-rate claim. Load three positions together when practical, and schedule useful CPU work between issuing a GTE command and consuming its result.

### Choose between face-local and shared-vertex processing

| Strategy | Benefit | Cost to measure |
|---|---|---|
| Transform each face's vertices | Simple, compact streaming loop | Shared positions are transformed repeatedly |
| Transform unique vertices into a temporary array | Reuses screen coordinates and depth | Extra stores, reloads, and temporary memory |
| Transform small indexed batches | Bounds scratch size and reuse scope | Export/runtime batch complexity |

The PSYQo teapot example contains a useful three-pass implementation: project unique vertices in groups of three, calculate vertex lighting, then assemble and reject indexed quads. It also uses triple lighting commands and maintains projected-position, depth, and shade arrays. It is a readable starting point for measuring shared-vertex processing.[^18]

**Recommendation:** begin with batches of a few dozen positions, then sweep batch sizes. Deduplicate only attributes with the same transformation semantics: skinning weights, morph results, and different object transforms can prevent reuse even when source positions match. Do not transform an entire level merely because it has an index buffer.

Move object/view matrix composition outside the face loop. For static geometry, consider exporting cell-local positions or a common static-world space. The benefit must be balanced against the numeric range and the cost of loading another matrix for each small cell.

### Pipeline scheduling

Use a conservative schedule first: issue a transform, prepare unrelated packet fields or loop bookkeeping, then read results. Avoid changing registers still consumed by the active GTE operation. PSYQo exposes low-level kernels and safe/unsafe register access facilities; its kernel header explicitly places hazard responsibility on the programmer.[^17]

New input-latency research identifies opportunities to refill some registers before an operation finishes. Its published tables were measured on one SCPH-5501, with code-layout sensitivity and explicit methodological limits. Treat early register overwrites as an advanced, separately validated optimization.[^20][^21]

When comparing implementations, keep input geometry and output packets equivalent. Record total frame time as well as cycles in the transform loop: a loop with fewer GTE instructions can still lose if it increases RAM traffic. Preserve a straightforward reference path for numerical comparisons.

<!--page-->
## 6. Clipping, subdivision, and sorting correctness

`NCLIP` evaluates projected winding; it does not cut a polygon against the near plane. The GTE's perspective divide saturates when depth is too small, including `SZ <= H/2`. The GPU also rejects polygon spans exceeding 1023 horizontally or 511 vertically, despite clipping ordinary drawing to the configured draw area.[^3][^4]

**Recommended architecture:** give fully safe geometry a short fast path and send exceptional faces to a bounded clipping path. Keep signed camera-space data for boundary cases, clip against a chosen positive near plane, then project generated vertices. Choose that plane consistently with the projection parameters and numerical limits.

Use camera-space edge interpolation to calculate intersections and carry UV/color attributes consistently. A triangle cut by one plane becomes zero, one, or two triangles. Fully reject it only when the entire triangle is outside the relevant half-space. Rejecting every triangle with one near-plane vertex will create holes.

A projected vertex clamp is not a substitute for clipping: it changes shape and interpolation. Likewise, repeatedly subdividing a face may reduce artifacts but does not prove it has been clipped correctly. Exercise the slow path with the camera inside geometry, moving across large floors, and looking nearly parallel to a wall.

### Subdivision as a controlled quality expense

Because texture mapping is affine, subdivision can reduce visible warping; smaller pieces can also reduce depth-order ambiguity. It increases transforms, packet work, and triangle setup, so it should be a local quality decision with a budget. Prefer offline subdivision where camera relationships are predictable; reserve dynamic subdivision for demonstrably problematic nearby surfaces.

A practical proposed trigger combines screen-space size, variation in vertex depth, proximity to the near plane, and a maximum output-face budget. A size-only trigger can subdivide a large, nearly front-facing wall unnecessarily. Add a maximum recursion depth and a deterministic fallback when the output budget is exhausted.

Adjacent faces need compatible edge splits. Cache shared edge decisions or use authored subdivision boundaries; otherwise different recursion choices can produce T-junctions. This matters especially under integer projection, where a seam can flicker as the camera moves.

### Ordering remains approximate

More ordering-table buckets reduce depth quantization but do not solve intersecting polygons or surfaces whose order changes across their area. Depth bias can be a controlled material or asset parameter for decals and similar cases, but it is not a geometric solution to interpenetration. Split troublesome geometry or use a more suitable static ordering representation.

Do not globally batch 3D primitives by material. With no Z-buffer, an opaque surface drawn late can cover a nearer surface. Material grouping is safe only where relative visibility is preserved, such as a proven independent region or a correctly ordered fragment. Semi-transparent surfaces must also occupy the correct relationship to the surrounding opaque geometry; a universal final transparent pass is insufficient.

<!--page-->
## 7. Ordering tables and GPU packet cost

An ordering table is a software structure in RAM. DMA follows the resulting linked list and sends GPU words in that order. A linked-list node has a header containing a 24-bit next address and an 8-bit payload-word count. The GPU does not sort the list for the application.[^5]

For a conventional head-insertion bucket, the most recently inserted primitive executes first within that bucket. The inspected PSYQo `OrderingTable::insert` also links at the head. Verify a library's actual insertion behavior when constructing adjacent multipass primitives; its descriptive documentation may be less precise than its implementation.[^16]

The following sizes are calculated from GPU command layouts, assuming one four-byte DMA tag per primitive. They exclude the ordering table and draw-state commands.[^4]

| Primitive | GPU words | Bytes including tag |
|---|---:|---:|
| Flat untextured triangle, F3 | 4 | 20 |
| Gouraud untextured triangle, G3 | 6 | 28 |
| Flat textured triangle, FT3 | 7 | 32 |
| Gouraud textured triangle, GT3 | 9 | 40 |
| Flat untextured quad, F4 | 5 | 24 |
| Gouraud untextured quad, G4 | 8 | 36 |
| Flat textured quad, FT4 | 9 | 40 |
| Gouraud textured quad, GT4 | 12 | 52 |

Thus one GT4 is 52 bytes against 80 for two separate GT3 packets, a 35% packet-space saving. It still rasterizes as two triangles. Use quads where their fixed split matches the intended surface; do not convert arbitrary adjacent triangles solely to obtain the smaller packet.[^4]

**Recommendation:** preinitialize invariant packet fields, then patch coordinates, depth links, and changing colors/UVs. A frame arena provides predictable allocation and a high-water mark. Align headers correctly and check capacity before writing. The same packet memory must remain stable until its DMA reader has finished.

For a conventional table with 4096 four-byte entries, one table costs 16 KiB and two cost 32 KiB; a 1024-entry table costs one quarter as much. Some libraries add sentinels or metadata. Sweep table size with a camera path that includes close intersecting details and the distant horizon, measuring both sort artifacts and clearing/traversal cost.

A preordered static fragment can share a DMA header across several commands, reducing tag overhead. It also reduces freedom to interleave other surfaces. Keep fragment lengths within the hardware and SDK constraints and preserve GPU FIFO handling. The bare-metal DMA example deliberately uses conservative packet sizes; PSYQo's GPU implementation has its own transfer scheduling.[^9][^15]

<!--page-->
## 8. DMA overlap, memory traffic, and frame ownership

PSYQo's chaining model sends prepared fragments asynchronously and requires double-buffered fragments and ordering tables. Its concepts guide contrasts this with blocking per-primitive submission and explains reuse of cached packet fields.[^15]

**Recommended frame model:** present one completed framebuffer while the GPU draws another; let the CPU prepare a future command arena when one is free. Keep framebuffer ownership and command-arena ownership as separate state machines. Two buffers can provide useful overlap, but poorly placed waits can serialize the same design.

| Resource/event | Safe condition | Common error |
|---|---|---|
| Command arena | DMA no longer reads it | Overwriting coordinates while transfer is active |
| Render target | All preceding rendering to it is complete | Displaying a partially drawn frame |
| Texture upload destination | Earlier users are finished or correctly ordered | Replacing a texture still used by queued draws |
| Display swap | Completed image plus intended presentation boundary | Assuming VBlank itself proves drawing finished |

DMA completion establishes transfer progress; it does not by itself establish that all rasterization has finished. Use the chosen SDK's documented drawing-completion mechanism, and verify that it measures the event the application actually needs. Hardware status tests are useful for validating low-level synchronization assumptions.[^32]

CPU/GPU overlap is constrained by the memory system. During DMA, the CPU can run under restricted conditions using cached instructions, registers, scratchpad, and coprocessor operations; RAM and I/O access can stall it. DMA scheduling and other active channels therefore affect the benefit of overlap.[^5]

**Optimization experiment:** compare a small transform/packet loop with a RAM-resident temporary array against a scratchpad-resident working block. Keep final DMA packets in DMA-addressable main RAM. Allocate scratchpad regions centrally and reserve space for any stack or interrupt convention in use.

Scratchpad is data storage, not an executable code cache. Keep hot code compact enough to benefit from the instruction cache instead. Aggressive inlining or unrolling can increase instruction-cache conflict and erase a local arithmetic saving.[^1]

Use dense arrays and minimize repeated pointer chasing. Prepack stable RGB/command and UV/page information where the API permits it. Keep instrumentation out of inner loops: increment counters cheaply, then print or upload summaries after the measured interval.

Streaming belongs in the frame budget. Preload the next visibility region before it becomes visible, stage decompression, and schedule VRAM changes at known safe boundaries. A renderer benchmark with all assets resident cannot predict the spikes of a game that streams textures while drawing. Measure both operating modes before fixing the final memory allocation.

<!--page-->
## 9. Texture locality and software mipmaps

Use texture format as a material-level choice. The calculated storage for a 64 x 64 image is:

| Format | Texel storage | One palette | Combined storage |
|---|---:|---:|---:|
| 4 bpp indexed | 2,048 bytes | 32 bytes | 2,080 bytes |
| 8 bpp indexed | 4,096 bytes | 512 bytes | 4,608 bytes |
| Direct color, 16-bit storage | 8,192 bytes | None | 8,192 bytes |

Palettes may be shared, so combined storage is an illustrative standalone allocation. The indexed-color bare-metal example shows actual CLUT and texture-page setup.[^14]

The GPU has a 2 KiB texture cache and a separate CLUT cache. Korth's current cache documentation describes fixed cache-line mapping, texture-page changes that invalidate cached texture data, and palette reloads on changed CLUT addresses. Locality therefore depends on address layout and access order, not just total texture bytes.[^7]

**Recommendation:** begin with 4 bpp for materials whose appearance survives palette reduction. Group textures commonly visible together into stable page layouts and avoid unnecessary CLUT variation. Test atlas arrangements at fixed draw order; an atlas that fits in VRAM can still produce repeated cache conflicts.

There is a documentation discrepancy: the community GPU page gives an 8 bpp footprint of 32 x 64 texels, while Korth's current cache chapter gives 64 x 32. Both represent 2 KiB, but orientation affects layout assumptions. Use the newer cache mapping as an experimental starting point and validate on target hardware.[^4][^7]

### Concrete software mipmapping sample

`src/10_mipmaps/main.c` in ps1-bare-metal packs several downscaled texture variants into one sheet. It estimates projected quad area from `NCLIP` and uses the GTE leading-bit facility to select a mip level. The source explicitly assumes equal UV coverage for its plane faces; it is not a universal UV-footprint estimator.[^13]

**Recommended generalization:** store a precomputed UV-area or texel-density scale per face/material and combine it with projected size. Treat long thin or steeply oblique faces conservatively; screen area alone can select an overly coarse mip in one direction. Add hysteresis around thresholds and compare moving-camera image stability.

Mipmaps exchange extra stored images and selection work for a potentially smaller sampling footprint and reduced distant shimmer. A full power-of-two chain approaches one third additional texel storage before palette sharing and packing. This geometric-series calculation does not imply a fixed rendering speedup.

Upload mip variants ahead of use where possible. Avoid changing residency every time a polygon changes mip level. Compare mip selection enabled and disabled with identical geometry, camera motion, and material order; log drawing time and texture-page/CLUT changes alongside image captures.

<!--page-->
## 10. Fill cost, lighting, and 2D paths

Korth's measured rasterization model separates setup, scanline, pixel, cache-miss, and transparency costs, with differences between GPU revisions. It also describes overlap between a triangle's setup and preceding rasterization. Consequently, neither polygon count nor covered pixels alone predicts total GPU time.[^6]

**Recommendation:** track primitive mode, approximate covered area, and overlap layers. A small number of full-screen blended effects can be more expensive than a larger set of small opaque surfaces. Equally, excessive tiny triangles can be limited by setup even when their combined area is modest.

Opaque cutout textures and semi-transparent rendering are different. A zero texture color is discarded; blending is controlled by the primitive mode and texture color's high bit. Do not enable semi-transparency on an otherwise opaque material just because its image has transparent holes.[^4]

Trim empty sprite margins at export time. Compare a tight polygon outline against a large rectangular cutout, because tighter coverage saves pixel work but adds geometry/setup. For particles, cap both count and estimated screen coverage; count alone misses the cost of nearby effects.

### Lighting and material specialization

Bake static environmental illumination into vertex colors or textures where appropriate. Use runtime GTE lighting for objects that need it and consider face lighting for distant models. The bare-metal lighting example provides a small reference for normal and light-matrix handling.[^12]

Keep separate paths for flat untextured, Gouraud untextured, flat textured, and Gouraud textured content. The right choice is determined by the look and the measured workload. Untextured Gouraud still incurs color interpolation; raw textured rendering and a neutral modulation color are not interchangeable API flags.

Fog only reduces workload when it enables distance rejection or a cheaper representation. Blending more fog into the same fully submitted scene can increase work. For arbitrary colored fog on textured surfaces, inspect how the renderer implements the effect: the inspected psxsplash path can emit an untextured fog layer plus an additive textured layer, increasing packet and fill cost.[^25]

### Dedicated 2D rendering

Use GPU rectangle/sprite primitives for suitable unrotated, unscaled, axis-aligned elements such as HUD glyphs and tiles. Their sampling behavior is not a general substitute for an arbitrarily scaled or rotated textured quad. The GPU documentation and timing measurements distinguish these primitive families.[^4][^6]

For a billboard, transform its center and derive screen placement when that representation is suitable; use textured quads if its texture must scale independently of its source texel dimensions. Keep fixed-size UI packets cached and update only changed pieces. Use the dedicated VRAM fill operation for an appropriate clear, observing its alignment and state behavior.[^6]

Render-to-texture effects, projected shadows, reflections, and full-screen overlays should each have a measured budget. The PSYQo torus example is a useful native reference for a textured animated object with a projected shadow; treat the effect as additional work to inspect, not a free capability.[^34]

<!--page-->
## 11. Source code to study first

The entries below are native implementations or hardware tests unless explicitly described as desktop analysis. Paths are relative to each repository root. The Sources pages contain clickable exact-file links.

| Repository / path | What to extract from it | Qualification |
|---|---|---|
| ps1-bare-metal: `src/03_dmaChain/main.c` | Linked GPU commands and buffer ownership | Clear baseline, not a peak-throughput benchmark |
| ps1-bare-metal: `src/07_orderingTable/main.c` | Ordering-table construction | Learn actual link order before batching |
| ps1-bare-metal: `src/08_spinningCube/main.c` | GTE projection, winding, depth bucket | Extend boundary handling for real scenes |
| ps1-bare-metal: `src/09_faceLighting/main.c` | GTE light and normal setup | Face-lighting reference |
| ps1-bare-metal: `src/10_mipmaps/main.c` | Area-based mip selection | Assumes the sample's UV layout |
| nugget: `psyqo/examples/hello-chained/` | C++ asynchronous packet submission | Follow fragment lifetime rules |
| nugget: `psyqo/examples/teapot/teapot.cpp` | Unique-vertex projection and lighting | Inspect memory cost and numerical assumptions |
| nugget: `psyqo/examples/torus/torus.cpp` | Textured 3D and projected shadow | More complex example |
| psxsplash: `src/renderer.cpp` | Room/portal integration and face emission | Contains quality/performance tradeoffs |
| psxsplash: `src/bvh.cpp` | Iterative visibility traversal | Review capacity and overflow behavior |
| ps1-tests: `gpu/benchmark/`, `gpu/bandwidth/` | Rasterization and transfer experiments | Existing build uses PSn00bSDK |
| nugget: `tests/gte-latency/` | Reproduce GTE register timing probes | Results have console/build limitations |

These paths were checked against repository contents.[^9][^10][^11][^12][^13][^18][^21][^25][^26][^32][^34]

**Suggested reading order:** DMA chain, ordering table, cube, lighting, mipmaps; then PSYQo's ownership model and indexed teapot; finally psxsplash's world traversal. That sequence separates hardware submission, geometry, and world visibility instead of learning them through one large renderer.

<!--page-->
## 12. Benchmark plan and implementation sequence

**Establish a replayable baseline before changing algorithms.** Use fixed camera paths and deterministic object motion. Record console model, video mode, compiler version/options, SDK revision, scene revision, and enabled effects. Keep debug output outside timed regions.

Use CPU timers for stage timing and an appropriate GPU-completion boundary for drawing measurements. PS1 counters are 16-bit; Timer 2 can use the system clock divided by eight. Handle wraparound with an extended counter or measure sufficiently short intervals. A single modular subtraction is not enough for arbitrary full-frame timing.[^33]

| Experiment | Hold constant | Vary | Question answered |
|---|---|---|---|
| Coarse culling | Camera and visible result | Culling level | Are rejected transforms worth traversal cost? |
| Vertex reuse | Mesh and material modes | Face-local vs indexed batches | Do saved transforms exceed temporary traffic? |
| Submission | Identical command content | Blocking vs chained | How much useful overlap is realized? |
| Fill cost | Geometry and camera | Resolution, effect layers | Is the scene limited by raster work? |
| Texture locality | Primitive order | Page/CLUT layout and mip selection | Does memory access dominate? |
| Tessellation | Camera replay | Thresholds and output budget | What is the quality cost per added face? |
| CPU hot loop | Equivalent output | Compiler settings / assembly | Does local speedup improve the whole frame? |

Capture median, high-percentile, and worst observed frame times after warmup, plus separate counters for transformed positions, emitted triangles/quads, packet bytes, table occupancy, subdivision output, and streamed bytes. Ensure that a faster run has not silently dropped geometry because a fixed-capacity buffer filled.

For isolated drawing tests, submit a controlled command stream and measure through actual drawing completion. For production tests, include audio, streaming, interrupts, and normal frame pacing. Do not add together overlapping CPU and GPU durations and label the sum frame time. Presentation waits should be reported separately from work.

### Proposed delivery stages

1. **Baseline:** working double buffering, correct clipping/order, a reproducible scene, and stage counters.
2. **Visibility:** conservative object/cell rejection and useful LODs; prove no missing surfaces on camera replays.
3. **Data path:** packed assets, shared-position batches, reusable packet arenas, and bounded allocations.
4. **GPU budget:** texture locality, software mipmaps, suitable primitive modes, and capped effect coverage.
5. **Scheduling:** asynchronous submission, scratchpad experiments, then narrowly targeted assembly.

Retain only changes that improve whole-frame performance at acceptable visual quality. Validate on real hardware, including additional GPU revisions for timing-dependent optimizations; emulator timings alone do not establish console performance.

<!--page-->
## 13. Reuse, evidence limits, and immediate next steps

### License and provenance findings

| Project | Inspected license/status | Use in this report |
|---|---|---|
| ps1-bare-metal | MIT root; MIT/ISC source notices | Preferred self-contained hardware examples |
| nugget / PSYQo | MIT root and relevant source headers; dependencies have their own notices | Preferred C++ implementation reference |
| psxsplash | MIT root; PSYQo-based runtime | Visibility and integration reference |
| PSn00bSDK | MPL-2.0 core; example exception; separately licensed tools | Useful reference with an explicit provenance caveat |
| Tundra | GPL-3.0; PSn00bSDK dependency; inactive repository | Architecture and thesis study |
| Spyro-Scope | GPL-3.0; archived desktop tool | Evidence about near/far game data |
| ps1-tests | MIT root; PSn00bSDK build dependency | Hardware validation and benchmark design |

License findings come from the repositories themselves.[^8][^19][^24][^27][^29][^30][^32] They identify declared terms, not an independent audit of every dependency or asset.

PSn00bSDK's current README explicitly says some GTE assembly was lifted from Sony libraries and lists rewriting it as unfinished work. Its license file also shows that describing the entire current SDK as simply ISC is inaccurate. For a new project seeking straightforward code provenance, favor the self-contained examples or the relevant PSYQo components until this issue is resolved.[^28][^29]

### What the evidence does and does not establish

Historical developer accounts substantiate design strategies, not repeatable speedup factors for a different engine. Inspected homebrew code provides concrete mechanisms, but this report has not compiled or run those repositories. Published GPU timing models contain measurement qualifications; aggressive GTE overlap results have limited hardware coverage.

The Tundra thesis is an additional caution: its experimental setup uses PCSX-Redux and DuckStation, and it explicitly discusses inaccurate rasterization timing. Its title page says June 2024, while the repository description refers to fall 2024; the bibliographic entry follows the document itself.[^31]

### Immediate implementation choice

Start with a textured test scene containing an indexed object, a large floor crossing the near plane, a hidden room, distant geometry, and several overlapping effects. Add counters before attempting assembly. Implement conservative coarse culling and a shared-position batch path, then compare against the baseline.

If the scene is CPU-limited, concentrate on object setup, vertex reuse, packed packets, and scheduling. If drawing is limiting it, reduce visible pixel work and expensive material modes, then test texture residency and mipmaps. If stutters occur only during movement, examine streaming and worst-case visibility transitions before changing the arithmetic kernel.

<!--sources-->
## Sources

Hardware documentation and moving repository pages were accessed on 15 September 2026. Repository-file citations below use pinned revisions when the repository was inspected locally. Dates are publication dates where established; otherwise the access date applies. License descriptions refer to the inspected revision.

[^1]: psx-spx contributors / Martin Korth. [Memory Map](https://psx-spx.consoledev.net/memorymap/). Memory pools, instruction cache, and scratchpad.
[^2]: psx-spx contributors / Martin Korth. [CPU Specifications](https://psx-spx.consoledev.net/cpuspecifications/). MIPS delays, loads, and stores.
[^3]: psx-spx contributors / Martin Korth. [Geometry Transformation Engine](https://psx-spx.consoledev.net/geometrytransformationenginegte/). Commands, numerical formats, saturation, and instruction cycle counts.
[^4]: psx-spx contributors / Martin Korth. [Graphics Processing Unit](https://psx-spx.consoledev.net/graphicsprocessingunitgpu/). Command formats, raster bounds, texture modes, and ordering.
[^5]: psx-spx contributors / Martin Korth. [DMA Channels](https://psx-spx.consoledev.net/dmachannels/). Linked-list headers and CPU operation during DMA.
[^6]: Martin Korth. [GPU Rendering Timings](https://problemkaputt.de/psxspx-gpu-rendering-timings.htm). Current no$psx v2.3 documentation, copyright 2026. Measured rasterization model and its caveats.
[^7]: Martin Korth. [GPU Texture Caching](https://problemkaputt.de/psxspx-gpu-texture-caching.htm). Current no$psx v2.3 documentation, copyright 2026. Cache mapping, CLUT behavior, and invalidation.
[^8]: spicyjpeg. [ps1-bare-metal README](https://github.com/spicyjpeg/ps1-bare-metal/blob/33df9422bd5191def47f631b6fe2ffa647822502/README.md) and [LICENSE](https://github.com/spicyjpeg/ps1-bare-metal/blob/33df9422bd5191def47f631b6fe2ffa647822502/LICENSE). Self-contained C examples; MIT/ISC terms.
[^9]: spicyjpeg. [src/03_dmaChain/main.c](https://github.com/spicyjpeg/ps1-bare-metal/blob/33df9422bd5191def47f631b6fe2ffa647822502/src/03_dmaChain/main.c). GPU DMA-chain tutorial.
[^10]: spicyjpeg. [src/07_orderingTable/main.c](https://github.com/spicyjpeg/ps1-bare-metal/blob/33df9422bd5191def47f631b6fe2ffa647822502/src/07_orderingTable/main.c). Ordering-table tutorial.
[^11]: spicyjpeg. [src/08_spinningCube/main.c](https://github.com/spicyjpeg/ps1-bare-metal/blob/33df9422bd5191def47f631b6fe2ffa647822502/src/08_spinningCube/main.c). GTE projection example.
[^12]: spicyjpeg. [src/09_faceLighting/main.c](https://github.com/spicyjpeg/ps1-bare-metal/blob/33df9422bd5191def47f631b6fe2ffa647822502/src/09_faceLighting/main.c). Runtime face lighting.
[^13]: spicyjpeg. [src/10_mipmaps/main.c](https://github.com/spicyjpeg/ps1-bare-metal/blob/33df9422bd5191def47f631b6fe2ffa647822502/src/10_mipmaps/main.c). Software mip selection and its assumptions.
[^14]: spicyjpeg. [src/05_palettes/main.c](https://github.com/spicyjpeg/ps1-bare-metal/blob/33df9422bd5191def47f631b6fe2ffa647822502/src/05_palettes/main.c). Indexed textures and palettes.
[^15]: PCSX-Redux authors. [psyqo/CONCEPTS.md](https://github.com/pcsx-redux/nugget/blob/625da805c4a9bfd56dd9b2b17409805c0591dd42/psyqo/CONCEPTS.md) and [psyqo/gpu.hh](https://github.com/pcsx-redux/nugget/blob/625da805c4a9bfd56dd9b2b17409805c0591dd42/psyqo/gpu.hh). Fragment ownership, chaining, and transfer API.
[^16]: PCSX-Redux authors. [psyqo/ordering-table.hh](https://github.com/pcsx-redux/nugget/blob/625da805c4a9bfd56dd9b2b17409805c0591dd42/psyqo/ordering-table.hh). Actual insertion implementation and table representation.
[^17]: PCSX-Redux authors. [psyqo/gte-kernels.hh](https://github.com/pcsx-redux/nugget/blob/625da805c4a9bfd56dd9b2b17409805c0591dd42/psyqo/gte-kernels.hh) and [psyqo/gte-registers.hh](https://github.com/pcsx-redux/nugget/blob/625da805c4a9bfd56dd9b2b17409805c0591dd42/psyqo/gte-registers.hh). GTE wrappers and hazard handling.
[^18]: PCSX-Redux authors. [psyqo/examples/teapot/teapot.cpp](https://github.com/pcsx-redux/nugget/blob/625da805c4a9bfd56dd9b2b17409805c0591dd42/psyqo/examples/teapot/teapot.cpp). Shared-vertex projection, triple lighting, and packet assembly.
[^19]: PCSX-Redux authors. [nugget LICENSE](https://github.com/pcsx-redux/nugget/blob/625da805c4a9bfd56dd9b2b17409805c0591dd42/LICENSE) and [PSYQo README](https://github.com/pcsx-redux/nugget/blob/625da805c4a9bfd56dd9b2b17409805c0591dd42/psyqo/README.md). MIT terms and C++ library scope.
[^20]: psx-spx contributors. [GTE Pipeline Timings](https://psx-spx.consoledev.net/gtepipelinetimings/). Input-latency measurements, methodology, and SCPH-5501 limitation.
[^21]: PCSX-Redux authors. [tests/gte-latency/README.md](https://github.com/pcsx-redux/nugget/blob/625da805c4a9bfd56dd9b2b17409805c0591dd42/tests/gte-latency/README.md) and [RESULTS.md](https://github.com/pcsx-redux/nugget/blob/625da805c4a9bfd56dd9b2b17409805c0591dd42/tests/gte-latency/RESULTS.md). Reproducible timing probe.
[^22]: Andy Gavin, with Jason Rubin commentary. [Making Crash Bandicoot - part 3](https://all-things-andy-gavin.com/2011/02/04/making-crash-bandicoot-part-3/). 4 February 2011. First-person account of visibility, sorting, animation, and tools.
[^23]: Craig Stitt and John Fiorito. [Lessons in Color Theory for Spyro the Dragon](https://www.gamedeveloper.com/art/lessons-in-color-theory-for-i-spyro-the-dragon-i-). Game Developer / Gamasutra, 2 May 2000. First-person art and visibility constraints.
[^24]: FranklyGD. [Spyro-Scope](https://github.com/FranklyGD/Spyro-Scope). Near/far mesh inspection tool; GPL-3.0; archived 2 April 2026.
[^25]: psxsplash contributors. [src/renderer.cpp](https://github.com/psxsplash/psxsplash/blob/cbf08035c826cb3d9c2cbba65f17066cd44466b3/src/renderer.cpp). Portal traversal, triangle processing, subdivision, and fog paths.
[^26]: psxsplash contributors. [src/bvh.cpp](https://github.com/psxsplash/psxsplash/blob/cbf08035c826cb3d9c2cbba65f17066cd44466b3/src/bvh.cpp). Iterative bounded BVH traversal.
[^27]: psxsplash contributors. [README](https://github.com/psxsplash/psxsplash/blob/cbf08035c826cb3d9c2cbba65f17066cd44466b3/README.md) and [LICENSE](https://github.com/psxsplash/psxsplash/blob/cbf08035c826cb3d9c2cbba65f17066cd44466b3/LICENSE). Runtime scope and MIT terms.
[^28]: Lameguy64 and contributors. [PSn00bSDK README](https://github.com/Lameguy64/PSn00bSDK). See To-do List, libpsxgte. Maintainer's explicit provenance warning.
[^29]: Lameguy64 and contributors. [PSn00bSDK LICENSE.md](https://github.com/Lameguy64/PSn00bSDK/blob/master/LICENSE.md). MPL-2.0 core, example exception, and other component licenses.
[^30]: Malte Bryndum Pedersen. [Tundra README](https://github.com/maltebp/tundra) and [license.md](https://github.com/maltebp/tundra/blob/main/license.md). GPL-3.0, C++20, PSn00bSDK dependency, and project status.
[^31]: Malte Bryndum Pedersen. [A general-purpose game engine for the original Sony PlayStation](https://github.com/maltebp/tundra/blob/main/thesis.pdf). MSc thesis, IT University of Copenhagen, June 2024 per title page. Sections 4.1, 5.1, 6.1.1, and 6.2; experimental limits and optimization discussion.
[^32]: JaCzekanski and contributors. [ps1-tests](https://github.com/JaCzekanski/ps1-tests), especially gpu/benchmark, gpu/bandwidth, gpu/gpustat, gpu/clut-cache, and dma/otc-test. MIT hardware-test suite; PSn00bSDK build dependency.
[^33]: psx-spx contributors / Martin Korth. [Timers](https://psx-spx.consoledev.net/timers/). Counter width, clock selection, and wrap behavior.
[^34]: PCSX-Redux authors. [psyqo/examples/torus/torus.cpp](https://github.com/pcsx-redux/nugget/blob/625da805c4a9bfd56dd9b2b17409805c0591dd42/psyqo/examples/torus/torus.cpp) and [hello-chained.cpp](https://github.com/pcsx-redux/nugget/blob/625da805c4a9bfd56dd9b2b17409805c0591dd42/psyqo/examples/hello-chained/hello-chained.cpp). Native textured 3D and chaining examples.
