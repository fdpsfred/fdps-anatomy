"""play.py -- stage and launch the original or the rebuilt game under DOSBox-X.

Ticket 24's integration check is the developer playing both versions side by
side (ADR-0003).  This script makes that a one-line command and makes the two
setups identical except for the one file under test:

    workspace/game_build/play/ORIGINAL/   the shipped FDPS.EXE
    workspace/game_build/play/REBUILT/    workspace/game_build/out/FDE.EXE,
                                          copied in under the name FDPS.EXE

Both directories get the same game files, the shipped DOS4GW.EXE, the same
DOSBox-X conf (disc image on E: -- Disk.No says "CDROM at e:" -- SB16, memory,
CPU), and the same starting save.  The palette caches (FMER1.TMP ...) are NOT
copied: main builds them on first start when FMer1.tmp is missing, and each
version has to build its own or the rebuilt one would never run that code.

The directories persist between runs, so saves made while playing stay where
they were made; `save-copy` moves FDE.SAV from one version to the other, which
is both the "same save in both versions" setup and the save-compatibility
check.

Subcommands:
    stage VARIANT       (re)copy the game files into the variant's directory
    run VARIANT         stage, then open DOSBox-X for a human to play
    boot VARIANT        unattended: stage, launch, optionally type keys, take
                        window screenshots at given times, scan for faults,
                        then close.  This is the machine half of "does it
                        start and get into the game"; it is not a playtest.
    save-copy FROM TO   copy FDE.SAV from one variant's directory to the other
    selftest            conf generation checks that need no DOSBox-X

VARIANT is `original` or `rebuilt`.

Usage: python tools/game_build/play.py run rebuilt
       python tools/game_build/play.py boot rebuilt --shots 15,30 --keys "enter"
"""
import argparse
import ctypes
import json
import os
import shutil
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "fdps_build"))
sys.path.insert(0, str(ROOT / "tools" / "ail_link"))
import build_min as bm  # noqa: E402
import link_ail  # noqa: E402

GAME = ROOT / "fdps_game_files"
BUILT = ROOT / "workspace" / "game_build" / "out"
PLAY = ROOT / "workspace" / "game_build" / "play"

VARIANTS = {"original": "ORIGINAL", "rebuilt": "REBUILT"}

# What the game directory must not inherit from fdps_game_files/.  FDPS.EXE is
# the file under test and comes from the variant; FDPS.LE is the unbound image
# this project analyses, not part of an install; the .TMP files are caches main
# regenerates when absent (see the module docstring).
CACHES = {"FMER1.TMP", "FMER2.TMP", "MER1.TMP", "MER2.TMP"}
NOT_STAGED = {"FDPS.EXE", "FDPS.LE"} | CACHES
SAVE = "FDE.SAV"

# Written by the guest the moment FDPS.EXE returns to DOS, for whatever reason
# -- the farewell after "quit", an exit(1) at a start-up check, or a DOS/4GW
# fault.  None of those reach dosbox.log: the fault dump is printed on the
# guest's text screen.  `pause` then keeps DOSBox-X open over that screen, so
# without this marker a crashed game and a running one look the same from the
# host.
EXIT_MARKER = "GAMEEXIT.TXT"

# Same SB16 as tools/ail_link: DOSBox-X's defaults spelled out, and repeated
# into BLASTER because DIG.INI asks for IRQ/DMA autodetection from there.
SB = {"base": link_ail.SB_BASE, "irq": link_ail.SB_IRQ, "dma": link_ail.SB_DMA,
      "hdma": link_ail.SB_HDMA}

MEMSIZE_MB = 32
DEFAULT_CYCLES = "max"


# ------------------------------------------------------------------- staging

def variant_dir(variant):
    return PLAY / VARIANTS[variant]


def exe_source(variant):
    if variant == "original":
        return bm.find_ci(GAME, "FDPS.EXE")
    return bm.find_ci(BUILT, "FDE.EXE")


def clear_leftovers(d):
    """Delete what the previous run left in d: the palette caches and the
    exit marker.  Matched case-insensitively -- the guest writes 8.3 names in
    whatever case the program asked for (main opens "FMer1.tmp")."""
    for p in list(d.iterdir()):
        if p.is_file() and p.name.upper() in CACHES | {EXIT_MARKER}:
            p.unlink()


