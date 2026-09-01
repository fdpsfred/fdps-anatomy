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

#include "fdpstype.h"

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

/* 00069e22.  The 50-byte DPMI real mode call structure every real-mode
   interrupt this module issues is described in.  50 is
   sizeof(struct fdps_dpmi_real_mode_call) and the size the symbol has in the
   image, so the module's clear of it covers the object exactly and stops at
   its end (contract B).

   It is an in-out block: DPMI function 0300h reads the real-mode register
   state out of it, issues the interrupt, and writes the state the handler
   returned back over it, so nothing the caller put in it survives the call. */
extern struct fdps_dpmi_real_mode_call data_fdps_cd_real_mode_call;

/* 00069dfc.  How many CD-ROM drives MSCDEX reported -- BX returned by INT 2Fh
   AX=1500h, stored as a word at 0003c67e.  That store is the only instruction
   in the image that names this address: nothing ever reads the count back, so
   it records what the installation check found rather than steering anything.
   The module addresses the first drive and only the first drive. */
extern unsigned short data_fdps_cdrom_drive_count;

/* 00069dfe.  Drive letter index of the first CD-ROM drive MSCDEX reports,
   0 for A: -- the low byte of CX returned by INT 2Fh AX=1500h, stored at
   0003c689.  It is what the module passes in CX to every MSCDEX call that
   names a drive, so the module only ever addresses the first CD-ROM drive. */
extern unsigned char data_fdps_cdrom_drive_letter_index;

/* 00069e54.  Real-mode segment of the 512-byte block the MSCDEX request header
   is built in.  It doubles as the module's "buffers are already allocated"
   flag: fdps_cdrom_detect allocates only when it is still zero -- CMP word ptr
   [0x00069e54],0x0 / JNZ at 0003c68e. */
extern unsigned short data_fdps_cd_request_header_real_mode_seg;

/* 00069e1c.  The four-byte device status the CD-ROM driver returned to the last
   MSCDEX Device Status request -- control block 06h's reply, taken from control
   block offset 1 and stored whole, unmasked and not sign extended.
   fdps_cdrom_read_device_status is the only code in the image that touches it,
   and it only writes: its one caller wants the request header's status word
   instead, so nothing ever reads this back. */
extern unsigned int data_fdps_cdrom_device_status;

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

/* Hands whatever request header the module has already built in the DOS block
   to the CD-ROM device driver, as real-mode INT 2Fh AX=1510h -- MSCDEX Send
   Device Request -- issued through DPMI function 0300h.

   Takes nothing and returns nothing: the header, the drive letter index and
   the header's real-mode segment are all globals the caller has set up, and
   the driver's answer is the status word the driver writes back into that same
   header.  This function never looks at that status word, so every caller has
   to read it for itself.

   A failure it does report is a failure of the DPMI call rather than of the
   device request: if INT 31h comes back with carry set it prints
   "DEVICE REQUEST FAILED!!!" and returns anyway, leaving the caller to read a
   header the driver never touched. */
extern void fdps_cd_device_request(void);
#pragma aux fdps_cd_device_request "*" parm caller [];

/* Asks the CD-ROM driver where its head is sitting and returns the answer as an
   HSG logical sector number -- MSCDEX IOCTL Input, control block 01h Location
   of Head, with the addressing-mode byte 0 that selects sector numbers over
   minute/second/frame.

   Takes nothing: the drive it asks is the module's one drive letter index and
   the buffers it stages through are the module's two DOS blocks, so everything
   it needs is already global.

   Success is not folded into the return value -- a refused request and a head
   parked at sector 0 come back the same -- so a caller that needs to know reads
   data_fdps_cd_last_request_status, which this leaves holding the status word
   the driver wrote into the request header.  Nothing in the image calls it. */
extern unsigned int fdps_cd_read_head_sector(void);
#pragma aux fdps_cd_read_head_sector "*" parm caller [];

