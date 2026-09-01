/* tests/cd.c -- cover for src/cd.c.
 *
 * One section per function -- fdps_cd_alloc_dos_buffers at 0003bade,
 * fdps_cd_device_request at 0003bb7d, fdps_cd_read_head_sector at 0003bce2 and
 * fdps_cd_read_audio_channel_info at 0003bd99 -- and each says where its own
 * expected values come from.
 *
 * Expected values come from the assembly at 0003bade -- MOV word ptr
 * [0x00069dc8],0x100 and MOV word ptr [0x00069dcc],0x20 into the input
 * register block that starts at 0x00069dc8, PUSH 0xc / PUSH 0 / PUSH 0x69df0 /
 * CALL memset, the two PUSH 0x31 / CALL int386x pairs, and the four stores
 * that publish the results (0003bb2a, 0003bb3d, 0003bb65, 0003bb77) -- plus
 * the two symbol sizes ghidra_snapshot records for the register blocks, 28 for
 * a union REGS and 12 for a struct SREGS.  None of them is read off the
 * emitted C.
 *
 * The DPMI allocation is made for real: the test executable runs under DOS/4GW
 * inside DOSBox-X, which is the same DPMI host the game has, so INT 31h
 * function 0100h is answered by the same code that answers it in the game.  A
 * fabricated stand-in would only prove the arithmetic agrees with itself.  The
 * two 512-byte blocks each call allocates are never freed, here as in the
 * game.
 *
 * Nothing below asserts what any of the globals holds on its own -- ticket 23
 * owns their contents.  Every expectation is either a relationship between two
 * values the function itself wrote, or a poison value the test put there.
 */
#include <stddef.h>
#include <string.h>
#include <i86.h>
#include "testharn.h"
#include "gamedata.h"
#include "cd.h"

/* Fill the two shared register blocks with values the function is expected to
   overwrite, and values it is expected to leave alone, then run it.

   Only the 16-bit members are poisoned, so the filler halves of the input
   block stay zero and the interrupt is issued with the same full-width EAX and
   EBX the game issues it with.  CX and DX reach the interrupt carrying the
   poison; DPMI function 0100h reads neither.  ES and DS are left alone
   because int386x loads them into the segment registers -- an invalid selector
   there would fault before the interrupt ever happened -- and the function
   zeroes them itself anyway. */
static void alloc_with_poisoned_state(void)
{
    data_fdps_cd_int_regs_in.w.ax = 0x999;
    data_fdps_cd_int_regs_in.w.bx = 0x888;
    data_fdps_cd_int_regs_in.w.cx = 0x1234;
    data_fdps_cd_int_regs_in.w.dx = 0x5678;
    data_fdps_cd_int_sregs.cs = 0x1111;
    data_fdps_cd_int_sregs.ss = 0x2222;
    data_fdps_cd_int_sregs.fs = 0x3333;
    data_fdps_cd_int_sregs.gs = 0x4444;
    fdps_cd_alloc_dos_buffers();
}

/* The two register blocks are addressed as whole objects by the original --
   PUSH 0x69dc8, PUSH 0x69dac, PUSH 0x69df0 hand int386x their bases -- so
   their sizes are what decides whether the callee reads past them into the
   neighbouring globals (contract B).  The field the second store lands on is
   pinned the same way: 0x00069dcc is the input block's base plus four, which
   is where the 32-bit union REGS puts w.bx and nowhere else. */
static void cd_register_blocks_have_the_image_layout(void)
{
    union REGS probe;

    CHECK_EQ((int) sizeof(union REGS), 28);
    CHECK_EQ((int) sizeof(struct SREGS), 12);
    CHECK_EQ((int) ((char *) &probe.x.eax - (char *) &probe), 0);
    CHECK_EQ((int) ((char *) &probe.w.ax - (char *) &probe), 0);
    CHECK_EQ((int) ((char *) &probe.w.bx - (char *) &probe), 4);
}

/* MOV word ptr [0x00069dc8],0x100 / MOV word ptr [0x00069dcc],0x20 are the
   only two stores the body makes into the input block, and int386x never
   writes to it, so CX and DX come back holding the poison. */
