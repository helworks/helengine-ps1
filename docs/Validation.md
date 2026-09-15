# PS1 Task 1 validation

## Portable checks

Run the host tests without creating Python bytecode in the source checkout:

```powershell
python -B -m unittest discover -s tests/host -p 'test_*.py' -v
```

`check-retail-memory.py` parses ELF32 little-endian headers, section tables,
allocated NOBITS ranges, linker symbols, and the PS-X EXE header. The loader
metadata section at `0x8000f800` is classified separately from the loaded
`0x80010000..0x80200000` RAM range. Scratchpad (`0x1f800000..0x1f800400`)
is checked as a separate region. The checker reports the measured static
footprint and heap capacity below the reserved 64 KiB stack range.

The Release clear build produced these measured values:

| Value | Result |
| --- | ---: |
| PS-X EXE load / entry | `0x80010000` |
| PS-X EXE payload | `0x4800` bytes |
| Retail RAM top | `0x80200000` |
| Stack reservation start | `0x801f0000` |
| Initial stack pointer | `0x801fff00` |
| Static end / heap start | `0x80015ea8` |
| Static footprint from load address | `24,232` bytes |
| Heap capacity below stack reservation | `1,941,848` bytes |

## Emulator setup

The supplied PCSX-Redux archive was extracted to
`C:\dev\helworks\builds\helengine-ps1\emulators\pcsx-redux-25363.20260913.2`.
It contains `pcsx-redux.exe`, `version.json` (`25363.20260913.2.x64`, changeset
`c2e2dec197d3eb8f3db2ee63b8037321dbe1085e`), and the shipped OpenBIOS images
including `openbios.bin`. The executable's noninteractive `-dumpproto` probe
returned exit code 0. The documented runtime flags accepted by this build are
`-bios`, `-exe`/`-loadexe`, `-run`, `-stdout`, `-no-gui-log`, `-safe`, and
`-portable`; the launch wrapper uses those flags with an explicit manifest
artifact and the shipped OpenBIOS image.

Launch an explicitly verified build with:

```powershell
.\scripts\launch_in_emulator.ps1 `
  -Manifest C:\absolute\path\to\manifest.json `
  -EmulatorPath C:\absolute\path\to\pcsx-redux.exe
```

The wrapper verifies every manifest artifact hash, requires a sibling
`openbios*.bin`, starts the emulator hidden, and writes stdout/stderr logs and
`last-launch.json` beside the manifest. No newest-artifact discovery is used.

## Runtime evidence status

The native build and checker pass. The emulator archive and OpenBIOS are
available; a hidden launch/log probe started PCSX-Redux and OpenBIOS reached the
PSYQo application, but the pinned emulator's software and hardware renderers
both report `Got an unknown GPU data word: 1f000000` and hit the SDK's bounded
`PS1_GPU_COMPLETION_TIMEOUT`. Its GPU command decoder does not implement GP0(1F)
IRQ markers, so it cannot validate this completion fence. A separate hidden
DuckStation probe accepted the PS-X EXE command line but produced no guest or
framebuffer log within the bounded run, so it is not claimed as first-frame
evidence.
Framebuffer layout is the pinned PSYQo configuration: display buffers at
`(0,0)` and `(0,256)`, 320x240 progressive NTSC, 15-bit color stored in a
16-bit framebuffer. The patched `GPU::flip()` owns ordered GPU completion and
fresh-VBlank presentation; the host does not create a second presentation loop.
Startup writes a guard at `__stack_start`, checks it at each diagnostic frame,
and logs the heap bounds after initialization.

Physical-console validation remains pending because no retail console was
available for this task.
