import tempfile
import unittest
from pathlib import Path

from compare import parse_log


class ParseLogTests(unittest.TestCase):
    def test_parses_concatenated_metric_lines(self) -> None:
        line = (
            "[bench] start board=Test Board id=test\n"
            "[bench] metric=first ok=1 us=120 iterations=1 avg_us=120 deadline_misses=3"
            "[bench] metric=second ok=1 us=45 iterations=1 avg_us=45 deadline_misses=0\n"
        )
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "20260811-000000-test.log"
            path.write_text(line, encoding="utf-8")
            rows = parse_log(path)

        self.assertEqual(["first", "second"], [row.metric for row in rows])
        self.assertEqual([120, 45], [row.us for row in rows])
        self.assertEqual([3, 0], [row.deadline_misses for row in rows])
        self.assertEqual("Test Board", rows[0].board)

    def test_ignores_truncated_metric_lines(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "20260811-000000-test.log"
            path.write_text(
                "[bench] metric=broken ok=1 ms=4 heap_after=12 deadline_misses=0\n"
                "[bench] metric=complete ok=1 us=45 iterations=1 avg_us=45 deadline_misses=0\n",
                encoding="utf-8",
            )
            rows = parse_log(path)

        self.assertEqual(["complete"], [row.metric for row in rows])


if __name__ == "__main__":
    unittest.main()
