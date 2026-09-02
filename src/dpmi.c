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

int fdps_dpmi_alloc_dos_memory(unsigned paragraphs, unsigned *out_linear,
                               unsigned *out_real_mode_ptr,
                               unsigned *out_selector)
{
    /* Two distinct register sets, as the original's 0x38-byte frame holds:
       the input block at ESP+0 and the output block at ESP+0x1c.  Neither is
       cleared, so every member these two stores do not touch reaches the
       interrupt holding whatever the stack already contained; DPMI function
       0100h reads only AX and BX. */
    union REGS dpmi_in;
    union REGS dpmi_out;
    unsigned block_linear; /* first byte of the block, as the lock call sees it */
    unsigned block_last;   /* last byte of the block, inclusive */

    dpmi_in.x.eax = 0x0100;      /* DPMI Allocate DOS Memory Block */
    dpmi_in.x.ebx = paragraphs;  /* fn 0100h takes the paragraph count in BX */
    int386(0x31, &dpmi_in, &dpmi_out);

    /* CMP dword ptr [ESP+0x34],0 / JZ: on a set carry the routine falls
       straight to XOR ESI,ESI and out through the tail, so not one of the
       three out-parameters is written.  Zeroing them here -- the obvious
       defensive spelling -- would write into the caller's storage on a path
       where the original never touches it: both AIL loaders point all three
       parameters at fields of the driver descriptor they are filling in
       (000456db passes EBP, EBP+4 and EBP+8), and on failure those fields keep
       whatever they held. */
    if (dpmi_out.x.cflag != 0) {
        return 0;
    }

    /* AX carries the real-mode segment of the block and DX the protected-mode
       selector DPMI created alongside it.  The order of these three stores is
       the original's and is observable: a caller that aims two of the pointers
       at one word gets the last store, and the lock range below is recomputed
       by reading *out_real_mode_ptr back rather than by keeping the segment in
       a register. */
    *out_real_mode_ptr = dpmi_out.x.eax << 16;             /* seg:0000 */
    *out_linear = (dpmi_out.x.eax & 0xffffu) << 4;         /* seg * 16 */
    *out_selector = dpmi_out.x.edx & 0xffffu;

    /* MOV EAX,[EBX] / SHR EAX,0xc: the block's linear address, recovered from
       the far pointer that was just stored rather than from the segment.  The
       upper endpoint is inclusive -- SHL EDX,4 / ADD EDX,EAX / DEC EDX -- so
       it is the last byte of the block, which is what fdps_dpmi_lock_region
       expects; see rebuild_info/pitfalls.md. */
    block_linear = *out_real_mode_ptr >> 12;
    block_last = (paragraphs << 4) + block_linear - 1;

    /* The lock's result is deliberately discarded: the original returns 1
       whenever fn 0100h succeeded, whether or not the page lock did.  Writing
       `return fdps_dpmi_lock_region(...)` would make a refused lock look like
       a failed allocation to the AIL driver loaders, which then take their
       error path over a block that really was allocated. */
    fdps_dpmi_lock_region(block_linear, block_last);
    return 1;
}

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

int fdps_dpmi_unlock_region(unsigned start, unsigned end)
{
    union REGS dpmi_in;
    union REGS dpmi_out;
    unsigned base;   /* lower endpoint -- the first byte of the range */
    unsigned last;   /* upper endpoint -- the last byte of the range */
    unsigned length; /* byte count handed to fn 0601h in SI:DI */

    /* 0003cb79: CMP EDX,EBX, then two JNC arms reading the one set of flags --
       EAX takes the smaller endpoint and EDX the larger -- so the two
       arguments may arrive in either order.  Identical to the ordering in
       fdps_dpmi_lock_region above, and for the same reason: the six AIL
       teardown wrappers push a code extent low-then-high while
       fdps_dpmi_unlock_size pushes base and base + size. */
    if (start < end) {
        base = start;
        last = end;
    } else {
        base = end;
        last = start;
    }

    /* SUB EDX,EAX / INC EDX.  `last` is the address of the final byte of the
       range, not one past it, so both endpoints are counted; see
       rebuild_info/pitfalls.md.  Dropping the INC releases one byte less than
       the original, and through fdps_dpmi_unlock_size -- which passes
       end = base + size -- that is a whole page left locked whenever
       base + size falls on a page boundary. */
    length = (last - base) + 1;

    /* Fn 0601h takes the linear base in BX:CX and the byte count in SI:DI.
       Only these five members are written: the original leaves the EDX slot
       holding whatever the stack already contained, and fn 0601h does not read
       DX, so clearing it here would be a behaviour the original does not have.

       In the image this body ends at the 0x0601 store and jumps to 0003cb24,
       the tail inside fdps_dpmi_lock_region, so the two functions share one
       copy of everything below -- the tail's ADD ESP,0x38 and RET are the only
       epilogue either of them has.  That is a codegen decision about identical
       instruction sequences, not a difference in what the two functions do,
       and ADR-0001 puts it outside the equivalence being reproduced. */
    dpmi_in.x.eax = 0x0601;               /* DPMI Unlock Linear Region */
    dpmi_in.x.ebx = base >> 16;
    dpmi_in.x.ecx = base & 0xffffu;
    dpmi_in.x.esi = length >> 16;
    dpmi_in.x.edi = length & 0xffffu;
    int386(0x31, &dpmi_in, &dpmi_out);

    /* CMP dword ptr [ESP+0x34],0 / SETZ AL / AND EAX,0xff on the out set's
       carry word: 1 means the host cleared carry, i.e. the range is unlocked.
       int386's own return value is discarded -- the shared tail overwrites AL
       before reading it. */
    return dpmi_out.x.cflag == 0;
}
