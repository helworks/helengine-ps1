# PS1 texture formats

The PS1 texture cook exposes the GPU's three texture-page modes:

| Engine format | GPU texel storage | Palette | Coverage |
| --- | --- | --- | --- |
| `Indexed4` | 4 bits per texel | 16 BGR555 entries uploaded from the engine RGBA palette | Binary or opaque |
| `Indexed8` | 8 bits per texel | 256 BGR555 entries uploaded from the engine RGBA palette | Binary or opaque |
| `Ps1Bgr555` | Little-endian 16-bit direct color | None | Binary or opaque |

The editor's PS1 platform texture and font-atlas settings publish all three modes. Existing defaults remain `Indexed8` for images and `Indexed4` for font atlases. The cook resizes according to `maxResolution` and rejects results larger than 256 by 256 texels, including uncapped sources that exceed one GPU texture page.

Direct-color words store red in bits 0–4, green in 5–9, blue in 10–14 and STP in bit 15. Transparent pixels encode as zero. Visible pixels set STP, preserving visible black as `0x8000` and allowing the primitive's blend setting to control transparency. STP is a blend participation flag, not a conventional alpha bit. The decoder treats only the all-zero word as transparent.

The runtime uploads direct-color texels without allocating a CLUT and selects the sixteen-bit TPAGE mode. Texture release frees only reservations actually owned by that texture. Indexed4 engine payloads pack texels continuously across the image; upload repacks each row into whole VRAM words, including odd-width rows.

## TIM source images

The editor registers `.tim` as a texture extension with the `ps1-tim` importer. The PS1 builder uses the same decoder when a cook work item names a TIM source, so these images can be authored through the ordinary asset import pipeline.

Standard uncompressed TIM modes 0–3 are supported: 4-bit indexed, 8-bit indexed, 16-bit BGR555 and 24-bit RGB. The decoder expands them to RGBA32 source images before processing. A 24-bit TIM is an importable source; its cooked PS1 texture still uses one of the three GPU modes above. Header destination coordinates do not relocate the imported image, and row padding is skipped for 24-bit sources.

Indexed TIM files must carry a CLUT. Normal editor import selects palette bank zero; `PlayStationTimTextureImporter.ImportTexture(stream, paletteIndex)` can select another embedded bank. Import uses binary coverage: zero native color is transparent, and every other native color is visible. It does not retain an original per-pixel STP mask or original VRAM placement through the RGBA source conversion. Compressed TIM variants, mixed-depth VRAM dumps, external CLUTs and game-specific containers require separate decoders and are rejected rather than guessed.

The parser checks the signature, reserved flags, positive dimensions, declared section lengths, complete payloads and palette bank bounds. It accepts up to three declared section alignment bytes and leaves the caller's stream open.

Format references: [TIM sections](https://psx-spx.consoledev.net/ps1/cdr/cdromfileformats/graphics/) and [PS1 VRAM color modes](https://psx-spx.consoledev.net/ps1/gpu/video-memory-vram/).

## Validation

`tests/helengine.ps1.platformbuilder.tests/PlayStation1TextureTests.cs` exercises all four TIM depths, palette banks, RGB order, row padding, malformed and truncated files, binary thresholds, visible black, indexed cooking, resize bounds and serialized native colors. `tests/host/Ps1AuthoredUiGeometryTests.cpp` checks direct-color VRAM allocation and release, plus golden odd-width Indexed4 upload rows. The engine editor-host registration and packaged texture-reader tests cover TIM routing and the persisted format byte.
