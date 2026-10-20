"""Compile real binary32 kernels and compare independent references; audit MIPS imports.

Run in the pinned toolchain container with the real checkout mounted at /repo
and HELENGINE_PS1_TEST_OUTPUT_ROOT=/proof/float-math. No repository outputs.
"""
import ctypes
import hashlib
import json
import math
import os
import pathlib
import random
import struct
import subprocess
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
LIBRARY = ROOT / "third_party/newlib-float"
OUTPUT = pathlib.Path(os.environ["HELENGINE_PS1_TEST_OUTPUT_ROOT"])
FLAGS = ["-O2", "-fno-builtin", "-ffp-contract=off", "-fsingle-precision-constant"]


def bits(value):
    """Read the exact IEEE binary32 representation of a reference or native result."""
    return struct.unpack("<I", struct.pack("<f", value))[0]


def value(word):
    """Construct a reference input without decimal parsing or double arithmetic."""
    return struct.unpack("<f", struct.pack("<I", word))[0]


def ordered(word):
    """Map finite IEEE words monotonically for a same-sign ULP comparison."""
    return (~word & 0xffffffff) if word & 0x80000000 else word | 0x80000000


class NumericMathTests(unittest.TestCase):
    """Exercise actual vendored sources, without substituting hosted float wrappers."""

    @classmethod
    def setUpClass(cls):
        """Build a host library from the exact production sources in one fixed output."""
        OUTPUT.mkdir(parents=True, exist_ok=True)
        cls.sources = sorted(LIBRARY.glob("*.c"))
        cls.run_command(["gcc", *FLAGS, "-shared", "-fPIC", *map(str, cls.sources),
                         "-o", str(OUTPUT / "libnumeric_math.so")])
        cls.library = ctypes.CDLL(str(OUTPUT / "libnumeric_math.so"))

    @staticmethod
    def run_command(arguments):
        """Report compiler/linker errors instead of ignoring a failed fixture build."""
        result = subprocess.run(arguments, capture_output=True, text=True)
        if result.returncode:
            raise AssertionError(result.stdout + result.stderr)
        return result.stdout

    def test_pinned_sources_are_unmodified(self):
        """The revision manifest binds every upstream source actually compiled."""
        manifest = json.loads((LIBRARY / "upstream.json").read_text())
        self.assertEqual(set(manifest["files"]), {source.name for source in self.sources})
        for name, expected in manifest["files"].items():
            self.assertEqual(expected, hashlib.sha256((LIBRARY / name).read_bytes()).hexdigest(), name)

    def test_float_kernels_match_independent_reference(self):
        """Check broad exponents, signed zero, subnormals and domains against Python libm.

        For |angle| <= 1024 the trig bound is four binary32 ULP. Larger
        angles use absolute 2**-22 for sin/cos and 2**-21 radians of
        atan(tan) phase, reflecting binary32 argument reduction. Asin/atan2
        retain four ULP; sqrt/fabs/floor/ceil/fmod require exact bits.
        """
        generator = random.Random(0x505331)
        words = [0, 0x80000000, 1, 0x80000001, 0x007fffff, 0x00800000,
                 0x3f000000, 0xbf000000, 0x3f800000, 0xbf800000, 0x7f7fffff,
                 0x475253c1, 0x735b1072, 0x5f4ae3e2]
        words += [generator.randrange(0xff000000) for _ in range(50000)]
        cases = [("sinf", math.sin, 4), ("cosf", math.cos, 4), ("tanf", math.tan, 4),
                 ("sqrtf", math.sqrt, 0), ("asinf", math.asin, 4),
                 ("fabsf", math.fabs, 0), ("floorf", math.floor, 0), ("ceilf", math.ceil, 0)]
        for name, reference, tolerance in cases:
            function = getattr(self.library, name)
            function.argtypes = [ctypes.c_float]
            function.restype = ctypes.c_float
            for word in words:
                argument = value(word)
                if not math.isfinite(argument):
                    continue
                if name == "sqrtf" and argument < 0 or name == "asinf" and abs(argument) > 1:
                    self.assertTrue(math.isnan(function(argument)), (name, hex(word)))
                    continue
                expected = reference(argument)
                if name in ("floorf", "ceilf") and expected == 0:
                    expected = math.copysign(0.0, argument)
                actual_word, expected_word = bits(function(argument)), bits(expected)
                if abs(argument) > 1024 and name in ("sinf", "cosf", "tanf"):
                    actual, rounded = value(actual_word), value(expected_word)
                    if name == "tanf":
                        self.assertLessEqual(abs(math.atan(actual) - math.atan(rounded)), 2**-21,
                                             (name, hex(word), actual, rounded))
                    else:
                        self.assertLessEqual(abs(actual - rounded), 2**-22,
                                             (name, hex(word), actual, rounded))
                    continue
                self.assertLessEqual(abs(ordered(actual_word) - ordered(expected_word)), tolerance,
                                     (name, hex(word), hex(actual_word), hex(expected_word)))
            if name in ("sinf", "cosf", "tanf", "asinf"):
                self.assertTrue(math.isnan(function(float("inf"))), name)
            self.assertTrue(math.isnan(function(float("nan"))), name)
        for name, reference, tolerance in [("atan2f", math.atan2, 4), ("fmodf", math.fmod, 0)]:
            function = getattr(self.library, name)
            function.argtypes = [ctypes.c_float, ctypes.c_float]
            function.restype = ctypes.c_float
            for first, second in zip(words, reversed(words)):
                left, right = value(first), value(second)
                if not math.isfinite(left) or not math.isfinite(right) or name == "fmodf" and right == 0:
                    continue
                actual_word, expected_word = bits(function(left, right)), bits(reference(left, right))
                self.assertLessEqual(abs(ordered(actual_word) - ordered(expected_word)), tolerance,
                                     (name, hex(first), hex(second), hex(actual_word), hex(expected_word)))

    def test_actual_mips_link_has_no_double_arithmetic(self):
        """Link every float kernel plus the real API consumer and reject wider helpers."""
        objects = []
        for source in self.sources:
            target = OUTPUT / (source.stem + ".o")
            self.run_command(["mipsel-none-elf-gcc", "-msoft-float", "-ffreestanding", *FLAGS,
                              "-Wdouble-promotion", "-Werror=double-promotion", "-c", str(source), "-o", str(target)])
            objects.append(target)
        api = OUTPUT / "api.o"
        self.run_command(["mipsel-none-elf-g++", "-std=c++20", "-msoft-float", "-ffreestanding",
                          "-I" + str(ROOT), "-c", str(ROOT / "tests/host/numeric_math.cpp"), "-o", str(api)])
        linked = OUTPUT / "numeric_math.o"
        self.run_command(["mipsel-none-elf-ld", "-r", *map(str, objects), str(api), "-o", str(linked)])
        unresolved = self.run_command(["mipsel-none-elf-nm", "-u", str(linked)])
        forbidden = ["df", "sqrt", "sin", "cos", "tan", "fmod", "floor", "ceil", "fabs"]
        for line in unresolved.splitlines():
            symbol = line.split()[-1]
            self.assertTrue(symbol.startswith("__"), unresolved)
            self.assertFalse(any(part in symbol for part in forbidden), unresolved)
        print("MIPS binary32 imports:", unresolved.strip())


if __name__ == "__main__":
    unittest.main()
