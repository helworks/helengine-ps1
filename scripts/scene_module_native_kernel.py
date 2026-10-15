"""Measure native host imports using the canonical Makefile's source list and per-target flags."""
import hashlib
import json
import pathlib
import shlex
import shutil
import subprocess


def run(command, cwd, log):
    result = subprocess.run(command, cwd=cwd, capture_output=True, text=True)
    with log.open('a', encoding='utf-8') as stream:
        stream.write(json.dumps(command) + '\n' + result.stdout + result.stderr + '\n')
    if result.returncode:
        raise RuntimeError('Native kernel command failed; see ' + str(log))
    return result.stdout


def compile_native_kernel(args, generated, repository):
    """Compile only the platform-native objects selected by make, without touching source inputs."""
    root = args.output / 'native-kernel'
    root.mkdir(parents=True, exist_ok=True)
    objects = root / 'objects'
    objects.mkdir(exist_ok=True)
    work = root / 'inputs'
    work.mkdir(exist_ok=True)
    log = root / 'probe.log'
    log.write_text('', encoding='utf-8')
    shutil.copyfile(repository / 'Makefile', work / 'Makefile')
    for name, target in (('src', repository / 'src'), ('third_party', repository / 'third_party'), ('generated-core', generated)):
        link = work / name
        if link.is_symlink():
            if link.resolve() != target.resolve():
                raise ValueError('Native kernel input link changed: ' + name)
        elif link.exists():
            raise ValueError('Native kernel input must be an isolated read-only link: ' + name)
        else:
            link.symlink_to(target, target_is_directory=True)
    modules = work / 'scene-modules'
    modules.mkdir(exist_ok=True)
    (modules / 'resident-sources.mk').write_text('PS1_RESIDENT_CORE_SRCS :=\n', encoding='utf-8')
    run(['python3', '-B', str(repository / 'scripts/write-scene-module-metadata.py'),
         '--declarations-only', '--output', str(modules / 'metadata')], work, log)
    # common.mk normally remakes included .dep files even for a query. The isolated
    # query sets its documented deepclean exclusion without requesting that target.
    (work / 'kernel-query.mk').write_text(
        'override MAKECMDGOALS := deepclean\ninclude Makefile\n'
        '.PHONY: he_ps1_native_sources\nhe_ps1_native_sources:\n'
        "\t@printf '%s\\n' $(filter src/%.cpp,$(SRCS))\n", encoding='utf-8')
    common = ['make', '--no-print-directory', '-f', 'kernel-query.mk',
              'BUILD=Release', 'CORE_BOOT=1', 'AUTHORED=1', 'SCENE_MODULES=1']
    selected = run([*common, '-s', 'he_ps1_native_sources'], work, log).splitlines()
    sources = sorted(set(line.strip() for line in selected if line.strip()))
    if not sources:
        raise ValueError('Canonical makefile selected no resident native host sources')
    units = []
    imports = set()
    for relative in sources:
        source = (work / relative).resolve()
        if not source.is_relative_to((repository / 'src').resolve()) or not source.is_file():
            raise ValueError('Native make source escapes the platform source tree: ' + relative)
        target = pathlib.PurePosixPath(relative).with_suffix('.o').as_posix()
        dry = run([*common, '-n', target], work, log)
        commands = [shlex.split(line) for line in dry.splitlines() if line.startswith(args.compiler + ' ')]
        if len(commands) != 1 or '-c' not in commands[0] or '-o' not in commands[0]:
            raise ValueError('Expected exactly the real compiler command for ' + relative)
        command = commands[0]
        if command[-1] != relative:
            raise ValueError('Make compiler command selected a different native source')
        identity = hashlib.sha256(relative.encode()).hexdigest()[:24]
        obj = objects / (identity + '.o')
        dependency = objects / (identity + '.d')
        command[command.index('-o') + 1] = str(obj)
        command += ['-MMD', '-MF', str(dependency)]
        run(command, work, log)
        undefined = run([args.nm, '-u', str(obj)], work, log)
        symbols = sorted({line.split()[-1] for line in undefined.splitlines() if line.split()})
        imports.update(symbols)
        units.append({'sourcePath': str(source), 'objectPath': str(obj), 'commandArguments': command,
                      'sourceSha256': hashlib.sha256(source.read_bytes()).hexdigest(), 'undefinedSymbols': symbols})
    result = {'schemaVersion': 1, 'policy': 'make-nm-v1', 'nativeUnits': units,
              'residentNativeImportSymbols': sorted(imports),
              'makefileSha256': hashlib.sha256((repository / 'Makefile').read_bytes()).hexdigest(),
              'metadataDeclarationsSha256': hashlib.sha256((modules / 'metadata/ps1_scene_module_metadata.hpp').read_bytes()).hexdigest(),
              'entireCoreCompiled': False}
    (root / 'native-symbols.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    return result
