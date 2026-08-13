import tempfile
import unittest
from pathlib import Path

from compare import WpmSweepRow, latest_wpm_sweep, parse_log, parse_wpm_sweep, write_summary


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

    def test_wpm_sweep_produces_analysis_and_svg_graph(self) -> None:
        log = (
            "[bench] start board=Test Board id=test\n"
            "[bench] metric=render ok=1 us=90000 iterations=2 avg_us=45000 deadline_misses=0\n"
            "[bench] wpm_transition phase=multilingual wpm=10 deadline_misses=0\n"
            "[bench] wpm_transition phase=multilingual wpm=1000 deadline_misses=1\n"
            "[bench] wpm_sweep phase=multilingual min_wpm=10 max_wpm=1000 step_wpm=10 transitions=2 "
            "samples=2 avg_us=45000 p50_us=40000 p95_us=40000 p99_us=40000 max_us=70000\n"
        )
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            path = root / "20260811-000000-test.log"
            path.write_text(log, encoding="utf-8")
            metrics = parse_log(path)
            sweep = parse_wpm_sweep(path)
            summary = root / "result.md"
            write_summary(metrics, summary, sweep)

            self.assertEqual(list(range(10, 1001, 10)), [row.wpm for row in sweep])
            self.assertIn("first observed failure is 1000 WPM", summary.read_text(encoding="utf-8"))
            graph = summary.with_suffix(".wpm.svg")
            self.assertTrue(graph.is_file())
            self.assertIn("WPM vs multilingual RSVP rendering time", graph.read_text(encoding="utf-8"))

    def test_latest_wpm_sweep_prefers_the_complete_reader_cycle(self) -> None:
        common = dict(
            log="20260811-000000-test.log",
            env="test",
            board="Test Board",
            wpm=1000,
            samples=2,
            avg_us=45_000,
            p50_us=40_000,
            p95_us=50_000,
            p99_us=55_000,
            max_us=59_000,
            deadline_us=60_000,
            deadline_misses=0,
        )
        frame = WpmSweepRow(phase="reading_multilingual_rsvp_frame", **common)
        cycle = WpmSweepRow(phase="reading_multilingual_rsvp_cycle", **common)

        self.assertEqual([cycle], latest_wpm_sweep([frame, cycle]))


if __name__ == "__main__":
    unittest.main()
