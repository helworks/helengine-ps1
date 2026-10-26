"""Execute calls linked with the overlay's real flags and require checked scalar/array allocation boundaries."""
import argparse
import importlib.util
import pathlib
import subprocess


def main():
    """Build a tiny native consumer against distinct raw and checked SDK allocation symbols."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", required=True, type=pathlib.Path)
    parser.add_argument("--compiler", default="gcc")
    args = parser.parse_args()
    repository = pathlib.Path(__file__).resolve().parents[2]
    spec = importlib.util.spec_from_file_location("scene_builder", repository / "scripts/build-scene-modules.py")
    builder = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(builder)
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    resident = output / "allocation-resident.c"
    consumer = output / "allocation-consumer.c"
    program = output / "allocation-wraps.exe"
    resident.write_text('''
static int scalar_storage, array_storage;
void* _Znwj(unsigned int bytes) { (void)bytes; return 0; }
void* _Znaj(unsigned int bytes) { (void)bytes; return 0; }
void* __wrap__Znwj(unsigned int bytes) { return bytes == 23 ? &scalar_storage : 0; }
void* __wrap__Znaj(unsigned int bytes) { return bytes == 47 ? &array_storage : 0; }
void he_ps1_module_register(void) {}
void he_ps1_module_unregister(void) {}
''', encoding="utf-8")
    consumer.write_text('''
void* _Znwj(unsigned int);
void* _Znaj(unsigned int);
int main(void) {
    void* scalar = _Znwj(23);
    void* array = _Znaj(47);
    if (!scalar) return 1;
    if (!array) return 2;
    if (scalar == array) return 3;
    return 0;
}
''', encoding="utf-8")
    flags = builder.module_gc_link_flags("overlay", [{"moduleId": "overlay", "importSymbols": []}], {"overlay": set()})
    compiled = subprocess.run([args.compiler, "-ffunction-sections", "-fdata-sections", str(resident),
                               str(consumer), *flags, "-o", str(program)], capture_output=True, text=True)
    (output / "allocation-wraps.log").write_text(compiled.stdout + compiled.stderr, encoding="utf-8")
    assert compiled.returncode == 0, compiled.stdout + compiled.stderr
    executed = subprocess.run([str(program)], capture_output=True, text=True)
    assert executed.returncode == 0, (
        "Overlay allocation escaped the checked resident boundary: "
        f"exit={executed.returncode} (1=raw scalar new, 2=raw array new), flags={flags}")
    print("PASS overlay scalar and array allocation calls reach the checked resident symbols")


if __name__ == "__main__":
    main()
