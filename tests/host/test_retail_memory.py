import importlib.util
import struct
import unittest
from pathlib import Path


CHECKER = Path(__file__).parents[2] / "scripts" / "check-retail-memory.py"
SPEC = importlib.util.spec_from_file_location("check_retail_memory", CHECKER)
check_retail_memory = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(check_retail_memory)
validate_layout = check_retail_memory.validate_layout


def valid_symbols():
    return {
        "__ram_top": 0x80200000,
        "__stack_start": 0x801F0000,
        "__sp": 0x801FFF00,
        "__bss_end": 0x801F0000,
        "__heap_start": 0x801F0000,
    }


def valid_exe_header():
    return {
        "load_address": 0x80010000,
        "entry_point": 0x80010000,
        "payload_size": 0x800,
        "stack_pointer": 0x801FFF00,
    }


class RetailMemoryValidationTests(unittest.TestCase):
    def test_bss_crosses_stack_reservation(self):
        sections = [{"name": ".bss", "address": 0x801EFFF0, "size": 32, "allocated": True}]
        with self.assertRaisesRegex(ValueError, "bss"):
            validate_layout(sections, valid_symbols(), valid_exe_header())

    def test_section_end_equal_to_heap_ceiling_is_valid(self):
        sections = [{"name": ".bss", "address": 0x801EFFF0, "size": 16, "allocated": True}]
        validate_layout(sections, valid_symbols(), valid_exe_header())

    def test_rejects_missing_required_symbol(self):
        symbols = valid_symbols()
        del symbols["__sp"]
        with self.assertRaisesRegex(ValueError, "__sp"):
            validate_layout([], symbols, valid_exe_header())

    def test_rejects_static_image_above_heap_start(self):
        symbols = valid_symbols()
        symbols["__bss_end"] = symbols["__heap_start"] = 0x80015000
        sections = [{"name": ".data", "address": 0x80010000, "size": 0x5001, "allocated": True}]
        with self.assertRaisesRegex(ValueError, "__heap_start"):
            validate_layout(sections, symbols, valid_exe_header())

    def test_rejects_non_retail_stack_boundary(self):
        symbols = valid_symbols()
        symbols["__stack_start"] = 0x801EFFFF
        with self.assertRaisesRegex(ValueError, "__stack_start"):
            validate_layout([], symbols, valid_exe_header())

    def test_rejects_allocated_section_in_unknown_region(self):
        sections = [{"name": ".bad", "address": 0x90000000, "size": 4, "allocated": True}]
        with self.assertRaisesRegex(ValueError, "bad"):
            validate_layout(sections, valid_symbols(), valid_exe_header())

    def test_accepts_scratchpad_as_separate_allocated_region(self):
        sections = [{"name": ".scratchpad", "address": 0x1F800000, "size": 0x400, "allocated": True}]
        validate_layout(sections, valid_symbols(), valid_exe_header())

    def test_rejects_section_overflow(self):
        sections = [{"name": ".overflow", "address": 0xFFFFFFFF, "size": 2, "allocated": True}]
        with self.assertRaisesRegex(ValueError, "overflow"):
            validate_layout(sections, valid_symbols(), valid_exe_header())

    def test_rejects_oversized_exe_load(self):
        header = valid_exe_header()
        header["payload_size"] = 0x001F1000
        with self.assertRaisesRegex(ValueError, "payload"):
            validate_layout([], valid_symbols(), header)

    def test_rejects_exe_payload_overlapping_reserved_stack(self):
        header = valid_exe_header()
        header["load_address"] = 0x801EFFF0
        header["entry_point"] = 0x801EFFF0
        header["payload_size"] = 0x20
        with self.assertRaisesRegex(ValueError, "payload"):
            validate_layout([], valid_symbols(), header)

    def test_accepts_exe_payload_ending_at_stack_boundary(self):
        header = valid_exe_header()
        header["load_address"] = 0x801EFFF0
        header["entry_point"] = 0x801EFFF0
        header["payload_size"] = 0x10
        validate_layout([], valid_symbols(), header)

    def test_accepts_header_metadata_at_loader_address(self):
        sections = [{"name": ".PSX_EXE_Header", "address": 0x8000F800, "size": 2048, "allocated": True}]
        validate_layout(sections, valid_symbols(), valid_exe_header())

    def test_rejects_entry_outside_loadable_ram(self):
        header = valid_exe_header()
        header["entry_point"] = 0x90000000
        with self.assertRaisesRegex(ValueError, "entry"):
            validate_layout([], valid_symbols(), header)

    def test_rejects_stack_pointer_outside_reserved_stack(self):
        symbols = valid_symbols()
        symbols["__sp"] = 0x801EFFF0
        header = valid_exe_header()
        header["stack_pointer"] = symbols["__sp"]
        with self.assertRaisesRegex(ValueError, "stack"):
            validate_layout([], symbols, header)

    def test_rejects_bad_header_magic_when_parsing(self):
        data = bytearray(check_retail_memory.ELF32_HEADER.size)
        with self.assertRaisesRegex(ValueError, "ELF identity"):
            check_retail_memory.Elf32Parser(bytes(data))

    def test_rejects_non_mips_elf_when_parsing(self):
        ident = b"\x7fELF" + bytes([1, 1, 1]) + bytes(9)
        data = check_retail_memory.ELF32_HEADER.pack(
            ident,
            check_retail_memory.ET_EXEC,
            3,
            check_retail_memory.EV_CURRENT,
            0,
            0,
            0,
            0,
            check_retail_memory.ELF32_HEADER.size,
            0,
            0,
            check_retail_memory.ELF32_SECTION.size,
            1,
            0,
        )
        with self.assertRaisesRegex(ValueError, "ELF machine"):
            check_retail_memory.Elf32Parser(data)


if __name__ == "__main__":
    unittest.main()