static void cd_alloc_fills_the_dpmi_request(void)
{
    alloc_with_poisoned_state();
    CHECK_EQ(data_fdps_cd_int_regs_in.w.ax, 0x100);
    CHECK_EQ(data_fdps_cd_int_regs_in.w.bx, 0x20);
    CHECK_EQ(data_fdps_cd_int_regs_in.w.cx, 0x1234);
    CHECK_EQ(data_fdps_cd_int_regs_in.w.dx, 0x5678);
}

/* PUSH 0xc / PUSH 0x0 / PUSH 0x69df0 / CALL memset clears all twelve bytes of
   the segment register block before the first interrupt.  int386x writes back
   only ES at +0 and DS at +6 (0004e27a, 0004e276), so the four members it
   never touches are the ones that show the memset happened -- and they show it
   only because the test poisoned them first: the block is zero-filled bss
   otherwise. */
static void cd_alloc_clears_the_segment_registers(void)
{
    alloc_with_poisoned_state();
    CHECK_EQ(data_fdps_cd_int_sregs.cs, 0);
    CHECK_EQ(data_fdps_cd_int_sregs.ss, 0);
    CHECK_EQ(data_fdps_cd_int_sregs.fs, 0);
    CHECK_EQ(data_fdps_cd_int_sregs.gs, 0);
}

/* MOV AX,[0x69dac] / MOV [0x69e54],AX publishes the segment, and MOV
   EAX,[0x69dac] / AND EAX,0xffff / SHL EAX,0x4 / MOV [0x69de8],EAX publishes
   the same 16 bits shifted left four.  The two are therefore locked together
   whatever DPMI returned, which is what makes this assertion independent of
   the host.  A zero segment would mean the allocation never happened: the
   caller reads exactly that as "already allocated" (CMP word ptr
   [0x00069e54],0x0 / JNZ at 0003c68e). */
static void cd_alloc_publishes_the_request_header_block(void)
{
    alloc_with_poisoned_state();
    CHECK_EQ(data_fdps_cd_request_header_real_mode_seg != 0, 1);
    CHECK_EQ((long) (unsigned long) data_fdps_cd_request_header_buffer,
             (long) ((unsigned long) data_fdps_cd_request_header_real_mode_seg
                     << 4));
}

/* MOVZX EAX,word ptr [0x69dac] / SHL EAX,0x10 packs the second block's segment
   into the high half with a zero offset below it, and the same AND 0xffff /
   SHL 0x4 pair as above publishes its flat address, so the packed pointer's
   high half shifted left four has to be the flat address.

   x.cflag is int386x's sign-extended carry (SBB EAX,EAX at 0004e26d): zero
   means the second DPMI allocation reported success, which is what makes the
   segment above a segment rather than an error code.  The two blocks are
   separate allocations and a DOS memory block cannot be handed out twice, so
   their flat addresses differ. */
static void cd_alloc_publishes_the_ioctl_block(void)
{
    alloc_with_poisoned_state();
    CHECK_EQ(data_fdps_cdrom_int_out_regs.x.cflag, 0);
    CHECK_EQ(data_fdps_cd_ioctl_buffer_real_mode_ptr & 0xffff, 0);
    CHECK_EQ((long) (unsigned long) data_fdps_cd_ioctl_buffer,
             (long) ((data_fdps_cd_ioctl_buffer_real_mode_ptr >> 16) << 4));
    CHECK_EQ(data_fdps_cd_ioctl_buffer != data_fdps_cd_request_header_buffer,
             1);
}

/* fdps_cd_device_request, 0003bb7d.
 *
 * The interrupt is issued for real, as above, and the drive it names is the
 * one the test puts in data_fdps_cdrom_drive_letter_index.  It is set to 0xff
 * -- past every drive letter there is -- so MSCDEX rejects the request on the
 * drive number before it ever follows ES:BX, and where no CD-ROM drive is
 * mounted at all there is no MSCDEX handler on INT 2Fh and the multiplex
 * returns untouched.  Either way nothing reads or writes the request header,
 * which is what makes running this against a real DPMI host safe on a machine
 * whose state the test does not control.
 *
 * What the function writes into the real mode call structure cannot be read
 * back after the call: DPMI function 0300h writes the register state the
 * real-mode handler returned back over the whole block, so every field of it
 * holds the interrupt's answer by the time the function returns.  The input
 * register block is the opposite -- int386x only reads it -- so the assertions
 * below are on that block, plus on the two struct layouts that decide where
 * the body's stores land.
 */
