/* aildpmi.c -- game-side DPMI service routines the Miles AIL library calls.
 *
 * DPMI INT 31h wrappers for DOS memory allocation (0100h) and linear region
 * locking (0601h). AIL's vendor object references each of them by EXTDEF, so
 * they must be defined and linked in; the library does not provide them.
 * Function 0101h, the DOS memory free, and function 0600h, the region lock,
 * are in src/dpmi.c.
 */
#include <i86.h>
#include "aildpmi.h"
#include "dpmi.h"

/* Probe-free in the original: 0003ca49 and 0003cb01 open with a bare
 * PUSH/SUB ESP, no PUSH n / CALL __CHK. The pinned flag set has -s, so this
 * pragma changes nothing today; it is here because these run on AIL's driver
 * setup and interrupt paths, where a stack probe is exactly what must not
 * appear, and a build variant without -s would otherwise add one silently. */
#pragma off (check_stack)

/* None of the three wrappers below clears `union REGS` before filling it in,
 * so the members they do not assign reach int386 as whatever was on the
 * stack. That is what the original does -- 0003ca49 stores only EAX and EBX
 * into the 0x38-byte frame and calls straight through -- and none of the four
 * DPMI functions used here reads a register these leave alone. Adding the
 * memset that the same routines carry in FD2's source would put code in the
 * function that FDPS's does not have. */

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

int fdps_dpmi_alloc_dos_memory(unsigned paragraphs, unsigned *out_linear,
                               unsigned *out_real_mode_ptr,
                               unsigned *out_selector)
{
    union REGS regs;
    unsigned linear, end;

    regs.x.eax = 0x0100;
    regs.x.ebx = paragraphs;
    int386(0x31, &regs, &regs);
    if (regs.x.cflag != 0) {
        return 0;
    }

    /* AX carries the real-mode segment, DX the protected-mode selector. */
    *out_real_mode_ptr = regs.x.eax << 16;
    *out_linear = (regs.x.eax & 0xffffu) << 4;
    *out_selector = regs.x.edx & 0xffffu;

    /* The block is locked immediately: AIL hands it to DMA and to interrupt
       handlers, neither of which can take a page fault. */
    linear = *out_real_mode_ptr >> 12;
    end = (paragraphs << 4) + linear - 1;
    fdps_dpmi_lock_region(linear, end);
    return 1;
}
