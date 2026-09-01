/* cd.c -- the CD-ROM device layer: DOS real-mode buffer allocation and the
 * MSCDEX device driver request path.
 *
 * Everything the module sends to the CD-ROM driver has to be built in memory
 * the real-mode driver can address, so this file owns the two 512-byte DOS
 * blocks the rest of the CD code writes into, and the register blocks the INT
 * calls share.  See cd.h for what each global holds.
 *
 * memset and memcpy come from <string.h>, int386x and the FP_SEG/FP_OFF pair
 * from <i86.h>, and printf from <stdio.h>; memset, memcpy, int386x and printf
 * are real calls in the image, while FP_SEG is the compiler's own inline
 * expansion of a near-to-far pointer conversion and FP_OFF is a cast that costs
 * nothing.
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

/* 0003bce2.  An MSCDEX IOCTL Input request -- command code 3 -- carrying the
   one-byte control block 01h, Location of Head, with its addressing-mode byte
   set to 0 so the driver answers in HSG logical sectors instead of Red Book
   minute/second/frame.

   There is no branch in the body at all: the header and the control block are
   built, staged into the two DOS blocks, sent, and read back, in a straight
   line.  Both blocks are built as locals and copied, rather than being written
   into the DOS blocks in place, so the driver never sees a half-built header.

   The staged header declares itself 0x1e bytes long while only the 26
   documented ones are filled in, and the memcpy that stages it copies that
   declared 0x1e -- MOV byte ptr [ESP],0x1e and PUSH 0x1e at 0003bd2b -- so four
   bytes of live stack go out past the end of the record.  MSCDEX reads 26 for
   an IOCTL Input request and never looks at them; the over-declaration is in
   the original and is left alone.  The status and reserved fields are likewise
   never initialised, and reach the driver as whatever the frame held.

   The read-back lengths differ from the send: the header comes back for as many
   bytes as its own length byte says -- MOVZX EAX,byte ptr [ESP] at 0003bd5a,
   zero-extended, and that byte is the local's own 0x1e, not anything the driver
   wrote -- while the control block comes back for a flat 6.  What the driver
   answered with is therefore the dword at control block +2, read straight out
   of the six bytes as a dword at 0003bd91 with nothing masking or sign
   extending it.

   The status word is published before the answer is picked up, in
   data_fdps_cd_last_request_status, because this function does not fold success
   into the sector it returns: nothing about the value distinguishes a head at
   sector 0 from a request the driver refused, so a caller that cares has to
   read that word.  No caller does -- nothing in the image calls this function
   at all, so the return's signedness is not observable anywhere. */
unsigned int fdps_cd_read_head_sector(void)
{
    struct fdps_cd_request_header request_header;
    unsigned char control_block[6];

    request_header.header_length = 0x1e;
    request_header.subunit = 0;
    request_header.command = 3;
    request_header.volume_id_ptr = 0;
    request_header.start_sector = 0;
    request_header.media_descriptor = 0;
    request_header.transfer_address = data_fdps_cd_ioctl_buffer_real_mode_ptr;
    request_header.transfer_byte_count = 6;
    control_block[0] = 1;
    control_block[1] = 0;

    memcpy(data_fdps_cd_request_header_buffer, &request_header, 0x1e);
    memcpy(data_fdps_cd_ioctl_buffer, control_block, 6);
    fdps_cd_device_request();
    memcpy(&request_header, data_fdps_cd_request_header_buffer,
           request_header.header_length);
    memcpy(control_block, data_fdps_cd_ioctl_buffer, 6);

    data_fdps_cd_last_request_status = request_header.status;
    return *(unsigned int *) &control_block[2];
}

