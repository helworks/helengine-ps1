"""Link real MIPS resident images with zero and nonzero overlay arenas."""
import argparse
import importlib.util
import pathlib
import subprocess


parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--output", required=True, type=pathlib.Path)
args = parser.parse_args()
repository = pathlib.Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("scene_builder", repository / "scripts/build-scene-modules.py")
builder = importlib.util.module_from_spec(spec)
spec.loader.exec_module(builder)

output = args.output.resolve()
output.mkdir(parents=True, exist_ok=True)
source = output / "arena-resident.s"
obj = output / "arena-resident.o"
elf = output / "arena-resident.elf"
arena_script = output / "arenas.ld"
source.write_text(".section .text\n.globl _start\n_start:\n jr $ra\n nop\n.section .bss\n.space 36\n")
subprocess.run(["mipsel-none-elf-gcc", "-march=mips1", "-mabi=32", "-msoft-float", "-c", str(source), "-o", str(obj)], check=True)


def link_symbols():
    """Use the production SDK linker layout, including its BSS and heap aliases."""
    subprocess.run(["mipsel-none-elf-gcc", "-march=mips1", "-mabi=32", "-msoft-float", "-nostdlib",
                    "-T", str(repository / "third_party/nugget/nooverlay.ld"),
                    "-T", str(arena_script), "-T", str(repository / "third_party/nugget/ps-exe.ld"),
                    "-Wl,--oformat=elf32-littlemips", str(obj), "-o", str(elf)], check=True)
    nm = subprocess.check_output(["mipsel-none-elf-nm", "-n", str(elf)], text=True)
    return {line.split()[-1]: int(line.split()[0], 16) for line in nm.splitlines() if len(line.split()) == 3}


arena_script.write_text("/* Preliminary resident link. */\n")
baseline = link_symbols()
assert baseline["__bss_end"] % 2048 != 0, "Fixture must expose alignment padding"
for slots in ({}, {0: {"capacityBytes": 2048}, 1: {"capacityBytes": 4096}}):
    layout = builder.module_arena_layout(baseline["__bss_end"], slots)
    arena_script.write_text(layout["linkerScript"])
    symbols = link_symbols()
    for name in ("__bss_end", "__heap_base", "__heap_start"):
        assert symbols[name] == layout["arenaEnd"], (slots, name, hex(symbols[name]), hex(layout["arenaEnd"]))
    if slots:
        assert symbols["__ps1_module_slot_0"] % 2048 == 0
        assert symbols["__ps1_module_slot_1"] == symbols["__ps1_module_slot_0"] + 2048
    else:
        assert symbols["__heap_start"] == baseline["__heap_start"], "No modules must reserve no arena or padding"
print("PASS MIPS arena layout: empty arenas reserve no RAM; populated arenas retain aligned slots and exact BSS/heap boundaries")
