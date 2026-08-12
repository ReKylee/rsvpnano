#!/usr/bin/env python3

import argparse
import datetime as dt
import re
from collections import defaultdict
from dataclasses import dataclass
from pathlib import Path


START_RE = re.compile(r"\[bench\] start board=(.+?) id=(.+)$")
METRIC_RE = re.compile(r"\[bench\] metric=([^\s]+)(.*?)(?=\[bench\] metric=|$)")
VALUE_RE = re.compile(r"([a-z_]+)=(-?\d+)")


@dataclass
class MetricRow:
    log: str
    env: str
    board: str
    metric: str
    ok: str
    ms: int
    us: int
    iterations: int
    avg_us: int
    bytes: int
    rate_kib_s: int
    heap_before: int
    heap_after: int
    heap_min: int
    deadline_misses: int


def env_from_log_name(path: Path) -> str:
    return re.sub(r"^\d{8}-\d{6}-", "", path.stem)


def parse_log(path: Path) -> list[MetricRow]:
    rows: list[MetricRow] = []
    board = ""
    env = env_from_log_name(path)
    with path.open("r", encoding="utf-8", errors="replace") as log_file:
        for line in log_file:
            start = START_RE.search(line)
            if start:
                board = start.group(1)
                continue

            for metric in METRIC_RE.finditer(line):
                values = {key: int(value) for key, value in VALUE_RE.findall(metric.group(2))}
                if not {"ok", "us", "iterations", "avg_us", "deadline_misses"} <= values.keys():
                    continue

                rows.append(
                    MetricRow(
                        log=path.name,
                        env=env,
                        board=board,
                        metric=metric.group(1),
                        ok=str(values.get("ok", 0)),
                        ms=values.get("ms", 0),
                        us=values.get("us", values.get("ms", 0) * 1000),
                        iterations=values.get("iterations", 1),
                        avg_us=values.get("avg_us", values.get("us", values.get("ms", 0) * 1000)),
                        bytes=values.get("bytes", 0),
                        rate_kib_s=values.get("rate_kib_s", 0),
                        heap_before=values.get("heap_before", 0),
                        heap_after=values.get("heap_after", 0),
                        heap_min=values.get("heap_min", 0),
                        deadline_misses=values.get("deadline_misses", 0),
                    )
                )
    return rows


def latest_by_env_and_metric(rows: list[MetricRow]) -> list[MetricRow]:
    latest: dict[tuple[str, str], MetricRow] = {}
    for row in sorted(rows, key=lambda item: item.log):
        latest[(row.env, row.metric)] = row
    return sorted(latest.values(), key=lambda item: (item.env, item.metric))


def write_summary(rows: list[MetricRow], output: Path) -> None:
    lines: list[str] = [
        "# Benchmark Summary",
        "",
        f"Generated: {dt.datetime.now().strftime('%Y-%m-%d %H:%M:%S')}",
        "",
    ]

    if not rows:
        lines.append("No benchmark metrics found.")
        output.write_text("\n".join(lines) + "\n", encoding="utf-8")
        return

    lines += [
        "| Log | Env | Board | Metric | OK | total us | iterations | avg us | deadline misses | bytes | KiB/s | heap delta | min heap |",
        "| --- | --- | --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |",
    ]
    for row in rows:
        lines.append(
            f"| {row.log} | {row.env} | {row.board} | {row.metric} | {row.ok} | "
            f"{row.us} | {row.iterations} | {row.avg_us} | {row.deadline_misses} | {row.bytes} | {row.rate_kib_s} | "
            f"{row.heap_after - row.heap_before} | {row.heap_min} |"
        )

    lines += [
        "",
        "## Latest Metrics",
        "",
        "| Env | Metric | OK | avg us | deadline misses | KiB/s | heap delta | Log |",
        "| --- | --- | ---: | ---: | ---: | ---: | ---: | --- |",
    ]
    latest_rows = latest_by_env_and_metric(rows)
    for row in latest_rows:
        lines.append(
            f"| {row.env} | {row.metric} | {row.ok} | {row.avg_us} | {row.deadline_misses} | {row.rate_kib_s} | "
            f"{row.heap_after - row.heap_before} | {row.log} |"
        )

    lines += ["", "## Quick Analysis", ""]
    by_metric: dict[str, list[MetricRow]] = defaultdict(list)
    for row in latest_rows:
        by_metric[row.metric].append(row)

    for metric_name in sorted(by_metric):
        successful = [row for row in by_metric[metric_name] if row.ok == "1"]
        if not successful:
            lines.append(f"- `{metric_name}`: no successful samples.")
            continue
        fastest = min(successful, key=lambda item: item.avg_us)
        slowest = max(successful, key=lambda item: item.avg_us)
        lines.append(
            f"- `{metric_name}`: fastest `{fastest.env}` at {fastest.avg_us} us/iteration; "
            f"slowest `{slowest.env}` at {slowest.avg_us} us/iteration."
        )

    output.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description="Create a Markdown summary from benchmark logs.")
    parser.add_argument("--logs-dir", default=str(Path(__file__).resolve().parent / "logs"))
    parser.add_argument("--output", default=str(Path(__file__).resolve().parent / "logs" / "summary.md"))
    args = parser.parse_args()

    logs_dir = Path(args.logs_dir)
    output = Path(args.output)
    logs_dir.mkdir(parents=True, exist_ok=True)

    rows: list[MetricRow] = []
    for log_path in sorted(logs_dir.glob("*.log"), key=lambda item: item.stat().st_mtime):
        rows.extend(parse_log(log_path))

    write_summary(rows, output)
    print(f"[bench-script] wrote {output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
