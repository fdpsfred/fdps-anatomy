/* cd.c -- the CD-ROM device layer: DOS real-mode buffer allocation and the
 * MSCDEX device driver request path.
 *
 * Everything the module sends to the CD-ROM driver has to be built in memory
 * the real-mode driver can address, so this file owns the two 512-byte DOS
 * blocks the rest of the CD code writes into, and the register blocks the INT
 * calls share.  See cd.h for what each global holds.
 *
 * memset comes from <string.h>, int386x and the FP_SEG/FP_OFF pair from
 * <i86.h>, and printf from <stdio.h>; memset, int386x and printf are real
 * calls in the image, while FP_SEG is the compiler's own inline expansion of a
 * near-to-far pointer conversion and FP_OFF is a cast that costs nothing.
 *
 * Note on this module's original build flags: it was not compiled with the
 * flag set the rest of the game was.  Every function in the block from
 * 0003bade to 0003c96x opens with PUSH <frame size> / CALL __CHK, which -s
 * removes and which no other game function has, and the block pushes the
 * address of a global as PUSH imm32, which the disabled optimiser never emits.
 * Compiling this file with -bt=dos4g -mf -4s -fpi -os -- no -s, and -os in
 * place of -ot -od -- reproduces fdps_cd_alloc_dos_buffers byte for byte,
 * all 0x9f of them.  The rebuild has one flag set for every unit
 * (rebuild_info/build_flags.md), so what it builds from this file is the same
 * code without the stack probe.
 */
#include <stdio.h>
#include <string.h>
#include <i86.h>
#include "gamedata.h"
#include "cd.h"

/* 0003bade.  Two DPMI INT 31h function 0100h allocations of 0x20 paragraphs
   each, published into four globals.

   The segment register block is zeroed first, so the first interrupt is issued
   with ES and DS holding the null selector; DPMI 0100h reads neither.  It is
   zeroed once and not again between the two calls, and int386x has written the
   live ES and DS back into it by then, so the second interrupt is issued with
   real selectors loaded.  That difference is in the original.

   The input block is likewise filled once: int386x leaves it alone, so the
   second call reuses AX = 0x0100 and BX = 0x0020 without restating them.

   AX comes back holding the real-mode segment and DX the protected-mode
   selector; the selector is dropped on the floor, which is why nothing here
   can ever release the memory.  Both flat addresses are the segment shifted
   left four, because DOS/4GW identity maps the first megabyte -- and both are
   taken from the low half of the returned EAX, not from a widened AX. */
void fdps_cd_alloc_dos_buffers(void)
{
    memset(&data_fdps_cd_int_sregs, 0, sizeof(struct SREGS));

    data_fdps_cd_int_regs_in.w.ax = 0x100;
    data_fdps_cd_int_regs_in.w.bx = 0x20;
    int386x(0x31, &data_fdps_cd_int_regs_in, &data_fdps_cdrom_int_out_regs,
            &data_fdps_cd_int_sregs);
    data_fdps_cd_request_header_real_mode_seg =
        data_fdps_cdrom_int_out_regs.w.ax;
    data_fdps_cd_request_header_buffer = (unsigned char *)
        ((data_fdps_cdrom_int_out_regs.x.eax & 0xffff) << 4);

    int386x(0x31, &data_fdps_cd_int_regs_in, &data_fdps_cdrom_int_out_regs,
            &data_fdps_cd_int_sregs);
    data_fdps_cd_ioctl_buffer_real_mode_ptr =
        (unsigned int) data_fdps_cdrom_int_out_regs.w.ax << 16;
    data_fdps_cd_ioctl_buffer = (unsigned char *)
        ((data_fdps_cdrom_int_out_regs.x.eax & 0xffff) << 4);
}

/* 0003bb7d.  The module's one way of reaching the CD-ROM device driver: the
   request header the caller has already built in the DOS block is handed to
   MSCDEX as real-mode INT 2Fh AX=1510h, and that real-mode interrupt is issued
   from protected mode through DPMI function 0300h, Simulate Real Mode
   Interrupt.

   Two blocks are cleared before anything is filled in, and their lengths are
   the whole of each object: 12 bytes for the segment register block and 50 for
   the real mode call structure.  The clear matters for the real mode call
   structure, because DPMI reads every field of it -- including SS:SP, where
   zero is what asks the host to supply a real-mode stack, and the caller of
   this function never sets any of them.

   The registers the driver sees are AX = 0x1510, CX = the drive letter index,
   and ES:BX addressing the request header, which is why BX is zeroed: the DOS
   block was allocated on a paragraph boundary and is published as a segment,
   so the whole address is in ES.  ES is set from the segment global rather
   than from the flat pointer, because the driver runs in real mode and cannot
   use the flat one.

   The DPMI call itself goes out as AX = 0x0300, BL = the interrupt number
   2Fh, BH = 0, CX = 0 -- no words are copied from the protected-mode stack to
   the real-mode one -- and ES:EDI addressing the real mode call structure.
   ES there is the caller's own DS, taken through FP_SEG, which is the flat
   data selector the whole program runs under.

   Only the DPMI call's own carry flag is checked.  A driver that refuses the
   request reports it in the status word inside the request header, which
   nothing here reads, so a caller that wants to know whether the device
   request worked has to read that word itself. */
void fdps_cd_device_request(void)
{
    memset(&data_fdps_cd_int_sregs, 0, sizeof(struct SREGS));
    memset(&data_fdps_cd_real_mode_call, 0,
           sizeof(struct fdps_dpmi_real_mode_call));

    data_fdps_cd_real_mode_call.eax = 0x1510;
    data_fdps_cd_real_mode_call.ecx = data_fdps_cdrom_drive_letter_index;
    data_fdps_cd_real_mode_call.ebx = 0;
    data_fdps_cd_real_mode_call.es = data_fdps_cd_request_header_real_mode_seg;

    data_fdps_cd_int_regs_in.w.ax = 0x300;
    data_fdps_cd_int_regs_in.h.bl = 0x2f;
    data_fdps_cd_int_regs_in.h.bh = 0;
    data_fdps_cd_int_regs_in.w.cx = 0;
    data_fdps_cd_int_sregs.es =
        FP_SEG((void __far *) &data_fdps_cd_real_mode_call);
    data_fdps_cd_int_regs_in.x.edi = FP_OFF(&data_fdps_cd_real_mode_call);

    int386x(0x31, &data_fdps_cd_int_regs_in, &data_fdps_cdrom_int_out_regs,
            &data_fdps_cd_int_sregs);
    if (data_fdps_cdrom_int_out_regs.x.cflag != 0) {
        printf("DEVICE REQUEST FAILED!!!\n");
    }
}
