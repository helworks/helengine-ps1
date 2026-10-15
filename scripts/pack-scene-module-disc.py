"""Write deterministic ISO9660 data sectors and MODE2/2048 CUE for the PS1 scene-module package."""
import argparse
import hashlib
import json
import os
import pathlib
import re
import struct

SECTOR = 2048


def both32(value):
    return struct.pack("<I", value) + struct.pack(">I", value)


def both16(value):
    return struct.pack("<H", value) + struct.pack(">H", value)


def record(identifier, sector, size, directory=False):
    length = 33 + len(identifier) + (1 if len(identifier) % 2 == 0 else 0)
    return (bytes((length, 0)) + both32(sector) + both32(size) +
            bytes((126, 1, 1, 0, 0, 0, 0, 2 if directory else 0, 0, 0)) +
            both16(1) + bytes((len(identifier),)) + identifier + bytes(length - 33 - len(identifier)))


def validate_executable(image):
    if len(image) < SECTOR or image[:8] != b"PS-X EXE":
        raise ValueError("Resident boot file is not a PS-X EXE")
    address, count = struct.unpack_from("<II", image, 24)
    if address < 0x80010000 or address >= 0x801f0000 or count > 0x801f0000 - address:
        raise ValueError("Resident executable violates retail RAM/stack reserve")
    if len(image) != SECTOR + count or count % SECTOR:
        raise ValueError("Resident executable text length differs from its header")


def package(executable, archive, modules, output):
    exe_bytes = executable.read_bytes()
    validate_executable(exe_bytes)
    files = {"BOOT.EXE": exe_bytes, "ASSETS.BIN": archive.read_bytes(),
             "SYSTEM.CNF": b"BOOT = cdrom:\\BOOT.EXE;1\r\nTCB = 4\r\nEVENT = 10\r\nSTACK = 801FFF00\r\n"}
    for module in modules:
        name = module.name.upper()
        if not re.fullmatch(r"[A-Z0-9_]{1,8}\.[A-Z0-9_]{1,3}", name) or name in files:
            raise ValueError("Duplicate or non-ISO8.3 module name: " + name)
        files[name] = module.read_bytes()
    if len(files) > 256:
        raise ValueError("Disc root exceeds bounded package index")
    names = sorted(files)
    directory = bytearray(record(b"\0", 20, 0, True) + record(b"\1", 20, 0, True))
    for name in names:
        entry = record((name + ";1").encode("ascii"), 0, len(files[name]))
        gap = SECTOR - len(directory) % SECTOR
        if gap < len(entry):
            directory.extend(bytes(gap))
        directory.extend(entry)
    directory.extend(bytes((-len(directory)) % SECTOR))
    directory_length = len(directory)
    first_file = 20 + directory_length // SECTOR
    ranges = {}
    for name in names:
        ranges[name] = (first_file, len(files[name]))
        first_file += (len(files[name]) + SECTOR - 1) // SECTOR
    directory = bytearray(record(b"\0", 20, directory_length, True) + record(b"\1", 20, directory_length, True))
    for name in names:
        sector, size = ranges[name]
        entry = record((name + ";1").encode("ascii"), sector, size)
        gap = SECTOR - len(directory) % SECTOR
        if gap < len(entry):
            directory.extend(bytes(gap))
        directory.extend(entry)
    directory.extend(bytes(directory_length - len(directory)))
    volume = bytearray(SECTOR)
    volume[:7] = b"\x01CD001\x01"
    volume[8:40] = b"PLAYSTATION".ljust(32, b" ")
    volume[40:72] = b"HELENGINE_PS1".ljust(32, b" ")
    volume[80:88] = both32(first_file)
    volume[120:124] = both16(1)
    volume[124:128] = both16(1)
    volume[128:132] = both16(SECTOR)
    volume[132:140] = both32(10)
    struct.pack_into("<I", volume, 140, 18)
    struct.pack_into(">I", volume, 148, 19)
    volume[156:190] = record(b"\0", 20, directory_length, True)
    volume[190:318] = b"HELENGINE_PS1".ljust(128, b" ")
    volume[446:574] = b"HELENGINE".ljust(128, b" ")
    for offset in (813, 830, 847, 864):
        volume[offset:offset+17] = b"2026010100000000\0"
    volume[881] = 1
    terminator = b"\xffCD001\x01" + bytes(SECTOR-7)
    path_le = bytes((1, 0)) + struct.pack("<IH", 20, 1) + bytes(2)
    path_be = bytes((1, 0)) + struct.pack(">IH", 20, 1) + bytes(2)
    output.parent.mkdir(parents=True, exist_ok=True)
    pending = output.with_suffix(".pending")
    with pending.open("wb") as destination:
        destination.write(bytes(16 * SECTOR))
        destination.write(volume)
        destination.write(terminator)
        destination.write(path_le + bytes(SECTOR - len(path_le)))
        destination.write(path_be + bytes(SECTOR - len(path_be)))
        destination.write(directory)
        for name in names:
            destination.write(files[name])
            destination.write(bytes((-len(files[name])) % SECTOR))
    if pending.stat().st_size != first_file * SECTOR:
        raise ValueError("ISO sector accounting mismatch")
    os.replace(pending, output)
    output.with_suffix(".cue").write_text('FILE "' + output.name + '" BINARY\n  TRACK 01 MODE2/2048\n    INDEX 01 00:00:00\n', encoding="ascii")
    receipt = {"schemaVersion": 1, "sectorBytes": SECTOR, "sectorMode": "MODE2/2048",
               "discBytes": output.stat().st_size, "discSha256": hashlib.sha256(output.read_bytes()).hexdigest(),
               "files": [{"name": name, "firstSector": ranges[name][0], "byteLength": ranges[name][1],
                          "sha256": hashlib.sha256(files[name]).hexdigest()} for name in names]}
    output.with_suffix(".json").write_text(json.dumps(receipt, indent=2)+"\n", encoding="utf-8")
    return receipt


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", required=True, type=pathlib.Path)
    parser.add_argument("--archive", required=True, type=pathlib.Path)
    parser.add_argument("--module", action="append", default=[], type=pathlib.Path)
    parser.add_argument("--output", required=True, type=pathlib.Path)
    args = parser.parse_args()
    package(args.executable, args.archive, args.module, args.output)


if __name__ == "__main__":
    main()
