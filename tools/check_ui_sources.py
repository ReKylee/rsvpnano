"""Check actual PlatformIO source selection without downloading any target toolchain."""

from pathlib import Path

from platformio.fs import match_src_files
from platformio.project.config import ProjectConfig

PROJECT = Path(__file__).resolve().parents[1]
WATCH_WIDGETS = {"ui/Cards.cpp", "ui/ProgressRing.cpp", "ui/Pagination.cpp"}


def check() -> None:
    config = ProjectConfig(str(PROJECT / "platformio.ini"))
    src = PROJECT / "src"
    checked = 0
    for name in config.envs():
        section = f"env:{name}"
        if name.startswith("native_") and not config.get(section, "test_build_src"):
            continue
        files = set(match_src_files(str(src), config.get(section, "build_src_filter"), ["cpp"]))
        if not any(path.startswith("app/screens/") for path in files):
            continue
        flags = config.get(section, "build_flags")
        layout = "watch" if "-DRSVP_UI_WATCH=1" in flags else "regular"
        other = "regular" if layout == "watch" else "watch"
        unexpected = [path for path in files if f"/{other}/" in path and path.startswith("app/screens/")]
        assert not unexpected, f"{name}: {layout} state compiled with {other} sources: {unexpected}"
        if not name.startswith("native_"):
            for path in (src / "app/screens").rglob(f"{layout}/*.cpp"):
                relative = path.relative_to(src).as_posix()
                assert relative in files, f"{name}: missing {relative}"
            actual = files & WATCH_WIDGETS
            expected = WATCH_WIDGETS if layout == "watch" else set()
            assert actual == expected, f"{name}: wrong optional widgets: {actual ^ expected}"
            coordinator = {path for path in files if path.startswith("app/") and path.count("/") == 1}
            isolated = name.startswith(("benchmark_", "companion_api_test_"))
            expected_coordinator = set() if isolated else {
                path.relative_to(src).as_posix() for path in (src / "app").glob("*.cpp")
            }
            assert coordinator == expected_coordinator, f"{name}: incorrect application entrypoints"
        checked += 1
        print(f"{name}: {layout}, {len(files)} source files")
    assert checked > 0, "No UI environments were checked"
    # Library code may not depend on application screens or storage workflows.
    for path in (src / "ui").glob("*"):
        if path.suffix in {".h", ".cpp"}:
            assert '"app/' not in path.read_text(), f"{path}: UI depends on application"
    print(f"Checked {checked} UI environments")


if __name__ == "__main__":
    check()
