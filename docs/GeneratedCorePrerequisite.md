# Generated Core prerequisite for PS1

The camera-and-cube scene milestone reaches a toolchain compatibility
checkpoint before native scene loading can be implemented as planned.

## Initial compiler investigation (before generic capability support)

The pinned GCC 16.2 PS1 image is configured with `--without-headers` and
`--disable-hosted-libstdcxx`. It supplies freestanding C++ headers and archives,
but not hosted `<string>`, `<vector>`, `<stdexcept>`, target libc or libm.
A compile probe including `<string>` fails immediately with that header absent.
The original cube does not require those facilities.

Generated Core and its runtime helpers currently emit `std::string`, container,
exception and RTTI constructs. The generator's CustomRetro profile writes
configuration flags but does not replace all corresponding types or emission
paths. Existing `throw` and `typeid` probes also fail under PSYQo's
`-fno-exceptions -fno-rtti` flags. Enabling exceptions introduces exception ABI
dependencies; it is not an established working port.

Small EASTL string/vector/hash-map probes compile with the pinned PSYQo flags.
This establishes that the SDK has a possible container foundation. It does
not establish that generated Core links, fits in retail memory, or runs.

## Selected direction and boot acceptance

### Current milestone (2026-09-15)

The selected architecture remains generated Core. Generic storage, exception,
RTTI and provider options are implemented in csharpcodegen commit `23ea90e`.
Those fixture checks do not establish a working Core build.

The immediate milestone is now **boot the game with generated Core built into
the executable**, before loading the exported scene. Acceptance requires:

1. Generate the real Core through the engine's native export configuration.
2. Compile its final translation unit with the pinned PS1 toolchain; fix generic
   runtime/codegen gaps in csharpcodegen and SDK hooks in the PS1 project.
3. Link Core into the PS-X EXE and inspect the map and retail-memory report.
4. Execute a real Core path at startup and record a successful boot marker
   followed by the existing game loop in DuckStation under Helena's user.

Merely compiling a provider fixture, including a Core header, or linking unused
objects that the linker removes does not meet this milestone. Scene loading and
authored rendering follow it.

The real Core now generates successfully with the reflection-disable symbols,
engine feature catalog, and explicit PS1/little-endian configuration. The
`native-core-boot` preset is deliberately not used: it replaces platform
metadata with big-endian PowerPC defaults. The PS1 script selects the equivalent
feature constraints and each generic runtime capability explicitly.

Core's serialized-payload cleanup now uses `finally` and an explicit transfer
of stream ownership. Ownership annotations on stream-backed reader/writer
factories were corrected in the isolated engine source checkout. The focused
managed payload tests passed.

Core generation is finalized by calling the engine-owned translation-unit
writer through `tools/helengine.ps1.coregen`. No generated source is patched.
Current native integration work supplies provider-backed collections,
delegates, timing, shared ownership, mathematical functions, and memory streams.
The target uses compiler RTTI with the toolchain's `libsupc++`, while exception
unwinding remains disabled. Unsupported runtime exceptions terminate through
the provider's fatal hook.

Core generation and small native probes are not a successful boot result.
The final executable must still satisfy the acceptance checks above.

### Boot result (2026-09-15, run `20260915-184730-065-Release-0cdf40dacd90411b8ad9b365ed9c1898`)

The generated Core now compiles, links, passes the retail memory check, and
boots in DuckStation under Helena's user with the verified OpenBIOS firmware.
Guest TTY evidence (`duckstation-guest.log` beside the manifest) shows
`PS1_CORE_INITIALIZING`, `PS1_CORE_CONSTRUCTED`, `PS1_CORE_READY`, then the
existing cube-loop markers `PS1_STACK_GUARD`, `PS1_HEAP_INIT`,
`PS1_RENDER_DIAGNOSTIC` and `PS1_FIRST_FRAME_PRESENTED parity=0 frame=9`, with
no fatal or abort message over a 45-second run. The link map retains
`Core::Initialize` and `helengine::ps1::InitializeCoreBoot`.

| Measure | Value |
| --- | --- |
| PS-X EXE payload | 278528 bytes |
| Static footprint | 299112 bytes |
| Heap capacity | 1666968 bytes |
| `.text` / `.rodata` / `.data` / `.sbss` | 0x399f0 / 0x9198 / 0x1440 / 0x5060 |

Source fixes required for this result, all outside generated output:

- csharpcodegen: `Split(char, StringSplitOptions)` now lowers to
  `String::Split(receiver, separator, options)` and optional enum defaults emit
  the named member (`StringSplitOptions::None`) instead of a raw constant. The
  runtime gained the matching single-separator overload with `None` preserving
  empty segments.
- PS1 link order: `libsupc++` and `libgcc` follow `libpsyqo`, so operator
  new/delete resolve to the PSYQo allocator. With `libsupc++` first, its
  throwing operator new dragged in the exception allocator, terminate handlers,
  personality routine and unwinder, which need `malloc`, `free` and `strcmp`.
- `Ps1RuntimeProvider.cpp` supplies `strcmp` for `libsupc++` type-name
  comparison. Only the RTTI objects of `libsupc++` are linked.
- `linker/retail-memory.ld` defines a link-time `_gp` and asserts `.data` stays
  within gp-relative reach. The `libsupc++` pointer type-info catch matchers carry
  gp-relative relocations; they never execute because exceptions stay disabled.
- `Ps1RuntimeClock` extends the 32-bit GPU microsecond counter and reads the
  epoch while unbound. Generated static initializers (the engine logger's
  `Stopwatch.StartNew()`) sample the clock before the GPU exists; the earlier
  provider treated that as fatal and stopped the boot before Core initialized.

Scene loading with the exported `cube_test` scene is the next checkpoint and is
not part of this result.
### Historical alternatives

1. Keep the approved generated-Core architecture and add a freestanding
   generator/runtime port first: container/string abstractions, explicit fatal
   failure hooks, removal of RTTI dependencies, required math support, and
   actual generated-Core compile/link/memory tests.
2. Revise the milestone to a native loader of exported camera/cube data in the
   existing standalone host. That is a different architecture and would not
   prove generated Core or execution of the original generated spin component.

The user selected the first path, with reusable capabilities owned by
csharpcodegen. Scene inspection and typed subset serialization are complete;
native Core integration remains the next checkpoint.

## Local evidence

Throwaway probes are under
`C:/dev/helworks/builds/helengine-ps1/core-feasibility`.
The detailed agent report and source references are in the ignored plan ledger
directory `.superpowers/sdd/2026-09-15-ps1-cube-test/core-feasibility-report.md`.
No SDK pin or generated source was changed for these probes.