/* Asks the CD-ROM driver how it has routed and mixed its audio output and
   fills the caller's nine-byte control block with the answer -- MSCDEX IOCTL
   Input, control block 04h Audio Channel Info, whose reply is four (input
   channel, volume) pairs, one pair per output channel.

   channel_info points at nine bytes the caller owns.  Byte 0 is stamped with
   the function code here, so the caller need not set it, but bytes 1..8 are
   sent to the driver exactly as the caller left them and are overwritten with
   the driver's answer on the way back; this routine never clears them.

   Returns nothing.  Whether the driver accepted the request is visible only in
   data_fdps_cd_last_request_status, which this leaves holding the status word
   the driver wrote into the request header -- bit 15 is error.  Without it a
   refused request is indistinguishable from a drive reporting every channel
   muted.  Nothing in the image calls it. */
extern void fdps_cd_read_audio_channel_info(unsigned char *channel_info);
#pragma aux fdps_cd_read_audio_channel_info "*" parm caller [];

/* Tells the CD-ROM driver how to route and mix its audio output, from the
   caller's nine-byte control block -- MSCDEX IOCTL Output, control block 03h
   Audio Channel Control, whose payload is four (input channel, volume) pairs,
   one pair per output channel.  The write-direction counterpart of
   fdps_cd_read_audio_channel_info.

   control_block points at nine bytes the caller owns.  Byte 0 is stamped with
   the control block code here, so the caller need not set it; bytes 1..8 are
   the four pairs and are sent exactly as the caller left them.  Nothing is
   copied back into the block -- this is an order to the drive, not a question,
   so the caller's buffer is unchanged apart from that first byte.

   Returns nothing.  Whether the driver accepted the order is visible only in
   data_fdps_cd_last_request_status, which this leaves holding the status word
   the driver wrote into the request header -- bit 15 is error.  Nothing in the
   image calls it. */
extern void fdps_cd_set_audio_channel_control(unsigned char *control_block);
#pragma aux fdps_cd_set_audio_channel_control "*" parm caller [];

/* Asks the CD-ROM driver what kind of device it is and what state it is in --
   MSCDEX IOCTL Input, control block 06h Device Status -- and leaves the
   four-byte answer in data_fdps_cdrom_device_status.

   Takes nothing and returns nothing: the drive it asks is the module's one
   drive letter index, the buffers it stages through are the module's two DOS
   blocks, and both answers leave through globals.

   The answer its only caller actually uses is the other one.
   data_fdps_cd_last_request_status is left holding the status word the driver
   wrote into the request header, and fdps_cd_audio_is_idle calls this routine
   purely to refresh that word before testing its busy bit -- so the device
   status dword is written and never read, while the status word is what makes
   the call worth making. */
extern void fdps_cdrom_read_device_status(void);
#pragma aux fdps_cdrom_read_device_status "*" parm caller [];

/* Sends the CD-ROM driver one bare order -- MSCDEX IOCTL Output whose whole
   control block is the single function-code byte, for the drive commands that
   carry no data of their own.

   command_code is that byte.  Only its low eight bits are used; the argument is
   pushed as a dword and read back as MOV AL,byte ptr [ESP+0x20].
   fdps_cd_close_tray passes 5, Close Tray, and it is the only caller in the
   image; 0 (Eject Disk) and 2 (Reset Drive) are the other codes this request
   shape carries.

   Returns nothing.  Whether the drive accepted the order is visible only in
   data_fdps_cd_last_request_status, which this leaves holding the status word
   the driver wrote into the request header -- the word fdps_cd_status_is_not_
   busy tests. */
extern void fdps_cd_ioctl_output_command(int command_code);
#pragma aux fdps_cd_ioctl_output_command "*" parm caller [];