def stage(variant, fresh_save=False):
    """Copy the install into the variant's directory.

    Large containers are copied only when absent or a different size.  The
    save is copied only when the directory has none (or fresh_save), so a save
    made while playing is never overwritten by a restage.  The palette caches
    the previous run wrote are deleted every time: the directory persists, and
    a cache left over from an earlier FDE.EXE would stop the current one from
    ever running its own table builder.
    """
    exe = exe_source(variant)
    if exe is None:
        raise SystemExit("play: no executable for %s -- %s" % (
            variant, "fdps_game_files/FDPS.EXE is missing" if variant == "original"
            else "build it first: python tools/game_build/build_game.py"))
    d = variant_dir(variant)
    d.mkdir(parents=True, exist_ok=True)
    clear_leftovers(d)
    copied = []
    for src in sorted(GAME.iterdir()):
        name = src.name.upper()
        if not src.is_file() or name in NOT_STAGED:
            continue
        dst = d / name
        if name == SAVE:
            if dst.is_file() and not fresh_save:
                continue
        elif (dst.is_file() and dst.stat().st_size == src.stat().st_size
              and int(dst.stat().st_mtime) == int(src.stat().st_mtime)):
            # copy2 keeps the modification time, so size and time together
            # say "this is the copy made last time"; size alone would miss an
            # edited DIG.INI of the same length.
            continue
        shutil.copy2(src, dst)
        copied.append(name)
    shutil.copy2(exe, d / "FDPS.EXE")
    copied.append("FDPS.EXE <- %s" % exe.name)
    print("[stage] %s: %s" % (d, ", ".join(copied)))
    return d


# ---------------------------------------------------------------------- conf

def gen_conf(variant, disc, sound=True, cycles=DEFAULT_CYCLES, keys=None,
             key_wait=10.0, key_pace=0.5):
    """A DOSBox-X conf that runs the game from its own directory.

    `sound=False` is the "no sound card" run: the SB16 is removed, so
    AIL_install_DIG_INI fails and the game takes its no-driver path, which is
    one of the two runs ticket 24 asks for.  `keys` is a list of AUTOTYPE
    button names typed after `key_wait` seconds, `key_pace` apart.
    """
    d = variant_dir(variant)
    lines = ["[dosbox]", "memsize=%d" % MEMSIZE_MB,
             "[sdl]", "output=surface", "autolock=false",
             "[cpu]", "core=auto", "cputype=pentium_mmx", "cycles=%s" % cycles,
             "[log]", "logfile=%s" % (d / "dosbox.log")]
    if sound:
        lines += ["[sblaster]", "sbtype=sb16", "sbbase=%x" % SB["base"],
                  "irq=%d" % SB["irq"], "dma=%d" % SB["dma"],
                  "hdma=%d" % SB["hdma"]]
    else:
        lines += ["[sblaster]", "sbtype=none"]
    lines += ["[autoexec]",
              'mount C "%s"' % d,
              'imgmount %s -t cdrom "%s"' % (bm.DRV_DISC, disc)]
    if sound:
        lines.append("set BLASTER=A%X I%d D%d H%d T6"
                     % (SB["base"], SB["irq"], SB["dma"], SB["hdma"]))
    lines += ["set PATH=Z:\\;C:\\", "C:"]
    if keys:
        lines.append("AUTOTYPE -w %g -p %g %s" % (key_wait, key_pace,
                                                  " ".join(keys)))
    lines.append("FDPS.EXE")
    lines.append("echo exited > C:\\%s" % EXIT_MARKER)
    # A DOS/4GW fault dump or the game's own exit message is printed to the
    # text screen; without the pause DOSBox-X would close over it.
    lines.append("pause")
    lines.append("exit")
    return "\n".join(lines) + "\n"


def write_conf(variant, disc, **kw):
    d = variant_dir(variant)
    conf = d / "play.conf"
    conf.write_text(gen_conf(variant, disc, **kw), encoding="latin-1")
    log = d / "dosbox.log"
    if log.is_file():
        # DOSBox-X appends; a stale log would carry an old fault into this run.
        log.unlink()
    return conf


def launch(conf, log):
    # Never -silent: it silences the SB16 as well (rebuild_info/pitfalls.md),
    # and `boot` needs a window to take screenshots of.
    return bm.launch(bm.resolve_dosbox(), conf, log, silent=False)


# --------------------------------------------------------------- screenshots