static void device_request_with_poisoned_state(void)
{
    data_fdps_cdrom_drive_letter_index = 0xff;
    data_fdps_cd_int_regs_in.x.eax = 0x99990999;
    data_fdps_cd_int_regs_in.x.ebx = 0x12340888;
    data_fdps_cd_int_regs_in.x.ecx = 0x43210777;
    data_fdps_cd_int_regs_in.x.edx = 0x5678;
    data_fdps_cd_int_sregs.cs = 0x1111;
    data_fdps_cd_int_sregs.ss = 0x2222;
    data_fdps_cd_int_sregs.fs = 0x3333;
    data_fdps_cd_int_sregs.gs = 0x4444;
    fdps_cd_device_request();
}

/* The four stores into the real mode call structure are at [0x69e3e],
   [0x69e3a], [0x69e32] and [0x69e44], which are +0x1c, +0x18, +0x10 and +0x22
   from the block's base at 0x69e22, and the block is cleared with a length of
   0x32.  The stores into the input register block are at +0, +4, +5 and +8 and
   the edi store at +0x14, all from 0x69dc8; the carry test reads [0x69dc4],
   which is +0x18 from the output block at 0x69dac.  If any of those
   displacements does not land on the member the C names, the emitted body
   writes a different field of the same block (contract H). */
static void cd_device_request_fields_sit_where_the_stores_land(void)
{
    union REGS probe;

    CHECK_EQ((int) sizeof(struct fdps_dpmi_real_mode_call), 0x32);
    CHECK_EQ((int) offsetof(struct fdps_dpmi_real_mode_call, eax), 0x1c);
    CHECK_EQ((int) offsetof(struct fdps_dpmi_real_mode_call, ecx), 0x18);
    CHECK_EQ((int) offsetof(struct fdps_dpmi_real_mode_call, ebx), 0x10);
    CHECK_EQ((int) offsetof(struct fdps_dpmi_real_mode_call, es), 0x22);
    CHECK_EQ((int) ((char *) &probe.h.bl - (char *) &probe), 4);
    CHECK_EQ((int) ((char *) &probe.h.bh - (char *) &probe), 5);
    CHECK_EQ((int) ((char *) &probe.w.cx - (char *) &probe), 8);
    CHECK_EQ((int) ((char *) &probe.x.edi - (char *) &probe), 0x14);
    CHECK_EQ((int) ((char *) &probe.x.cflag - (char *) &probe), 0x18);
}

/* MOV word ptr [0x69dc8],0x300 asks DPMI for Simulate Real Mode Interrupt, MOV
   byte ptr [0x69dcc],0x2f names INT 2Fh and MOV byte ptr [0x69dcd],0x0 clears
   the rest of BX, and MOV word ptr [0x69dd0],0x0 copies no words from the
   protected-mode stack.  Each of the three is narrower than the register it
   lands in -- word, byte, byte, word -- so the halves above them keep the
   poison, which is what pins the widths.  EDX is never written at all. */
static void cd_device_request_asks_dpmi_to_simulate_int_2f(void)
{
    device_request_with_poisoned_state();
    CHECK_EQ(data_fdps_cd_int_regs_in.w.ax, 0x300);
    CHECK_EQ((int) (data_fdps_cd_int_regs_in.x.eax >> 16), 0x9999);
    CHECK_EQ(data_fdps_cd_int_regs_in.h.bl, 0x2f);
    CHECK_EQ(data_fdps_cd_int_regs_in.h.bh, 0);
    CHECK_EQ((int) (data_fdps_cd_int_regs_in.x.ebx >> 16), 0x1234);
    CHECK_EQ(data_fdps_cd_int_regs_in.w.cx, 0);
    CHECK_EQ((int) (data_fdps_cd_int_regs_in.x.ecx >> 16), 0x4321);
    CHECK_EQ(data_fdps_cd_int_regs_in.x.edx, 0x5678);
}

