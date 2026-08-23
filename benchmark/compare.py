#!/usr/bin/env python3

import argparse
import datetime as dt
import html
import math
import re
from collections import defaultdict
from dataclasses import dataclass
from pathlib import Path


START_RE = re.compile(r"\[bench\] start board=(.+?) id=(.+)$")
METRIC_RE = re.compile(r"\[bench\] metric=([^\s]+)(.*?)(?=\[bench\] metric=|$)")
WPM_SWEEP_RE = re.compile(r"\[bench\] wpm_sweep phase=([^\s]+)(.*?)(?=\[bench\]|$)")
WPM_TRANSITION_RE = re.compile(r"\[bench\] wpm_transition phase=([^\s]+)(.*?)(?=\[bench\]|$)")
VALUE_RE = re.compile(r"([a-z][a-z0-9_]*)=(-?\d+)")


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


@dataclass
class WpmSweepRow:
    log: str
    env: str
    board: str
    phase: str
    wpm: int
    samples: int
    avg_us: int
    p50_us: int
    p95_us: int
    p99_us: int
    max_us: int
    deadline_us: int
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


def parse_wpm_sweep(path: Path) -> list[WpmSweepRow]:
    rows: list[WpmSweepRow] = []
    board = ""
    env = env_from_log_name(path)
    legacy_required = {
        "wpm", "samples", "avg_us", "p50_us", "p95_us", "p99_us", "max_us", "deadline_us",
        "deadline_misses",
    }
    summary_required = {
        "min_wpm", "max_wpm", "step_wpm", "transitions", "samples", "avg_us", "p50_us", "p95_us",
        "p99_us", "max_us",
    }
    summaries: list[tuple[str, str, dict[str, int]]] = []
    transitions: dict[str, list[tuple[int, int]]] = defaultdict(list)
    with path.open("r", encoding="utf-8", errors="replace") as log_file:
        for line in log_file:
            start = START_RE.search(line)
            if start:
                board = start.group(1)
                continue

            for sweep in WPM_SWEEP_RE.finditer(line):
                values = {key: int(value) for key, value in VALUE_RE.findall(sweep.group(2))}
                phase = sweep.group(1)
                if legacy_required <= values.keys():
                    rows.append(
                        WpmSweepRow(
                            log=path.name,
                            env=env,
                            board=board,
                            phase=phase,
                            **{key: values[key] for key in legacy_required},
                        )
                    )
                elif summary_required <= values.keys():
                    summaries.append((phase, board, values))

            for transition in WPM_TRANSITION_RE.finditer(line):
                values = {key: int(value) for key, value in VALUE_RE.findall(transition.group(2))}
                if {"wpm", "deadline_misses"} <= values.keys():
                    transitions[transition.group(1)].append((values["wpm"], values["deadline_misses"]))

    for phase, summary_board, values in summaries:
        phase_transitions = sorted(set(transitions[phase]))
        if (len(phase_transitions) != values["transitions"]
                or not phase_transitions
                or phase_transitions[0][0] != values["min_wpm"]):
            continue

        transition_index = 0
        misses = 0
        for wpm in range(values["min_wpm"], values["max_wpm"] + 1, values["step_wpm"]):
            while transition_index < len(phase_transitions) and phase_transitions[transition_index][0] <= wpm:
                _, misses = phase_transitions[transition_index]
                transition_index += 1
            rows.append(
                WpmSweepRow(
                    log=path.name,
                    env=env,
                    board=summary_board,
                    phase=phase,
                    wpm=wpm,
                    samples=values["samples"],
                    avg_us=values["avg_us"],
                    p50_us=values["p50_us"],
                    p95_us=values["p95_us"],
                    p99_us=values["p99_us"],
                    max_us=values["max_us"],
                    deadline_us=60_000_000 // wpm,
                    deadline_misses=misses,
                )
            )
    return rows


def latest_by_env_and_metric(rows: list[MetricRow]) -> list[MetricRow]:
    latest: dict[tuple[str, str], MetricRow] = {}
    for row in sorted(rows, key=lambda item: item.log):
        latest[(row.env, row.metric)] = row
    return sorted(latest.values(), key=lambda item: (item.env, item.metric))


def latest_wpm_sweep(rows: list[WpmSweepRow]) -> list[WpmSweepRow]:
    if not rows:
        return []
    latest_log = max(row.log for row in rows)
    by_phase: dict[str, list[WpmSweepRow]] = defaultdict(list)
    for row in rows:
        if row.log == latest_log:
            by_phase[row.phase].append(row)
    _, sweep = max(by_phase.items(), key=lambda item: (item[0].endswith("_cycle"), len(item[1])))
    return sorted(sweep, key=lambda row: row.wpm)


