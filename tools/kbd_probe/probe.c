/* probe.c -- what does each keyboard layer report after the BIOS ring is
 * stuffed by hand, the way tests/title.c stuffs it?
 *
 * One line per iteration in PROBE.OUT.  Fields:
 *   it      iteration number
 *   var     variant: 0 plain, 1 after a mode 13h/text round trip,
 *           2 IRQ1 left unmasked
 *   ms      BIOS tick at stuffing (0x46c low word)
 *   mask    8259 master mask before masking
 *   h0 t0   head/tail before stuffing
 *   s e     0x480 / 0x482 buffer start / end
 *   h1 t1   head/tail right after stuffing
 *   cb      CRT ungetch byte (_cbyte)
 *   kb      kbhit() -- first poll, exactly as the test does
 *   d0..d4  INT 21h AH=0Bh AL, five back-to-back polls
 *   z1      INT 16h AH=01h ZF (1 = no key)
 *   d5      INT 21h AH=0Bh after the INT 16h poll
 *   h2 t2   head/tail after all polls
 *   kb2     kbhit() after all polls
 */
#include <stdio.h>
#include <string.h>
#include <conio.h>
#include <i86.h>

extern unsigned char crt_cbyte;
#pragma aux crt_cbyte "_cbyte";

extern unsigned char mask_irq1(void);
#pragma aux mask_irq1 = \
    "in al,21h" "mov ah,al" "or al,2" "out 21h,al" "mov al,ah" \
    value [al] modify [eax];
extern void restore_mask(unsigned char m);
#pragma aux restore_mask = "out 21h,al" parm [al] modify [eax];

#define W(a) (*(volatile unsigned short *)(a))

static int dos0b(void)
{
    union REGS r;
    memset(&r, 0, sizeof(r));
    r.h.ah = 0x0b;
    int386(0x21, &r, &r);
    return r.h.al;
}

extern int bios01_zf(void);
#pragma aux bios01_zf = "mov ah,1" "int 16h" "mov eax,0" "setz al"     value [eax] modify [eax];

static void set_mode(int m)
{
    union REGS r;
    memset(&r, 0, sizeof(r));
    r.x.eax = (unsigned) m;
    int386(0x10, &r, &r);
}

static void hb(int i)
{
    FILE *f = fopen("HB.TXT", "w");
    if (f) { fprintf(f, "%d\n", i); fclose(f); }
}


static void stuff(void)
{
    volatile unsigned short *ring = (volatile unsigned short *) 0x41e;
    ring[0] = 0x1c0d; ring[1] = 0x1c0d;
    W(0x41a) = 0x1e; W(0x41c) = 0x22;
}
static void clear(void) { W(0x41c) = W(0x41a); }
static void wait_tick(void)
{
    unsigned short t0 = W(0x46c);
    while (W(0x46c) == t0) { }
}
/* How many further 0Bh polls until keys become visible, capped. */
static long polls_until_seen(int use16)
{
    long i;
    for (i = 0; i < 2000000L; i++) {
        if (use16 ? !bios01_zf() : dos0b() != 0) return i;
    }
    return -1;
}

int main(void)
{
    FILE *out;
    int it, var, arm, chk, first;
    unsigned short tk0, tk1;
    unsigned char mask;
    long more;

    out = fopen("PROBE.OUT", "w");
    if (out == NULL) return 1;
    /* var: arm = how the empty ring was polled just before stuffing
       (0 none, 1 kbhit/0Bh, 2 INT 16h AH=01h, 3 kbhit then wait a tick)
       chk = what polls after stuffing (0 kbhit, 1 INT 16h) */
    for (it = 0; it < 64; it++) {
        var = it % 8;
        arm = var % 4;
        chk = var / 4;
        hb(it);
        wait_tick();          /* start every iteration at a tick boundary */
        mask = mask_irq1();
        clear();
        if (arm == 1 || arm == 3) (void) kbhit();
        if (arm == 2) (void) bios01_zf();
        if (arm == 3) wait_tick();
        tk0 = W(0x46c);
        stuff();
        first = chk ? !bios01_zf() : (kbhit() != 0);
        more = first ? 0 : polls_until_seen(chk);
        tk1 = W(0x46c);
        clear();
        restore_mask(mask);
        fprintf(out, "it=%d arm=%d chk=%d first=%d more=%ld dtick=%u\n",
                it, arm, chk, first, more, (unsigned) (tk1 - tk0));
    }
    fclose(out);
    out = fopen("RUN.DON", "w");
    if (out) { fputs("done\n", out); fclose(out); }
    return 0;
}
