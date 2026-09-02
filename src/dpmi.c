/* dpmi.c -- DPMI services: DOS conventional-memory blocks and linear region
 * locking, issued through int386 on INT 31h.
 *
 * The game reaches DPMI through the public int386 API rather than an inline
 * INT, so every routine here builds a `union REGS` input set, calls int386
 * with function 31h and reads back whatever the DPMI function returns.  The
 * call direction for these is library -> game: the Miles AIL vendor object
 * references them with EXTDEF records (rebuild_info/ail_link.md), so they must
 * be defined and linked in even though nothing under src/ calls them.
 */
#include <i86.h>
#include "dpmi.h"

/* Probe-free in the original: 0003cad2 opens with a bare SUB ESP,0x38 -- no
 * PUSH n / CALL __CHK.  The pinned flag set has -s, so this pragma changes
 * nothing today; it is here because this runs on AIL's driver setup and
 * teardown paths, where a stack probe is exactly what must not appear, and a
 * build variant without -s would otherwise add one silently. */
#pragma off (check_stack)

void fdps_dpmi_free_dos_memory(unsigned linear_unused, unsigned segment_unused,
                               unsigned selector)
{
    /* The original reserves 0x38 bytes for two adjacent 0x1c-byte register
       sets and hands int386 the two distinct addresses ESP+0x00 and ESP+0x1c.
       Neither is cleared first, so the members these two stores do not touch
       reach the interrupt holding whatever the stack already contained; DPMI
       function 0101h reads only AX and DX, so nothing else matters. */
    union REGS dpmi_in;
    union REGS dpmi_out;

    /* Carried only so the vendor object's three pushed dwords land on the
       parameters the original declares.  Neither is read: the original's body
       touches [ESP+0x44] and no other argument slot. */
    (void) linear_unused;
    (void) segment_unused;

    dpmi_in.x.eax = 0x0101;             /* DPMI Free DOS Memory Block */
    dpmi_in.x.edx = selector & 0xffffu; /* fn 0101h takes the selector in DX */
    int386(0x31, &dpmi_in, &dpmi_out);
    /* The output set is never examined.  The carry flag at ESP+0x34 is
       discarded, so a failed free is silent and callers cannot tell one from a
       successful free -- that is the original's behaviour, and adding a return
       value here would give the vendor object a result it never reads. */
}

int fdps_dpmi_lock_region(unsigned start, unsigned end)
{
    union REGS dpmi_in;
    union REGS dpmi_out;
    unsigned base;   /* lower endpoint -- the first byte of the locked range */
    unsigned last;   /* upper endpoint -- the last byte of the locked range */
    unsigned length; /* byte count handed to fn 0600h in SI:DI */

    /* CMP EDX,EBX with two JNC arms: the endpoints are ordered with an
       unsigned compare and swapped when they arrive the wrong way round, so
       either argument may be the larger one. */
    if (start < end) {
        base = start;
        last = end;
    } else {
        base = end;
        last = start;
    }

    /* INC EDX after the subtraction.  `last` is the address of the final byte
       of the range, not one past it, so the count is inclusive of both
       endpoints; see rebuild_info/pitfalls.md. */
    length = (last - base) + 1;

    /* Fn 0600h takes the linear base in BX:CX and the byte count in SI:DI.
       The original leaves the EDX slot alone, so it reaches the interrupt
       holding whatever the stack already contained; fn 0600h does not read
       DX, and clearing it here would be a behaviour the original does not
       have. */
    dpmi_in.x.eax = 0x0600;               /* DPMI Lock Linear Region */
    dpmi_in.x.ebx = base >> 16;
    dpmi_in.x.ecx = base & 0xffffu;
    dpmi_in.x.esi = length >> 16;
    dpmi_in.x.edi = length & 0xffffu;
    int386(0x31, &dpmi_in, &dpmi_out);

    /* SETZ AL / AND EAX,0xff on the out set's carry word: the sense is
       inverted from the hardware flag, so 1 is success.  int386's own return
       value is discarded -- the assembly overwrites AL before reading it. */
    return dpmi_out.x.cflag == 0;
}