/* Finds out whether the game can talk to a CD-ROM drive at all, and brings the
   whole module up on the way: the MSCDEX installation check INT 2Fh AX=1500h,
   the drive count and first drive letter it answers with, the one-time DOS
   buffer allocation, and a first Read Disk Info against the disc that is in
   the drive.

   Takes nothing.  Returns 0 when no CD-ROM redirector answered the multiplex
   at all, 2 when the disc-info request came back with status 0x810C -- the
   driver's error bit over device error 0x0C, general failure, which is what a
   drive with no readable disc in it reports -- and 1 otherwise.  Its one
   caller, main, treats 1 as the only acceptable answer and quits on the other
   two.

   Calling it a second time is not the same as calling it once: the DOS buffers
   are allocated only while data_fdps_cd_request_header_real_mode_seg is still
   zero, so a later call re-reads the disc without reallocating.  That makes it
   the module's initialiser and its "is the right disc still in the drive"
   probe at the same time. */
extern short fdps_cdrom_detect(void);
#pragma aux fdps_cdrom_detect "*" parm caller [];

/* Orders the drive to close its tray -- MSCDEX IOCTL Output control block 05h,
   sent as the bare one-byte order fdps_cd_ioctl_output_command builds -- and
   returns fdps_cd_status_is_not_busy's reading of the status word that request
   left behind: 1 when the request header's busy bit is clear, 0 when it is
   still set.  The original reaches that predicate by running off the end of
   its own body into it.

   Takes nothing; the drive and the staging buffers are the module's globals.
   Nothing in the image calls it. */
extern unsigned short fdps_cd_close_tray(void);
#pragma aux fdps_cd_close_tray "*" parm caller [];

/* 0003c6d0.  Reads data_fdps_cd_last_request_status -- the status word the
   module's last MSCDEX device request left in the request header -- and
   returns 1 when bit 0x0200 of it, the DOS device driver request header's busy
   bit, is clear, 0 when it is set.  Takes nothing and looks at nothing else,
   so it reports on whatever request ran last.

   The two routines that end in it, fdps_cd_close_tray and
   fdps_cd_audio_is_idle, both reach it as a tail call. */
extern unsigned short fdps_cd_status_is_not_busy(void);
#pragma aux fdps_cd_status_is_not_busy "*" parm caller [];

/* 0003c6f9.  Asks the CD-ROM driver whether the disc has been swapped since it
   was last asked -- MSCDEX IOCTL Input, control block 09h Media Changed -- and
   hands back the driver's answer byte.

   Takes nothing; the drive is the module's one drive letter index and the
   staging blocks are its two DOS blocks.

   The answer is MSCDEX's tri-state media-change value and not a boolean: 1
   means the disc has NOT been changed, 0 means the driver cannot tell, and
   0xff means it has been changed.  Reading it as a truth value inverts the
   common case (rebuild_info/pitfalls.md).

   Whether the driver answered at all is visible only in
   data_fdps_cd_last_request_status, which this leaves holding the status word
   the driver wrote into the request header -- bit 15 is error.  A refused
   request returns whatever byte the frame held.  Nothing in the image calls
   it. */
extern unsigned short fdps_cd_read_media_change_status(void);
#pragma aux fdps_cd_read_media_change_status "*" parm caller [];

/* Locks or unlocks the CD-ROM drive door, so the disc cannot be swapped while
   the game is using it -- MSCDEX IOCTL Output, control block 01h Lock/Unlock
   Door, whose payload is the function code and the door state behind it.

   lock is that state: 1 locks the drive door, 0 unlocks it.  Only its low eight
   bits are used; the argument is pushed as a dword and read back as MOV
   AL,byte ptr [ESP+0x20].

   Returns nothing.  Whether the drive accepted the order is visible only in
   data_fdps_cd_last_request_status, which this leaves holding the status word
   the driver wrote into the request header -- the word fdps_cd_status_is_not_
   busy tests.  Nothing in the image calls it. */
extern void fdps_cd_set_door_lock(int lock);
#pragma aux fdps_cd_set_door_lock "*" parm caller [];

#endif
