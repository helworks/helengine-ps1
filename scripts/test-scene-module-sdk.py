"""Links two genuine PS1 overlays against resident ELF exports and validates their HPM1 files."""
import argparse
import hashlib
import importlib.util
import json
import pathlib
import struct
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", required=True, type=pathlib.Path)
    parser.add_argument("--output", required=True, type=pathlib.Path)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    log = []

    def run(command):
        result = subprocess.run(command, capture_output=True, text=True)
        log.append(json.dumps(command) + "\n" + result.stdout + result.stderr)
        (args.output / "sdk-fixtures.log").write_text("\n".join(log), encoding="utf-8")
        if result.returncode:
            raise RuntimeError("SDK command failed: " + str(command))

    compiler = "mipsel-none-elf-g++"
    flags = ["-std=c++20", "-march=mips1", "-mabi=32", "-EL", "-fno-pic", "-mno-shared",
             "-mno-abicalls", "-mfp32", "-mno-llsc", "-msoft-float", "-fno-stack-protector",
             "-nostdlib", "-ffreestanding", "-mno-gpopt", "-fno-builtin", "-fno-exceptions",
             "-fno-rtti", "-fno-use-cxa-atexit", "-Os"]
    source = args.repo / "tests/scene-modules"
    sdk = args.repo / "third_party/nugget"
    run([compiler, *flags, "-I"+str(args.repo / "src"), "-I"+str(sdk),
         "-I"+str(sdk / "third_party/EASTL/include"), "-I"+str(sdk / "third_party/EABase/include/Common"),
         "-c", str(source / "sdk-reader.cpp"), "-o", str(args.output / "sdk-reader.o")])
    resident = args.output / "resident.elf"
    run([compiler, *flags, "-Wl,-T,"+str(source / "resident.ld"), str(source / "resident.cpp"), "-o", str(resident)])
    spec = importlib.util.spec_from_file_location("packer", args.repo / "scripts/pack-scene-module.py")
    packer = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(packer)
    symbols = packer.read_symbols("mipsel-none-elf-nm", resident)
    imports = {name: symbols[name] for name in ("ResidentNote", "ResidentScale")}
    build_id = hashlib.sha256(json.dumps(imports, sort_keys=True).encode()).digest()
    records = []
    for index, vma in enumerate((0x80140000, 0x80160000)):
        elf = args.output / ("module"+str(index)+".elf")
        run([compiler, *flags, "-mlong-calls", "-DMODULE_EXTRA="+str(index*4+3),
             "-Wl,--defsym,MODULE_VMA="+hex(vma), "-Wl,--defsym,MODULE_CAPACITY=65536",
             "-Wl,-T,"+str(args.repo / "linker/scene-module.ld"),
             "-Wl,--just-symbols="+str(resident), str(source / "module.cpp"), "-o", str(elf)])
        module_symbols = packer.read_symbols("mipsel-none-elf-nm", elf)
        assert all(module_symbols[name] == address for name,address in imports.items())
        record = packer.package(elf, args.output / ("MOD0000"+str(index)+".BIN"), build_id,65536,
                                "mipsel-none-elf-nm", "mipsel-none-elf-objcopy")
        image = (args.output / ("MOD0000"+str(index)+".BIN")).read_bytes()
        assert struct.unpack_from("<I", image,44)[0] == 1
        assert struct.unpack_from("<I", image,52)[0] == 1
        assert record["bssBytes"] > 0
        records.append(record)
    # Independent ISO9660 directory traversal verifies the exact boot/module/archive bytes.
    disc_spec = importlib.util.spec_from_file_location("disc", args.repo / "scripts/pack-scene-module-disc.py")
    disc = importlib.util.module_from_spec(disc_spec)
    disc_spec.loader.exec_module(disc)
    raw_resident=args.output / "resident.payload"
    run(["mipsel-none-elf-objcopy","-O","binary",str(resident),str(raw_resident)])
    payload=raw_resident.read_bytes()
    payload+=bytes((-len(payload))%2048)
    header=bytearray(2048);header[:8]=b"PS-X EXE"
    struct.pack_into("<I",header,16,symbols["_start"])
    struct.pack_into("<II",header,24,0x80010000,len(payload))
    struct.pack_into("<I",header,48,0x801fff00)
    exe=args.output / "fixture.ps-exe";exe.write_bytes(header+payload)
    archive=args.output / "fixture.assets";archive.write_bytes(b"HEP1"+bytes(2047))
    modules=[args.output / ("MOD0000"+str(i)+".BIN") for i in range(2)]
    iso=args.output / "fixture.bin"
    disc_receipt=disc.package(exe,archive,modules,iso)
    image=iso.read_bytes();pvd=image[16*2048:17*2048]
    assert pvd[:7]==b"\x01CD001\x01"
    assert struct.unpack_from("<I",pvd,80)[0]==struct.unpack_from(">I",pvd,84)[0]==len(image)//2048
    root_sector=struct.unpack_from("<I",pvd,158)[0]
    root_bytes=struct.unpack_from("<I",pvd,166)[0]
    directory=image[root_sector*2048:root_sector*2048+root_bytes]
    found={};offset=0
    while offset<len(directory):
        length=directory[offset]
        if length==0: offset=(offset//2048+1)*2048;continue
        identifier=directory[offset+33:offset+33+directory[offset+32]]
        if identifier not in (b"\0",b"\1"):
            sector=struct.unpack_from("<I",directory,offset+2)[0]
            count=struct.unpack_from("<I",directory,offset+10)[0]
            assert sector==struct.unpack_from(">I",directory,offset+6)[0]
            assert count==struct.unpack_from(">I",directory,offset+14)[0]
            found[identifier.decode("ascii")]=image[sector*2048:sector*2048+count]
        offset+=length
    assert found["BOOT.EXE;1"]==exe.read_bytes()
    assert found["ASSETS.BIN;1"]==archive.read_bytes()
    for module in modules: assert found[module.name+";1"]==module.read_bytes()
    assert len(found)==5
    (args.output / "sdk-fixtures-receipt.json").write_text(json.dumps({
        "passed": True, "actualMipsModulesLinked": 2, "residentImports": imports,
        "residentBuildId": build_id.hex(), "sdkAsyncReaderCompiled": True,
        "executedOnMips": False, "isoDirectoryRoundTripPassed":True,
        "discReceipt":disc_receipt,"records": records}, indent=2)+"\n", encoding="utf-8")


if __name__ == "__main__":
    main()
