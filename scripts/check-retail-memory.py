"""Validate a PS-X EXE and its ELF allocation against the retail PS1 map.

The checker intentionally uses explicit 32-bit little-endian formats instead of
the host's native ``long`` size.  Its public ``validate_layout`` function is
also used by the small host test suite for boundary cases that are awkward to
construct as complete ELF files.
"""

from __future__ import annotations

import argparse
import json
import struct
from pathlib import Path


ELF32_HEADER = struct.Struct("<16sHHIIIIIHHHHHH")
ELF32_SECTION = struct.Struct("<IIIIIIIIII")
ELF32_SYMBOL = struct.Struct("<IIIBBH")

ELFCLASS32 = 1
ELFDATA2LSB = 1
EV_CURRENT = 1
EM_MIPS = 8
ET_EXEC = 2
SHF_ALLOC = 0x2
SHT_NOBITS = 8

RAM_START = 0x80010000
RAM_TOP = 0x80200000
SCRATCHPAD_START = 0x1F800000
SCRATCHPAD_END = 0x1F800400
EXE_HEADER_ADDRESS = 0x8000F800
EXE_HEADER_SIZE = 2048
REQUIRED_SYMBOLS = ("__ram_top", "__stack_start", "__sp", "__bss_end", "__heap_start")


def _fail(subject: str, reason: str) -> None:
    raise ValueError(f"{subject}: {reason}")


def _checked_end(name: str, address: int, size: int) -> int:
    if address < 0 or address > 0xFFFFFFFF:
        _fail(name, f"address 0x{address:x} is outside ELF32")
    if size < 0 or size > 0xFFFFFFFF:
        _fail(name, f"size 0x{size:x} is outside ELF32")
    end = address + size
    if end > 0x1_0000_0000:
        _fail(name, "address plus size overflows ELF32")
    return end


def _read_c_string(table: bytes, offset: int, subject: str) -> str:
    if offset >= len(table):
        _fail(subject, f"string offset 0x{offset:x} is outside its table")
    end = table.find(b"\0", offset)
    if end < 0:
        _fail(subject, "unterminated string")
    return table[offset:end].decode("ascii", errors="replace")


