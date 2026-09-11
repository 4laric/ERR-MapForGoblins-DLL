"""Create a separate experimental paired-DLL package; never edit an installation."""
import argparse
import hashlib
import json
import subprocess
from pathlib import Path
import zipfile

PIN = "ed984d5bb3ee49e304ab02e5ac1bc1bfc3a6368c2bc8743f85edefe2a73f2ea3"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--upstream", type=Path, required=True)
    parser.add_argument("--adapter", type=Path, required=True)
    parser.add_argument("--minhook-license", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[1]
    with zipfile.ZipFile(args.upstream) as release:
        dll = release.read("MapForGoblins.dll")
        ini = release.read("MapForGoblins.ini")
        # Both menu modes use the same native marker renderer. Choose the overlay
        # menu so the adapter's AP section and upstream controls share one panel.
        old = b"menu_render_mode = native"
        if ini.count(old) != 1:
            raise SystemExit("Unexpected upstream menu preset")
        ini = ini.replace(old, b"menu_render_mode = imgui")
        for before, after in ((b"overlay_font_scale = 1.0", b"overlay_font_scale = 1.2"),
                              (b"overlay_window_w = 560", b"overlay_window_w = 780"),
                              (b"overlay_window_h = 680", b"overlay_window_h = 900")):
            if ini.count(before) != 1:
                raise SystemExit("Unexpected upstream overlay geometry preset")
            ini = ini.replace(before, after)
        license_text = release.read("LICENSE.txt")
    if hashlib.sha256(dll).hexdigest() != PIN:
        raise SystemExit("Unsupported upstream release: SHA-256 does not match 2.1.3 pin")
    adapter = args.adapter.read_bytes()
    if not adapter.startswith(b"MZ") or adapter == dll:
        raise SystemExit("--adapter must be the built adapter DLL")
    # Resolve every input before opening the output; exclusive creation protects
    # existing packages. No extractall, no archive-controlled output paths.
    payload = {
        "MapForGoblins.dll": adapter,
        "MapForGoblins.upstream.dll": dll,
        "MapForGoblins.ini": ini,
        "MapForGoblins.AP.ini": (repo / "adapter/MapForGoblins.AP.ini").read_bytes(),
        "README.md": (repo / "adapter/README.md").read_bytes(),
        "LICENSE.txt": license_text,
        "licenses/adapter.txt": (repo / "LICENSE.txt").read_bytes(),
        "licenses/minhook.txt": args.minhook_license.read_bytes(),
    }
    revision = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=repo, text=True).strip()
    manifest = {"status": "experimental", "source_revision": revision, "upstream_version": "2.1.3", "files": {
        name: hashlib.sha256(data).hexdigest() for name, data in payload.items()}}
    payload["manifest.json"] = (json.dumps(manifest, indent=2) + "\n").encode()
    with zipfile.ZipFile(args.output, "x", zipfile.ZIP_DEFLATED) as package:
        for name, data in payload.items():
            package.writestr(name, data)
    print(f"Created {args.output}; upstream pin {PIN}")


if __name__ == "__main__":
    main()
