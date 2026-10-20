"""Build measured PS1 fixed-VMA overlays and publish a verified CD artifact atomically."""
import argparse
import hashlib
import importlib.util
import json
import pathlib
import shutil
import subprocess


def runtime_config_hashes(generated):
    """Require and fingerprint both ABI configuration and composed module features."""
    return {"runtimeConfigSha256": hashlib.sha256((generated / "helcpp_config.hpp").read_bytes()).hexdigest(),
            "runtimeFeatureConfigSha256": hashlib.sha256((generated / "runtime/feature_config.hpp").read_bytes()).hexdigest()}


def resident_build_id(symbols, flags, heap_start, slots, config_hashes, toolchain_hash):
    """Bind overlay imports to the resident addresses and complete native configuration."""
    return hashlib.sha256(json.dumps({"symbols": symbols, "flags": flags,
        "heapStart": heap_start, "slots": slots, **config_hashes,
        "toolchainLockSha256": toolchain_hash}, sort_keys=True).encode()).digest()


def validate_slot_assignments(plan):
    """Require exact arena coverage and the fixed native capacity before writing RAM."""
    declared = [slot["index"] for slot in plan["slots"]]
    assigned = {module["slot"] for module in plan["modules"]}
    if len(declared) != len(set(declared)) or set(declared) != assigned:
        raise ValueError("Conflict-graph slots differ from module assignments")
    if any(not isinstance(index, int) or isinstance(index, bool) or index < 0 or index >= 8 for index in declared):
        raise ValueError("Physical overlay slots exceed native capacity of eight arenas")
    return {slot["index"]: dict(slot) for slot in plan["slots"]}


