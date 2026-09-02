/* aildpmi.c -- game-side DPMI service routines the Miles AIL library calls.
 *
 * DPMI INT 31h wrappers for linear region unlocking (0601h), plus the two
 * size-taking spellings of the lock pair. AIL's vendor object references each
 * of them by EXTDEF, so they must be defined and linked in; the library does
 * not provide them. Function 0100h, the DOS memory allocation, function 0101h,
 * the free, and function 0600h, the region lock, are in src/dpmi.c.
 */
#include <i86.h>
#include "aildpmi.h"
#include "dpmi.h"

/* Probe-free in the original: 0003cb6e, 0003cb93 and 0003cbaa open with a
 * bare PUSH/SUB ESP, no PUSH n / CALL __CHK. The pinned flag set has -s, so
 * this pragma changes nothing today; it is here because these run on AIL's
 * driver setup and interrupt paths, where a stack probe is exactly what must
 * not appear, and a build variant without -s would otherwise add one
 * silently. */
#pragma off (check_stack)

/* The wrapper below does not clear `union REGS` before filling it in, so the
 * members it does not assign reach int386 as whatever was on the stack. That
 * is what the original does -- 0003cb6e stores only the function code and the
 * four range registers into its frame and calls straight through -- and fn
 * 0601h reads no register it leaves alone. Adding the memset that the same
 * routines carry in FD2's source would put code in the function that FDPS's
 * does not have. */

/* 0003cb01 and 0003cb6e are one routine each in the original, the second
   jumping into the first's body after storing its own function code. 0003cb01
   is emitted in src/dpmi.c; what is left here is the 0601h half, still going
   through the shared helper. */
static int fdps_dpmi_lock_call(unsigned func, unsigned start, unsigned end)
{
    union REGS regs;
    unsigned base, len;

    base = (start < end) ? start : end;
    /* The pair is an inclusive range: the last byte is `end`, so the length
       handed to DPMI is (max - min) + 1. */
    len = ((start < end) ? end : start) - base + 1;

    regs.x.eax = func;
    regs.x.ebx = base >> 16;
    regs.x.ecx = base & 0xffffu;
    regs.x.esi = len >> 16;
    regs.x.edi = len & 0xffffu;
    int386(0x31, &regs, &regs);
    return regs.x.cflag == 0;
}

int fdps_dpmi_unlock_region(unsigned start, unsigned end)
{
    return fdps_dpmi_lock_call(0x0601, start, end);
}

/* `base + size` is the last byte locked, so these lock one byte more than
   `size`. That is the original's behaviour and callers depend on it -- see
   rebuild_info/pitfalls.md. */
int fdps_dpmi_lock_size(unsigned base, unsigned size)
{
    return fdps_dpmi_lock_region(base, base + size);
}

int fdps_dpmi_unlock_size(unsigned base, unsigned size)
{
    return fdps_dpmi_unlock_region(base, base + size);
}