class Elf32Parser:
    """Parse only the ELF32 structures required for the retail checks."""

    def __init__(self, data: bytes):
        self.data = data
        self.sections: list[dict] = []
        self.symbols: dict[str, int] = {}
        self.header: dict[str, int] = {}
        self._parse()

    @classmethod
    def from_path(cls, path: Path) -> "Elf32Parser":
        return cls(path.read_bytes())

    def _parse(self) -> None:
        if len(self.data) < ELF32_HEADER.size:
            _fail("ELF header", "truncated ELF header")
        ident, e_type, machine, version, entry, phoff, shoff, flags, ehsize, phentsize, phnum, shentsize, shnum, shstrndx = ELF32_HEADER.unpack_from(self.data)
        if ident[:4] != b"\x7fELF":
            _fail("ELF identity", "missing ELF magic")
        if ident[4] != ELFCLASS32 or ident[5] != ELFDATA2LSB:
            _fail("ELF identity", "expected ELF32 little-endian")
        if ident[6] != EV_CURRENT or version != EV_CURRENT:
            _fail("ELF identity", "unsupported ELF version")
        if machine != EM_MIPS:
            _fail("ELF machine", f"expected MIPS ({EM_MIPS}), got {machine}")
        if e_type != ET_EXEC:
            _fail("ELF type", f"expected ET_EXEC ({ET_EXEC}), got {e_type}")
        if ehsize != ELF32_HEADER.size:
            _fail("ELF header", f"unexpected ELF header size {ehsize}")
        if shentsize != ELF32_SECTION.size:
            _fail("section table", f"unexpected section-header size {shentsize}")
        if shnum == 0:
            _fail("section table", "extended section counts are not supported")
        section_table_size = shnum * shentsize
        if shoff > len(self.data) or section_table_size > len(self.data) - shoff:
            _fail("section table", "section-table bounds exceed the ELF file")
        if shstrndx >= shnum:
            _fail("section table", f"section-name table index {shstrndx} is out of range")

        self.header = {
            "entry_point": entry,
            "section_offset": shoff,
            "section_count": shnum,
            "section_size": shentsize,
            "section_name_index": shstrndx,
        }
        raw_sections = [
            ELF32_SECTION.unpack_from(self.data, shoff + index * shentsize)
            for index in range(shnum)
        ]
        _sh_name, _sh_type, _sh_flags, _sh_address, name_offset, name_size, _sh_link, _sh_info, _sh_addralign, _sh_entsize = raw_sections[shstrndx]
        if name_offset > len(self.data) or name_size > len(self.data) - name_offset:
            _fail("section names", "section-name table exceeds the ELF file")
        names = self.data[name_offset:name_offset + name_size]

        for index, raw in enumerate(raw_sections):
            sh_name, sh_type, sh_flags, sh_address, sh_offset, sh_size, sh_link, sh_info, sh_addralign, sh_entsize = raw
            name = _read_c_string(names, sh_name, f"section {index}") if sh_name else ""
            end = _checked_end(name or f"section {index}", sh_address, sh_size)
            if sh_type != SHT_NOBITS and (sh_offset > len(self.data) or sh_size > len(self.data) - sh_offset):
                _fail(name or f"section {index}", "file range exceeds the ELF file")
            self.sections.append(
                {
                    "name": name,
                    "address": sh_address,
                    "size": sh_size,
                    "allocated": bool(sh_flags & SHF_ALLOC),
                    "type": sh_type,
                    "file_offset": sh_offset,
                    "link": sh_link,
                    "info": sh_info,
                    "entsize": sh_entsize,
                    "end": end,
                }
            )

        for section in self.sections:
            if section["type"] not in (2, 11):  # SHT_SYMTAB / SHT_DYNSYM
                continue
            if section["entsize"] != ELF32_SYMBOL.size:
                _fail(section["name"], f"unexpected symbol size {section['entsize']}")
            if section["link"] >= len(self.sections):
                _fail(section["name"], "symbol string-table link is out of range")
            strings = self.sections[section["link"]]
            if strings["type"] == SHT_NOBITS or strings["file_offset"] > len(self.data) or strings["size"] > len(self.data) - strings["file_offset"]:
                _fail(section["name"], "symbol string table is invalid")
            string_data = self.data[strings["file_offset"]:strings["file_offset"] + strings["size"]]
            count = section["size"] // section["entsize"]
            for index in range(count):
                offset = section["file_offset"] + index * section["entsize"]
                st_name, st_value, _st_size, _st_info, _st_other, _st_shndx = ELF32_SYMBOL.unpack_from(self.data, offset)
                if st_name == 0:
                    continue
                symbol_name = _read_c_string(string_data, st_name, f"symbol {index}")
                self.symbols[symbol_name] = st_value


def parse_exe_header(data: bytes) -> dict[str, int]:
    """Read the PS-X EXE fields needed by the loader and memory checks."""

    if len(data) < EXE_HEADER_SIZE:
        _fail("PS-X EXE header", "truncated 2048-byte header")
    if data[:8] != b"PS-X EXE":
        _fail("PS-X EXE header", "missing PS-X EXE magic")
    entry_point = struct.unpack_from("<I", data, 0x10)[0]
    load_address = struct.unpack_from("<I", data, 0x18)[0]
    payload_size = struct.unpack_from("<I", data, 0x1C)[0]
    stack_pointer = struct.unpack_from("<I", data, 0x30)[0]
    if payload_size > len(data) - EXE_HEADER_SIZE:
        _fail("PS-X EXE payload", "header payload size exceeds the file")
    return {
        "entry_point": entry_point,
        "load_address": load_address,
        "payload_size": payload_size,
        "stack_pointer": stack_pointer,
    }


