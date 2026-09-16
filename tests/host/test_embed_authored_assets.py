import importlib.util
import json
import struct
import tempfile
import unittest
from pathlib import Path


PACKER = Path(__file__).parents[2] / "scripts" / "embed-authored-assets.py"
SPEC = importlib.util.spec_from_file_location("embed_authored_assets", PACKER)
embed_authored_assets = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(embed_authored_assets)
pack_archive = embed_authored_assets.pack_archive
parse_archive = embed_authored_assets.parse_archive
write_incbin_assembly = embed_authored_assets.write_incbin_assembly
ArchiveError = embed_authored_assets.ArchiveError


class EmbeddedAssetArchiveTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.scene = self.root / "scenes" / "subset.helen"
        self.scene.parent.mkdir(parents=True)
        self.scene.write_bytes(b"HELE" + bytes(range(37)))
        self.material = self.root / "materials" / "CubeTestSolid.cooked"
        self.material.parent.mkdir(parents=True)
        self.material.write_bytes(b"MAT")
        self.undeclared = self.root / "materials" / "stray.bin"
        self.undeclared.write_bytes(b"STRAY")

    def tearDown(self):
        self.temp.cleanup()

    def declared(self):
        return [
            ("scenes/subset.helen", self.scene),
            ("materials/CubeTestSolid.cooked", self.material),
        ]

    def test_archive_is_little_endian_sorted_and_four_byte_aligned(self):
        archive = pack_archive(self.declared())
        magic, version, count, total = struct.unpack_from("<4sIII", archive, 0)
        self.assertEqual(magic, b"HEP1")
        self.assertEqual(version, 1)
        self.assertEqual(count, 2)
        self.assertEqual(total, len(archive))
        entries = parse_archive(archive)
        self.assertEqual([entry.relative_path for entry in entries], ["materials/CubeTestSolid.cooked", "scenes/subset.helen"])
        for entry in entries:
            self.assertEqual(entry.data_offset % 4, 0)
        self.assertEqual(entries[0].data, b"MAT")
        self.assertEqual(entries[1].data, b"HELE" + bytes(range(37)))
        self.assertEqual(len(archive) % 4, 0)

    def test_archive_bytes_are_deterministic_regardless_of_declaration_order(self):
        forward = pack_archive(self.declared())
        reversed_order = pack_archive(list(reversed(self.declared())))
        self.assertEqual(forward, reversed_order)
        self.assertEqual(forward, pack_archive(self.declared()))

    def test_undeclared_staging_files_are_excluded(self):
        entries = parse_archive(pack_archive(self.declared()))
        self.assertNotIn("materials/stray.bin", [entry.relative_path for entry in entries])

    def test_duplicate_normalized_paths_are_rejected(self):
        declared = self.declared() + [("scenes\\subset.helen", self.material)]
        with self.assertRaisesRegex(ArchiveError, "duplicate"):
            pack_archive(declared)

    def test_unsafe_paths_are_rejected(self):
        for unsafe in ("../scenes/subset.helen", "/scenes/subset.helen", "", "scenes/./subset.helen", "C:/scenes/subset.helen"):
            with self.subTest(path=unsafe):
                with self.assertRaisesRegex(ArchiveError, "path"):
                    pack_archive([(unsafe, self.scene)])

    def test_missing_declared_file_is_rejected(self):
        with self.assertRaisesRegex(ArchiveError, "missing"):
            pack_archive([("scenes/subset.helen", self.root / "scenes" / "absent.helen")])

    def test_truncated_archive_is_rejected(self):
        archive = pack_archive(self.declared())
        with self.assertRaisesRegex(ArchiveError, "truncated"):
            parse_archive(archive[:-1])
        with self.assertRaisesRegex(ArchiveError, "truncated"):
            parse_archive(archive[:12])

    def test_corrupt_entry_range_is_rejected(self):
        archive = bytearray(pack_archive(self.declared()))
        # First entry record starts after the 16-byte header; its data_length is the fourth field.
        struct.pack_into("<I", archive, 16 + 12, 0x7FFFFFFF)
        with self.assertRaisesRegex(ArchiveError, "range"):
            parse_archive(bytes(archive))

    def test_declarations_accept_utf8_bom(self):
        declarations = self.root / "declarations.json"
        declarations.write_bytes(b"\xef\xbb\xbf" + json.dumps({"files": [{"relativePath": "scenes/subset.helen", "sourcePath": str(self.scene)}]}).encode("utf-8"))
        loaded = embed_authored_assets.load_declarations(declarations)
        self.assertEqual([("scenes/subset.helen", str(self.scene))], loaded)

    def test_incbin_assembly_exposes_start_and_end_symbols(self):
        text = write_incbin_assembly("authored_assets.bin", "he_ps1_authored_archive")
        self.assertIn(".incbin \"authored_assets.bin\"", text)
        self.assertIn(".global he_ps1_authored_archive", text)
        self.assertIn("he_ps1_authored_archive_end", text)
        self.assertIn(".section .rodata", text)
        self.assertIn(".balign 4", text)


if __name__ == "__main__":
    unittest.main()