/* 0003bd99.  An MSCDEX IOCTL Input request -- command code 3 -- carrying the
   nine-byte control block 04h, Audio Channel Info, which asks the drive which
   input channel and what volume it has routed to each of its four output
   channels.

   The control block is the caller's, not a local: byte 0 is stamped with the
   function code here and bytes 1..8 are whatever the caller left in them.  All
   nine go out to the driver and all nine come back overwritten, so a caller
   that wants a clean question has to clear its own buffer first -- nothing
   here does.

   No branch in the body: build the header, stage both blocks into the two DOS
   blocks, send, read both back.  The header is built as a local and copied so
   the driver never sees a half-built one, and the declared length is 0x1a --
   26 bytes, exactly the record -- which is also the length the staging memcpy
   copies (MOV byte ptr [ESP],0x1a and PUSH 0x1a at 0003bddf), so unlike
   fdps_cd_read_head_sector nothing goes out past the end of it.  The status
   word and the eight reserved bytes are still never initialised and reach the
   driver as whatever the frame held; the driver overwrites the first and
   ignores the rest.

   The read-back of the header is for as many bytes as the header's own length
   byte says -- MOVZX EAX,byte ptr [ESP] at 0003be0d, zero extended, and that
   byte is the local's own 0x1a rather than anything the driver wrote, because
   the driver wrote into the DOS block and not into this frame.  The control
   block comes back for a flat 9.

   The status word is published in data_fdps_cd_last_request_status because
   this function returns nothing at all: the only way a caller can tell a
   refused request from a drive that really has all four channels muted is to
   read that word, where bit 15 is the driver's error flag.  Nothing in the
   image calls this function.

   The transfer-address field carries data_fdps_cd_ioctl_buffer_real_mode_ptr,
   the packed real-mode far pointer, while the two memcpy's go through
   data_fdps_cd_ioctl_buffer, the flat linear address of that same DOS block.
   The two are not interchangeable (rebuild_info/pitfalls.md). */
void fdps_cd_read_audio_channel_info(unsigned char *channel_info)
{
    struct fdps_cd_request_header request_header;

    request_header.header_length = 0x1a;
    request_header.subunit = 0;
    request_header.command = 3;
    request_header.volume_id_ptr = 0;
    request_header.start_sector = 0;
    request_header.media_descriptor = 0;
    request_header.transfer_address = data_fdps_cd_ioctl_buffer_real_mode_ptr;
    request_header.transfer_byte_count = 9;
    channel_info[0] = 4;

    memcpy(data_fdps_cd_request_header_buffer, &request_header, 0x1a);
    memcpy(data_fdps_cd_ioctl_buffer, channel_info, 9);
    fdps_cd_device_request();
    memcpy(&request_header, data_fdps_cd_request_header_buffer,
           request_header.header_length);
    memcpy(channel_info, data_fdps_cd_ioctl_buffer, 9);

    data_fdps_cd_last_request_status = request_header.status;
}

/* 0003be36.  An MSCDEX IOCTL Output request -- command code 0x0c -- carrying
   the nine-byte control block 03h, Audio Channel Control, which tells the drive
   which input channel and what volume to route to each of its four output
   channels.

   The mirror image of fdps_cd_read_audio_channel_info: same nine-byte block,
   same four (input channel, volume) pairs, but going the other way.  That is
   the whole reason the command byte is 0x0c rather than 3 and the reason
   nothing is copied back out of the IOCTL block afterwards -- the caller asked
   a question of the drive in the read direction and is issuing an order in this
   one, so the reply half of the exchange is only the request header's status
   word.  Byte 0 of the caller's block is stamped with the control block code
   here; bytes 1..8 go to the driver exactly as the caller left them.

   No branch in the body: stamp, build the header, stage both blocks, send, read
   the header back.

   The declared header length is 0x18 -- twenty-four bytes, two short of the
   twenty-six the record actually is -- and 0x18 is also what the staging memcpy
   copies (MOV byte ptr [ESP],0x18 at 0003be4a and PUSH 0x18 at 0003be6d).  So
   the request goes out with start_sector and the low half of volume_id_ptr
   holding whatever the frame held, and unlike the two IOCTL Input builders
   above this body never writes either of them: there is no store at [ESP+0x14]
   or [ESP+0x16] anywhere in it.  Zeroing them here to make the request look
   tidy would send two bytes the original does not.  The status word and the
   eight reserved bytes are uninitialised for the same reason they are in the
   siblings -- the driver writes the first and ignores the rest.

   The read-back is for as many bytes as the header's own length byte says --
   MOVZX EAX,byte ptr [ESP] at 0003be9b, zero extended, and that byte is the
   local's own 0x18, because the driver wrote into the DOS block and not into
   this frame.  From the PUSH of that length onwards the code is shared: both
   fdps_cd_seek and fdps_cd_play_audio_range push their own length and jump
   straight to 0003bea0.  That is the compiler folding three identical tails
   together, not a routine any of them calls.

   The status word is published in data_fdps_cd_last_request_status because the
   function returns nothing: a drive that refused the request and a drive that
   accepted it are otherwise indistinguishable to the caller, and bit 15 of that
   word is the driver's error flag.  Nothing in the image calls this function.

   The transfer-address field carries data_fdps_cd_ioctl_buffer_real_mode_ptr,
   the packed real-mode far pointer, while the memcpy that fills the block goes
   through data_fdps_cd_ioctl_buffer, the flat linear address of that same DOS
   block.  The two are not interchangeable (rebuild_info/pitfalls.md). */
