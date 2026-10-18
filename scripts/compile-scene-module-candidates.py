"""Compile authoritative generated source units and export actual MIPS symbol dependencies."""
import argparse
import concurrent.futures
import hashlib
import importlib.util
import json
import pathlib
import subprocess


def workspace_path(value, workspace, path_mappings=()):
    """Translate only declared host mounts and keep traversal within each mount."""
    normalized = str(value).replace("\\", "/")
    mappings = list(path_mappings) or [("C:/dev/helworks", workspace)]
    for host, mounted in mappings:
        prefix = str(host).replace("\\", "/").rstrip("/")
        if normalized.lower() == prefix.lower() or normalized.lower().startswith(prefix.lower() + "/"):
            root = pathlib.Path(mounted).resolve()
            path = (root / normalized[len(prefix):].lstrip("/")).resolve()
            if not path.is_relative_to(root):
                raise ValueError("Source/include escapes mount: " + str(value))
            return path
    path = pathlib.Path(normalized).resolve()
    allowed = [workspace.resolve(), *(pathlib.Path(mounted).resolve() for _, mounted in mappings)]
    if not any(path.is_relative_to(root) for root in allowed):
        raise ValueError("Source/include has no declared mount: " + str(value))
    return path


def symbols(nm, obj, defined):
    result = subprocess.run([nm, "-g", "--defined-only" if defined else "-u", str(obj)],
                            check=True, capture_output=True, text=True)
    return sorted(set(line.split()[-1] for line in result.stdout.splitlines() if line.split()))


def compile_unit(candidate, args, flags):
    """Compile and inspect one isolated candidate without changing SDK invocation semantics."""
    source = workspace_path(candidate["sourcePath"], args.workspace, args.path_map)
    identity = hashlib.sha256(str(candidate["sourcePath"]).encode()).hexdigest()[:24]
    obj = args.output / (identity + ".o")
    command = [args.compiler, *flags, "-c", str(source), "-o", str(obj)]
    result = subprocess.run(command, capture_output=True, text=True)
    (args.output / (identity + ".log")).write_text(result.stdout + result.stderr, encoding="utf-8")
    if result.returncode:
        raise RuntimeError("Candidate compile failed: " + str(source) + " (" + identity + ".log)")
    return {"sourcePath": candidate["sourcePath"], "objectPath": str(obj),
            "sourceSha256": hashlib.sha256(source.read_bytes()).hexdigest(),
            "definedSymbols": symbols(args.nm, obj, True),
            "undefinedSymbols": symbols(args.nm, obj, False)}


def compile_units(candidates, args, flags):
    """Bound in-flight work and publish ordered results only after every candidate succeeds."""
    units = [None] * len(candidates)
    next_index = 0
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as executor:
        pending = {}
        while next_index < len(candidates) and len(pending) < args.jobs:
            pending[executor.submit(compile_unit, candidates[next_index], args, flags)] = next_index
            next_index += 1
        while pending:
            completed, _ = concurrent.futures.wait(pending, return_when=concurrent.futures.FIRST_COMPLETED)
            failures = [future for future in completed if future.exception() is not None]
            if failures:
                failure = min(failures, key=lambda future: pending[future])
                for future in pending:
                    future.cancel()
                raise failure.exception()
            for future in completed:
                units[pending.pop(future)] = future.result()
            while next_index < len(candidates) and len(pending) < args.jobs:
                pending[executor.submit(compile_unit, candidates[next_index], args, flags)] = next_index
                next_index += 1
    return units


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--plan", required=True, type=pathlib.Path)
    parser.add_argument("--output", required=True, type=pathlib.Path)
    parser.add_argument("--workspace", default="/workspace", type=pathlib.Path)
    parser.add_argument("--path-map", nargs=2, action="append", default=[], metavar=("HOST", "MOUNT"))
    parser.add_argument("--compiler", default="mipsel-none-elf-g++")
    parser.add_argument("--nm", default="mipsel-none-elf-nm")
    parser.add_argument("--jobs", type=int, choices=range(1, 5), default=4)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    manifest_path = args.output / "object-symbols.json"
    manifest_path.unlink(missing_ok=True)
    plan = json.loads(args.plan.read_text(encoding="utf-8-sig"))
    root = workspace_path(plan["generatedCoreRoot"], args.workspace, args.path_map)
    repo = args.workspace / "helengine-ps1"
    sdk = repo / "third_party/nugget"
    includes = [root, repo / "src", repo / "src/platform/ps1", sdk, sdk / "third_party/EASTL/include",
                sdk / "third_party/EABase/include/Common"]
    includes.extend(workspace_path(value, args.workspace, args.path_map) for value in plan.get("includePaths", []))
    flags = ["-std=c++20", "-march=mips1", "-mabi=32", "-EL", "-fno-pic", "-mno-shared", "-mlong-calls",
             "-mno-abicalls", "-mfp32", "-mno-llsc", "-msoft-float", "-fno-stack-protector",
             "-nostdlib", "-ffreestanding", "-ffunction-sections", "-fdata-sections", "-mno-gpopt",
             "-fno-builtin", "-fno-strict-aliasing", "-fno-exceptions", "-fno-use-cxa-atexit", "-frtti", "-Os", "-g"]
    flags.extend("-I" + str(value) for value in includes)
    flags.extend("-D" + str(value) for value in plan.get("defines", []))
    native = None
    if plan.get("nativeKernelPolicy") == "make-nm-v1":
        spec = importlib.util.spec_from_file_location("scene_module_native_kernel", repo / "scripts/scene_module_native_kernel.py")
        helper = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(helper)
        native = helper.compile_native_kernel(args, root, repo)
    elif "nativeKernelPolicy" in plan:
        raise ValueError("Unknown native kernel policy")
    units = compile_units(plan["candidates"], args, flags)
    report = {"schemaVersion": 1, "units": units}
    if native is not None:
        report["residentNativeImportSymbols"] = native["residentNativeImportSymbols"]
        report["nativeKernelProof"] = {"policy": native["policy"], "nativeUnitCount": len(native["nativeUnits"]),
                                      "makefileSha256": native["makefileSha256"]}
    manifest_path.write_text(json.dumps(report, indent=2)+"\n", encoding="utf-8")


if __name__ == "__main__":
    main()
