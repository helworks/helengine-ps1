# PS1 native build

Task 1 builds a standalone PS-X EXE in a fresh directory below
`C:\dev\helworks\builds\helengine-ps1`. The source checkout is never used as a
compiler working directory. Each run copies the explicit build inputs and the
pinned Nugget tree, applies `patches/nugget/0001-frame-completion.patch` with a
checked hunk, and invokes Docker with a structured argument array.

```powershell
.\scripts\build.ps1 -Configuration SmallDebug -DemoMode Clear
.\scripts\build.ps1 -Configuration Release -DemoMode Clear
```

`Configuration` is `SmallDebug` or `Release`. The wrapper accepts the planned
demo-mode names (`Clear`, `Static`, `Rotate`, `DepthOrder`, `InvalidPose`,
`Capacity`, and `CompletionStress`) and translates the selected name to one
`PS1_DEMO_MODE_*` definition. Task 1 implements the clear-only scene; later
renderer tasks add behavior for the other definitions.

The dependency and compiler lock is [toolchain.lock.json](../toolchain.lock.json).
The pinned build image is
`ghcr.io/grumpycoders/pcsx-redux-build@sha256:4f99ff0dd972096280cd6faf27c4255cc2bcf932397f8f9276ee22b6b1a67555`.
Observed versions inside that image are MIPS GCC 16.2.0, GNU Binutils
2.47.20260726, GNU Make 4.4.1, host GCC 15.2.0, and Python 3.14.4.

The resulting `out/` directory contains `helengine_ps1.ps-exe`, the matching
unstripped `helengine_ps1.elf`, and `helengine_ps1.map`. A manifest is written
only after artifact hashes and the retail-memory checker pass. It records the
source revision/dirty state, copied-input hashes, SDK patch hash, toolchain
lock hash, artifact hashes, and measured static/heap ranges.

The SDK's recursive library build must not inherit the application `BINDIR`:
PSYQo's makefile expects `libpsyqo.a` beside its makefile. The wrapper therefore
passes only `BUILD`, `DEMO_MODE`, and `all`; the application Makefile's
`BINDIR ?= out/` controls the final artifacts.

## Upstream compatibility check

Before adapting the host, the pinned SDK's `psyqo/examples/hello-chained`
minimal example was inspected and built in a separate copied SDK directory.
Its two-clear-fragment ownership pattern is the basis for the Task 1 root
scene. Generated objects and that artifact remain under the build root. The
reproducible command was:

```powershell
docker run --rm --mount "type=bind,source=C:\dev\helworks\builds\helengine-ps1\upstream-minimal-20260915,target=/work" --workdir /work ghcr.io/grumpycoders/pcsx-redux-build@sha256:4f99ff0dd972096280cd6faf27c4255cc2bcf932397f8f9276ee22b6b1a67555 make -C /work/nugget/psyqo/examples/hello-chained BUILD=SmallDebug
```

It exited 0 and produced `hello-chained.elf`, `hello-chained.ps-exe`, and
`hello-chained.map`. The same pinned image reported MIPS GCC 16.2.0, GNU
Binutils 2.47.20260726, GNU Make 4.4.1, host GCC 15.2.0, and Python 3.14.4.
