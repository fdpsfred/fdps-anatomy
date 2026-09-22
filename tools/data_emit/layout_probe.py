"""layout_probe.py -- where Watcom 10.0a and wlink put a translation unit's data.

Ticket 23 has to reproduce a handful of layouts the original depends on (a
table indexed one past its end reading the next table, a cursor that overruns
three arrays in turn).  How to write them in C depends on facts about the
toolchain, not on anything in FDPS.LE, so they are measured here once:

  * does an explicitly initialised global -- including `= 0` and `= {0}` --
    go to _DATA in source order, with or without alignment padding;
  * does a tentative (uninitialised) global go somewhere else, and in what
    order;
  * does a file-scope `static` sit in the same run as its public neighbours;
  * do two translation units' _DATA come out in link order.

The probe compiles two small units with the project's flags, links them with
`option map`, and prints every probe symbol's address and segment from the
map.  It is a measurement, not a gate; its conclusions live in
rebuild_info/data_emit.md.

Usage: python tools/data_emit/layout_probe.py
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "fdps_build"))
import build_min as bm  # noqa: E402

WORK = ROOT / "workspace" / "data_emit" / "probe"
SRC = WORK / "src"
OUT = WORK / "out"

UNIT_A = r"""
int pr_a_int = 5;
int pr_a_tab6[6] = { 1, 2, 3, 4, 5, 6 };
int pr_a_zero6[6] = { 0 };
unsigned char pr_a_byte = 0;
static int pr_a_guard = 0;
void *pr_a_handles[8] = { 0 };
short pr_a_short = 0x19;
volatile int pr_a_volatile = 0;
unsigned char pr_a_q1[200] = { 0 };
unsigned char pr_a_q2[200] = { 0 };
unsigned char pr_a_q3[200] = { 0 };
int pr_a_qcount = 0;
int pr_a_tent1;
int pr_a_tent2;
unsigned char pr_a_tent3;
int pr_a_tent4;
static int pr_a_static_tent;

int pr_a_use(void)
{
    return pr_a_int + pr_a_tent1 + pr_a_tent2 + pr_a_tent3 + pr_a_tent4
         + pr_a_static_tent + *(int *)&pr_a_guard;
}
"""

UNIT_B = r"""
extern int pr_a_use(void);
int pr_b_first = 7;
unsigned char pr_b_byte = 1;
int pr_b_after_byte = 9;
int pr_b_tent;
#pragma pack(1)
unsigned char pr_b_pk_byte = 1;
short pr_b_pk_short = 2;
unsigned int pr_b_pk_uint = 3;
unsigned char pr_b_pk_byte2 = 4;
#pragma pack()
unsigned char pr_b_np_byte = 1;
short pr_b_np_short = 2;
short pr_b_np_short2 = 2;
unsigned char pr_b_np_arr7[7] = { 0 };
unsigned char pr_b_np_arr6[6] = { 0 };
unsigned int pr_b_np_uint = 5;

int main(void)
{
    return pr_a_use() + pr_b_first + pr_b_tent;
}
"""


def build(dosbox, watcom):
    for d in (SRC, OUT):
        d.mkdir(parents=True, exist_ok=True)
        for p in d.iterdir():
            if p.is_file():
                p.unlink()
    (SRC / "PRA.C").write_text(UNIT_A, encoding="latin-1")
    (SRC / "PRB.C").write_text(UNIT_B, encoding="latin-1")
    w = bm.DRV_WORK
    lnk = ["system dos4g", r"name %s:\OUT\PROBE.EXE" % w, "option stack=8k",
           r"option map=%s:\OUT\PROBE.MAP" % w,
           r"file %s:\OUT\PRA.OBJ" % w, r"file %s:\OUT\PRB.OBJ" % w]
    lnk += ["library %s" % lib for lib in bm.CRT_LIBS]
    (WORK / "PROBE.LNK").write_text("\n".join(lnk) + "\n", encoding="latin-1")
    bat = [r"echo c > %s:\OUT\HB.TXT" % w,
           r"WCC386 PRA.C -fo=%s:\OUT\PRA.OBJ > %s:\OUT\BUILD.OUT" % (w, w),
           r"WCC386 PRB.C -fo=%s:\OUT\PRB.OBJ >> %s:\OUT\BUILD.OUT" % (w, w),
           r"echo l > %s:\OUT\HB.TXT" % w,
           r"WLINK @%s:\PROBE.LNK >> %s:\OUT\BUILD.OUT" % (w, w),
           r"echo done > %s:\OUT\BUILD.DON" % w, ""]
    (WORK / "BUILD.BAT").write_text("\r\n".join(bat), encoding="latin-1")
    bm.write_conf(
        WORK / "probe.conf",
        [(bm.DRV_SRC, SRC), (bm.DRV_WATCOM, watcom), (bm.DRV_WORK, WORK)],
        None,
        ["set WATCOM=%s:\\" % bm.DRV_WATCOM,
         "set PATH=Z:\\;" + ";".join("%s:\\%s" % (bm.DRV_WATCOM, d)
                                     for d in bm.BINDIRS),
         "set INCLUDE=%s:\\H" % bm.DRV_WATCOM,
         "set WCC386=" + bm.CFLAGS,
         "%s:" % bm.DRV_SRC,
         r"%s:\BUILD.BAT" % bm.DRV_WORK],
        logfile=WORK / "dosbox.log")
    proc, fp = bm.launch(dosbox, WORK / "probe.conf", WORK / "stdio.log")
    mode, _, secs = bm.wait(proc, OUT, "build.don", 300)
    fp.close()
    out = bm.find_ci(OUT, "build.out")
    print("[probe] build %s in %ds" % (mode, secs))
    if out:
        print("\n".join("  | " + l for l in
                        out.read_text(encoding="latin-1").splitlines()[-8:]))
    return bm.find_ci(OUT, "probe.map")


def main():
    watcom = bm.WATCOM_DEFAULT
    dosbox = bm.preflight(watcom, bm.DISC_DEFAULT, need_disc=False)
    mp = build(dosbox, watcom)
    if mp is None:
        print("[probe] no map produced")
        return 1
    rows = []
    for line in mp.read_text(encoding="latin-1").splitlines():
        m = re.match(r"^([0-9a-fA-F]{4}):([0-9a-fA-F]{8})[+*]?\s+(\S+)", line)
        if m and m.group(3).startswith(("pr_", "_pr_")):
            rows.append((int(m.group(2), 16), m.group(1), m.group(3)))
    for addr, seg, name in sorted(rows):
        print("  %s:%08x  %s" % (seg, addr, name))
    text = mp.read_text(encoding="latin-1")
    i = text.find("Segment")
    print(text[i:i + 1500] if i >= 0 else "(no segment table)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
