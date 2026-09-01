/* cd.h -- the CD-ROM device layer: the DOS real-mode buffers the MSCDEX
 * request path is built in, and the register blocks every INT call in the
 * module shares.
 *
 * The module talks to MSCDEX through the real-mode device driver interface, so
 * both the request header and the transfer buffer have to live in DOS memory
 * below 1MB.  They are allocated once through DPMI at detection time and kept
 * for the lifetime of the process; nothing ever frees them.
 *
 * The block addresses that cdaudio.c and cdtoc.c also read --
 * data_fdps_cd_request_header_buffer, data_fdps_cd_ioctl_buffer and
 * data_fdps_cd_ioctl_buffer_real_mode_ptr -- are declared in gamedata.h, not
 * here.
 */
#ifndef CD_H
#define CD_H

#include <i86.h>

/* 00069dc8.  The input register block the module fills in before handing it to
   int386x, and never a local: every INT call in this translation unit writes
   the fields it needs straight into these 28 bytes.  28 is sizeof(union REGS)
   for the 32-bit compiler and is the size the symbol has in the image, so the
   union is the whole object and int386x reads nothing past it (contract B).

   Only the fields a given call needs are written, and nothing clears the rest
   between calls: the block carries whatever the previous call left in it.
   int386x does not write to it -- 0004e291-0004e29f loads the registers out of
   it and never stores back -- so a caller can read its own inputs afterwards. */
extern union REGS data_fdps_cd_int_regs_in;

/* 00069dac.  The output register block int386x fills in from the registers the
   interrupt returned: eax..edi at +0x00..+0x14 and the carry flag, sign
   extended, at +0x18 (0004e25c-0004e26f). */
extern union REGS data_fdps_cdrom_int_out_regs;

/* 00069df0.  The segment register block passed to int386x.  12 bytes, which is
   sizeof(struct SREGS) with the 386 fs and gs members present, and the size
   the symbol has in the image -- the 8-byte 16-bit shape would leave the
   module's memset writing over the neighbouring globals.

   It is an in-out block, not an input: int386x loads ES from +0 and DS from +6
   before issuing the interrupt (0004e28b, 0004e2a2) and writes the
   post-interrupt ES back to +0 and the caller's own DS to +6 (0004e27a,
   0004e276).  The other four members, cs ss fs gs, are never touched by it. */
extern struct SREGS data_fdps_cd_int_sregs;

/* 00069e54.  Real-mode segment of the 512-byte block the MSCDEX request header
   is built in.  It doubles as the module's "buffers are already allocated"
   flag: fdps_cdrom_detect allocates only when it is still zero -- CMP word ptr
   [0x00069e54],0x0 / JNZ at 0003c68e. */
extern unsigned short data_fdps_cd_request_header_real_mode_seg;

/* Allocates the module's two 512-byte DOS real-mode blocks through DPMI INT
   31h function 0100h and publishes each one twice: as a real-mode segment (or
   a packed seg:0000 far pointer) for the MSCDEX request header to carry, and
   as a flat linear address for the game to dereference, which works because
   DOS/4GW identity maps the first megabyte.

   The first block becomes the request header
   (data_fdps_cd_request_header_real_mode_seg and
   data_fdps_cd_request_header_buffer), the second the IOCTL transfer buffer
   (data_fdps_cd_ioctl_buffer_real_mode_ptr and data_fdps_cd_ioctl_buffer).

   Nothing is checked: a DPMI allocation that fails returns with carry set and
   an error code where the segment should be, and that error code is stored and
   used as an address by every later driver call.  The only guard is the
   caller's, and it guards against allocating twice rather than against
   failing. */
extern void fdps_cd_alloc_dos_buffers(void);
#pragma aux fdps_cd_alloc_dos_buffers "*" parm caller [];

#endif