/* MOV dword ptr [0x69ddc],0x69e22 hands DPMI the address of the real mode call
   structure, as the address of the symbol and never as a literal (contract E),
   and MOV DX,DS / MOV word ptr [0x69df0],DX puts the flat data selector beside
   it in ES.  int386x writes the post-interrupt ES back over +0, so the
   selector is asserted as the one the program is still running under rather
   than against a captured value. */
static void cd_device_request_points_dpmi_at_the_call_block(void)
{
    device_request_with_poisoned_state();
    CHECK_EQ((long) data_fdps_cd_int_regs_in.x.edi,
             (long) (unsigned long) &data_fdps_cd_real_mode_call);
    CHECK_EQ(data_fdps_cd_int_sregs.es,
             FP_SEG((void __far *) &data_fdps_cd_real_mode_call));
}

/* PUSH 0xc / PUSH 0x0 / PUSH 0x69df0 / CALL memset clears all twelve bytes of
   the segment register block on the way in.  int386x writes back only ES at +0
   and DS at +6, so cs, ss, fs and gs are the four the clear has to have
   reached, and they show it only because the test poisoned them first. */
static void cd_device_request_clears_the_segment_registers(void)
{
    device_request_with_poisoned_state();
    CHECK_EQ(data_fdps_cd_int_sregs.cs, 0);
    CHECK_EQ(data_fdps_cd_int_sregs.ss, 0);
    CHECK_EQ(data_fdps_cd_int_sregs.fs, 0);
    CHECK_EQ(data_fdps_cd_int_sregs.gs, 0);
}

/* CMP dword ptr [0x69dc4],0x0 / JZ is the body's only branch, and it is on the
   DPMI call's carry rather than on anything the CD-ROM driver said.  DPMI
   0300h returns with carry clear whenever it could issue the interrupt at all,
   which it can here -- a valid interrupt number and a call block inside the
   program's own data -- so the branch is taken and "DEVICE REQUEST FAILED!!!"
   is not printed.  A non-zero cflag would mean the emitted C had handed DPMI
   something the original does not. */
static void cd_device_request_reports_only_a_failed_dpmi_call(void)
{
    device_request_with_poisoned_state();
    CHECK_EQ(data_fdps_cdrom_int_out_regs.x.cflag, 0);
}

/* fdps_cd_read_head_sector, 0003bce2.
 *
 * The request goes out for real, at the same drive letter index 0xff the
 * device-request tests above use, so MSCDEX rejects it on the drive number and
 * never follows ES:BX into the request header.  That is what makes the staged
 * bytes readable afterwards: the two DOS blocks still hold exactly what the
 * function put there, and the function's own read-back copied them into its
 * locals unchanged.
 *
 * So the assertions are of two kinds.  The staged bytes are checked against the
 * immediates in the body -- MOV byte ptr [ESP],0x1e, [ESP+1],0 and [ESP+2],3
 * at 0003bcef..0003bcf8, MOV dword ptr [ESP+0x16],0 and word ptr [ESP+0x14],0,
 * MOV byte ptr [ESP+0xd],0, the transfer address loaded from [0x00069da8],
 * MOV word ptr [ESP+0x12],6, and MOV byte ptr [ESP+0x20],1 / [ESP+0x21],0 for
 * the control block.  The published status and the returned sector are checked
 * against the bytes they were read out of, because what the driver leaves in
 * those two fields is not the test's to decide: the point being pinned is the
 * displacement and the width, MOV EAX,[ESP+3] / MOV [0x00069e20],AX for the
 * status and MOV EAX,[ESP+0x22] for the answer.
 *
 * Nothing here asserts a particular sector number.  A rejected request leaves
 * the field holding whatever the stack held, and a machine with a real disc
 * would put its own head position there.
 */
static unsigned int staged_dword(unsigned char *block, int offset)
{
    unsigned int value;

    memcpy(&value, block + offset, 4);
    return value;
}

static unsigned short staged_word(unsigned char *block, int offset)
{
    unsigned short value;

    memcpy(&value, block + offset, 2);
    return value;
}

