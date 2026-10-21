"""Ensure a release tag matches every version marker used by shipped builds."""
from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from release.generate_manifest import VERSION_RE


class VersionError(ValueError):
    """Raised when source version markers are missing or inconsistent."""


def _read(relative: str) -> str:
    path = ROOT / relative
    try:
        return path.read_text(encoding="utf-8")
    except OSError as exc:
        raise VersionError(f"cannot read {relative}") from exc


def _one(relative: str, pattern: str, label: str) -> str:
    matches = re.findall(pattern, _read(relative), flags=re.MULTILINE)
    if len(matches) != 1:
        raise VersionError(
            f"expected one {label} version marker in {relative}, found {len(matches)}"
        )
    return matches[0]


def source_versions() -> dict[str, str]:
    resource = _read("native/src/resource.h")
    resource_parts: list[str] = []
    for name in ("MAJOR", "MINOR", "PATCH", "BUILD"):
        matches = re.findall(
            rf"^#define MINITRANS_VERSION_{name}\s+([0-9]+)\s*$",
            resource,
            flags=re.MULTILINE,
        )
        if len(matches) != 1:
            raise VersionError(f"resource.h has an invalid {name.lower()} marker")
        resource_parts.append(matches[0])

    return {
        "native CMake": _one(
            "native/CMakeLists.txt",
            r"^project\(MiniTranslatorNative VERSION ([0-9]+\.[0-9]+\.[0-9]+)",
            "native CMake",
        ),
        "native resource": ".".join(resource_parts),
    }


def validate(expected: str) -> dict[str, str]:
    if VERSION_RE.fullmatch(expected) is None:
        raise VersionError("--expected must be SemVer")

    core = expected.split("-", 1)[0].split("+", 1)[0]
    if expected != core:
        raise VersionError(
            "native releases currently require a final x.y.z version; "
            "prerelease and build suffixes are not compiled into PROJECT_VERSION"
        )

    cmake = _read("native/CMakeLists.txt")
    version_header = _read("native/src/version.hpp")
    if r'MINITRANS_VERSION_WIDE=L\"${PROJECT_VERSION}\"' not in cmake:
        raise VersionError("native runtime version is not derived from CMake PROJECT_VERSION")
    if "native_version = MINITRANS_VERSION_WIDE" not in version_header:
        raise VersionError("native version.hpp is not derived from MINITRANS_VERSION_WIDE")

    versions = source_versions()
    expected_by_source = {
        "native CMake": core,
        "native resource": f"{core}.0",
    }
    errors = [
        f"{label} is {actual}, expected {expected_by_source[label]}"
        for label, actual in versions.items()
        if actual != expected_by_source[label]
    ]

    # User-facing runtime strings must derive from the single CMake version.
    runtime_derivations = (
        (
            "native HTTP user agent",
            "native/src/http.cpp",
            'L"Mozilla/5.0 MiniTranslator/" + std::wstring(native_version)',
        ),
        (
            "native updater user agent",
            "native/src/updater.cpp",
            '{L"User-Agent", L"MiniTranslator/" + std::wstring(native_version)}',
        ),
        (
            "native update status text",
            "native/src/update_window.cpp",
            'L"当前安装的 " + std::wstring(native_version)',
        ),
    )
    for label, relative, fragment in runtime_derivations:
        count = _read(relative).count(fragment)
        if count != 1:
            errors.append(
                f"{label} must use native_version exactly once; found {count} occurrences"
            )

    # A hard-coded version in these strings would bypass the derivation check.
    literal_version_markers = (
        (
            "native HTTP user agent",
            "native/src/http.cpp",
            r'MiniTranslator/([0-9]+\.[0-9]+\.[0-9]+(?:-[0-9A-Za-z.-]+)?)',
        ),
        (
            "native updater user agent",
            "native/src/updater.cpp",
            r'MiniTranslator/([0-9]+\.[0-9]+\.[0-9]+(?:-[0-9A-Za-z.-]+)?)',
        ),
        (
            "native update status text",
            "native/src/update_window.cpp",
            r'当前安装的 ([0-9]+\.[0-9]+\.[0-9]+(?:-[0-9A-Za-z.-]+)?)',
        ),
    )
    for label, relative, pattern in literal_version_markers:
        for actual in re.findall(pattern, _read(relative)):
            errors.append(f"{label} hard-codes version {actual}; use native_version")

    if errors:
        raise VersionError("; ".join(errors))
    return versions


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--expected", required=True, help="SemVer from the release tag")
    args = parser.parse_args(argv)
    try:
        versions = validate(args.expected)
    except VersionError as exc:
        parser.error(str(exc))
    details = ", ".join(f"{name}={version}" for name, version in versions.items())
    print(f"[version] release {args.expected} is consistent ({details})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
