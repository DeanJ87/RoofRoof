"""Fix 041: tools/firmware/stack_depth.py's reading of a disassembly."""

import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import stack_depth  # noqa: E402 - imported once its folder is on the path

DISASSEMBLY = """
40100000 <outer>:
40100000:\tentry\ta1, 64
40100003:\tcall8\t40100100 <middle>
40100006:\tl32r\ta8, 400d0010 <_stext+0x10> (40100200 <deep>)
40100009:\tcallx8\ta8
4010000c:\tl32r\ta9, 400d0014 <_stext+0x14> (40100300 <abort>)
4010000f:\tcallx8\ta9
40100012:\tcallx8\ta10
40100100 <middle>:
40100100:\tentry\ta1, 0x20
40100103:\tcall8\t40100000 <outer>
40100200 <deep>:
40100200:\tentry\ta1, 800
40100203:\tcall8\t40100400 <leaf>
40100300 <abort>:
40100300:\tentry\ta1, 9000
40100400 <leaf>:
40100400:\tentry\ta1, 32
""".strip().splitlines()


class ParseTest(unittest.TestCase):
    def setUp(self):
        self.graph = stack_depth.parse(DISASSEMBLY)

    def test_frames_come_from_entry(self):
        self.assertEqual(self.graph.frame["outer"], 64)
        self.assertEqual(self.graph.frame["middle"], 32)
        self.assertEqual(self.graph.frame["deep"], 800)

    def test_direct_and_literal_calls_are_followed(self):
        self.assertEqual(self.graph.calls["outer"], {"middle", "deep", "abort"})

    def test_the_deepest_path_is_summed_and_the_device_ending_ones_cut(self):
        depth, path = self.graph.depth("outer")
        self.assertEqual(depth, 64 + 800 + 32)
        self.assertEqual(path, ["outer", "deep", "leaf"])

    def test_recursion_counts_once(self):
        graph = stack_depth.parse(DISASSEMBLY)
        self.assertEqual(graph.depth("middle")[0], 32 + 64 + 800 + 32)

    def test_reaches_finds_a_direct_path(self):
        self.assertEqual(self.graph.reaches("outer", {"leaf"}, set()), ["outer", "deep", "leaf"])
        self.assertIsNone(self.graph.reaches("leaf", {"outer"}, set()))


class TaskCheckTest(unittest.TestCase):
    def check(self, stack_bytes: int) -> bool:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "Task.h").write_text(f"constexpr uint32_t STACK_BYTES = {stack_bytes};\n")
            task = stack_depth.TaskCheck("t", ("Task.h", r"STACK_BYTES = ([\d\s*]+);"), ["^outer$", "^leaf$"])
            ok, _ = stack_depth.check_task(stack_depth.parse(DISASSEMBLY), task, root, False)
            return ok

    def test_a_stage_counts_on_top_of_the_stages_above(self):
        # outer's own path is deepest here (896 B); leaf under outer's frame is 96 B.
        need = 64 + 800 + 32 + stack_depth.MARGIN_BYTES
        self.assertTrue(self.check(need))
        self.assertFalse(self.check(need - 1))

    def test_sizes_read_like_the_heap_budget(self):
        self.assertEqual(stack_depth.size_of("10 * 1024"), 10240)
        with self.assertRaises(ValueError):
            stack_depth.size_of("STACK")


class RealSourcesTest(unittest.TestCase):
    def test_every_checked_stack_size_is_found(self):
        for task in stack_depth.TASK_CHECKS:
            self.assertGreater(stack_depth.source_size(*task.stack), 4096, task.task)


if __name__ == "__main__":
    unittest.main()