/* The DOS blocks are allocated the way the module allocates them, and only if
   they are not there yet -- data_fdps_cd_request_header_real_mode_seg is the
   module's own "already allocated" flag.  Allocating again would work but would
   leak the previous pair, which nothing in the module can free. */
static unsigned int read_head_sector_from_a_rejected_drive(void)
{
    if (data_fdps_cd_request_header_real_mode_seg == 0) {
        fdps_cd_alloc_dos_buffers();
    }
    data_fdps_cdrom_drive_letter_index = 0xff;
    return fdps_cd_read_head_sector();
}

/* Every store the body makes into the request header is an ESP displacement,
   so the struct's offsets are what decides which field each one lands on
   (contract H).  The nine below are the displacements at 0003bcef..0003bd1a
   plus the status read at 0003bd87, and the size is the 26 documented bytes --
   which is four short of the 0x1e the header declares itself to be, so a struct
   that had grown to 30 would silently swallow the over-declaration this
   function is supposed to reproduce. */
static void cd_head_sector_header_fields_sit_where_the_stores_land(void)
{
    CHECK_EQ((int) sizeof(struct fdps_cd_request_header), 26);
    CHECK_EQ((int) offsetof(struct fdps_cd_request_header, header_length), 0);
    CHECK_EQ((int) offsetof(struct fdps_cd_request_header, subunit), 1);
    CHECK_EQ((int) offsetof(struct fdps_cd_request_header, command), 2);
    CHECK_EQ((int) offsetof(struct fdps_cd_request_header, status), 3);
    CHECK_EQ((int) offsetof(struct fdps_cd_request_header, media_descriptor),
             0xd);
    CHECK_EQ((int) offsetof(struct fdps_cd_request_header, transfer_address),
             0xe);
    CHECK_EQ((int) offsetof(struct fdps_cd_request_header,
                            transfer_byte_count), 0x12);
    CHECK_EQ((int) offsetof(struct fdps_cd_request_header, start_sector), 0x14);
    CHECK_EQ((int) offsetof(struct fdps_cd_request_header, volume_id_ptr),
             0x16);
}

/* Command code 3 is IOCTL Input, and the transfer it describes is six bytes
   into the second DOS block: the address field carries the packed real-mode far
   pointer the module keeps for exactly this, not the flat pointer, because the
   driver runs in real mode.  Start sector and volume-ID pointer are zero
   because an IOCTL request transfers no disc data. */
static void cd_head_sector_stages_an_ioctl_input_request(void)
{
    unsigned char *header;

    read_head_sector_from_a_rejected_drive();
    header = data_fdps_cd_request_header_buffer;
    CHECK_EQ(header[0], 0x1e);
    CHECK_EQ(header[1], 0);
    CHECK_EQ(header[2], 3);
    CHECK_EQ(header[0xd], 0);
    CHECK_EQ((long) staged_dword(header, 0xe),
             (long) data_fdps_cd_ioctl_buffer_real_mode_ptr);
    CHECK_EQ(staged_word(header, 0x12), 6);
    CHECK_EQ(staged_word(header, 0x14), 0);
    CHECK_EQ((long) staged_dword(header, 0x16), 0L);
}

/* Control block code 1 is Location of Head and the byte after it is the
   addressing mode, 0 for HSG -- which is the whole reason this function returns
   a sector number instead of a minute/second/frame triple. */
static void cd_head_sector_asks_for_the_head_location_in_hsg(void)
{
    read_head_sector_from_a_rejected_drive();
    CHECK_EQ(data_fdps_cd_ioctl_buffer[0], 1);
    CHECK_EQ(data_fdps_cd_ioctl_buffer[1], 0);
}

/* Both blocks are copied back out of the DOS memory before anything is read out
   of them, so the two values the function produces have to agree with the bytes
   still sitting in those blocks: the status with the word at header+3, and the
   returned sector with the dword at control block +2.  Reading the status one
   byte later, or the answer from the block's start, would break both. */
