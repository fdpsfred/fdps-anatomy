/* smoke.c -- toolchain smoke test for the FDPS rebuild.
 *
 * Built with the flag set rebuild_info/build_flags.md pins on the original
 * FDPS.LE (-bt=dos4g -mf -4s -fpi -s -ot -od, linked system dos4g against
 * clib3s/math387s/emu387 with stack=8k). Running it proves the whole chain
 * works end to end: wcc386 accepts the flags, wlink produces a DOS/4G LE
 * image, the DOS/4GW extender loads it, the Watcom CRT starts, and the
 * mounted CD image is readable from inside the guest.
 *
 * Everything it learns goes to RESULT.TXT in the current directory as
 * key=value lines; the host build script parses that file. RUN.DON is written
 * last and says only "this program ran to the end" -- whether the probes
 * passed is the `verdict` field, which the host checks separately. Keeping the
 * two apart is what lets a failure report which probe failed instead of a
 * vague "never finished". HB.TXT is rewritten between probes as the heartbeat
 * the host watches for a stall. Both markers use 8.3 names because the guest
 * has no LFN support.
 *
 * The CD probe reads the 24-byte signature the VFS header carries at 0x0B
 * (resource_info/vfs_container.md) out of PACK.VFS on the mounted disc.
 * DOSBox-X reports success from IMGMOUNT whether or not the image mounted, so
 * only reading real bytes off the disc proves the mount took.
 *
 * The MSCDEX probe repeats what the original main() does at 0x3c636: a DPMI
 * simulate-real-mode-interrupt (INT 31h AX=0300h) carrying INT 2Fh AX=1500h,
 * the installation check, whose BX answer is the number of CD drives. The
 * original exits(1) when that path fails, so a rebuild that cannot make this
 * call cannot boot at all.
 */
#include <stdio.h>
#include <stddef.h>
#include <string.h>
#include <i86.h>

#define RESULT_FILE "RESULT.TXT"
#define DONE_FILE   "RUN.DON"
#define HB_FILE     "HB.TXT"
#define VFS_SIG     "Dynasty Information Co.,"
#define VFS_SIG_OFF 0x0b
#define VFS_SIG_LEN 24

/* DPMI 0.9 real-mode call structure, 50 bytes, field order fixed by the spec. */
struct rm_call {
    unsigned long edi, esi, ebp, reserved, ebx, edx, ecx, eax;
    unsigned short flags, es, ds, fs, gs, ip, cs, sp, ss;
};

/* Field offsets here are the compiler's struct packing, reported as-is. */
struct pack_probe {
    char a;
    int b;
    char c;
    short d;
    double e;
};

static void heartbeat(const char *step)
{
    FILE *fp = fopen(HB_FILE, "w");
    if (fp != NULL) {
        fprintf(fp, "%s\n", step);
        fclose(fp);
    }
}

/* Read the VFS signature off the disc; 1 when it matches. */
static int probe_disc(FILE *out, const char *path)
{
    char sig[VFS_SIG_LEN + 1];
    FILE *fp;
    size_t got;

    fp = fopen(path, "rb");
    if (fp == NULL) {
        fprintf(out, "disc_open=fail path=%s\n", path);
        return 0;
    }
    if (fseek(fp, VFS_SIG_OFF, SEEK_SET) != 0) {
        fprintf(out, "disc_open=ok disc_seek=fail path=%s\n", path);
        fclose(fp);
        return 0;
    }
    got = fread(sig, 1, VFS_SIG_LEN, fp);
    fclose(fp);
    sig[VFS_SIG_LEN] = '\0';
    if (got != VFS_SIG_LEN) {
        fprintf(out, "disc_open=ok disc_read=short got=%u path=%s\n",
                (unsigned)got, path);
        return 0;
    }
    fprintf(out, "disc_open=ok disc_sig=%s\n", sig);
    if (memcmp(sig, VFS_SIG, VFS_SIG_LEN) != 0) {
        fprintf(out, "disc_probe=mismatch path=%s\n", path);
        return 0;
    }
    fprintf(out, "disc_probe=ok path=%s\n", path);
    return 1;
}

/* INT 2Fh AX=1500h through DPMI; 1 when at least one CD drive answers. */
static int probe_mscdex(FILE *out)
{
    struct rm_call rmcs;
    union REGS r;
    struct SREGS sr;

    memset(&rmcs, 0, sizeof(rmcs));
    rmcs.eax = 0x1500;
    rmcs.ebx = 0;

    segread(&sr);
    sr.es = sr.ds;              /* flat model: the call structure lives in DS */
    memset(&r, 0, sizeof(r));
    r.w.ax = 0x0300;            /* DPMI simulate real mode interrupt */
    r.h.bl = 0x2f;
    r.h.bh = 0;
    r.w.cx = 0;                 /* no stack words to copy */
    r.x.edi = (unsigned long)&rmcs;
    int386x(0x31, &r, &r, &sr);

    if (r.x.cflag) {
        fprintf(out, "dpmi_int2f=fail carry=1\n");
        return 0;
    }
    fprintf(out, "dpmi_int2f=ok cd_drives=%u first_drive=%u\n",
            (unsigned)(rmcs.ebx & 0xffff), (unsigned)(rmcs.ecx & 0xffff));
    return (rmcs.ebx & 0xffff) != 0;
}

int main(int argc, char **argv)
{
    const char *disc_path = (argc > 1) ? argv[1] : "E:\\PACK.VFS";
    FILE *out;
    double x = 1.0;
    int disc_ok, cd_ok;

    heartbeat("start");
    out = fopen(RESULT_FILE, "w");
    if (out == NULL) {
        printf("smoke: cannot write %s\n", RESULT_FILE);
        return 1;
    }

    fprintf(out, "crt=ok\n");
    fprintf(out, "sizes int=%u long=%u ptr=%u double=%u\n",
            (unsigned)sizeof(int), (unsigned)sizeof(long),
            (unsigned)sizeof(void *), (unsigned)sizeof(double));
    fprintf(out, "pack_offsets a=%u b=%u c=%u d=%u e=%u total=%u\n",
            (unsigned)offsetof(struct pack_probe, a),
            (unsigned)offsetof(struct pack_probe, b),
            (unsigned)offsetof(struct pack_probe, c),
            (unsigned)offsetof(struct pack_probe, d),
            (unsigned)offsetof(struct pack_probe, e),
            (unsigned)sizeof(struct pack_probe));

    /* forces the x87/emulator path in, which -fpi selects */
    heartbeat("fpu");
    x = x / 3.0 + 0.5;
    fprintf(out, "fpu=%.6f\n", x);

    heartbeat("disc");
    disc_ok = probe_disc(out, disc_path);

    heartbeat("mscdex");
    cd_ok = probe_mscdex(out);

    fprintf(out, "verdict=%s\n", (disc_ok && cd_ok) ? "ok" : "partial");
    fclose(out);

    heartbeat("done");
    out = fopen(DONE_FILE, "w");
    if (out != NULL) {
        fprintf(out, "done\n");
        fclose(out);
    }
    printf("smoke: done, disc=%d cd=%d\n", disc_ok, cd_ok);
    return 0;
}
