"""Verify native metadata uses real archive ranges and retains persistent dependencies."""
import argparse
import hashlib
import importlib.util
import json
import pathlib
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", required=True, type=pathlib.Path)
    parser.add_argument("--output", required=True, type=pathlib.Path)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    for name in ("build-scene-modules.py", "write-scene-module-metadata.py", "compile-scene-module-candidates.py"):
        path = args.repo / "scripts" / name
        compile(path.read_text(encoding="utf-8"), str(path), "exec")
    driver_spec = importlib.util.spec_from_file_location("module_driver", args.repo / "scripts/build-scene-modules.py")
    driver = importlib.util.module_from_spec(driver_spec)
    driver_spec.loader.exec_module(driver)
    generated = args.output / "config-fixture"
    (generated / "runtime").mkdir(parents=True, exist_ok=True)
    config = generated / "helcpp_config.hpp"
    features = generated / "runtime/feature_config.hpp"
    config.write_text('#pragma once\n#include "runtime/feature_config.hpp"\n', encoding="utf-8")
    features.write_text('#pragma once\n#define HE_CPP_FEATURE_TEXT2D 0\n', encoding="utf-8")
    initial_hashes = driver.runtime_config_hashes(generated)
    initial_id = driver.resident_build_id({"entry": 0x80010000}, ["-mabi=32"], 0x80020000, [], initial_hashes, "sdk")
    assert initial_id == driver.resident_build_id({"entry": 0x80010000}, ["-mabi=32"], 0x80020000, [], driver.runtime_config_hashes(generated), "sdk")
    features.write_text('#pragma once\n#define HE_CPP_FEATURE_TEXT2D 1\n', encoding="utf-8")
    changed_hashes = driver.runtime_config_hashes(generated)
    assert initial_hashes["runtimeConfigSha256"] == changed_hashes["runtimeConfigSha256"]
    assert initial_id != driver.resident_build_id({"entry": 0x80010000}, ["-mabi=32"], 0x80020000, [], changed_hashes, "sdk")
    features.unlink()
    try:
        driver.runtime_config_hashes(generated)
    except FileNotFoundError:
        pass
    else:
        raise AssertionError("Missing module feature configuration must fail before native build")
    features.write_text('#pragma once\n#define HE_CPP_FEATURE_TEXT2D 1\n', encoding="utf-8")
    numeric_spec = importlib.util.spec_from_file_location("numeric_packer", args.repo / "scripts/pack-scene-module.py")
    numeric = importlib.util.module_from_spec(numeric_spec);numeric_spec.loader.exec_module(numeric)
    for bits in (32, 64, 32):
        identity = f"real-v1-s{bits}-a{bits}"
        config.write_text(f'#define HE_CPP_REAL_NUMERIC_IDENTITY "{identity}"\n', encoding="utf-8")
        numeric.require_numeric_config(config, identity)
        try:
            numeric.require_numeric_config(config, "real-v1-s16-a16")
        except ValueError:
            pass
        else:
            raise AssertionError("Different Core and overlay policy must reject before native compilation")
    spec = importlib.util.spec_from_file_location("archive_packer", args.repo / "scripts/embed-authored-assets.py")
    packer = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = packer
    spec.loader.exec_module(packer)
    files = []
    assets = []
    for index, name in enumerate(("cooked/scenes/menu", "cooked/scenes/loading")):
        path = args.output / ("source" + str(index))
        path.write_bytes(bytes((index,)) * (2057 + index))
        files.append((name, path))
        assets.append({"relativePath": name, "byteLength": path.stat().st_size,
                       "sha256": hashlib.sha256(path.read_bytes()).hexdigest()})
    image = packer.pack_archive(files)
    archive = args.output / "ASSETS.BIN"
    archive.write_bytes(image)
    plan = {"numericPolicy": "real-v1-s32-a32", "schemaVersion": 1, "assets": assets, "modules": [], "slots": [],
            "persistentAssetPaths": ["cooked/scenes/loading"],
            "scenes": [{"sceneId": "menu", "cookedRelativePath": "cooked/scenes/menu",
                        "assetPaths": ["cooked/scenes/menu"], "requiredCodeModuleIds": []}]}
    plan_path = args.output / "partition-plan.json"
    plan_path.write_text(json.dumps(plan), encoding="utf-8")
    destination = args.output / "metadata"
    command = [sys.executable, "-B", str(args.repo / "scripts/write-scene-module-metadata.py"),
               "--plan", str(plan_path), "--archive", str(archive), "--output", str(destination),
               "--cache-budget-bytes", "16384"]
    result = subprocess.run(command, capture_output=True, text=True)
    (args.output / "metadata.log").write_text(result.stdout + result.stderr, encoding="utf-8")
    assert result.returncode == 0, result.stderr
    cpp = (destination / "ps1_scene_module_metadata.cpp").read_text(encoding="utf-8")
    assert 'SceneAssets0[] = {0,1}' in cpp
    assert '"menu",SceneAssets0,2' in cpp
    snapshot = {path.name: hashlib.sha256(path.read_bytes()).hexdigest() for path in destination.iterdir()}
    plan["assets"][0]["sha256"] = "00" * 32
    plan_path.write_text(json.dumps(plan), encoding="utf-8")
    rejected = subprocess.run(command, capture_output=True, text=True)
    assert rejected.returncode != 0
    assert snapshot == {path.name: hashlib.sha256(path.read_bytes()).hexdigest() for path in destination.iterdir()}
    (args.output / "receipt.json").write_text(json.dumps({"passed": True, "checks": 7,
        "featureOnlyChangeAltersResidentBuildId": True, "unchangedInputsKeepResidentBuildId": True,
        "missingFeatureConfigurationRejected": True,
        "pythonSyntax": True, "persistentUnion": True, "realArchiveRanges": True,
        "hashMismatchRejectedBeforeWrite": True}, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
