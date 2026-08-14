"""Link one probe without `option stack` to record wlink's default stack size.

Needed to answer whether the original's 8K stack was an explicit directive or
just the linker default: if the default differs, omitting the directive in the
rebuild changes how deep the game can recurse before it dies.
"""

import os
import struct
import subprocess
import time

WATCOM = r"C:\Users\fdpsf\Documents\WATCOM_10_series\WATCOM_10.0a"
DOSBOX = r"C:\DOSBox-X\dosbox-x.exe"
HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
OUT = os.path.join(REPO, "workspace", "build_flags", "link")


def w(name, text):
    with open(os.path.join(OUT, name), "w", encoding="latin-1", newline="\r\n") as fh:
        fh.write(text)


def main():
    w("nostack.lnk",
      "system dos4g\nname nostack.exe\noption map=nostack.map\n"
      "file fpi87_8k.obj\nlibrary clib3s.lib\nlibrary math387s.lib\n"
      "library emu387.lib\n")
    w("build2.bat",
      "@echo off\nset WATCOM=D:\\\nset PATH=Z:\\;D:\\BIN;D:\\BINB\nc:\n"
      "wlink @nostack.lnk >>build2.out\necho done >DONE2.TXT\nexit\n")
    w("link2.conf",
      "[cpu]\ncycles=max\n[autoexec]\n"
      'mount c "%s"\nmount d "%s"\nc:\ncall build2.bat\n' % (OUT, WATCOM))

    for stale in ("DONE2.TXT", "build2.out", "nostack.exe"):
        p = os.path.join(OUT, stale)
        if os.path.exists(p):
            os.remove(p)
    proc = subprocess.Popen([DOSBOX, "-silent", "-conf", "link2.conf"], cwd=OUT)
    deadline = time.time() + 180
    while time.time() < deadline:
        if os.path.exists(os.path.join(OUT, "DONE2.TXT")) or proc.poll() is not None:
            break
        time.sleep(1)
    if proc.poll() is None:
        proc.terminate()

    exe = os.path.join(OUT, "nostack.exe")
    if not os.path.exists(exe):
        print("link failed; see build2.out")
        return
    with open(exe, "rb") as fh:
        data = fh.read()
    base = struct.unpack_from("<I", data, 0x3C)[0]
    esp_obj, esp = struct.unpack_from("<II", data, base + 0x20)
    print("no `option stack`: esp = object %d + 0x%x" % (esp_obj, esp))
    with open(os.path.join(OUT, "nostack.map"), errors="replace") as fh:
        for line in fh:
            if "Stack size" in line or line.startswith("STACK"):
                print("   " + line.rstrip())


if __name__ == "__main__":
    main()
