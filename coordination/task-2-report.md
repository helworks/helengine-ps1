# Task 2 report: bounded static unlit PS1 cube

Status: **DONE_WITH_CONCERNS**. Task2 implementation, host tests, target
builds, and target packet probes are complete. Full framebuffer pixel
inspection and physical-console validation remain pending.

## Implementation

- Added persistent two-context PS1 packet ownership in
  `src/platform/ps1/rendering/Ps1FrameContext.*`: typed triangle storage,
  `OrderingTable<256>`, 4 KiB packet budget, sequence-based reclamation, and
  complete-or-cancel submission.
- Added portable `Ps1PacketBudget`, bounded signed `Ps1DepthBucket`, raw ±64
  cube mesh, and fixed-point `Ps1RasterRenderer` with explicit GTE setup,
  immediate FLAG capture, signed NCLIP culling, flat colors, and low-frequency
  pose/submission/failure diagnostics.
- Added deterministic DepthOrder reversal and host policy tests for packet
  capacity, depth bounds, mesh indices, outward winding, and color pairing.
- Extended `patches/nugget/0001-frame-completion.patch` so target OT clearing
  writes the preceding entry address. PS1 `ChainEntryPS1::set` copies a link
  head, so the original clear call left every target entry as an end marker.
- Fixed `scripts/test.ps1` to capture Docker Python output through
  `Start-Process`, preserving the native exit code under bundled PowerShell.

## Verification

- `scripts/test.ps1`: PASS, host smoke/policy executables and all 14 Python
  tests. Summary:
  `C:\dev\helworks\builds\helengine-ps1\host-tests-20260915-105843-910-76ed6f9a67e04ae3b7dbf1d1f09a444d\summary.json`
- Final Static build (`Release`, `Static`): PASS. Manifest:
  `C:\dev\helworks\builds\helengine-ps1\20260915-110639-258-Release-56798e0174d34c888b0da8eb1e8bc414\manifest.json`
  - PS-EXE SHA-256: `49f8bdd4021e1beb648ed1f80e195d1676fb7293ba1dc5c8002b9b3b96a66684`
  - ELF SHA-256: `1ebe772615655fed20562e366762ef0eb8771c59122a044a9a7fbc46b195c697`
  - SDK patch SHA-256: `9eeef76aff2ee8a187c8648c0d52d2cd9344af313133589b1d29ba0131631b5c`
  - Retail static footprint: 45,184 bytes.
- DepthOrder build (`Release`, `DepthOrder`): PASS. Manifest:
  `C:\dev\helworks\builds\helengine-ps1\20260915-110317-174-Release-41fa895b60c64ad3a7b291d4337ffb4d\manifest.json`
  - PS-EXE SHA-256: `0d1dd2aec6a35446698c2e06c91c9d5935479f6cf4b4194340e879fd75a69da0`
  - Retail static footprint: 41,088 bytes.
- `git diff --check`: PASS.

## Runtime evidence

Root's official Static GDB/DuckStation probe recorded 263 links per parity:
257 OT links plus six triangle packets, with expected near/left/bottom colors.
Identity, Y=512, and X=512 produced two near, two left, and two bottom
triangles. Reversing the first near triangle reduced the near count to one,
proving reversed winding rejection. Evidence:
`C:\dev\helworks\builds\helengine-ps1\controller-duck-probe\static-official-packets`.

The official DepthOrder probe recorded consecutive sequences 902 and 903 with
`reverseInsertion=1` and `0`; each chain had 259 links, and the far green
packet appeared before the near red packet with identical coordinates
`(100,80)`, `(220,80)`, `(160,180)`. Evidence:
`C:\dev\helworks\builds\helengine-ps1\controller-duck-probe\depth-official-packets`.

The equal-bucket target probe recorded both insertion reversals in
`C:\dev\helworks\builds\helengine-ps1\controller-duck-probe\equal-bucket\equal-bucket.json`.
The equal-depth case follows the SDK table's LIFO head insertion behavior in
both reversals; this is documented as an ordering property, not intersecting-
polygon depth correctness.

Before the OT clear correction, the target chain had one link and no triangle
packets. Afterward it had 257 OT links plus six submitted triangles per parity.
Framebuffer pixel inspection and physical-console validation remain pending.

## Commit

The Task2 implementation, tests, SDK patch, wrapper fix, and this report are
The implementation commit is available in the repository history with message
`feat: render bounded unlit PS1 cube`.
