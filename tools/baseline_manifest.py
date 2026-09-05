#!/usr/bin/env python3
"""Content-addressed inventory for an offline MFG vanilla-profile rebuild.

Never executes game code. A complete inventory proves presence, not pristine
vanilla provenance, successful extraction, or runtime compatibility.
"""
import argparse
import hashlib
import json
import subprocess
from pathlib import Path


def inventory(root, patterns, *, exact=False):
    result = {}
    for pattern in patterns:
        if not pattern:
            continue
        for path in ([root / pattern] if exact else sorted(root.glob(pattern))):
            if path.is_file():
                digest = hashlib.sha256()
                with path.open("rb") as stream:
                    for block in iter(lambda: stream.read(1024 * 1024), b""):
                        digest.update(block)
                result[path.relative_to(root).as_posix()] = {
                    "bytes": path.stat().st_size, "sha256": digest.hexdigest()
                }
    return dict(sorted(result.items()))


def manifest(game, repo):
    groups = {
        "executable": ["eldenring.exe"],
        "regulation": ["regulation.bin"],
        "oodle": ["oo2core_6_win64.dll"],
        "msbs": ["map/MapStudio/*.msb.dcx"],
        "events": ["event/*.emevd.dcx"],
        "item_messages": ["msg/engus/item_dlc02.msgbnd.dcx"],
        "menu_messages": ["msg/engus/menu_dlc02.msgbnd.dcx"],
    }
    inputs = {name: inventory(game, patterns) for name, patterns in groups.items()}
    return {
        "schema": 1, "profile": "vanilla",
        "provenance": "user-supplied; pristine vanilla status unverified",
        "missing_groups": [name for name, files in inputs.items() if not files],
        "inputs": inputs,
        "build_sources": inventory(repo, subprocess.check_output(
            ["git", "-C", str(repo), "ls-files", "-z"], text=True
        ).rstrip("\0").split("\0"), exact=True),
        "generated": inventory(repo, ["src/generated_shared/*", "src/generated_vanilla/*"]),
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game-dir", type=Path, required=True)
    parser.add_argument("--repo", type=Path, default=Path(__file__).resolve().parent.parent)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--compare-inputs", type=Path, help="fail if input content differs from this manifest")
    args = parser.parse_args()
    report = manifest(args.game_dir, args.repo)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print("Missing input groups:", ", ".join(report["missing_groups"]) or "none")
    print("Generated files:", len(report["generated"]))
    changed = False
    if args.compare_inputs:
        previous = json.loads(args.compare_inputs.read_text(encoding="utf-8"))
        changed = previous["inputs"] != report["inputs"]
        print("Input content changed:", changed)
    return bool(report["missing_groups"]) or changed


if __name__ == "__main__":
    raise SystemExit(main())
