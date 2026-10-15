"""Package a real fixed-VMA ELF as HPM1; never rewrites emitted C++ or relocates code."""
import argparse
import hashlib
import json
import pathlib
import struct
import subprocess


def read_symbols(nm, elf):
    result = subprocess.run([nm, "-n", "--defined-only", str(elf)], check=True,
                            capture_output=True, text=True)
    symbols = {}
    for line in result.stdout.splitlines():
        words = line.split()
        if len(words) == 3:
            symbols[words[2]] = int(words[0], 16)
    return symbols


def package(elf, output, resident_build_id, arena_bytes, nm, objcopy):
    symbols = read_symbols(nm, elf)
    base = symbols["__module_start"]
    load_bytes = symbols["__module_load_end"] - base
    bss_bytes = symbols["__module_end"] - symbols["__module_load_end"]
    if base < 0x80010000 or base + arena_bytes > 0x801f0000 or load_bytes + bss_bytes > arena_bytes:
        raise ValueError("Module violates retail RAM/arena bounds")
    if load_bytes <= 0 or len(resident_build_id) != 32:
        raise ValueError("Invalid module load size or resident ABI identity")
    temporary = output.with_suffix(".payload")
    subprocess.run([objcopy, "-O", "binary", str(elf), str(temporary)], check=True)
    payload = temporary.read_bytes()
    if len(payload) != load_bytes:
        raise ValueError("ELF flat image length differs from linker load range")
    entries = []
    for name in ("he_ps1_module_register", "he_ps1_module_unregister"):
        offset = symbols[name] - base
        if offset < 0 or offset >= load_bytes or offset % 4:
            raise ValueError("Export outside loaded executable range: " + name)
        entries.append(offset)
    arrays = []
    for name in ("init", "fini"):
        offset = symbols["__module_" + name + "_start"] - base
        length = symbols["__module_" + name + "_end"] - base - offset
        if offset < 0 or length < 0 or offset + length > load_bytes or offset % 4 or length % 4:
            raise ValueError("Invalid initializer array")
        for index in range(offset, offset + length, 4):
            pointer = struct.unpack_from("<I", payload, index)[0]
            if pointer % 4 or pointer < base or pointer >= base + load_bytes:
                raise ValueError("Initializer references code outside its owning module")
        arrays.extend((offset, length // 4))
    payload_digest = hashlib.sha256(payload).digest()
    header = struct.pack("<15I", 0x314d5048, 1, 128, 128 + load_bytes,
                         base, load_bytes, load_bytes, bss_bytes, *entries, *arrays, 0)
    image = header + resident_build_id + payload_digest + bytes(4) + payload
    output.write_bytes(image)
    temporary.unlink()
    return {"fileBytes": len(image), "loadAddress": base, "loadBytes": load_bytes,
            "bssBytes": bss_bytes, "arenaBytes": arena_bytes,
            "residentBuildId": resident_build_id.hex(), "payloadSha256": payload_digest.hex(),
            "fileSha256": hashlib.sha256(image).hexdigest(), "exports": entries}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--elf", required=True, type=pathlib.Path)
    parser.add_argument("--output", required=True, type=pathlib.Path)
    parser.add_argument("--resident-build-id", required=True)
    parser.add_argument("--arena-bytes", required=True, type=lambda value: int(value, 0))
    parser.add_argument("--nm", default="mipsel-none-elf-nm")
    parser.add_argument("--objcopy", default="mipsel-none-elf-objcopy")
    args = parser.parse_args()
    receipt = package(args.elf, args.output, bytes.fromhex(args.resident_build_id),
                      args.arena_bytes, args.nm, args.objcopy)
    args.output.with_suffix(".json").write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