def module_gc_link_flags(module_id, modules, definitions):
    """Discard unreachable sections while rooting loader callbacks and exports used by other modules."""
    imports = {symbol for module in modules if module["moduleId"] != module_id
               for symbol in module["importSymbols"]}
    roots = {"he_ps1_module_register", "he_ps1_module_unregister"} | (imports & definitions[module_id])
    return ["-Wl,--gc-sections", *["-Wl,-u," + symbol for symbol in sorted(roots)]]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--plan", required=True, type=pathlib.Path)
    parser.add_argument("--configuration", required=True)
    parser.add_argument("--sdk-defines", default="")
    parser.add_argument("--cpu-profile", action="store_true")
    parser.add_argument("--runtime-heap-reserve-bytes", type=int, default=65536)
    args = parser.parse_args()
    if args.cpu_profile and args.configuration != "Release":
        parser.error("--cpu-profile requires Release configuration")
    root = pathlib.Path.cwd().resolve()
    generated = root / "generated-core"
    config_hashes = runtime_config_hashes(generated)
    task = root / "scene-modules"
    objects = task / "objects"
    modules_out = task / "modules"
    pending = task / "build-out"
    for folder in (objects, modules_out, pending):
        folder.mkdir(parents=True, exist_ok=True)
    original = json.loads(args.plan.read_text(encoding="utf-8-sig"))
    if original["schemaVersion"] != 1 or args.runtime_heap_reserve_bytes < 0:
        raise ValueError("Unsupported partition schema or heap reservation")
    original_root = str(original["generatedCoreRoot"]).replace("\\", "/").rstrip("/")

    def source_path(value):
        normalized = str(value).replace("\\", "/")
        if normalized.lower().startswith(original_root.lower() + "/"):
            result = generated / normalized[len(original_root) + 1:]
        else:
            result = pathlib.Path(normalized)
        result = result.resolve()
        if not result.is_relative_to(generated) or not result.is_file():
            raise ValueError("Partition source escapes generated input or is missing: " + normalized)
        return result

    plan = dict(original)
    plan["generatedCoreRoot"] = str(generated)
    plan["residentSources"] = [str(source_path(value)) for value in original["residentSources"]]
    plan["modules"] = [dict(module, sourcePaths=[str(source_path(value)) for value in module["sourcePaths"]],
                            registrationSourcePath=str(source_path(module["registrationSourcePath"])))
                       for module in original["modules"]]
    persistent = set(original.get("persistentAssetPaths", []))
    plan["scenes"] = [dict(scene, assetPaths=sorted(set(scene["assetPaths"]) | persistent)) for scene in original["scenes"]]
    module_ids = {module["moduleId"] for module in plan["modules"]}
    if len(module_ids) != len(plan["modules"]):
        raise ValueError("Duplicate code module ID")
    for module in plan["modules"]:
        if any(value not in module_ids for value in module["dependencyModuleIds"]):
            raise ValueError("Resident physical code must not be a disc dependency")
    for scene in plan["scenes"]:
        if any(value not in module_ids for value in scene["requiredCodeModuleIds"]):
            raise ValueError("Scene refers to an undeclared disc module")
        closure = set()
        def visit(module_id, visiting):
            if module_id in visiting:
                raise ValueError("Dependency cycle escaped symbol ownership grouping")
            if module_id in closure:
                return
            module = next(value for value in plan["modules"] if value["moduleId"] == module_id)
            for dependency in module["dependencyModuleIds"]:
                visit(dependency, visiting | {module_id})
            closure.add(module_id)
        for module_id in scene["requiredCodeModuleIds"]:
            visit(module_id, set())
        required_slots = [value["slot"] for value in plan["modules"] if value["moduleId"] in closure]
        if len(required_slots) != len(set(required_slots)):
            raise ValueError("Co-required scene dependency modules share an overlay slot")
    slots = validate_slot_assignments(plan)
    cache_budget = original.get("maximumSceneCacheByteLength")
    if not isinstance(cache_budget, int) or cache_budget <= 0:
        raise ValueError("Measured full-scene cache budget is required")
    # Reserve exact cache buffers and resident record storage; the DMA buffer belongs to the driver.
    maximum_asset_bytes = max((sum(asset["byteLength"] for asset in plan["assets"]
        if asset["relativePath"] in set(scene["assetPaths"])) for scene in plan["scenes"]), default=0)
    conservative_record_bytes = len(plan["assets"]) * 32
    cache_budget = max(cache_budget, maximum_asset_bytes + conservative_record_bytes + 2048)
    log_path = task / "build-scene-modules.log"
    log = []

    def run(command):
        result = subprocess.run([str(value) for value in command], capture_output=True, text=True)
        log.append(json.dumps([str(value) for value in command]) + "\n" + result.stdout + result.stderr)
        log_path.write_text("\n".join(log), encoding="utf-8")
        if result.returncode:
            raise RuntimeError("Native scene-module command failed: " + str(command[0]))
        return result.stdout

    def load_script(name):
        spec = importlib.util.spec_from_file_location(name.replace("-", "_"), root / "scripts" / (name + ".py"))
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        return module

    packer = load_script("pack-scene-module")
    packer.require_numeric_config(generated / "helcpp_config.hpp", plan.get("numericPolicy"))
    disc = load_script("pack-scene-module-disc")
    run(["python3", "-B", root / "scripts/embed-authored-assets.py", "--declarations",
         root / "authored-declarations.json", "--output-archive", task / "ASSETS.BIN",
         "--output-assembly", task / "unused-archive.s", "--incbin-path", "scene-modules/ASSETS.BIN"])
    resident_sources = sorted(set(plan["residentSources"]))
    (task / "resident-sources.mk").write_text("PS1_RESIDENT_CORE_SRCS = " +
        " ".join(str(pathlib.Path(value).relative_to(root)).replace("\\", "/") for value in resident_sources) + "\n", encoding="utf-8")
    measured_plan = task / "measured-partition-plan.json"
    resident_imports = set(original.get("residentImportSymbols", []))

    def metadata(final=False):
        plan["slots"] = list(slots.values())
        measured_plan.write_text(json.dumps(plan, indent=2) + "\n", encoding="utf-8")
        command = ["python3", "-B", root / "scripts/write-scene-module-metadata.py", "--plan", measured_plan,
                   "--archive", task / "ASSETS.BIN", "--output", task / "metadata",
                   "--cache-budget-bytes", str(cache_budget)]
        if final:
            command += ["--module-receipts", modules_out]
        run(command)

    def make_resident():
        # Linker metadata/arena scripts are inputs even when make's object timestamps did not change.
        for extension in ("elf", "ps-exe", "map"):
            (pending / ("helengine_ps1." + extension)).unlink(missing_ok=True)
        import_flags = " ".join("-Wl,-u," + value for value in sorted(resident_imports))
        run(["make", "BUILD=" + args.configuration, "CORE_BOOT=1", "AUTHORED=1", "SCENE_MODULES=1",
             "CPU_PROFILE=" + str(int(args.cpu_profile)),
             "PS1_MODULE_IMPORT_FLAGS=" + import_flags,
             "SDK_DEFINES=" + args.sdk_defines, "all"])
        elf = pending / "helengine_ps1.elf"
        return elf, packer.read_symbols("mipsel-none-elf-nm", elf)

    # Metadata has invariant field count/types. Provisional scalar values never execute.
    for slot in slots.values():
        slot.update(loadAddress=0x80010000, capacityBytes=4)
    (task / "arenas.ld").write_text("/* Preliminary link measures resident BSS only. */\n", encoding="utf-8")
    flags = ["-std=c++20", "-march=mips1", "-mabi=32", "-EL", "-fno-pic", "-mno-shared", "-mlong-calls",
             "-mno-abicalls", "-mfp32", "-mno-llsc", "-msoft-float", "-fno-stack-protector", "-nostdlib",
             "-ffreestanding", "-ffunction-sections", "-fdata-sections", "-mno-gpopt", "-fno-builtin",
             "-fno-strict-aliasing", "-fno-exceptions", "-fno-use-cxa-atexit", "-frtti", "-Os", "-g"]
    if args.cpu_profile:
        flags.append("-DPS1_CPU_PROFILE=1")
    sdk = root / "third_party/nugget"
    flags += ["-I" + str(path) for path in (generated, root / "src", root / "src/platform/ps1", sdk,
              sdk / "third_party/EASTL/include", sdk / "third_party/EABase/include/Common")]
    flags += ["-D" + value for value in args.sdk_defines.split()]
    linked = {}
    compiling = set()
    for module in plan["modules"]:
        unit_sources = sorted(set(module["sourcePaths"] + [module["registrationSourcePath"]]))
        module["objects"] = []
        for value in unit_sources:
            identity = hashlib.sha256((module["moduleId"] + "\0" + value).encode()).hexdigest()[:24]
            obj = objects / (identity + ".o")
            run(["mipsel-none-elf-g++", *flags, "-DHELENGINE_CODE_MODULE_ID=" + json.dumps(module["moduleId"]),
                 "-c", value, "-o", obj])
            module["objects"].append(str(obj))
    # Registration units are emitted after the candidate symbol plan. Include their actual
    # imports before resident GC so registry/teardown and SDK exports survive --just-symbols.
    overlay_definitions = set()
    overlay_imports = set()
    module_definitions = {}
    for module in plan["modules"]:
        definitions = module_definitions[module["moduleId"]] = set()
        for obj in module["objects"]:
            definitions.update(line.split()[-1] for line in
                run(["mipsel-none-elf-nm", "-g", "--defined-only", obj]).splitlines() if line.split())
            overlay_imports.update(line.split()[-1] for line in
                run(["mipsel-none-elf-nm", "-u", obj]).splitlines() if line.split())
        overlay_definitions.update(definitions)
    resident_imports.update(overlay_imports - overlay_definitions)
    metadata()
    resident_elf, resident_symbols = make_resident()
    by_id = {module["moduleId"]: module for module in plan["modules"]}

    def link_module(module_id, measured):
        if module_id in linked:
            return linked[module_id]
        if module_id in compiling:
            raise ValueError("Dependency cycle escaped symbol ownership grouping")
        compiling.add(module_id)
        module = by_id[module_id]
        dependencies = [link_module(value, measured) for value in module["dependencyModuleIds"]]
        imported = {}
        for dependency in dependencies:
            for name, address in dependency[1].items():
                if name in module["importSymbols"] and name not in resident_symbols:
                    if name in imported and imported[name] != address:
                        raise ValueError("Ambiguous imported symbol: " + name)
                    imported[name] = address
        slot = slots[module["slot"]]
        vma = slot["loadAddress"] if measured else 0x80010000
        capacity = slot["capacityBytes"] if measured else 0x1e0000
        index = plan["modules"].index(module)
        elf = modules_out / ("MOD" + str(index).zfill(5) + ".elf")
        command = ["mipsel-none-elf-g++", *flags, *module_gc_link_flags(module_id, plan["modules"], module_definitions),
                   "-Wl,--defsym,MODULE_VMA=" + hex(vma),
                   "-Wl,--defsym,MODULE_CAPACITY=" + str(capacity), "-Wl,-T," + str(root / "linker/scene-module.ld"),
                   "-Wl,--just-symbols=" + str(resident_elf)]
        command += ["-Wl,--defsym," + name + "=" + hex(address) for name, address in sorted(imported.items())]
        command += [*module["objects"], "-o", elf]
        run(command)
        symbols = packer.read_symbols("mipsel-none-elf-nm", elf)
        linked[module_id] = (elf, symbols)
        compiling.remove(module_id)
        return linked[module_id]

    for module in plan["modules"]:
        _, symbols = link_module(module["moduleId"], False)
        slots[module["slot"]]["capacityBytes"] = max(slots[module["slot"]]["capacityBytes"],
            (symbols["__module_end"] - symbols["__module_start"] + 2047) & ~2047)
    address = (resident_symbols["__bss_end"] + 2047) & ~2047
    for index in sorted(slots):
        slots[index]["loadAddress"] = address
        address += slots[index]["capacityBytes"]
    plan["slots"] = list(slots.values())
    measured_plan.write_text(json.dumps(plan, indent=2) + "\n", encoding="utf-8")
    ram_fits = address + cache_budget + args.runtime_heap_reserve_bytes <= 0x801f0000
    measurement = {"schemaVersion": 1, "retailRamGatePassed": ram_fits,
        "residentBssEnd": resident_symbols["__bss_end"],
        "residentRawImageBytes": (pending / "helengine_ps1.ps-exe").stat().st_size - 2048,
        "residentElfSha256": hashlib.sha256(resident_elf.read_bytes()).hexdigest(),
        "partitionPlanSha256": hashlib.sha256(args.plan.read_bytes()).hexdigest(),
        **config_hashes,
        "slots": list(slots.values()), "arenaEnd": address, "heapCeiling": 0x801f0000,
        "maximumSceneCacheByteLength": cache_budget,
        "maximumSceneAssetPayloadBytes": maximum_asset_bytes,
        "persistentAssetPaths": sorted(persistent),
        "cacheRecordCount": len(plan["assets"]),
        "conservativeCacheRecordBytes": conservative_record_bytes,
        "discDmaWindowBytes": 2048,
        "cacheBudgetIncludesAllocatorOverhead": False,
        "conservativeRuntimeHeapReserveBytes": args.runtime_heap_reserve_bytes,
        "runtimeHeapMeasured": False, "remainingAfterArenas": 0x801f0000 - address,
        "remainingAfterArenasAndCache": 0x801f0000 - address - cache_budget,
        "modules": [{"moduleId": module["moduleId"], "slot": module["slot"],
            "loadBytes": linked[module["moduleId"]][1]["__module_load_end"] - linked[module["moduleId"]][1]["__module_start"],
            "loadAndBssBytes": linked[module["moduleId"]][1]["__module_end"] - linked[module["moduleId"]][1]["__module_start"]}
            for module in plan["modules"]]}
    (task / "memory-measurement-receipt.json").write_text(json.dumps(measurement, indent=2) + "\n", encoding="utf-8")
    if not ram_fits:
        raise ValueError("Measured resident + overlay arenas + scene cache + runtime heap reserve exceed retail RAM")
    arena_script = "SECTIONS { .ps1_module_arenas " + hex((resident_symbols["__bss_end"] + 2047) & ~2047) + " (NOLOAD) : {\n"
    for index in sorted(slots):
        arena_script += "__ps1_module_slot_" + str(index) + " = .; . += " + str(slots[index]["capacityBytes"]) + ";\n"
    arena_script += "} } INSERT AFTER .sbss;\n__heap_base = ADDR(.ps1_module_arenas) + SIZEOF(.ps1_module_arenas);\n__heap_start = __heap_base;\n"
    (task / "arenas.ld").write_text(arena_script, encoding="utf-8")
    metadata()
    resident_elf, resident_symbols = make_resident()
    if any(resident_symbols[name] != address for name in ("__bss_end", "__heap_base", "__heap_start")):
        raise ValueError("Resident heap/BSS boundary differs from the measured overlay arena end")
    imported_names = sorted(resident_imports & resident_symbols.keys())
    abi = {name: resident_symbols[name] for name in imported_names}
    build_id = resident_build_id(abi, flags, resident_symbols["__heap_start"], list(slots.values()),
        config_hashes, hashlib.sha256((root / "toolchain.lock.json").read_bytes()).hexdigest())
    linked.clear()
    for index, module in enumerate(plan["modules"]):
        elf, _ = link_module(module["moduleId"], True)
        receipt = packer.package(elf, modules_out / ("MOD" + str(index).zfill(5) + ".BIN"), build_id,
                                 slots[module["slot"]]["capacityBytes"], "mipsel-none-elf-nm", "mipsel-none-elf-objcopy", plan["numericPolicy"])
        (modules_out / ("MOD" + str(index).zfill(5) + ".json")).write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8")
    metadata(True)
    _, final_symbols = make_resident()
    if any(final_symbols.get(name) != address for name, address in abi.items()) or final_symbols["__heap_start"] != resident_symbols["__heap_start"]:
        raise ValueError("Final resident ABI moved after module digest publication")
    run(["python3", "-B", root / "scripts/check-retail-memory.py", "--elf", resident_elf,
         "--exe", pending / "helengine_ps1.ps-exe"])
    disc_command = ["python3", "-B", root / "scripts/pack-scene-module-disc.py", "--executable", pending / "helengine_ps1.ps-exe",
                    "--archive", task / "ASSETS.BIN", "--output", pending / "helengine_ps1.bin"]
    for index in range(len(plan["modules"])):
        disc_command += ["--module", modules_out / ("MOD" + str(index).zfill(5) + ".BIN")]
    run(disc_command)
    receipt = {"schemaVersion": 1, "passed": True, "entryPoint": "helengine_ps1.cue",
        "cpuProfile": args.cpu_profile,
        **config_hashes,
        "numericPolicy": plan["numericPolicy"], "residentBuildId": build_id.hex(), "residentImportAddresses": abi, "slots": list(slots.values()),
        "sceneCount": len(plan["scenes"]), "cacheBudgetBytes": cache_budget,
        "runtimeHeapReserveBytes": args.runtime_heap_reserve_bytes,
        "heapStart": final_symbols["__heap_start"], "heapCeiling": 0x801f0000}
    (pending / "scene-modules-receipt.json").write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8")
    destination = root / "out"
    destination.mkdir(exist_ok=True)
    publication_names = ("helengine_ps1.bin", "helengine_ps1.cue", "helengine_ps1.json", "scene-modules-receipt.json",
                         "helengine_ps1.elf", "helengine_ps1.ps-exe", "helengine_ps1.map")
    # Legacy artifacts are replaced only after the complete CD/retail/ABI gates pass.
    for name in publication_names:
        source = pending / name
        if not source.is_file():
            raise ValueError("Expected module disc artifact missing: " + name)
    for name in publication_names:
        shutil.copyfile(pending / name, destination / (name + ".pending"))
        (destination / (name + ".pending")).replace(destination / name)


if __name__ == "__main__":
    main()
