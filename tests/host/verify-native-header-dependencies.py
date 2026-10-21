"""Verify real MIPS compilation refreshes dependencies when a header adds another include."""
import argparse
import hashlib
import pathlib
import subprocess
import time


parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--output", required=True, type=pathlib.Path)
args = parser.parse_args()
repository = pathlib.Path(__file__).resolve().parents[2]
output = args.output.resolve()
output.mkdir(parents=True, exist_ok=True)
source = output / "header-dependency.cpp"
entry = output / "header-entry.hpp"
added = output / "header-added.hpp"
obj = source.with_suffix(".o")
dep = source.with_suffix(".dep")
for file in [obj, dep]:
    file.unlink(missing_ok=True)
source.write_text('#include "header-entry.hpp"\nint value() { return VALUE; }\n')
entry.write_text('#define VALUE 1\n')
command = ["make", "-f", str(repository / "Makefile"), "SRCS=" + str(source), str(obj)]
subprocess.run(command, cwd=repository, check=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
time.sleep(1.1)
added.write_text('#define VALUE 2\n')
entry.write_text('#include "header-added.hpp"\n')
subprocess.run(command, cwd=repository, check=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
assert "header-added.hpp" in dep.read_text(), "Recompiling an object did not refresh newly introduced header dependencies"
before = hashlib.sha256(obj.read_bytes()).digest()
time.sleep(1.1)
added.write_text('#define VALUE 3\n')
subprocess.run(command, cwd=repository, check=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
assert hashlib.sha256(obj.read_bytes()).digest() != before, "Changing the newly included header reused a stale object"
print("PASS MIPS dependencies: added include tracked and later header-only change recompiles the object")
