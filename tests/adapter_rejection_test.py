"""Exercise the actual Windows DLL's hash gate without loading upstream/game code."""
import ctypes
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time


def child(path):
    dll = ctypes.WinDLL(str(path))
    query = dll.MFG_AP_QUERY_V1
    query.argtypes = [ctypes.c_uint32, ctypes.c_void_p, ctypes.c_uint32]
    query.restype = ctypes.c_uint32
    log = path.parent / "MapForGoblins.AP.log"
    deadline = time.monotonic() + 10
    while time.monotonic() < deadline:
        if log.exists() and "Unsupported upstream SHA256" in log.read_text():
            break
        time.sleep(0.05)
    else:
        raise AssertionError("Adapter did not report expected hash rejection")
    out = ctypes.create_string_buffer(16)
    assert query(1, out, 16) == 1, "Rejected adapter must return UNAVAILABLE"
    kernel = ctypes.WinDLL("kernel32")
    kernel.GetModuleHandleW.argtypes = [ctypes.c_wchar_p]
    kernel.GetModuleHandleW.restype = ctypes.c_void_p
    assert not kernel.GetModuleHandleW("MapForGoblins.upstream.dll")


if __name__ == "__main__":
    if sys.argv[1] == "--child":
        child(Path(sys.argv[2]))
    else:
        with tempfile.TemporaryDirectory(prefix="mfg-adapter-reject-") as temp:
            target = Path(temp) / "MapForGoblins.dll"
            shutil.copyfile(sys.argv[1], target)
            (target.parent / "MapForGoblins.upstream.dll").write_bytes(b"MZ unsupported release fixture")
            subprocess.run([sys.executable, __file__, "--child", str(target)], check=True, timeout=15)
        print("Unsupported upstream rejected before loading; API unavailable")
