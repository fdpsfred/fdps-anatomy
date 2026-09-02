/* dpmi.h -- DPMI services: DOS conventional-memory blocks and linear region
 * locking.
 *
 * Every symbol here is exported undecorated, because the Miles AIL vendor
 * object resolves against these names verbatim through the link-time aliases
 * in rebuild_info/ail_link.md.
 */
#ifndef DPMI_H
#define DPMI_H

/* Takes a block of DOS conventional memory from DPMI function 0100h and pins
   it into physical memory, reporting the block three ways.  Issues INT 31h
   with AX = 0100h and BX = `paragraphs` through int386.

   On success AX comes back with the block's real-mode segment and DX with the
   protected-mode selector DPMI created for it, and the three out-parameters
   receive, in this order, *out_real_mode_ptr = segment << 16 (the far pointer
   segment:0000), *out_linear = (segment & 0xffff) << 4 (the flat linear
   address of the same byte) and *out_selector = the selector, which is the
   argument fdps_dpmi_free_dos_memory takes to give the block back.  The whole
   block is then locked with fdps_dpmi_lock_region over the inclusive range
   [linear, linear + paragraphs * 16 - 1].

   Returns 1 when fn 0100h succeeded, WHETHER OR NOT the page lock did -- the
   lock's result is discarded -- and 0 when DPMI came back with the carry flag
   set.  On that failure path none of the three out-parameters is written at
   all: they are not cleared, so the caller's storage keeps whatever it held.
   Both AIL loaders aim all three at fields of the driver descriptor they are
   filling in, and both read only the returned int to decide whether the setup
   worked. */
extern int fdps_dpmi_alloc_dos_memory(unsigned paragraphs,
                                      unsigned *out_linear,
                                      unsigned *out_real_mode_ptr,
                                      unsigned *out_selector);
#pragma aux fdps_dpmi_alloc_dos_memory "*" parm caller [];

/* Gives a DOS conventional-memory block back to DPMI by handing function
   0101h the block's selector.  Issues INT 31h with AX = 0101h and DX =
   selector & 0xffff through int386, and examines nothing that comes back: the
   DPMI carry flag is discarded, so a failed free is silent and a caller cannot
   distinguish it from a successful one.  Nothing else in the input register
   set is written, so the remaining registers enter the interrupt holding
   whatever the stack already contained; fn 0101h reads only AX and DX.

   Only `selector` is read.  `linear_unused` and `segment_unused` are the
   protected-mode linear address (real-mode segment << 4) and the real-mode far
   pointer (real-mode segment << 16) of the same block, as the allocator at
   0003ca49 wrote them; they are declared because the four AIL callers push all
   three values out of the driver descriptor and drop them with ADD ESP,0xc.
   A one-argument spelling still links and still balances the stack, but then
   reads the block's linear address where the selector belongs, and because the
   carry flag is discarded every low-memory free fails silently. */
extern void fdps_dpmi_free_dos_memory(unsigned linear_unused,
                                      unsigned segment_unused,
                                      unsigned selector);
#pragma aux fdps_dpmi_free_dos_memory "*" parm caller [];

/* Pins a linear byte range into physical memory with DPMI function 0600h, so
   the pages stay resident while interrupt-time code touches them.  Issues INT
   31h through int386 with AX = 0600h, the linear base in BX:CX and the byte
   count in SI:DI.  Nothing else in the input register set is written, so the
   remaining registers enter the interrupt holding whatever the stack already
   contained; fn 0600h reads only AX, BX, CX, SI and DI.

   `start` and `end` are the two endpoints of the range and may arrive in
   either order: they are ordered with an unsigned compare, and the larger is
   taken as the address of the LAST BYTE of the range, not one past it, so the
   count sent to DPMI is (larger - smaller) + 1.  A caller that means "size"
   therefore passes base + size and gets size + 1 bytes locked, which is what
   fdps_dpmi_lock_size does.

   Returns 1 when the host came back with the carry flag clear -- i.e. the
   range is locked -- and 0 when the call failed.  The sense is inverted from
   the hardware flag. */
extern int fdps_dpmi_lock_region(unsigned start, unsigned end);
#pragma aux fdps_dpmi_lock_region "*" parm caller [];

#endif
