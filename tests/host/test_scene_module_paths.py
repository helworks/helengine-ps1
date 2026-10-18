"""Verify Docker mappings for authored sources outside the checkout."""
import importlib.util
import pathlib
import unittest

SCRIPT = pathlib.Path(__file__).resolve().parents[2] / "scripts/compile-scene-module-candidates.py"
spec = importlib.util.spec_from_file_location("candidate_compiler", SCRIPT)
compiler = importlib.util.module_from_spec(spec)
spec.loader.exec_module(compiler)

class SceneModulePathTests(unittest.TestCase):
    """Keep external caches explicit and reject unmapped or escaping paths."""
    def test_external_cache_maps_to_read_only_mount(self):
        mapped = compiler.workspace_path("C:/temp/helengine/generated/Entity.cpp", pathlib.Path("/workspace"), [("C:/temp/helengine", pathlib.Path("/cache"))])
        self.assertEqual(pathlib.Path("/cache/generated/Entity.cpp").resolve(), mapped)

    def test_unmapped_external_source_is_rejected(self):
        with self.assertRaises(ValueError):
            compiler.workspace_path("C:/other/Entity.cpp", pathlib.Path("/workspace"), [("C:/temp/helengine", pathlib.Path("/cache"))])

    def test_mapping_does_not_match_sibling_prefix(self):
        with self.assertRaises(ValueError):
            compiler.workspace_path("C:/temp/helengine-other/Entity.cpp", pathlib.Path("/workspace"), [("C:/temp/helengine", pathlib.Path("/cache"))])

    def test_parent_traversal_cannot_escape_mount(self):
        with self.assertRaises(ValueError):
            compiler.workspace_path("C:/temp/helengine/../../Entity.cpp", pathlib.Path("/workspace"), [("C:/temp/helengine", pathlib.Path("/cache"))])

if __name__ == "__main__":
    unittest.main()
