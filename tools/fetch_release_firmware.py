#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import os
import shutil
import urllib.error
import urllib.request
from pathlib import Path

from export_web_firmware import FLASH_EXPORTS, OTA_EXPORTS, write_release_metadata


ROOT = Path(__file__).resolve().parents[1]
FIRMWARE_DIR = ROOT / "build" / "firmware"
DEFAULT_REPO = "ionutdecebal/rsvpnano"
ASSET_FALLBACKS = {
    "rsvp-nano-esp32-s3-touch-lcd-3.49.bin": ("rsvp-nano.bin",),
    "rsvp-nano-esp32-s3-touch-lcd-3.49-rev2.bin": ("rsvp-nano-rev2.bin",),
}


def github_headers() -> dict[str, str]:
    headers = {
        "Accept": "application/vnd.github+json",
        "User-Agent": "rsvp-nano-web-firmware-fetcher",
    }
    token = os.environ.get("GITHUB_TOKEN", "").strip()
    if token:
        headers["Authorization"] = f"Bearer {token}"
    return headers


def fetch_json(url: str) -> dict:
    request = urllib.request.Request(url, headers=github_headers())
    try:
        with urllib.request.urlopen(request) as response:
            return json.load(response)
    except urllib.error.HTTPError as exc:
        body = exc.read().decode("utf-8", errors="replace").strip()
        detail = f": {body}" if body else ""
        raise SystemExit(f"GitHub API request failed with HTTP {exc.code}{detail}") from exc
    except urllib.error.URLError as exc:
        raise SystemExit(f"GitHub API request failed: {exc.reason}") from exc


def download_file(url: str, destination: Path) -> None:
    request = urllib.request.Request(url, headers=github_headers())
    try:
        with urllib.request.urlopen(request) as response, destination.open("wb") as output:
            shutil.copyfileobj(response, output)
    except urllib.error.HTTPError as exc:
        raise SystemExit(f"Asset download failed with HTTP {exc.code}: {destination.name}") from exc
    except urllib.error.URLError as exc:
        raise SystemExit(f"Asset download failed for {destination.name}: {exc.reason}") from exc


def latest_release(repo: str) -> dict:
    return fetch_json(f"https://api.github.com/repos/{repo}/releases/latest")


def find_asset(release: dict, name: str, required: bool = True) -> dict | None:
    for asset in release.get("assets", []):
        if asset.get("name") == name:
            return asset
    if required:
        raise SystemExit(f"Latest release is missing required asset: {name}")
    return None


def find_asset_with_fallback(release: dict, name: str, required: bool = True) -> tuple[dict | None, str | None]:
    asset = find_asset(release, name, required=False)
    if asset is not None:
        return asset, name

    for fallback in ASSET_FALLBACKS.get(name, ()):
        asset = find_asset(release, fallback, required=False)
        if asset is not None:
            return asset, fallback

    if required:
        raise SystemExit(f"Latest release is missing required asset: {name}")
    return None, None


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Populate build/firmware from the latest published GitHub Release."
    )
    parser.add_argument(
        "--repo",
        default=os.environ.get("GITHUB_REPOSITORY", DEFAULT_REPO),
        help="GitHub repository in owner/name form.",
    )
    parser.add_argument(
        "--asset",
        action="append",
        dest="assets",
        help="Release asset to download. Repeat to request multiple assets.",
    )
    args = parser.parse_args()

    release = latest_release(args.repo)
    tag_name = str(release.get("tag_name", "")).strip()
    if not tag_name:
        raise SystemExit("Latest release is missing tag_name.")

    FIRMWARE_DIR.mkdir(parents=True, exist_ok=True)

    requested_assets = tuple(args.assets) if args.assets else None
    available_firmware: dict[str, str] = {}
    all_exports = (*FLASH_EXPORTS, *OTA_EXPORTS)
    exports = all_exports if requested_assets is not None else FLASH_EXPORTS
    for export in exports:
        asset_name = export["binary"]
        if requested_assets is not None and asset_name not in requested_assets:
            continue
        asset, release_asset_name = find_asset_with_fallback(
            release,
            asset_name,
            required=requested_assets is not None,
        )
        if asset is None:
            print(f"Skipping release asset not present in {tag_name}: {asset_name}")
            continue
        url = str(asset.get("browser_download_url", "")).strip()
        if not url:
            if requested_assets is not None:
                raise SystemExit(f"Release asset is missing browser_download_url: {asset_name}")
            print(f"Skipping release asset with no download URL: {asset_name}")
            continue
        destination = FIRMWARE_DIR / asset_name
        print(f"Downloading {release_asset_name} from {tag_name} -> {destination}")
        download_file(url, destination)
        if "id" in export:
            available_firmware[export["id"]] = asset_name

    if requested_assets is not None:
        unknown_assets = set(requested_assets) - {export["binary"] for export in all_exports}
        if unknown_assets:
            formatted = ", ".join(sorted(unknown_assets))
            raise SystemExit(f"Unknown firmware asset requested: {formatted}")
    if requested_assets is None and not available_firmware:
        raise SystemExit(f"Release {tag_name} has no browser-flasher images.")

    if available_firmware:
        write_release_metadata(tag_name, available_firmware)
    print(f"Web firmware updated to release {tag_name}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
