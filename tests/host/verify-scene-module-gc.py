"""Link a real MIPS overlay and verify dead code removal, exports and finalizers."""
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
link_flags = getattr(builder, "module_gc_link_flags", None)
assert callable(link_flags), "Module linking must discard unused sections while retaining exports and callbacks"

output = args.output.resolve()
output.mkdir(parents=True, exist_ok=True)
source = output / "module-gc.cpp"
obj = output / "module-gc.o"
elf = output / "module-gc.elf"
source.write_text('''
volatile int module_state;
struct Owner {
    Owner() { module_state = 37; }
    ~Owner() { module_state = 0; }
};
Owner owner;
extern "C" int imported_callback() { return module_state + 1; }
extern "C" int unused_helper() { return module_state + 83; }
extern "C" void he_ps1_module_register() { module_state = 41; }
extern "C" void he_ps1_module_unregister() { module_state = 0; }
''')
flags = ["-std=c++20", "-march=mips1", "-mabi=32", "-msoft-float", "-mno-abicalls", "-mno-gpopt",
         "-fno-pic", "-nostdlib", "-ffreestanding", "-fno-exceptions", "-fno-rtti",
         "-fno-use-cxa-atexit", "-ffunction-sections", "-fdata-sections", "-Os"]
subprocess.run(["mipsel-none-elf-g++", *flags, "-c", str(source), "-o", str(obj)], check=True)
nm = subprocess.check_output(["mipsel-none-elf-nm", "-g", "--defined-only", str(obj)], text=True)
definitions = {"producer": {line.split()[-1] for line in nm.splitlines() if line.split()}}
modules = [{"moduleId": "producer", "importSymbols": []},
           {"moduleId": "consumer", "importSymbols": ["imported_callback"]}]
subprocess.run(["mipsel-none-elf-g++", *flags, *link_flags("producer", modules, definitions),
                "-Wl,--defsym,MODULE_VMA=0x80010000", "-Wl,--defsym,MODULE_CAPACITY=0x1e0000",
                "-Wl,-T," + str(repository / "linker/scene-module.ld"), str(obj), "-o", str(elf)], check=True)
nm = subprocess.check_output(["mipsel-none-elf-nm", "-n", str(elf)], text=True)
symbols = {line.split()[-1]: int(line.split()[0], 16) for line in nm.splitlines() if len(line.split()) == 3}
assert "unused_helper" not in symbols, "Unreachable helper survived section GC"
for name in ["imported_callback", "he_ps1_module_register", "he_ps1_module_unregister"]:
    assert name in symbols, "Required export or callback removed: " + name
for name in ["init", "fini"]:
    assert symbols["__module_" + name + "_end"] > symbols["__module_" + name + "_start"], name + " callbacks removed"
print("PASS MIPS module GC: unused helper removed; cross-module export, registration, unregistration, init and fini retained")
