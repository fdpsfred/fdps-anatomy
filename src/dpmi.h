/* dpmi.h -- DPMI services: DOS conventional-memory blocks and linear region
 * locking.
 *
 * Every symbol here is exported undecorated, because the Miles AIL vendor
 * object resolves against these names verbatim through the link-time aliases
 * in rebuild_info/ail_link.md.
 */
#ifndef DPMI_H
#define DPMI_H

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

#endif
