"""Packs declared cooked assets into the versioned archive embedded in the PS-X EXE.

Archive layout (all little-endian, version 1):

    offset 0   : magic "HEP1"
    offset 4   : uint32 version (1)
    offset 8   : uint32 entry count
    offset 12  : uint32 total archive size in bytes
    offset 16  : entry records, 16 bytes each:
                 uint32 path offset, uint32 path length,
                 uint32 data offset, uint32 data length
    then       : UTF-8 path table (no terminators; lengths are explicit)
    then       : payloads, each aligned to four bytes

Entries are sorted by their normalized relative path so identical inputs always
produce identical bytes. Only declared files enter the archive; anything else in
the staging tree is ignored. The reader in src/platform/ps1 validates the same
invariants before serving streams.
"""

import argparse
import json
import struct
import sys
from dataclasses import dataclass
from pathlib import Path, PurePosixPath

MAGIC = b"HEP1"
VERSION = 1
HEADER_FORMAT = "<4sIII"
HEADER_SIZE = struct.calcsize(HEADER_FORMAT)
ENTRY_FORMAT = "<IIII"
ENTRY_SIZE = struct.calcsize(ENTRY_FORMAT)
ALIGNMENT = 4


class ArchiveError(ValueError):
    """Raised for invalid declarations or malformed archives."""


@dataclass(frozen=True)
class ArchiveEntry:
    """One declared asset as stored in the archive."""

    relative_path: str
    data_offset: int
    data: bytes


def normalize_relative_path(relative_path):
    """Returns the canonical forward-slash relative path or raises ArchiveError."""
    if not isinstance(relative_path, str) or relative_path == "":
        raise ArchiveError("empty asset path")
    candidate = relative_path.replace("\\", "/")
    if candidate.startswith("/") or ":" in candidate:
        raise ArchiveError(f"absolute asset path is not allowed: {relative_path}")
    parts = PurePosixPath(candidate).parts
    if not parts or any(part in ("", ".", "..") for part in parts):
        raise ArchiveError(f"unsafe asset path: {relative_path}")
    if candidate != "/".join(parts):
        raise ArchiveError(f"non-canonical asset path: {relative_path}")
    return candidate


def _align(value):
    return (value + ALIGNMENT - 1) // ALIGNMENT * ALIGNMENT


def pack_archive(declared):
    """Builds archive bytes from (relative_path, source_path) pairs."""
    normalized = {}
    for relative_path, source_path in declared:
        key = normalize_relative_path(relative_path)
        if key in normalized:
            raise ArchiveError(f"duplicate asset path: {key}")
        source = Path(source_path)
        if not source.is_file():
            raise ArchiveError(f"missing declared asset file: {source}")
        normalized[key] = source.read_bytes()

    ordered = sorted(normalized.items(), key=lambda item: item[0].encode("utf-8"))
    path_table = bytearray()
    path_ranges = []
    for key, _ in ordered:
        encoded = key.encode("utf-8")
        path_ranges.append((len(path_table), len(encoded)))
        path_table.extend(encoded)

    table_offset = HEADER_SIZE + ENTRY_SIZE * len(ordered)
    data_start = _align(table_offset + len(path_table))
    payload = bytearray()
    entries = bytearray()
    for (key, data), (path_offset, path_length) in zip(ordered, path_ranges):
        data_offset = data_start + len(payload)
        entries.extend(struct.pack(ENTRY_FORMAT, table_offset + path_offset, path_length, data_offset, len(data)))
        payload.extend(data)
        payload.extend(b"\0" * (_align(len(payload)) - len(payload)))

    body = bytearray(entries)
    body.extend(path_table)
    body.extend(b"\0" * (data_start - table_offset - len(path_table)))
    body.extend(payload)
    total = HEADER_SIZE + len(body)
    return struct.pack(HEADER_FORMAT, MAGIC, VERSION, len(ordered), total) + bytes(body)


def parse_archive(archive):
    """Validates archive bytes and returns its entries in stored order."""
    if len(archive) < HEADER_SIZE:
        raise ArchiveError("truncated archive header")
    magic, version, count, total = struct.unpack_from(HEADER_FORMAT, archive, 0)
    if magic != MAGIC:
        raise ArchiveError("unexpected archive magic")
    if version != VERSION:
        raise ArchiveError(f"unsupported archive version {version}")
    if total != len(archive):
        raise ArchiveError("truncated archive: declared size does not match")
    table_end = HEADER_SIZE + ENTRY_SIZE * count
    if table_end > len(archive):
        raise ArchiveError("truncated archive entry table")

    entries = []
    for index in range(count):
        path_offset, path_length, data_offset, data_length = struct.unpack_from(ENTRY_FORMAT, archive, HEADER_SIZE + ENTRY_SIZE * index)
        if path_offset + path_length > len(archive) or path_offset < table_end:
            raise ArchiveError(f"entry {index} path range is out of range")
        if data_offset % ALIGNMENT != 0 or data_offset + data_length > len(archive) or data_offset < table_end:
            raise ArchiveError(f"entry {index} data range is out of range")
        relative_path = archive[path_offset:path_offset + path_length].decode("utf-8")
        entries.append(ArchiveEntry(relative_path, data_offset, bytes(archive[data_offset:data_offset + data_length])))
    return entries


def write_incbin_assembly(archive_file_name, symbol):
    """Returns GNU assembler text that embeds the archive under start/end symbols."""
    return (
        "/* Generated by scripts/embed-authored-assets.py; do not edit. */\n"
        "    .section .rodata\n"
        "    .balign 4\n"
        f"    .global {symbol}\n"
        f"{symbol}:\n"
        f"    .incbin \"{archive_file_name}\"\n"
        f"    .global {symbol}_end\n"
        f"{symbol}_end:\n"
        "    .balign 4\n"
    )


def load_declarations(declaration_path):
    """Reads the builder's declared-file list: a JSON array of relativePath/sourcePath objects."""
    # PowerShell writers commonly prefix UTF-8 with a BOM; accept it rather than failing the build.
    document = json.loads(Path(declaration_path).read_text(encoding="utf-8-sig"))
    files = document["files"] if isinstance(document, dict) else document
    return [(entry["relativePath"], entry["sourcePath"]) for entry in files]


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--declarations", required=True, help="JSON list of declared files")
    parser.add_argument("--output-archive", required=True)
    parser.add_argument("--output-assembly", required=True)
    parser.add_argument("--symbol", default="he_ps1_authored_archive")
    parser.add_argument("--incbin-path", default="", help="Archive path as the assembler resolves it (defaults to the archive file name)")
    arguments = parser.parse_args(argv)

    archive = pack_archive(load_declarations(arguments.declarations))
    parse_archive(archive)
    archive_path = Path(arguments.output_archive)
    archive_path.parent.mkdir(parents=True, exist_ok=True)
    archive_path.write_bytes(archive)
    incbin_path = arguments.incbin_path if arguments.incbin_path else archive_path.name
    Path(arguments.output_assembly).write_text(write_incbin_assembly(incbin_path, arguments.symbol), encoding="utf-8", newline="\n")
    print(json.dumps({"archive": str(archive_path), "bytes": len(archive), "entries": len(parse_archive(archive))}))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