static void cd_head_sector_publishes_status_and_returns_the_block_dword(void)
{
    unsigned int head_sector;

    head_sector = read_head_sector_from_a_rejected_drive();
    CHECK_EQ(data_fdps_cd_last_request_status,
             staged_word(data_fdps_cd_request_header_buffer, 3));
    CHECK_EQ((long) head_sector,
             (long) staged_dword(data_fdps_cd_ioctl_buffer, 2));
}

/* fdps_cd_read_audio_channel_info, 0003bd99.
 *
 * Same arrangement as the head-sector cover above: the request is issued for
 * real at drive letter index 0xff, which MSCDEX rejects on the drive number
 * before it follows ES:BX, and where no CD-ROM drive is mounted there is no
 * MSCDEX handler on INT 2Fh at all.  Neither case touches the two DOS blocks,
 * so what is sitting in them afterwards is exactly what the function staged.
 *
 * That is what lets the two directions be told apart.  The caller's nine bytes
 * are poisoned with a known ramp before the call, so the bytes found in the
 * IOCTL block afterwards are the ones the function sent; and the tenth byte of
 * each buffer is poisoned too, so a copy that ran for ten bytes instead of
 * nine would show.  The expected values are the immediates in the body -- MOV
 * byte ptr [ESP],0x1a, [ESP+1],0 and [ESP+2],3 at 0003bda6..0003bdb4, MOV
 * dword ptr [ESP+0x16],0 and word ptr [ESP+0x14],0, MOV byte ptr [ESP+0xd],0,
 * the transfer address loaded from [0x00069da8], MOV word ptr [ESP+0x12],9,
 * MOV byte ptr [EAX],0x4 for the control block code, and the PUSH 0x1a / PUSH
 * 0x9 pairs that give the four copy lengths.
 *
 * Nothing here asserts a particular routing or volume.  A rejected request
 * leaves the reply bytes holding the question that was asked, and a machine
 * with a real drive would answer with its own mixer settings.
 */
static unsigned char audio_channel_probe[16];

/* The ramp starts at 0xa0 so every byte is distinct and none of them is 4, the
   code the function stamps into byte 0 -- otherwise the stamp would be
   invisible.  The tenth byte of the IOCTL block is poisoned separately because
   it is the one the send memcpy must not reach. */
static void read_audio_channel_info_from_a_rejected_drive(void)
{
    int i;

    if (data_fdps_cd_request_header_real_mode_seg == 0) {
        fdps_cd_alloc_dos_buffers();
    }
    data_fdps_cdrom_drive_letter_index = 0xff;
    for (i = 0; i < 16; i++) {
        audio_channel_probe[i] = (unsigned char) (0xa0 + i);
    }
    data_fdps_cd_ioctl_buffer[9] = 0x5a;
    fdps_cd_read_audio_channel_info(audio_channel_probe);
}

/* Command code 3 is IOCTL Input and the transfer it describes is nine bytes
   into the second DOS block, addressed by the packed real-mode far pointer and
   not by the flat one.  Start sector and volume-ID pointer are zero because an
   IOCTL request moves no disc data.

   The declared header length is 0x1a here, not the 0x1e fdps_cd_read_head_sector
   declares, and 0x1a is exactly the record: this function stages the whole
   header and nothing past it, so the length byte and the struct size have to
   agree.  A struct that had grown would break that equality rather than
   silently sending live stack. */
static void cd_audio_channel_stages_an_ioctl_input_request(void)
{
    unsigned char *header;

    read_audio_channel_info_from_a_rejected_drive();
    header = data_fdps_cd_request_header_buffer;
    CHECK_EQ(header[0], 0x1a);
    CHECK_EQ(header[0], (int) sizeof(struct fdps_cd_request_header));
    CHECK_EQ(header[1], 0);
    CHECK_EQ(header[2], 3);
    CHECK_EQ(header[0xd], 0);
    CHECK_EQ((long) staged_dword(header, 0xe),
             (long) data_fdps_cd_ioctl_buffer_real_mode_ptr);
    CHECK_EQ(staged_word(header, 0x12), 9);
    CHECK_EQ(staged_word(header, 0x14), 0);
    CHECK_EQ((long) staged_dword(header, 0x16), 0L);
}