def _window_of(pid):
    """The largest visible top-level window owned by `pid`, or None."""
    user32 = ctypes.windll.user32
    found = []
    proto = ctypes.WINFUNCTYPE(ctypes.c_bool, ctypes.c_void_p, ctypes.c_void_p)

    def cb(hwnd, _):
        owner = ctypes.c_ulong()
        user32.GetWindowThreadProcessId(hwnd, ctypes.byref(owner))
        if owner.value == pid and user32.IsWindowVisible(hwnd):
            rect = (ctypes.c_long * 4)()
            user32.GetClientRect(hwnd, rect)
            found.append((rect[2] * rect[3], hwnd, rect[2], rect[3]))
        return True

    user32.GetWindowThreadProcessId.argtypes = [ctypes.c_void_p, ctypes.c_void_p]
    user32.IsWindowVisible.argtypes = [ctypes.c_void_p]
    user32.GetClientRect.argtypes = [ctypes.c_void_p, ctypes.c_void_p]
    user32.EnumWindows(proto(cb), 0)
    if not found:
        return None
    found.sort(reverse=True)
    return found[0][1:]


def window_hung(pid):
    """True when Windows reports the DOSBox-X window as not responding."""
    hit = _window_of(pid)
    if hit is None:
        return False
    user32 = ctypes.windll.user32
    user32.IsHungAppWindow.argtypes = [ctypes.c_void_p]
    return bool(user32.IsHungAppWindow(ctypes.c_void_p(hit[0])))


def screenshot(pid, path, timeout=5.0):
    """Capture the DOSBox-X client area into a PNG.

    Returns "ok", "no window", "hung" or "failed".  PrintWindow asks the
    window to paint itself, so a window whose thread has stopped pumping
    messages would block the caller forever; it is therefore skipped when
    Windows already calls it hung, and run on a thread with a deadline
    otherwise.
    """
    import threading
    if _window_of(pid) is None:
        return "no window"
    if window_hung(pid):
        return "hung"
    box = {}
    t = threading.Thread(target=lambda: box.update(ok=_print_window(pid, path)),
                         daemon=True)
    t.start()
    t.join(timeout)
    if t.is_alive():
        return "hung"
    return "ok" if box.get("ok") else "failed"


def _print_window(pid, path):
    """PrintWindow rather than a screen grab, so a window that is covered by
    another one is still captured as DOSBox-X drew it."""
    from PIL import Image
    hit = _window_of(pid)
    if hit is None:
        return False
    hwnd, w, h = hit
    if w <= 0 or h <= 0:
        return False
    user32, gdi32 = ctypes.windll.user32, ctypes.windll.gdi32
    # Handles are pointer-sized; left to ctypes' int default they overflow on
    # a 64-bit host.
    user32.GetDC.argtypes = [ctypes.c_void_p]
    user32.GetDC.restype = ctypes.c_void_p
    gdi32.CreateCompatibleDC.argtypes = [ctypes.c_void_p]
    gdi32.CreateCompatibleDC.restype = ctypes.c_void_p
    gdi32.CreateCompatibleBitmap.argtypes = [ctypes.c_void_p, ctypes.c_int,
                                             ctypes.c_int]
    gdi32.CreateCompatibleBitmap.restype = ctypes.c_void_p
    gdi32.SelectObject.restype = ctypes.c_void_p
    gdi32.SelectObject.argtypes = [ctypes.c_void_p, ctypes.c_void_p]
    gdi32.GetDIBits.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_uint,
                                ctypes.c_uint, ctypes.c_void_p, ctypes.c_void_p,
                                ctypes.c_uint]
    user32.PrintWindow.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_uint]
    user32.ReleaseDC.argtypes = [ctypes.c_void_p, ctypes.c_void_p]
    gdi32.DeleteObject.argtypes = [ctypes.c_void_p]
    gdi32.DeleteDC.argtypes = [ctypes.c_void_p]

    wdc = user32.GetDC(ctypes.c_void_p(hwnd))
    mdc = gdi32.CreateCompatibleDC(wdc)
    bmp = gdi32.CreateCompatibleBitmap(wdc, w, h)
    gdi32.SelectObject(mdc, bmp)
    # PW_CLIENTONLY | PW_RENDERFULLCONTENT
    user32.PrintWindow(ctypes.c_void_p(hwnd), mdc, 3)

    class BIH(ctypes.Structure):
        _fields_ = [("biSize", ctypes.c_uint32), ("biWidth", ctypes.c_int32),
                    ("biHeight", ctypes.c_int32), ("biPlanes", ctypes.c_uint16),
                    ("biBitCount", ctypes.c_uint16),
                    ("biCompression", ctypes.c_uint32),
                    ("biSizeImage", ctypes.c_uint32),
                    ("biXPelsPerMeter", ctypes.c_int32),
                    ("biYPelsPerMeter", ctypes.c_int32),
                    ("biClrUsed", ctypes.c_uint32),
                    ("biClrImportant", ctypes.c_uint32)]

    bih = BIH(ctypes.sizeof(BIH), w, -h, 1, 32, 0, 0, 0, 0, 0, 0)
    buf = ctypes.create_string_buffer(w * h * 4)
    lines = gdi32.GetDIBits(mdc, bmp, 0, h, buf, ctypes.byref(bih), 0)
    gdi32.DeleteObject(bmp)
    gdi32.DeleteDC(mdc)
    user32.ReleaseDC(ctypes.c_void_p(hwnd), wdc)
    if lines != h:
        return False
    Image.frombuffer("RGBA", (w, h), buf.raw, "raw", "BGRA", 0, 1) \
        .convert("RGB").save(str(path))
    return True


