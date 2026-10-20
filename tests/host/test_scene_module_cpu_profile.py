"""Verify explicit profiling selection cannot silently degrade to an unprofiled build."""
import pathlib
import subprocess
import sys
import unittest

SCRIPT = pathlib.Path(__file__).resolve().parents[2] / "scripts/build-scene-modules.py"

class SceneModuleCpuProfileTests(unittest.TestCase):
    def test_cli_exposes_cpu_profile_for_module_builds(self):
        result = subprocess.run([sys.executable, "-B", str(SCRIPT), "--help"], capture_output=True, text=True)
        self.assertEqual(0, result.returncode, result.stderr)
        self.assertIn("--cpu-profile", result.stdout)

    def test_profile_rejects_non_release_before_touching_inputs(self):
        result = subprocess.run([sys.executable, "-B", str(SCRIPT), "--plan", "missing-plan.json",
                                 "--configuration", "SmallDebug", "--cpu-profile"], capture_output=True, text=True)
        self.assertEqual(2, result.returncode)
        self.assertIn("--cpu-profile requires Release", result.stderr)
        self.assertNotIn("Traceback", result.stderr)

if __name__ == "__main__":
    unittest.main()
