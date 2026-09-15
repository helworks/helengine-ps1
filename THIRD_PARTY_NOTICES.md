# Third-party notices

Task 1 vendors the exact Nugget/PSYQo revision
`625da805c4a9bfd56dd9b2b17409805c0591dd42` as `third_party/nugget`, together
with its pinned nested revisions. Nugget and the PSYQo sources used by this
build carry the MIT license; the complete upstream notice is retained in
`third_party/nugget/LICENSE` and source headers.

The build image is `ghcr.io/grumpycoders/pcsx-redux-build` pinned by digest in
`toolchain.lock.json`. The compiler, binutils, GNU Make, and their runtime
licenses remain properties of that image and are not copied into the engine
source tree.

PCSX-Redux is used only as a development emulator. The supplied archive
contains its own `LICENSE` and the OpenBIOS images. PCSX-Redux is GPL-2.0-or-
later; OpenBIOS source is MIT-covered in the upstream project. The launcher
uses the shipped OpenBIOS image and does not require or distribute a Sony BIOS.
