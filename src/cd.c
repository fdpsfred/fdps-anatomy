/* cd.c -- the CD-ROM device layer: DOS real-mode buffer allocation and the
 * MSCDEX device driver request path.
 *
 * Everything the module sends to the CD-ROM driver has to be built in memory
 * the real-mode driver can address, so this file owns the two 512-byte DOS
 * blocks the rest of the CD code writes into, and the register blocks the INT
 * calls share.  See cd.h for what each global holds.
 *
 * memset and memcpy come from <string.h>, int386, int386x and the FP_SEG/FP_OFF
 * pair from <i86.h>, and printf from <stdio.h>; memset, memcpy, int386, int386x
 * and printf are real calls in the image, while FP_SEG is the compiler's own
 * inline expansion of a near-to-far pointer conversion and FP_OFF is a cast
 * that costs nothing.
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
#include "cdtoc.h"

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

/* 0003c51c.  An MSCDEX IOCTL Output request -- command code 0x0c -- whose
   control block is one single byte: the function code alone, for the drive
   orders that carry no data of their own.  Its only caller, fdps_cd_close_tray,
   pushes 5, Close Tray; the same request shape is what 0 (Eject Disk) and 2
   (Reset Drive) would go out as.

   No branch in the body at all: stamp the command byte into a one-byte local,
   build the header, stage both into the two DOS blocks, send, read the header
   back, publish the status word.  The command byte is stamped first, before any
   header field is written -- MOV AL,byte ptr [ESP+0x20] / MOV byte ptr
   [ESP+0x18],AL at 0003c529 -- and only the low byte of the pushed argument is
   ever looked at.

   The declared header length is 0x18, two short of the twenty-six the record
   actually is, and 0x18 is also what the staging memcpy copies (MOV byte ptr
   [ESP],0x18 at 0003c531 and PUSH 0x18 at 0003c554).  So start_sector and the
   low half of volume_id_ptr go out holding whatever the frame held, and this
   body never writes either of them: there is no store at [ESP+0x14] or
   [ESP+0x16] anywhere in it.  Zeroing them to make the request look tidy would
   send two bytes the original does not.  The status word and the eight reserved
   bytes are uninitialised for the same reason as in the siblings -- the driver
   writes the first and ignores the rest.

   The read-back is a flat literal, PUSH 0x18 at 0003c583, not the header's own
   length byte the way fdps_cd_set_audio_channel_control reads it back; the two
   come to the same twenty-four here, because the driver wrote into the DOS
   block and never into this frame, but the literal is what this body was
   compiled from.  The control block is not read back at all: this is an order
   to the drive, not a question.

   From the control-block staging onward the code is shared with
   fdps_cd_set_door_lock, which builds its own two-byte block, pushes a count of
   2 and jumps to 0003c56b.  That is Watcom folding two identical tails
   together, not a routine either of them calls.

   The status word is published in data_fdps_cd_last_request_status because the
   function returns nothing: a drive that refused the order and a drive that
   carried it out are otherwise indistinguishable to the caller, and that word
   is what fdps_cd_status_is_not_busy reads.

   The transfer-address field carries data_fdps_cd_ioctl_buffer_real_mode_ptr,
   the packed real-mode far pointer, while the memcpy that fills the block goes
   through data_fdps_cd_ioctl_buffer, the flat linear address of that same DOS
   block.  The two are not interchangeable (rebuild_info/pitfalls.md). */
void fdps_cd_ioctl_output_command(int command_code)
{
    struct fdps_cd_request_header request_header;
    unsigned char control_block[1];

    control_block[0] = (unsigned char) command_code;
    request_header.header_length = 0x18;
    request_header.subunit = 0;
    request_header.command = 0xc;
    request_header.media_descriptor = 0;
    request_header.transfer_address = data_fdps_cd_ioctl_buffer_real_mode_ptr;
    request_header.transfer_byte_count = 1;

    memcpy(data_fdps_cd_request_header_buffer, &request_header, 0x18);
    memcpy(data_fdps_cd_ioctl_buffer, control_block, 1);
    fdps_cd_device_request();
    memcpy(&request_header, data_fdps_cd_request_header_buffer, 0x18);

    data_fdps_cd_last_request_status = request_header.status;
}

/* 0003c636.  The module's entry point: the MSCDEX installation check, INT 2Fh
   AX=1500h with BX=0, followed -- only if something answered it -- by the
   one-time DOS buffer allocation and a first Read Disk Info.  It is both the
   initialiser for everything else in this file and the answer to "is the game
   disc in the drive", which is why main calls nothing else before it.

   The interrupt is set up a byte at a time, AH then AL, and BX as a word: MOV
   byte ptr [0x00069dc9],0x15 / MOV byte ptr [0x00069dc8],0x0 / MOV word ptr
   [0x00069dcc],0x0.  Nothing clears the rest of the shared input block, so CX,
   DX, SI and DI reach the interrupt holding whatever the module's previous INT
   call left in them; BX is zeroed because the installation check requires it.
   This is the file's one int386 -- three arguments and the caller's own
   segment registers -- rather than the int386x every other request path here
   uses, because no real-mode call structure is involved.

   There are three branches and they are three different questions.

   The first is on the interrupt's answer: CMP word ptr [0x00069db0],0x0 tests
   BX, the number of CD-ROM drives, and the whole of the rest of the body hangs
   off it.  BX comes back holding the zero that went in when no redirector is
   loaded, because an unclaimed INT 2Fh returns with its registers untouched,
   so this is the no-driver case and it returns 0 without allocating anything.

   The second is the allocation guard: the request header's real-mode segment
   doubles as the module's "buffers are already there" flag, so a second call
   re-reads the disc without leaking another pair of DOS blocks (nothing in the
   module can free them).

   The third is on the status word fdps_cdrom_read_disk_info published, and it
   is a single equality against 0x810C rather than a test of the driver's error
   bit: 0x8000 is that bit, 0x0100 is done, and 0x0C is device error 12,
   general failure -- what a drive with no readable disc in it reports.  Any
   other failure the driver could report comes back as 1, indistinguishable
   here from success.  The compare is MOVZX EAX,word ptr [0x00069e20] / CMP
   EAX,0x810c, zero extended, which is the unsigned short the global is
   declared as; a signed one would sign extend and never match (contract C).

   The result is 16 bits wide: main sign-extends it with CWDE at 000292dc
   before storing it, which the compiler emits only for a short return. */