def _time_label(microseconds: float) -> str:
    if microseconds >= 1_000_000:
        return f"{microseconds / 1_000_000:g} s"
    if microseconds >= 1_000:
        return f"{microseconds / 1_000:g} ms"
    return f"{microseconds:g} us"


def write_wpm_graph(rows: list[WpmSweepRow], output: Path) -> None:
    rows = sorted(rows, key=lambda row: row.wpm)
    if not rows:
        return

    width, height = 1000, 600
    left, right, top, bottom = 88, 28, 100, 68
    plot_width = width - left - right
    plot_height = height - top - bottom
    minimum_wpm, maximum_wpm = rows[0].wpm, rows[-1].wpm
    series = (
        ("Deadline", "deadline_us", "#d97706", "8 5"),
        ("Average", "avg_us", "#2563eb", ""),
        ("p95", "p95_us", "#7c3aed", ""),
        ("p99", "p99_us", "#db2777", ""),
        ("Maximum", "max_us", "#dc2626", ""),
    )
    values = [getattr(row, field) for row in rows for _, field, _, _ in series]
    minimum_power = math.floor(math.log10(max(1, min(values))))
    maximum_power = math.ceil(math.log10(max(values)))
    if minimum_power == maximum_power:
        maximum_power += 1

    def x_position(wpm: int) -> float:
        if minimum_wpm == maximum_wpm:
            return left + plot_width / 2
        return left + (wpm - minimum_wpm) * plot_width / (maximum_wpm - minimum_wpm)

    def y_position(value: int) -> float:
        normalized = (math.log10(max(1, value)) - minimum_power) / (maximum_power - minimum_power)
        return top + plot_height * (1 - normalized)

    svg = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}">',
        '<rect width="100%" height="100%" fill="#ffffff"/>',
        '<style>text{font-family:system-ui,sans-serif;fill:#1f2937}.axis{stroke:#374151;stroke-width:1.5}.grid{stroke:#d1d5db;stroke-width:1}.series{fill:none;stroke-width:2.5;stroke-linejoin:round;stroke-linecap:round}</style>',
        f'<text x="{width / 2}" y="30" text-anchor="middle" font-size="22" font-weight="700">WPM vs multilingual RSVP rendering time</text>',
        '<text x="500" y="54" text-anchor="middle" font-size="13" fill="#6b7280">One physical render sample set; each WPM applies its supported per-word deadline</text>',
    ]

    legend_x = left
    for label, _, color, dash in series:
        dash_attribute = f' stroke-dasharray="{dash}"' if dash else ""
        svg.append(f'<line x1="{legend_x}" y1="76" x2="{legend_x + 28}" y2="76" stroke="{color}" stroke-width="3"{dash_attribute}/>')
        svg.append(f'<text x="{legend_x + 34}" y="81" font-size="13">{label}</text>')
        legend_x += 145

    for power in range(minimum_power, maximum_power + 1):
        value = 10 ** power
        y = y_position(value)
        svg.append(f'<line class="grid" x1="{left}" y1="{y:.1f}" x2="{left + plot_width}" y2="{y:.1f}"/>')
        svg.append(f'<text x="{left - 10}" y="{y + 4:.1f}" text-anchor="end" font-size="12">{_time_label(value)}</text>')

    x_ticks = {minimum_wpm, maximum_wpm, *range(100, maximum_wpm + 1, 100)}
    for wpm in sorted(wpm for wpm in x_ticks if minimum_wpm <= wpm <= maximum_wpm):
        x = x_position(wpm)
        svg.append(f'<line class="grid" x1="{x:.1f}" y1="{top}" x2="{x:.1f}" y2="{top + plot_height}"/>')
        svg.append(f'<text x="{x:.1f}" y="{top + plot_height + 24}" text-anchor="middle" font-size="12">{wpm}</text>')

    svg += [
        f'<line class="axis" x1="{left}" y1="{top}" x2="{left}" y2="{top + plot_height}"/>',
        f'<line class="axis" x1="{left}" y1="{top + plot_height}" x2="{left + plot_width}" y2="{top + plot_height}"/>',
        f'<text x="{left + plot_width / 2}" y="{height - 18}" text-anchor="middle" font-size="14">Words per minute</text>',
        f'<text x="20" y="{top + plot_height / 2}" text-anchor="middle" font-size="14" transform="rotate(-90 20 {top + plot_height / 2})">Time per frame (log scale)</text>',
    ]

    for _, field, color, dash in series:
        points = " ".join(f"{x_position(row.wpm):.1f},{y_position(getattr(row, field)):.1f}" for row in rows)
        dash_attribute = f' stroke-dasharray="{dash}"' if dash else ""
        svg.append(f'<polyline class="series" points="{points}" stroke="{color}"{dash_attribute}/>')

    for row in rows:
        if row.deadline_misses:
            svg.append(
                f'<circle cx="{x_position(row.wpm):.1f}" cy="{y_position(row.max_us):.1f}" r="4" '
                'fill="#111827"><title>Observed deadline miss</title></circle>'
            )

    svg.append(f'<text x="{width - right}" y="{height - 18}" text-anchor="end" font-size="11" fill="#6b7280">{html.escape(rows[0].phase)}</text>')
    svg.append("</svg>")
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text("\n".join(svg) + "\n", encoding="utf-8")