# ---------------------------------------------------------------------- boot

def boot(variant, disc, shots, keys, key_wait, key_pace, sound, cycles,
         fresh_save=False):
    """Launch unattended, screenshot at the given seconds, close.

    The verdict is deliberately narrow: at the last screenshot the game had not
    returned to DOS (no exit marker -- a DOS/4GW fault dump lands on the guest
    screen, not in any log, and `pause` keeps DOSBox-X alive over it), the
    DOSBox-X process was alive, and its log has no emulator-level fault.
    Whether what the screenshots show is right is for whoever looks at them;
    when the game did exit, the last screenshot is the screen it exited to.
    """
    d = stage(variant, fresh_save=fresh_save)
    shots_dir = d / "shots"
    shots_dir.mkdir(exist_ok=True)
    for p in shots_dir.glob("*.png"):
        p.unlink()
    conf = write_conf(variant, disc, sound=sound, cycles=cycles, keys=keys,
                      key_wait=key_wait, key_pace=key_pace)
    proc, fp = launch(conf, d / "stdio.log")
    start = time.time()
    taken, problems, alive = [], [], True
    try:
        for t in sorted(shots):
            while time.time() - start < t:
                if proc.poll() is not None:
                    alive = False
                    break
                time.sleep(0.25)
            if not alive:
                break
            path = shots_dir / ("t%03d.png" % t)
            status = screenshot(proc.pid, path)
            if status == "ok":
                taken.append(str(path))
            else:
                problems.append("t=%d: %s" % (t, status))
        alive = alive and proc.poll() is None
    finally:
        if proc.poll() is None:
            proc.kill()
        fp.close()
    fault = bm.scan_fault(d / "dosbox.log", d / "stdio.log")
    game_exited = bm.find_ci(d, EXIT_MARKER) is not None
    result = {"variant": variant, "alive_at_end": alive,
              "game_exited": game_exited, "fault": fault,
              "screenshots": taken, "screenshot_problems": problems,
              "keys": keys, "sound": sound}
    (d / "boot.json").write_text(json.dumps(result, indent=2) + "\n",
                                 encoding="utf-8")
    print(json.dumps(result, indent=2))
    return alive and not game_exited and not fault and not problems


# ------------------------------------------------------------------ selftest

