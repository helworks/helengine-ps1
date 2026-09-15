# Generated Core prerequisite for PS1

The camera-and-cube scene milestone reaches a toolchain compatibility
checkpoint before native scene loading can be implemented as planned.

## Measured compiler limitation

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

## Decision needed

1. Keep the approved generated-Core architecture and add a freestanding
   generator/runtime port first: container/string abstractions, explicit fatal
   failure hooks, removal of RTTI dependencies, required math support, and
   actual generated-Core compile/link/memory tests.
2. Revise the milestone to a native loader of exported camera/cube data in the
   existing standalone host. That is a different architecture and would not
   prove generated Core or execution of the original generated spin component.

Neither path has been silently substituted. Scene inspection and typed subset
serialization are useful to both; native integration remains pending the choice.

## Local evidence

Throwaway probes are under
`C:/dev/helworks/builds/helengine-ps1/core-feasibility`.
The detailed agent report and source references are in the ignored plan ledger
directory `.superpowers/sdd/2026-09-15-ps1-cube-test/core-feasibility-report.md`.
No SDK pin or generated source was changed for these probes.
