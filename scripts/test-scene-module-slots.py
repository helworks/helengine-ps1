"""Verify exact native slot coverage independently of the fixed eight-arena bound."""
import importlib.util
import json
import pathlib
import sys


def main():
    source = pathlib.Path(__file__).with_name("build-scene-modules.py")
    spec = importlib.util.spec_from_file_location("scene_module_driver", source)
    driver = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(driver)
    valid = {"slots": [{"index": i} for i in range(8)], "modules": [{"slot": i} for i in range(8)]}
    assert len(driver.validate_slot_assignments(valid)) == 8
    failures = [
        ({"slots": [{"index": 0}], "modules": [{"slot": 1}]}, "differ from module assignments"),
        ({"slots": [{"index": 0}, {"index": 0}], "modules": [{"slot": 0}]}, "differ from module assignments"),
        ({"slots": [{"index": i} for i in range(10)], "modules": [{"slot": i} for i in range(10)]}, "exceed native capacity"),
        ({"slots": [{"index": -1}], "modules": [{"slot": -1}]}, "exceed native capacity"),
        ({"slots": [{"index": 8}], "modules": [{"slot": 8}]}, "exceed native capacity"),
    ]
    for plan, diagnostic in failures:
        try:
            driver.validate_slot_assignments(plan)
        except ValueError as error:
            assert diagnostic in str(error), str(error)
        else:
            raise AssertionError("Invalid slot contract was accepted")
    replay_path = pathlib.Path(sys.argv[1])
    replay = json.loads(replay_path.read_text(encoding="utf-8-sig"))
    driver.validate_slot_assignments(replay)
    by_id = {module["moduleId"]: module for module in replay["modules"]}
    for scene in replay["scenes"]:
        ids = set(scene["requiredCodeModuleIds"])
        slots = [by_id[module_id]["slot"] for module_id in ids]
        assert len(slots) == len(set(slots)), scene["sceneId"]
        for module_id in ids:
            assert set(by_id[module_id]["dependencyModuleIds"]).issubset(ids), scene["sceneId"]
    output = replay_path.with_name("native-slot-contract-receipt.json")
    output.write_text(json.dumps({"passed": True, "checks": 7, "exactCoverageAndEightArenaLimitPreserved": True,
        "actualReplaySceneCount": len(replay["scenes"]), "actualReplayModuleCount": len(by_id),
        "actualReplaySlotCount": len(replay["slots"]), "fullBuildPassed": False}, indent=2) + "\n", encoding="utf-8")
    print("NATIVE_SLOT_CONTRACT_PASS7")


if __name__ == "__main__":
    main()
