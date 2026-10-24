import importlib.util
import unittest
from pathlib import Path


TOOL = Path(__file__).parents[2] / "scripts" / "heap_peak.py"
SPEC = importlib.util.spec_from_file_location("heap_peak", TOOL)
heap_peak = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(heap_peak)

MAP_EXCERPT = """\
 *fill*         0x80131715        0x3 
 .bss.s_heap_metadata
                0x80123450       0x18 /work/third_party/nugget/psyqo/libpsyqo.a(alloc.o)
 .bss._ZN12_GLOBAL__N_1L16s_playCDDAActionE
                0x80123468        0x4 /work/libpsyqo.a(cdrom.o)
                0x80158000                        __heap_start = __heap_base
"""


class HeapPeakTests(unittest.TestCase):
    def test_finds_symbol_in_map(self):
        self.assertEqual(0x80123450, heap_peak.find_symbol_address(MAP_EXCERPT, "s_heap_metadata"))
        self.assertEqual(0x80158000, heap_peak.find_symbol_address(MAP_EXCERPT, "__heap_start"))

    def test_missing_symbol_raises(self):
        with self.assertRaises(LookupError):
            heap_peak.find_symbol_address(MAP_EXCERPT, "no_such_symbol")

    def test_peak_takes_max_of_column(self):
        csv_text = "route_tick,ram_8012345c\n" +"0,0\n1,1500000\n2,1200000\n"
        self.assertEqual(1500000, heap_peak.peak_from_route_log(csv_text, "ram_8012345c"))

    def test_all_zero_column_raises(self):
        with self.assertRaisesRegex(ValueError, "never left 0"):
            heap_peak.peak_from_route_log("route_tick,ram_8012345c
0,0
1,0
", "ram_8012345c")

    def test_peak_below_heap_start_raises(self):
        with self.assertRaisesRegex(ValueError, "below __heap_start"):
            heap_peak.report(0x80100000, 0x80158000)

    def test_report_fails_inside_margin(self):
        result = heap_peak.report(0x801F0000 - 8 * 1024, 0x80158000)
        self.assertEqual(8 * 1024, result["free_bytes"])
        self.assertFalse(result["ok"])

    def test_report_passes_with_margin(self):
        result = heap_peak.report(0x801F0000 - 100 * 1024, 0x80158000)
        self.assertEqual(100 * 1024, result["free_bytes"])
        self.assertTrue(result["ok"])
        self.assertEqual(0x801F0000 - 0x80158000, result["ceiling_bytes"])

    def test_heap_end_field_offset(self):
        self.assertEqual(12, heap_peak.HEAP_END_FIELD_OFFSET)


if __name__ == "__main__":
    unittest.main()