def _selftest_rows():
    rows = []
    disc = Path(r"X:\disc.cue")
    conf = gen_conf("rebuilt", disc)
    rows.append(("disc on E:", 'imgmount E -t cdrom "X:\\disc.cue"' in conf, "yes"))
    rows.append(("SB16 with matching BLASTER",
                 "sbtype=sb16" in conf and ("irq=%d" % SB["irq"]) in conf
                 and (" I%d " % SB["irq"]) in conf, "yes"))
    rows.append(("runs FDPS.EXE from the variant dir",
                 conf.index("FDPS.EXE") > conf.index("mount C")
                 and str(variant_dir("rebuilt")) in conf, "yes"))
    rows.append(("exit marked, then pause keeps the exit screen",
                 ("FDPS.EXE\necho exited > C:\\%s\npause\nexit" % EXIT_MARKER)
                 in conf, "yes"))
    silent = gen_conf("original", disc, sound=False)
    rows.append(("no-sound run removes the card",
                 "sbtype=none" in silent and "BLASTER" not in silent, "yes"))
    typed = gen_conf("original", disc, keys=["enter", "down"], key_wait=7,
                     key_pace=0.4)
    rows.append(("keys typed before the game starts",
                 "AUTOTYPE -w 7 -p 0.4 enter down" in typed
                 and typed.index("AUTOTYPE") < typed.index("FDPS.EXE"), "yes"))
    rows.append(("same conf for both variants but the directory",
                 gen_conf("original", disc).replace("ORIGINAL", "V")
                 == gen_conf("rebuilt", disc).replace("REBUILT", "V"), "yes"))
    rows.append(("caches and the LE are not staged",
                 {"FDPS.LE", "FMER1.TMP", "MER2.TMP"} <= NOT_STAGED, "yes"))

    # A restage must clear what the last run left: its caches and exit marker,
    # in the case the guest wrote them, and nothing else.
    import tempfile
    tmp = Path(tempfile.mkdtemp(prefix="fdps_play_selftest_"))
    try:
        for name in ("FMer1.tmp", "MER2.TMP", "gameexit.txt", "FDE.SAV"):
            (tmp / name).write_text("x", encoding="latin-1")
        clear_leftovers(tmp)
        left = sorted(p.name for p in tmp.iterdir())
        rows.append(("restage clears caches and the exit marker only",
                     left == ["FDE.SAV"], ", ".join(left)))
    finally:
        shutil.rmtree(str(tmp), ignore_errors=True)
    return rows


def do_selftest():
    ok = True
    for name, passed, detail in _selftest_rows():
        print("[selftest] %-46s %s (%s)" % (name, "ok" if passed else "FAIL",
                                            detail))
        ok = ok and bool(passed)
    return ok


# ---------------------------------------------------------------------- main

def main():
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)
    for name in ("stage", "run", "boot"):
        p = sub.add_parser(name)
        p.add_argument("variant", choices=sorted(VARIANTS))
        p.add_argument("--fresh-save", action="store_true",
                       help="replace the directory's FDE.SAV with the shipped one")
        if name in ("run", "boot"):
            p.add_argument("--no-sound", action="store_true")
            p.add_argument("--cycles", default=DEFAULT_CYCLES)
        if name == "boot":
            p.add_argument("--shots", default="20",
                           help="comma-separated seconds after launch")
            p.add_argument("--keys", default="",
                           help="AUTOTYPE button names, space separated")
            p.add_argument("--key-wait", type=float, default=10.0)
            p.add_argument("--key-pace", type=float, default=0.5)
    p = sub.add_parser("save-copy")
    p.add_argument("src", choices=sorted(VARIANTS))
    p.add_argument("dst", choices=sorted(VARIANTS))
    sub.add_parser("selftest")
    args = ap.parse_args()

    if args.cmd == "selftest":
        ok = do_selftest()
        print("[result] %s" % ("PASS" if ok else "FAIL"))
        return 0 if ok else 1
    if args.cmd == "save-copy":
        if args.src == args.dst:
            raise SystemExit("play: save-copy needs two different variants")
        src = variant_dir(args.src) / SAVE
        if not src.is_file():
            raise SystemExit("play: %s has no %s" % (variant_dir(args.src), SAVE))
        variant_dir(args.dst).mkdir(parents=True, exist_ok=True)
        shutil.copy2(src, variant_dir(args.dst) / SAVE)
        print("[save-copy] %s -> %s" % (src, variant_dir(args.dst) / SAVE))
        return 0

    disc = Path(os.environ.get("FDPS_DISC1") or bm.DISC_DEFAULT)
    if not disc.is_file() or not bm.cue_bin(disc).is_file():
        raise SystemExit("play: disc image %s (or its .bin) not found -- the game "
                         "exits at its CD check without it" % disc)
    if args.cmd == "stage":
        stage(args.variant, fresh_save=args.fresh_save)
        return 0
    if args.cmd == "run":
        stage(args.variant, fresh_save=args.fresh_save)
        conf = write_conf(args.variant, disc, sound=not args.no_sound,
                          cycles=args.cycles)
        d = variant_dir(args.variant)
        proc, fp = launch(conf, d / "stdio.log")
        print("[run] DOSBox-X pid %d on %s" % (proc.pid, conf))
        return 0
    ok = boot(args.variant, disc,
              [int(s) for s in args.shots.split(",") if s.strip()],
              args.keys.split(), args.key_wait, args.key_pace,
              not args.no_sound, args.cycles, fresh_save=args.fresh_save)
    print("[result] %s" % ("PASS" if ok else "FAIL"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
