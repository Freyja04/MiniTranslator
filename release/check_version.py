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


def _runtime_sources() -> tuple[tuple[str, str], ...]:
    """Return every native source file that can carry runtime version text."""
    source_root = ROOT / "native" / "src"
    paths = sorted(
        path for path in source_root.rglob("*")
        if path.suffix in {".cpp", ".hpp", ".h"}
    )
    return tuple(
        (
            path.relative_to(ROOT).as_posix(),
            _read(path.relative_to(ROOT).as_posix()),
        )
        for path in paths
    )


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

    versions = {"native resource": ".".join(resource_parts)}

    # resource.h is the single version source: the CMake project version is
    # derived from it at configure time, so the build script only needs the
    # derivation snippet, not a second literal version.
    cmake = _read("native/CMakeLists.txt")
    derivation = "#define MINITRANS_VERSION_"
    if derivation not in cmake:
        raise VersionError(
            "native CMake does not derive its version from resource.h"
        )
    return versions


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
        "native resource": f"{core}.0",
    }
    errors = [
        f"{label} is {actual}, expected {expected_by_source[label]}"
        for label, actual in versions.items()
        if actual != expected_by_source[label]
    ]

    # User-facing runtime strings must derive from the single CMake version.
    # Scan the whole native source tree so moving code between translation units
    # cannot silently turn these checks into no-ops.
    runtime_sources = _runtime_sources()
    runtime_derivations = (
        (
            "native HTTP user agent",
            'L"Mozilla/5.0 MiniTranslator/" + std::wstring(native_version)',
        ),
        (
            "native updater user agent",
            '{L"User-Agent", L"MiniTranslator/" + std::wstring(native_version)}',
        ),
    )
    for label, fragment in runtime_derivations:
        occurrences = [
            (relative, text.count(fragment))
            for relative, text in runtime_sources
        ]
        total = sum(count for _, count in occurrences)
        if total != 1:
            found = ", ".join(
                f"{relative}:{count}" for relative, count in occurrences if count
            )
            errors.append(
                f"{label} must use native_version exactly once; "
                f"found {total} occurrences"
                + (f" ({found})" if found else "")
            )

    # A hard-coded version in these strings would bypass the derivation check.
    literal_version_markers = (
        (
            "native HTTP user agent",
            r'MiniTranslator/([0-9]+\.[0-9]+\.[0-9]+(?:-[0-9A-Za-z.-]+)?)',
        ),
        (
            "native updater user agent",
            r'MiniTranslator/([0-9]+\.[0-9]+\.[0-9]+(?:-[0-9A-Za-z.-]+)?)',
        ),
        (
            "native update version header",
            r'当前版本 ([0-9]+\.[0-9]+\.[0-9]+(?:-[0-9A-Za-z.-]+)?)',
        ),
    )
    for label, pattern in literal_version_markers:
        for relative, text in runtime_sources:
            for actual in re.findall(pattern, text):
                errors.append(
                    f"{label} hard-codes version {actual} in {relative}; "
                    "use native_version"
                )

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
