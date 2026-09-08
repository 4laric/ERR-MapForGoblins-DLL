#!/usr/bin/env python3
"""Write MFG-PROVENANCE.json next to a paired vanilla artifact.

The er-archipelago world repo validates a paired Map For Goblins artifact with its own
`tools/package_mfg.py`: the directory must hold MapForGoblins.dll, MapForGoblins.ini and
LICENSE.txt, plus a MFG-PROVENANCE.json whose five identity fields equal the world's
`release/MFG-VERSION.json` lock and whose three digests equal the files on disk.

This script writes exactly that manifest from the facts the build already knows, so the
artifact this fork's CI publishes is self-describing and can be checked without rebuilding.
Keep the field list in step with `package_mfg.IDENTITY` / `package_mfg.FILES` upstream.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import re
from pathlib import Path

SOURCE_REPOSITORY = "https://github.com/4laric/ERR-MapForGoblins-DLL"
FILES = {
    "dll_sha256": "MapForGoblins.dll",
    "ini_sha256": "MapForGoblins.ini",
    "license_sha256": "LICENSE.txt",
}


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def build(directory: Path, source_commit: str, input_sha256: str, profile: str) -> dict:
    for name, value, size in (("source_commit", source_commit, 40), ("input_sha256", input_sha256, 64)):
        if not re.fullmatch("[0-9a-f]{%d}" % size, value or ""):
            raise SystemExit("make_mfg_provenance: %s must be %d lowercase hex chars" % (name, size))
    manifest = {
        "schema_version": 1,
        "source_repository": SOURCE_REPOSITORY,
        "source_commit": source_commit,
        "profile": profile,
        "input_sha256": input_sha256,
    }
    for key, name in FILES.items():
        path = directory / name
        if not path.is_file():
            raise SystemExit("make_mfg_provenance: missing artifact file " + name)
        manifest[key] = digest(path)
    return manifest


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path, help="the paired artifact directory")
    parser.add_argument("--source-commit", required=True, help="full 40-hex commit this DLL was built from")
    parser.add_argument("--input-sha256", required=True, help="sha256 of the pinned generated-data archive")
    parser.add_argument("--profile", default="vanilla")
    args = parser.parse_args()
    manifest = build(args.directory, args.source_commit.lower(), args.input_sha256.lower(), args.profile)
    (args.directory / "MFG-PROVENANCE.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(manifest, indent=2))


if __name__ == "__main__":
    main()
