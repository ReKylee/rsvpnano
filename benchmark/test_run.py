import tempfile
import unittest
from pathlib import Path

from run import (
    DEVICE_EPUB_PATH,
    DEVICE_MULTILINGUAL_PATH,
    DEVICE_VERTICAL_EPUB_PATH,
    FONT_MANIFEST_PATH,
    RUN_READY_PATH,
    device_serial,
    find_benchmark_volume,
    monitor_command,
    prepare_volume,
    select_serial_port,
)


class BenchmarkRunTests(unittest.TestCase):
    def test_prepares_only_marked_volume_and_writes_ready_last(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            volume = root / "volume"
            other = root / "other"
            (volume / "benchmark").mkdir(parents=True)
            other.mkdir()
            (volume / "benchmark/.rsvpnano-benchmark").write_text("device\n", encoding="ascii")
            fixture = root / "fixture.epub"
            fixture.write_bytes(b"epub")
            multilingual = root / "multilingual.rsvp"
            multilingual.write_bytes(b"@rsvp 1\nmultilingual")
            vertical = root / "vertical.epub"
            vertical.write_bytes(b"vertical")
            fonts = root / "fonts"
            (fonts / "Reader Font").mkdir(parents=True)
            (fonts / "Reader Font/font.rfont4").write_bytes(b"font-v6")

            self.assertEqual(volume, find_benchmark_volume([other, volume]))
            prepare_volume(volume, fixture, multilingual, vertical, fonts)

            self.assertEqual(b"epub", (volume / DEVICE_EPUB_PATH).read_bytes())
            self.assertEqual(b"@rsvp 1\nmultilingual", (volume / DEVICE_MULTILINGUAL_PATH).read_bytes())
            self.assertEqual(b"vertical", (volume / DEVICE_VERTICAL_EPUB_PATH).read_bytes())
            self.assertEqual(b"font-v6", (volume / "fonts/Reader Font/font.rfont4").read_bytes())
            self.assertIn("Reader Font/font.rfont4", (volume / FONT_MANIFEST_PATH).read_text(encoding="ascii"))
            self.assertEqual("ready\n", (volume / RUN_READY_PATH).read_text(encoding="ascii"))

            installed_font = volume / "fonts/Reader Font/font.rfont4"
            installed_mtime = installed_font.stat().st_mtime_ns
            (volume / FONT_MANIFEST_PATH).unlink()
            prepare_volume(volume, fixture, multilingual, vertical, fonts)
            self.assertEqual(installed_mtime, installed_font.stat().st_mtime_ns)

    def test_does_not_rewrite_unchanged_fixtures(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            fixture = root / "fixture.epub"
            fixture.write_bytes(b"epub")
            multilingual = root / "multilingual.rsvp"
            multilingual.write_bytes(b"@rsvp 1\ntext")

            prepare_volume(root, fixture, multilingual)
            epub_mtime = (root / DEVICE_EPUB_PATH).stat().st_mtime_ns
            multilingual_mtime = (root / DEVICE_MULTILINGUAL_PATH).stat().st_mtime_ns
            prepare_volume(root, fixture, multilingual)

            self.assertEqual(epub_mtime, (root / DEVICE_EPUB_PATH).stat().st_mtime_ns)
            self.assertEqual(multilingual_mtime, (root / DEVICE_MULTILINGUAL_PATH).stat().st_mtime_ns)

    def test_tracks_device_across_com_port_change(self) -> None:
        devices = [{"port": "COM7", "hwid": "USB VID:PID=303A:1001 SER=44:1B:F6:85:56:E0"}]
        serial = device_serial({"port": "COM3", "hwid": "USB VID:PID=303A:1001 SER=441BF68556E0"})
        self.assertEqual("COM7", select_serial_port(devices, requested="COM3", serial=serial))

    def test_monitor_runs_in_the_platformio_python_process(self) -> None:
        command = monitor_command("python", "benchmark_env", "COM3", "115200")

        self.assertEqual(["python", "-m", "platformio", "device", "monitor"], command[:5])
        self.assertNotIn("uvx", command)


if __name__ == "__main__":
    unittest.main()
