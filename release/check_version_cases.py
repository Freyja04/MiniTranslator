"""Cross-check the shared SemVer fixtures in release/version_cases.json.

The release tooling parses versions with generate_manifest.VERSION_RE while the
in-app updater parses them with native/src/updater.cpp. Both must accept and
reject the same strings, otherwise a release can be published that the updater
refuses. This script validates the fixture against the Python tooling and
proves the C++ self-test carries the same literals.
"""
from __future__ import annotations

import json
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from generate_manifest import VERSION_RE  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
FIXTURES = ROOT / "release" / "version_cases.json"
UPDATER = ROOT / "native" / "src" / "updater.cpp"

CORE_RE = re.compile(r"^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)")


def check_release_tooling(fixtures: dict) -> list[str]:
    failures: list[str] = []
    valid = fixtures.get("valid")
    invalid = fixtures.get("invalid")
    if not isinstance(valid, list) or not valid:
        return ["version_cases.json must list at least one valid version"]
    if not isinstance(invalid, list) or not invalid:
        return ["version_cases.json must list at least one invalid version"]

    seen: set[str] = set()
    for case in valid:
        version = str(case.get("version", ""))
        if version in seen:
            failures.append(f"version_cases.json lists {version!r} twice")
        seen.add(version)
        if VERSION_RE.fullmatch(version) is None:
            failures.append(f"release tooling rejected the valid version {version!r}")
            continue
        core = CORE_RE.match(version)
        if core is None:
            failures.append(f"valid version {version!r} has no numeric core")
            continue
        expected = (case.get("major"), case.get("minor"), case.get("patch"))
        actual = tuple(int(part) for part in core.groups())
        if actual != expected:
            failures.append(
                f"release tooling parsed {version!r} as {actual}, fixture says {expected}"
            )
        rest = version[core.end():]
        prerelease = rest[1:].split("+", 1)[0] if rest.startswith("-") else ""
        if prerelease != case.get("prerelease"):
            failures.append(
                f"release tooling parsed prerelease {prerelease!r} for {version!r}, "
                f"fixture says {case.get('prerelease')!r}"
            )

    for version in invalid:
        if version in seen:
            failures.append(f"version_cases.json lists {version!r} as both valid and invalid")
        seen.add(version)
        if VERSION_RE.fullmatch(version) is not None:
            failures.append(f"release tooling accepted the invalid version {version!r}")
    return failures


def check_updater_source(fixtures: dict) -> list[str]:
    failures: list[str] = []
    source = UPDATER.read_text(encoding="utf-8")
    versions = [str(case["version"]) for case in fixtures["valid"]] + list(fixtures["invalid"])
    for version in versions:
        if version == "":
            continue  # an empty literal appears everywhere; the loop covers it
        if f'L"{version}"' not in source:
            failures.append(
                f"native/src/updater.cpp self-test is missing the fixture {version!r}"
            )
    return failures


def main() -> int:
    fixtures = json.loads(FIXTURES.read_text(encoding="utf-8"))
    failures = check_release_tooling(fixtures) + check_updater_source(fixtures)
    if failures:
        for failure in failures:
            print(f"version fixtures: {failure}", file=sys.stderr)
        return 1
    print(
        "version fixtures: "
        f"{len(fixtures['valid'])} valid and {len(fixtures['invalid'])} invalid cases agree"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