def validate_layout(sections, symbols, exe_header):
    """Validate allocated sections, linker symbols, and PS-X EXE ranges.

    ``sections`` contains dictionaries with ``name``, ``address``, ``size``,
    and ``allocated`` fields.  A summary dictionary is returned for build
    manifests, while all failures identify the offending section or symbol.
    """

    for required in REQUIRED_SYMBOLS:
        if required not in symbols:
            _fail(required, "required linker symbol is missing")
        if not isinstance(symbols[required], int):
            _fail(required, "linker symbol is not an integer")
    ram_top = symbols["__ram_top"]
    stack_start = symbols["__stack_start"]
    stack_pointer = symbols["__sp"]
    heap_start = symbols["__heap_start"]
    bss_end = symbols["__bss_end"]
    if ram_top != RAM_TOP:
        _fail("__ram_top", f"expected 0x{RAM_TOP:08x}, got 0x{ram_top:08x}")
    expected_stack_start = RAM_TOP - 64 * 1024
    if stack_start != expected_stack_start:
        _fail("__stack_start", f"expected 0x{expected_stack_start:08x} for the retail 64 KiB reservation")
    if not (stack_start <= stack_pointer < ram_top):
        _fail("__sp", "must be inside the reserved stack range")
    if not (RAM_START <= heap_start <= stack_start):
        _fail("__heap_start", "must be between RAM start and stack boundary")
    if bss_end != heap_start:
        _fail("__bss_end", "must equal __heap_start")

    static_end = RAM_START
    scratchpad_end = SCRATCHPAD_START
    for section in sections:
        name = section.get("name", "<unnamed section>")
        address = section.get("address")
        size = section.get("size")
        allocated = section.get("allocated")
        if not isinstance(address, int) or not isinstance(size, int):
            _fail(name, "address and size must be integers")
        end = _checked_end(name, address, size)
        if not allocated:
            continue
        if name == ".PSX_EXE_Header":
            if address != EXE_HEADER_ADDRESS or size != EXE_HEADER_SIZE:
                _fail(name, "must remain the 2048-byte loader metadata at 0x8000f800")
            continue
        if SCRATCHPAD_START <= address < SCRATCHPAD_END or (size and address < SCRATCHPAD_START < end):
            if address < SCRATCHPAD_START or end > SCRATCHPAD_END:
                _fail(name, "scratchpad allocation exceeds 0x1f800000-0x1f800400")
            scratchpad_end = max(scratchpad_end, end)
            continue
        if not (RAM_START <= address <= ram_top and end <= ram_top):
            _fail(name, "allocated address is outside retail RAM or scratchpad")
        if address >= stack_start or end > stack_start:
            _fail(name, "allocated range overlaps the reserved stack")
        static_end = max(static_end, end)

    if static_end > heap_start:
        _fail("__heap_start", f"static image ends at 0x{static_end:08x}, above heap start 0x{heap_start:08x}")

    if not (RAM_START <= exe_header.get("load_address", -1) < ram_top):
        _fail("PS-X EXE load address", "load address is outside retail RAM")
    load_address = exe_header["load_address"]
    payload_size = exe_header.get("payload_size", -1)
    if not isinstance(payload_size, int) or payload_size < 0:
        _fail("PS-X EXE payload", "payload size is invalid")
    load_end = _checked_end("PS-X EXE payload", load_address, payload_size)
    if load_end > ram_top:
        _fail("PS-X EXE payload", "load range exceeds retail RAM")
    if load_end > stack_start:
        _fail("PS-X EXE payload", "load range overlaps the reserved stack")
    entry_point = exe_header.get("entry_point", -1)
    if not (load_address <= entry_point < load_end):
        _fail("PS-X EXE entry", "entry point is outside the loaded payload")
    header_stack = exe_header.get("stack_pointer", -1)
    if header_stack != stack_pointer:
        _fail("PS-X EXE stack", "header stack pointer does not match __sp")
    if not (stack_start <= header_stack < ram_top):
        _fail("PS-X EXE stack", "header stack pointer is outside the reserved stack")

    return {
        "ram_start": RAM_START,
        "ram_top": ram_top,
        "stack_start": stack_start,
        "stack_pointer": stack_pointer,
        "heap_start": heap_start,
        "static_end": static_end,
        "static_footprint": max(0, static_end - RAM_START),
        "heap_capacity": max(0, stack_start - heap_start),
        "scratchpad_end": scratchpad_end,
        "exe_load_address": load_address,
        "exe_payload_size": payload_size,
        "exe_entry_point": entry_point,
    }


def validate_files(elf_path: Path, exe_path: Path) -> dict:
    elf = Elf32Parser.from_path(elf_path)
    exe_header = parse_exe_header(exe_path.read_bytes())
    summary = validate_layout(elf.sections, elf.symbols, exe_header)
    if elf.header["entry_point"] != exe_header["entry_point"]:
        _fail("ELF entry", "ELF entry point does not match PS-X EXE header")
    summary.update({"elf": str(elf_path), "exe": str(exe_path)})
    return summary


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--elf", type=Path, required=True)
    parser.add_argument("--exe", type=Path, required=True)
    args = parser.parse_args()
    summary = validate_files(args.elf, args.exe)
    print(json.dumps(summary, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