def write_summary(rows: list[MetricRow], output: Path, wpm_rows: list[WpmSweepRow] | None = None) -> None:
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

    sweep = latest_wpm_sweep(wpm_rows or [])
    if sweep:
        graph_path = output.with_suffix(".wpm.svg")
        write_wpm_graph(sweep, graph_path)
        highest = sweep[-1]
        worst_max_us = max(row.max_us for row in sweep)
        worst_p99_us = max(row.p99_us for row in sweep)
        first_failure = next((row for row in sweep if row.deadline_misses), None)
        strict_sustainable_wpm = 60_000_000 // worst_max_us if worst_max_us else highest.wpm
        p99_sustainable_wpm = 60_000_000 // worst_p99_us if worst_p99_us else highest.wpm
        headroom_us = highest.deadline_us - worst_max_us
        utilization = worst_max_us * 100 / highest.deadline_us if highest.deadline_us else 0

        lines += [
            "",
            "## WPM Rendering Sweep",
            "",
            f"![WPM rendering sweep]({graph_path.name})",
            "",
            f"- Evaluated {len(sweep)} supported speeds from {sweep[0].wpm} to {highest.wpm} WPM in "
            f"{sweep[1].wpm - sweep[0].wpm if len(sweep) > 1 else 0} WPM steps using "
            f"{highest.samples} physically rendered multilingual RSVP frames.",
            "- WPM changes the frame deadline, not the rendering path, so the same measured frames are checked "
            "against every supported speed without rerendering identical work.",
            f"- At {highest.wpm} WPM the deadline is {highest.deadline_us} us; the worst observed frame was "
            f"{worst_max_us} us ({utilization:.1f}% utilization, {headroom_us:+d} us headroom).",
            f"- Observed sustainable limit: {'at least ' if strict_sustainable_wpm >= highest.wpm else ''}"
            f"{min(strict_sustainable_wpm, highest.wpm)} WPM by maximum latency and "
            f"{'at least ' if p99_sustainable_wpm >= highest.wpm else ''}"
            f"{min(p99_sustainable_wpm, highest.wpm)} WPM by p99 latency.",
        ]
        if first_failure is None:
            lines.append("- Result: every supported WPM passed with zero observed frame deadline misses.")
        else:
            lines.append(
                f"- Result: the first observed failure is {first_failure.wpm} WPM with "
                f"{first_failure.deadline_misses}/{first_failure.samples} late frames."
            )

        selected_wpms = {sweep[0].wpm, highest.wpm, 100, 300, 600}
        if first_failure is not None:
            selected_wpms.add(first_failure.wpm)
            preceding = next((row for row in reversed(sweep) if row.wpm < first_failure.wpm), None)
            if preceding is not None:
                selected_wpms.add(preceding.wpm)
        selected = [row for row in sweep if row.wpm in selected_wpms]
        lines += [
            "",
            "| WPM | deadline us | avg us | p95 us | p99 us | max us | misses |",
            "| ---: | ---: | ---: | ---: | ---: | ---: | ---: |",
        ]
        lines.extend(
            f"| {row.wpm} | {row.deadline_us} | {row.avg_us} | {row.p95_us} | {row.p99_us} | "
            f"{row.max_us} | {row.deadline_misses}/{row.samples} |"
            for row in selected
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
    wpm_rows: list[WpmSweepRow] = []
    for log_path in sorted(logs_dir.glob("*.log"), key=lambda item: item.stat().st_mtime):
        rows.extend(parse_log(log_path))
        wpm_rows.extend(parse_wpm_sweep(log_path))

    write_summary(rows, output, wpm_rows)
    print(f"[bench-script] wrote {output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