void fdps_cd_set_audio_channel_control(unsigned char *control_block)
{
    struct fdps_cd_request_header request_header;

    control_block[0] = 3;
    request_header.header_length = 0x18;
    request_header.subunit = 0;
    request_header.command = 0xc;
    request_header.media_descriptor = 0;
    request_header.transfer_address = data_fdps_cd_ioctl_buffer_real_mode_ptr;
    request_header.transfer_byte_count = 9;

    memcpy(data_fdps_cd_request_header_buffer, &request_header, 0x18);
    memcpy(data_fdps_cd_ioctl_buffer, control_block, 9);
    fdps_cd_device_request();
    memcpy(&request_header, data_fdps_cd_request_header_buffer,
           request_header.header_length);

    data_fdps_cd_last_request_status = request_header.status;
}

/* 0003c34f.  An MSCDEX IOCTL Input request -- command code 3 -- carrying the
   five-byte control block 06h, Device Status, whose reply is a four-byte device
   status dword saying what kind of drive answered and what state it is in.

   No branch in the body: build the header, stage both blocks into the two DOS
   blocks, send, read both back, publish the two answers.

   The declared header length is 0x1a -- 26 bytes, exactly the record -- and
   0x1a is also what the staging memcpy copies (MOV byte ptr [ESP],0x1a at
   0003c35c and PUSH 0x1a at 0003c393), so nothing goes out past the end of it.
   Unlike its two IOCTL Input siblings the read-back length is a literal rather
   than the header's own length byte: PUSH 0x1a at 0003c3c2, against MOVZX
   EAX,byte ptr [ESP] at 0003bd5a and 0003be0d.  The two come to the same 26
   here, because the driver writes into the DOS block and never into this frame,
   but the literal is what this body was compiled from.

   Only byte 0 of the control block is written -- MOV byte ptr [ESP+0x1c],0x6 is
   the body's only store into it -- so bytes 1..4 go out to the driver holding
   whatever the frame held.  Those four are the ones the driver overwrites with
   its answer, so clearing them here to make the question look tidy would send
   four bytes the original does not.  The header's status word and its eight
   reserved bytes are uninitialised for the same reason as in the siblings: the
   driver writes the first and ignores the rest.

   The answer is the dword at control block +1, read straight out of the five
   bytes at 0003c3ec with nothing masking or sign extending it, and published in
   data_fdps_cdrom_device_status -- which nothing in the image reads back.  The
   status word is published in data_fdps_cd_last_request_status, and that one
   does have a reader: fdps_cd_audio_is_idle calls this function and then falls
   straight into fdps_cd_status_is_not_busy, which tests that word's busy bit
   0x0200.  Its store is the epilogue at 0003c0ba, which Watcom folded together
   with fdps_cdrom_read_disk_info's tail; the fold is codegen and the store is
   this function's own, so a body that stopped at the device status dword would
   leave the idle test answering from the previous request's word.

   The transfer-address field carries data_fdps_cd_ioctl_buffer_real_mode_ptr,
   the packed real-mode far pointer, while the four memcpy's go through
   data_fdps_cd_ioctl_buffer and data_fdps_cd_request_header_buffer, the flat
   linear addresses of the same two DOS blocks.  The two forms are not
   interchangeable (rebuild_info/pitfalls.md). */
void fdps_cdrom_read_device_status(void)
{
    struct fdps_cd_request_header request_header;
    unsigned char control_block[5];

    request_header.header_length = 0x1a;
    request_header.subunit = 0;
    request_header.command = 3;
    request_header.volume_id_ptr = 0;
    request_header.start_sector = 0;
    request_header.media_descriptor = 0;
    request_header.transfer_address = data_fdps_cd_ioctl_buffer_real_mode_ptr;
    request_header.transfer_byte_count = 5;
    control_block[0] = 6;

    memcpy(data_fdps_cd_request_header_buffer, &request_header, 0x1a);
    memcpy(data_fdps_cd_ioctl_buffer, control_block, 5);
    fdps_cd_device_request();
    memcpy(&request_header, data_fdps_cd_request_header_buffer, 0x1a);
    memcpy(control_block, data_fdps_cd_ioctl_buffer, 5);

    data_fdps_cdrom_device_status = *(unsigned int *) &control_block[1];
    data_fdps_cd_last_request_status = request_header.status;
}
