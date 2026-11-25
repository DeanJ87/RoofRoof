"""Unit tests for the heap budget check (spec 038); no build needed."""

import struct
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import heap_budget  # noqa: E402 - imported once its folder is on sys.path


def tiny_elf(sections: dict[str, int]) -> bytes:
    """A 32-bit little-endian ELF with only section headers: the null one, the named ones, the names."""
    names = b"\0" + b"".join(n.encode() + b"\0" for n in sections) + b".shstrtab\0"
    header_size = 0x34
    shoff = header_size + len(names)
    rows = [(0, 0, 0, 0, 0, 0, 0, 0, 0, 0)]
    offset = 1
    for name, size in sections.items():
        rows.append((offset, 1, 0, 0, 0, size, 0, 0, 1, 0))
        offset += len(name) + 1
    rows.append((offset, 3, 0, 0, header_size, len(names), 0, 0, 1, 0))
    elf = bytearray(header_size)
    elf[:5] = b"\x7fELF\x01"
    struct.pack_into("<I", elf, 0x20, shoff)
    struct.pack_into("<HHH", elf, 0x2E, 40, len(rows), len(rows) - 1)
    return bytes(elf) + names + b"".join(struct.pack("<10I", *row) for row in rows)


class HeapBudgetTest(unittest.TestCase):
    def test_sizes_from_the_sources(self):
        self.assertEqual(heap_budget.size_of("10 * 1024"), 10240)
        self.assertEqual(heap_budget.size_of("8192"), 8192)
        with self.assertRaises(ValueError):
            heap_budget.size_of("TASK_BYTES")

    def test_every_consumer_is_found_in_the_tree(self):
        found = heap_budget.read_consumers()
        self.assertIn("frame pool", found)
        self.assertGreater(found["loop task stack"], 4096)

    def test_static_sections_from_the_elf(self):
        elf = tiny_elf({".dram0.data": 100, ".text": 5000, ".dram0.bss": 300})
        self.assertEqual(heap_budget.section_sizes(elf, heap_budget.STATIC_SECTIONS), {".dram0.data": 100, ".dram0.bss": 300})

    def test_over_budget_fails(self):
        budget = {"limitBytes": 1000, "staticBytes": 400}
        self.assertTrue(heap_budget.evaluate({"a": 500}, 400, budget)[2])
        self.assertFalse(heap_budget.evaluate({"a": 700}, 400, budget)[2])
        # Without a build the static part comes from the record.
        self.assertTrue(heap_budget.evaluate({"a": 600}, None, budget)[2])
        self.assertFalse(heap_budget.evaluate({"a": 601}, None, budget)[2])


if __name__ == "__main__":
    unittest.main()
