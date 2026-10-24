"""Heap high-water tool for PS1 builds measured with PSoXide route logs.

The nugget allocator (third_party/nugget/common/libc/alloc.c) records its
high-water mark in `maximum_heap_end`, a field of the static `s_heap_metadata`.
`address` prints the RAM address to hand to `--route-watch-u32`; `peak` reads
the resulting `--route-log` CSVs and reports the headroom below the stack.
"""
import argparse
import csv
import io
import json
import re
import sys

# struct s_heap_metadata on MIPS32 (4-byte pointers, no padding), alloc.c:98-108:
#   0  empty_block *head
#   4  allocated_block *bottom
#   8  allocated_block *top
#  12  void *maximum_heap_end
#  16  empty_block marker (8 bytes)
HEAP_END_FIELD_OFFSET = 12

_ADDRESS = r"0x[0-9a-fA-F]+"


def find_symbol_address(map_text: str, name: str) -> int:
    """Return the address of `name` in a GNU ld map.

    Global/linker symbols appear as `0xADDR   name`. File-local statics such as
    `s_heap_metadata` have no symbol line; they appear as an input section
    `.bss.name` (or `.data.name`) whose address is on the same or next line.
    """
    symbol = re.compile(r"^\s+(" + _ADDRESS + r")\s+" + re.escape(name) + r"(?:\s|$)")
    section = re.compile(r"^\s*\.[A-Za-z0-9_]+\." + re.escape(name) + r"(?:\s+(" + _ADDRESS + r"))?\s*(?:0x[0-9a-fA-F]+\s.*)?$")
    address_line = re.compile(r"^\s+(" + _ADDRESS + r")\s+0x[0-9a-fA-F]+")
    lines = map_text.splitlines()
    for index, line in enumerate(lines):
        match = symbol.match(line)
        if match:
            return int(match.group(1), 16)
        match = section.match(line)
        if match:
            if match.group(1):
                return int(match.group(1), 16)
            if index + 1 < len(lines):
                following = address_line.match(lines[index + 1])
                if following:
                    return int(following.group(1), 16)
    raise LookupError(name)


def peak_from_route_log(csv_text: str, column: str) -> int:
    """Return the maximum of `column` (decimal, or 0x-hex) over all rows."""
    reader = csv.DictReader(io.StringIO(csv_text))
    if reader.fieldnames is None or column not in reader.fieldnames:
        raise LookupError(column)
    peak = 0
    for row in reader:
        peak = max(peak, int(row[column].strip(), 0))
    if peak == 0:
        raise ValueError("watch column %s never left 0: wrong address or allocator never ran" % column)
    return peak


def report(peak_heap_end: int, heap_start: int, ceiling: int = 0x801F0000, margin: int = 16 * 1024) -> dict:
    """Summarise heap use; a heap end below heap_start means a wrong watch address."""
    if peak_heap_end < heap_start:
        raise ValueError("peak heap end 0x%08x is below __heap_start 0x%08x: address outside the heap" % (peak_heap_end, heap_start))
    free_bytes = ceiling - peak_heap_end
    return {
        "peak_bytes": peak_heap_end - heap_start,
        "ceiling_bytes": ceiling - heap_start,
        "free_bytes": free_bytes,
        "ok": free_bytes >= margin,
    }


def _watch_address(map_text: str) -> int:
    return find_symbol_address(map_text, "s_heap_metadata") + HEAP_END_FIELD_OFFSET


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    addr = sub.add_parser("address")
    addr.add_argument("--map", required=True)
    peak = sub.add_parser("peak")
    peak.add_argument("--map", required=True)
    peak.add_argument("--route-log", required=True, nargs="+")
    args = parser.parse_args(argv)

    with open(args.map, encoding="utf-8", errors="replace") as handle:
        map_text = handle.read()
    watch = _watch_address(map_text)
    heap_start = find_symbol_address(map_text, "__heap_start")
    if args.command == "address":
        print("watch_address=0x%08x" % watch)
        print("heap_start=0x%08x" % heap_start)
        return 0
    column = "ram_%08x" % watch
    highest = 0
    try:
        for path in args.route_log:
            with open(path, encoding="utf-8") as handle:
                highest = max(highest, peak_from_route_log(handle.read(), column))
        print(json.dumps(report(highest, heap_start), indent=2))
    except (ValueError, LookupError) as error:
        print("heap_peak: %s" % error, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
