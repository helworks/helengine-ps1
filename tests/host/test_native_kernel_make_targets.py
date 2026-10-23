"""Query actual SDK make targets without compiling; regress the obsolete .o target bug."""
import argparse
import hashlib
import importlib.util
import pathlib
import types

parser = argparse.ArgumentParser()
parser.add_argument('--generated', type=pathlib.Path, required=True)
parser.add_argument('--fixture', type=pathlib.Path, required=True)
args = parser.parse_args()
repo = pathlib.Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('native_kernel', repo / 'scripts/scene_module_native_kernel.py')
helper = importlib.util.module_from_spec(spec)
spec.loader.exec_module(helper)
make_hash = hashlib.sha256((repo / 'Makefile').read_bytes()).hexdigest()
options = types.SimpleNamespace(output=args.fixture, compiler='mipsel-none-elf-g++')
root, work, log, modules, commands = helper.prepare_native_commands(options, args.generated, repo)
assert len(commands) >= 20, 'Native query lost platform source units'
assert not (work / 'Makefile').exists(), 'Native query copied the product Makefile'
query = (work / 'kernel-query.mk').read_text()
assert 'include ' + str(repo / 'Makefile') in query
authored = next(entry for entry in commands if entry['sourcePath'].endswith('/Ps1AuthoredScene.cpp'))
assert [flag for flag in authored['commandArguments'] if flag in ('-frtti', '-fno-rtti')][-1] == '-frtti', 'Canonical authored target RTTI flag was lost'
main = next(entry for entry in commands if entry['sourcePath'].endswith('/src/main.cpp'))
assert [flag for flag in main['commandArguments'] if flag in ('-frtti', '-fno-rtti')][-1] == '-fno-rtti', 'Non-RTTI source flags changed'
for entry in commands:
    command = entry['commandArguments']
    assert pathlib.Path(entry['sourcePath']).is_relative_to(repo / 'src')
    assert pathlib.Path(entry['makeTarget']).is_relative_to(root / 'objects')
    assert command[command.index('-o') + 1] == entry['objectPath']
    assert command[command.index('-MT') + 1] == entry['objectPath']
    assert pathlib.Path(command[command.index('-MF') + 1]).is_relative_to(root / 'objects')
    assert not pathlib.Path(entry['objectPath']).exists(), 'Query unexpectedly compiled a unit'
assert hashlib.sha256((repo / 'Makefile').read_bytes()).hexdigest() == make_hash
print('PASS: actual SDK make query for ' + str(len(commands)) + ' units; authored RTTI/main non-RTTI, real Makefile/source paths and external outputs; no compiler executed.')
