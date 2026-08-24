"""Ask whether two Watcom releases' compilers can be told apart from their output.

The library evidence names a release of the *runtime*, and the build_flags
verdict turns that into a release of the *compiler*.  That step is an inference:
the two ship together but are separate files, and a build machine could in
principle hold a mismatched pair.  This measures how much the inference is
worth by compiling the same corpus with each release's DOS-hosted `wcc386`
under the settled flag set and comparing the generated code.

If the releases generate identical code the compiler leaves no trace to find,
and the runtime is the only lever there will ever be.  If they differ, the
difference is a second, compiler-side discriminator that the game's own code
can be held against once it is emitted.

Comparison is on the assembled `_TEXT` image with each module's own fixup
fields ignored, so a differing symbol table or comment record does not count as
differing code.

Usage:
    python tools/crt_version/compiler_diff.py [--versions 10.0 10.0a 10.0b]
"""
from __future__ import annotations

import argparse
import json
import shutil
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from omf_image import read_module            # noqa: E402

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
WORK = REPO / "workspace" / "crt_version" / "compiler_diff"
SERIES = Path(r"C:\Users\fdpsf\Documents\WATCOM_10_series")
DOSBOX = Path(r"C:\DOSBox-X\dosbox-x.exe")

# the settled flag set; -zq only silences the banner
CFLAGS = "-bt=dos4g -mf -zq -4s -fpi -s -ot -od"
CORPUS = Path(r"C:\Users\fdpsf\Documents\WATCOM_10_series\WATCOM_10.0a_infobase\SAMPLES\CLIBEXAM")
BINDIRS = ["BINB", "BIN", "BINW", "BINP"]
BATCH = 60                      # DOS command lines per .bat, keeps each run short


def write(path: Path, text: str) -> None:
    path.write_text(text, encoding="latin-1", newline="\r\n")


def compile_corpus(version: str, sources: list[Path], headers_from: str | None) -> Path:
    """Compile every source under one release; returns the output directory.

    `headers_from` pins every release to one release's headers.  Without it each
    compiles against its own, and a difference in the output could be either the
    compiler or a changed macro in a header — two answers this is meant to keep
    apart.
    """
    root = SERIES / ("WATCOM_" + version)
    work = WORK / (version + ("_h" + headers_from if headers_from else ""))
    if work.exists():
        shutil.rmtree(work)
    work.mkdir(parents=True)

    names = []
    for i, src in enumerate(sources):
        tag = "S%03d" % i
        names.append(tag)
        shutil.copyfile(src, work / (tag + ".C"))

    path = ";".join("D:\\" + d for d in BINDIRS)
    include = "E:\\H" if headers_from else "D:\\H"
    lines = ["@echo off", "set WATCOM=D:\\", "set PATH=Z:\\;" + path,
             "set INCLUDE=" + include, "c:"]
    for tag in names:
        lines.append("wcc386 %s -fo=%s.OBJ %s.C >>BUILD.OUT" % (CFLAGS, tag, tag))
    lines += ["echo done >DONE.TXT", "exit"]
    write(work / "BUILD.BAT", "\n".join(lines) + "\n")
    mounts = 'mount c "%s"\nmount d "%s"\n' % (work, root)
    if headers_from:
        mounts += 'mount e "%s"\n' % (SERIES / ("WATCOM_" + headers_from))
    write(work / "run.conf",
          "[cpu]\ncycles=max\n[autoexec]\n" + mounts + "c:\ncall BUILD.BAT\n")

    proc = subprocess.Popen([str(DOSBOX), "-silent", "-conf", "run.conf"], cwd=work)
    deadline = time.time() + 1800
    while time.time() < deadline:
        if (work / "DONE.TXT").exists() or proc.poll() is not None:
            break
        time.sleep(2)
    if proc.poll() is None:
        proc.terminate()
    produced = len(list(work.glob("*.OBJ")))
    print("  %-6s compiled %d/%d" % (version, produced, len(sources)))
    return work


def text_image(obj: Path):
    """(_TEXT bytes, fixup mask) of one module, or None when there is no code."""
    mod = read_module(obj)
    seg = mod.segment("_TEXT")
    if seg is None or not seg.image:
        return None
    return bytes(seg.image), bytes(seg.mask)


def compare(a_dir: Path, b_dir: Path) -> dict:
    same, differ, only_one = [], [], []
    for obj in sorted(a_dir.glob("*.OBJ")):
        other = b_dir / obj.name
        if not other.is_file():
            only_one.append(obj.stem)
            continue
        ia, ib = text_image(obj), text_image(other)
        if ia is None or ib is None:
            continue
        (img_a, mask_a), (img_b, mask_b) = ia, ib
        if len(img_a) != len(img_b):
            differ.append({"module": obj.stem, "reason": "length",
                           "a": len(img_a), "b": len(img_b)})
            continue
        bad = [i for i in range(len(img_a))
               if not mask_a[i] and not mask_b[i] and img_a[i] != img_b[i]]
        if bad:
            differ.append({"module": obj.stem, "reason": "bytes",
                           "count": len(bad), "first": bad[:8],
                           "len": len(img_a)})
        else:
            same.append(obj.stem)
    return {"same": same, "differ": differ, "only_one_side": only_one}


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--versions", nargs="*", default=["10.0", "10.0a", "10.0b"])
    ap.add_argument("--corpus", type=Path, default=CORPUS)
    ap.add_argument("--limit", type=int, default=0)
    ap.add_argument("--headers-from", default=None,
                    help="compile every release against this release's headers")
    args = ap.parse_args()

    if not DOSBOX.is_file():
        raise SystemExit("DOSBox-X not found: %s" % DOSBOX)
    sources = sorted(args.corpus.glob("*.c"))
    if args.limit:
        sources = sources[:args.limit]
    if not sources:
        raise SystemExit("no sources under %s" % args.corpus)
    print("corpus: %d files   flags: %s" % (len(sources), CFLAGS))

    WORK.mkdir(parents=True, exist_ok=True)
    dirs = {v: compile_corpus(v, sources, args.headers_from) for v in args.versions}
    names = {"S%03d" % i: src.name for i, src in enumerate(sources)}

    report = {"flags": CFLAGS, "corpus": str(args.corpus),
              "headers_from": args.headers_from or "(each release's own)",
              "source_names": names, "pairs": {}}
    for i in range(len(args.versions)):
        for j in range(i + 1, len(args.versions)):
            a, b = args.versions[i], args.versions[j]
            res = compare(dirs[a], dirs[b])
            report["pairs"]["%s vs %s" % (a, b)] = res
            print("  %-6s vs %-6s  identical=%d  differing=%d  one-sided=%d"
                  % (a, b, len(res["same"]), len(res["differ"]),
                     len(res["only_one_side"])))
            for d in res["differ"][:10]:
                print("      %s (%s) -> %s" % (names.get(d["module"], d["module"]),
                                               d["module"], d))

    out = WORK / ("report%s.json" % ("_h" + args.headers_from if args.headers_from else ""))
    out.write_text(json.dumps(report, indent=1), encoding="utf-8")
    print("wrote %s" % out)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
