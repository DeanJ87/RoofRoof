"""Spec 029 research R11: the firmware registers no interrupt handler of its own.

ESP-IDF masks every interrupt not allocated with ESP_INTR_FLAG_IRAM while the
flash cache is off (an NVS or LittleFS write), so only an IRAM interrupt can
run then, and one whose code or data is in flash panics ("Cache disabled but
cached memory region accessed"). RoofRoof's timers dispatch to a task and its
inputs are polled by the roof's control task, so there is none. A change that
adds one must put the handler and everything it touches in IRAM/DRAM and be
listed here with the reason.
"""

import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PATTERN = re.compile(
    r"\b(attachInterrupt\w*|gpio_isr_handler_add|gpio_install_isr_service|esp_intr_alloc\w*|ESP_INTR_FLAG_IRAM"
    r"|ESP_TIMER_ISR|timerAttachInterrupt|IRAM_ATTR)\b"
)
# "path: pattern" -> why it is safe while the flash cache is off.
ALLOWED: dict[str, str] = {}


def sources():
    for folder in ("src", "include", "lib"):
        yield from (p for p in sorted((ROOT / folder).rglob("*")) if p.suffix in {".c", ".cpp", ".h", ".hpp"})


def findings() -> list[str]:
    out = []
    for path in sources():
        lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
        for number, line in enumerate(lines, 1):
            keys = (f"{path.relative_to(ROOT).as_posix()}: {m.group(1)}" for m in PATTERN.finditer(line))
            out.extend(f"{key} (line {number})" for key in keys if key not in ALLOWED)
    return out


class IsrAuditTest(unittest.TestCase):
    def test_no_interrupt_handlers_or_iram_code(self):
        self.assertEqual(findings(), [])

    def test_timers_dispatch_to_a_task(self):
        for path in (ROOT / "src").rglob("*.cpp"):
            text = path.read_text(encoding="utf-8")
            if "esp_timer_create" in text:
                self.assertIn("ESP_TIMER_TASK", text, f"{path.name}: an esp_timer callback must run in the timer task")


if __name__ == "__main__":
    unittest.main()