short fdps_cdrom_detect(void)
{
    data_fdps_cd_int_regs_in.h.ah = 0x15;
    data_fdps_cd_int_regs_in.h.al = 0;
    data_fdps_cd_int_regs_in.w.bx = 0;
    int386(0x2f, &data_fdps_cd_int_regs_in, &data_fdps_cdrom_int_out_regs);
    if (data_fdps_cdrom_int_out_regs.w.bx == 0) {
        return 0;
    }

    data_fdps_cdrom_drive_count = data_fdps_cdrom_int_out_regs.w.bx;
    data_fdps_cdrom_drive_letter_index = data_fdps_cdrom_int_out_regs.h.cl;

    if (data_fdps_cd_request_header_real_mode_seg == 0) {
        fdps_cd_alloc_dos_buffers();
    }
    fdps_cdrom_read_disk_info();

    if (data_fdps_cd_last_request_status == 0x810c) {
        return 2;
    }
    return 1;
}

/* 0003c6bc.  Tells the drive to close its tray -- MSCDEX IOCTL Output, control
   block 05h Close Tray -- and answers with whether the drive came back not
   busy.

   Takes nothing.  The drive is the module's one drive letter index and the
   staging blocks are its two DOS blocks, so the only thing this routine
   contributes to the request is the control block code: PUSH 0x5 at 0003c6c6,
   the single byte fdps_cd_ioctl_output_command wraps into a bare order.

   The answer is not its own work either.  The twenty bytes at 0003c6bc have no
   RET: they end in ADD ESP,0x4 at 0003c6cd and run straight into
   fdps_cd_status_is_not_busy at 0003c6d0.  That run-on is a tail call with the
   JMP left out because the predicate happens to be the next function --
   fdps_cd_audio_is_idle reaches the same predicate from further away and does
   carry an explicit JMP 0003c6d0 -- so the value handed back is the
   predicate's: 1 when bit 0x0200 of the status word, the DOS device driver
   request header's busy bit, is clear, and 0 when it is still set.  Written as
   a call, the result is the same value by the same test (ADR-0001); written as
   a void body it would be nothing at all.

   That word is the one the Close Tray request itself left in
   data_fdps_cd_last_request_status, read straight away, so the result reports
   on how the driver took the order rather than on a tray that has had time to
   finish moving.

   Nothing in the image calls this, which is the norm for this layer rather
   than a sign of misidentification: most of the CD module's entry points are
   equally unreferenced and it was linked whole. */
unsigned short fdps_cd_close_tray(void)
{
    fdps_cd_ioctl_output_command(5);
    return fdps_cd_status_is_not_busy();
}

/* 0003c6d0.  Answers whether the module's last MSCDEX device request came back
   with its busy bit clear: 1 when bit 0x0200 of the saved request-header
   status word is zero, 0 when it is set.

   Takes nothing and reads one word, data_fdps_cd_last_request_status, so what
   it reports on is whichever request ran last rather than a request of its
   own.  0x0200 is bit 9 of the DOS device driver request header's status word,
   the busy bit; bit 15 is the error flag and the low byte the driver's error
   code, and neither is looked at here.  A request that failed with the busy
   bit clear -- 0x810c, the no-readable-disc failure fdps_cdrom_detect tests
   for -- still answers 1.

   The mask is written against the whole word because that is what the two
   instructions do: XOR AL,AL at 0003c6d6 discards the error code byte and AND
   AH,0x2 keeps bit 9 of the high byte alone, which together are 0x0200 of the
   16-bit word.  The word is read unsigned -- MOV AX,[0x00069e20] with no sign
   extension anywhere in the body -- and the mask makes the comparison
   bit-for-bit either way, so no branch here turns on signedness (contract C).

   The result is 16 bits wide.  MOVZX EAX,AX at 0003c6db has already cleared
   the top half of EAX by the time SETZ AL writes the answer, and MOVZX AX,AL
   at 0003c6e3 clears AH, so what comes back is 0 or 1 in a zero-extended
   16-bit register: the unsigned short the two routines that end in this
   predicate hand on to their own callers.

   Neither of those routines reaches this body with a CALL.  fdps_cd_close_tray
   has no RET and runs off its own end at 0003c6cf into this one, and
   fdps_cd_audio_is_idle jumps here at 0003c6f7.  Both are the same test on the
   same word, so both are written as a call to it: the value handed back is
   identical and only the instruction that gets there differs (ADR-0001). */
unsigned short fdps_cd_status_is_not_busy(void)
{
    return (data_fdps_cd_last_request_status & 0x0200) == 0;
}