/* MOV EAX,dword ptr [ESP+0x20] / MOV byte ptr [EAX],0x4 stamps the control
   block code into the caller's own buffer before anything is copied, so the 4
   has to be visible in both the staged block and the caller's buffer
   afterwards -- the caller's copy is the stamp itself, the staged copy is that
   stamp having been sent. */
static void cd_audio_channel_stamps_the_control_block_code(void)
{
    read_audio_channel_info_from_a_rejected_drive();
    CHECK_EQ(data_fdps_cd_ioctl_buffer[0], 4);
    CHECK_EQ(audio_channel_probe[0], 4);
}

/* PUSH 0x9 / PUSH [ESP+0x24] / PUSH [0x00069da4] / CALL memcpy sends the
   caller's block as it stands: bytes 1..8 are never cleared or rewritten, so
   the ramp the test put there is what reaches the driver.  The tenth byte of
   the IOCTL block keeps its own poison, which is what pins the length at nine
   rather than at the ten or eleven other control blocks in this family use. */
static void cd_audio_channel_sends_the_callers_own_nine_bytes(void)
{
    int i;

    read_audio_channel_info_from_a_rejected_drive();
    for (i = 1; i < 9; i++) {
        CHECK_EQ(data_fdps_cd_ioctl_buffer[i], 0xa0 + i);
    }
    CHECK_EQ(data_fdps_cd_ioctl_buffer[9], 0x5a);
}

/* PUSH 0x9 / PUSH [0x00069da4] / PUSH [ESP+0x28] and the tail jump into the
   shared epilogue at 0003c590 copy the block back the other way, into the
   caller's buffer and nowhere else.  All nine bytes have to match the block
   they came from, and the caller's tenth byte has to still hold its ramp
   value: a read-back of ten would overwrite it. */
static void cd_audio_channel_copies_nine_bytes_back_to_the_caller(void)
{
    int i;

    read_audio_channel_info_from_a_rejected_drive();
    for (i = 0; i < 9; i++) {
        CHECK_EQ(audio_channel_probe[i], data_fdps_cd_ioctl_buffer[i]);
    }
    CHECK_EQ(audio_channel_probe[9], 0xa9);
}

/* The header is copied back out of the DOS block before the status is read, so
   the published word has to be the word still sitting at header+3.  The
   function returns nothing, so this global is the only thing it produces
   besides the caller's buffer; reading it a byte early or late would break the
   equality whatever the driver left there. */
static void cd_audio_channel_publishes_the_request_status(void)
{
    read_audio_channel_info_from_a_rejected_drive();
    CHECK_EQ(data_fdps_cd_last_request_status,
             staged_word(data_fdps_cd_request_header_buffer, 3));
}

void run_cd_tests(void)
{
    RUN_TEST(cd_register_blocks_have_the_image_layout);
    RUN_TEST(cd_alloc_fills_the_dpmi_request);
    RUN_TEST(cd_alloc_clears_the_segment_registers);
    RUN_TEST(cd_alloc_publishes_the_request_header_block);
    RUN_TEST(cd_alloc_publishes_the_ioctl_block);
    RUN_TEST(cd_device_request_fields_sit_where_the_stores_land);
    RUN_TEST(cd_device_request_asks_dpmi_to_simulate_int_2f);
    RUN_TEST(cd_device_request_points_dpmi_at_the_call_block);
    RUN_TEST(cd_device_request_clears_the_segment_registers);
    RUN_TEST(cd_device_request_reports_only_a_failed_dpmi_call);
    RUN_TEST(cd_head_sector_header_fields_sit_where_the_stores_land);
    RUN_TEST(cd_head_sector_stages_an_ioctl_input_request);
    RUN_TEST(cd_head_sector_asks_for_the_head_location_in_hsg);
    RUN_TEST(cd_head_sector_publishes_status_and_returns_the_block_dword);
    RUN_TEST(cd_audio_channel_stages_an_ioctl_input_request);
    RUN_TEST(cd_audio_channel_stamps_the_control_block_code);
    RUN_TEST(cd_audio_channel_sends_the_callers_own_nine_bytes);
    RUN_TEST(cd_audio_channel_copies_nine_bytes_back_to_the_caller);
    RUN_TEST(cd_audio_channel_publishes_the_request_status);
}
