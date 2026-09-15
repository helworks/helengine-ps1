# Third-party notices

## Nugget and PSYQo

This repository uses the Nugget/PSYQo source tree at commit
`625da805c4a9bfd56dd9b2b17409805c0591dd42`, with these nested revisions:

| Path | Revision |
| --- | --- |
| `third_party/uC-sdk` | `69e06871824e2d62069487a7426ded09090ceb69` |
| `third_party/psxlua` | `abed030e686b4e34987851bd0a028c93b1f73967` |
| `third_party/xmake-psx` | `e02b8cb6fdaf9258e74b80de806c8ac23222d41e` |

Nugget and the PSYQo sources used by this build carry the MIT license. The
complete upstream notice is retained in `third_party/nugget/LICENSE` and in
the source headers. The tracked frame-completion and target ordering-table
correction in `patches/nugget/0001-frame-completion.patch` applies only to this
exact SDK pin and preserves the upstream attribution.

## Build image

The build and host-test image is
`ghcr.io/grumpycoders/pcsx-redux-build@sha256:4f99ff0dd972096280cd6faf27c4255cc2bcf932397f8f9276ee22b6b1a67555`.
The lock records MIPS GCC 16.2.0, GNU Binutils 2.47.20260726, GNU Make 4.4.1,
host GCC 15.2.0, and Python 3.14.4. Those tools and their runtime licenses
remain properties of the image and are not copied into the engine source
tree.

## Development emulators and firmware

DuckStation is used as the runtime development emulator. The validated
controller copy was `0.1-11894-gc66b2694d`, configured in an isolated portable
directory with Software rendering, scale 1, PGXP disabled, retail 2 MiB RAM,
and no CPU overclock. The supplied distribution retains its own license files;
no emulator binaries or archives are committed here.

The supplied PCSX-Redux distribution is retained only for comparison. Its
version `25363.20260913.2.x64` / changeset
`c2e2dec197d3eb8f3db2ee63b8037321dbe1085e` does not decode GP0(1Fh), so it is
not the completion-fence runtime reference for this build.

The launcher uses the supplied OpenBIOS image as
`bios/openbios.bin`. The controller image SHA-256 is
`713ea2aed58606282b3ff5e91d77f8c5892ea36f5adcd279d2c7dd26604a625d`.
OpenBIOS source is MIT-covered in the upstream project. No Sony BIOS is
downloaded, bundled, or required by the repository.
