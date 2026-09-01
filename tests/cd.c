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

/* fdps_cd_set_audio_channel_control, 0003be36.
 *
 * Same arrangement again: the request is issued for real at drive letter index
 * 0xff, which MSCDEX rejects on the drive number before it follows ES:BX, and
 * where no CD-ROM drive is mounted there is no MSCDEX handler on INT 2Fh at
 * all.  Neither case touches the two DOS blocks, so what is in them afterwards
 * is exactly what the function staged.
 *
 * Expected values are the immediates in the body -- MOV byte ptr [ESP],0x18,
 * [ESP+1],0 and [ESP+2],0xc at 0003be4a..0003be58, MOV byte ptr [ESP+0xd],0,
 * the transfer address loaded from [0x00069da8], MOV word ptr [ESP+0x12],9,
 * MOV byte ptr [EAX],0x3 for the control block code at 0003be47, and the PUSH
 * 0x18 / PUSH 0x9 pair that gives the two send lengths.
 *
 * Two things separate this function from the IOCTL Input builders above and
 * both are asserted below.  It declares 0x18 where the record is 0x1a, and it
 * never stores to [ESP+0x14] or [ESP+0x16], so the two fields the siblings zero
 * go out holding live stack -- the poison at the far end of the DOS block is
 * what pins the send length at twenty-four rather than twenty-six, and nothing
 * asserts what lands in start_sector itself, because nothing may.  And it makes
 * no read-back of the control block: there are three CALL memcpy in the body,
 * not four.
 */
static unsigned char audio_control_probe[16];

/* The ramp starts at 0xa0 so every byte is distinct and none is 3, the code
   the function stamps into byte 0.  Byte 9 of the IOCTL block and bytes 0x18
   and 0x19 of the request-header block are poisoned separately: they are the
   two bytes just past the end of each send, so they are what shows a copy that
   ran one record too long. */
static void set_audio_channel_control_on_a_rejected_drive(void)
{
    int i;

    if (data_fdps_cd_request_header_real_mode_seg == 0) {
        fdps_cd_alloc_dos_buffers();
    }
    data_fdps_cdrom_drive_letter_index = 0xff;
    for (i = 0; i < 16; i++) {
        audio_control_probe[i] = (unsigned char) (0xa0 + i);
    }
    data_fdps_cd_ioctl_buffer[9] = 0x5a;
    data_fdps_cd_request_header_buffer[0x18] = 0x5b;
    data_fdps_cd_request_header_buffer[0x19] = 0x5c;
    fdps_cd_set_audio_channel_control(audio_control_probe);
}

/* Command code 0x0c is IOCTL Output -- the one field that makes this the write
   direction -- and the transfer it describes is nine bytes out of the second
   DOS block, addressed by the packed real-mode far pointer and not by the flat
   one. */
static void cd_set_audio_channel_stages_an_ioctl_output_request(void)
{
    unsigned char *header;

    set_audio_channel_control_on_a_rejected_drive();
    header = data_fdps_cd_request_header_buffer;
    CHECK_EQ(header[0], 0x18);
    CHECK_EQ(header[1], 0);
    CHECK_EQ(header[2], 0xc);
    CHECK_EQ(header[0xd], 0);
    CHECK_EQ((long) staged_dword(header, 0xe),
             (long) data_fdps_cd_ioctl_buffer_real_mode_ptr);
    CHECK_EQ(staged_word(header, 0x12), 9);
}

/* The declared length is two short of the record, and the send copies that
   declared length: PUSH 0x18 at 0003be6d, against a struct of 0x1a.  So the
   two bytes at the end of the record are never staged, and the poison the setup
   put at 0x18 and 0x19 is still there -- which is the only way to see the
   under-declaration from outside, since what does get sent in start_sector is
   uninitialised stack and cannot be asserted at all. */
static void cd_set_audio_channel_sends_two_bytes_short_of_the_record(void)
{
    set_audio_channel_control_on_a_rejected_drive();
    CHECK_EQ(data_fdps_cd_request_header_buffer[0] + 2,
             (int) sizeof(struct fdps_cd_request_header));
    CHECK_EQ(data_fdps_cd_request_header_buffer[0x18], 0x5b);
    CHECK_EQ(data_fdps_cd_request_header_buffer[0x19], 0x5c);
}

/* MOV EAX,dword ptr [ESP+0x1c] / MOV byte ptr [EAX],0x3 stamps the control
   block code into the caller's own buffer before anything is copied, so the 3
   is visible in both the caller's buffer and the staged block. */
static void cd_set_audio_channel_stamps_the_control_block_code(void)
{
    set_audio_channel_control_on_a_rejected_drive();
    CHECK_EQ(audio_control_probe[0], 3);
    CHECK_EQ(data_fdps_cd_ioctl_buffer[0], 3);
}

/* PUSH 0x9 / PUSH [ESP+0x20] / PUSH [0x00069da4] / CALL memcpy sends the
   caller's block as it stands: bytes 1..8 are never cleared or rewritten, so
   the ramp the test put there is what reaches the driver, and the tenth byte
   of the IOCTL block keeps its own poison. */
static void cd_set_audio_channel_sends_the_callers_own_nine_bytes(void)
{
    int i;

    set_audio_channel_control_on_a_rejected_drive();
    for (i = 1; i < 9; i++) {
        CHECK_EQ(data_fdps_cd_ioctl_buffer[i], 0xa0 + i);
    }
    CHECK_EQ(data_fdps_cd_ioctl_buffer[9], 0x5a);
}

/* There is no fourth memcpy: the body's three CALL 0x000435bc are the header
   out, the control block out, and the header back, and the control block is
   never read back.  So the caller's bytes 1..8 still hold the ramp afterwards,
   where fdps_cd_read_audio_channel_info would have overwritten all nine with
   whatever the DOS block held.  This is the assertion that tells the two
   directions apart. */
static void cd_set_audio_channel_leaves_the_callers_block_alone(void)
{
    int i;

    set_audio_channel_control_on_a_rejected_drive();
    for (i = 1; i < 16; i++) {
        CHECK_EQ(audio_control_probe[i], 0xa0 + i);
    }
}

/* The header is copied back out of the DOS block before the status is read --
   for its own length byte's worth of bytes, which covers offset 3 -- so the
   published word has to be the word still sitting at header+3.  MOV EAX,[ESP+3]
   / MOV [0x00069e20],AX is a 16-bit store out of a dword load, so a status read
   a byte early or late, or one that let the high half through, would break the
   equality whatever the driver left there. */
static void cd_set_audio_channel_publishes_the_request_status(void)
{
    set_audio_channel_control_on_a_rejected_drive();
    CHECK_EQ(data_fdps_cd_last_request_status,
             staged_word(data_fdps_cd_request_header_buffer, 3));
}

/* fdps_cdrom_read_device_status, 0003c34f.
 *
 * Same arrangement as the three request builders above: the request is issued
 * for real at drive letter index 0xff, which MSCDEX rejects on the drive number
 * before it follows ES:BX, and where no CD-ROM drive is mounted there is no
 * MSCDEX handler on INT 2Fh at all.  Neither case touches the two DOS blocks,
 * so what is in them afterwards is exactly what the function staged.
 *
 * Expected values are the immediates in the body -- MOV byte ptr [ESP],0x1a,
 * [ESP+1],0 and [ESP+2],3 at 0003c35c..0003c365, MOV dword ptr [ESP+0x16],0 and
 * word ptr [ESP+0x14],0, MOV byte ptr [ESP+0xd],0, the transfer address loaded
 * from [0x00069da8], MOV word ptr [ESP+0x12],5, MOV byte ptr [ESP+0x1c],0x6 for
 * the control block code, and the four PUSH 0x1a / PUSH 0x5 lengths at
 * 0003c393, 0003c3a8, 0003c3c2 and 0003c3d7.
 *
 * Nothing below asserts what the driver answered.  Bytes 1..4 of the control
 * block are never initialised by the body, so on a rejected request they hold
 * whatever the frame held, and on a machine with a real drive they hold that
 * drive's own device status.  The two answers are therefore checked against the
 * bytes they were read out of: what is pinned is the displacement and the
 * width, MOV EAX,[ESP+0x1d] / MOV [0x00069e1c],EAX for the device status and
 * MOV EAX,[ESP+3] / MOV [0x00069e20],AX for the request status.
 */
static void read_device_status_from_a_rejected_drive(void)
{
    if (data_fdps_cd_request_header_real_mode_seg == 0) {
        fdps_cd_alloc_dos_buffers();
    }
    data_fdps_cdrom_drive_letter_index = 0xff;
    data_fdps_cd_ioctl_buffer[5] = 0x5a;
    data_fdps_cd_request_header_buffer[0x1a] = 0x5b;
    fdps_cdrom_read_device_status();
}

/* Command code 3 is IOCTL Input and the transfer it describes is five bytes
   into the second DOS block, addressed by the packed real-mode far pointer and
   not by the flat one.  Start sector and volume-ID pointer are zero because an
   IOCTL request moves no disc data.

   The declared length is 0x1a, exactly the record, so the length byte and the
   struct size have to agree; and the byte just past the record keeps the poison
   the setup put there, which is what pins the staging copy at twenty-six rather
   than at the 0x1e fdps_cd_read_head_sector sends. */
static void cd_device_status_stages_an_ioctl_input_request(void)
{
    unsigned char *header;

    read_device_status_from_a_rejected_drive();
    header = data_fdps_cd_request_header_buffer;
    CHECK_EQ(header[0], 0x1a);
    CHECK_EQ(header[0], (int) sizeof(struct fdps_cd_request_header));
    CHECK_EQ(header[1], 0);
    CHECK_EQ(header[2], 3);
    CHECK_EQ(header[0xd], 0);
    CHECK_EQ((long) staged_dword(header, 0xe),
             (long) data_fdps_cd_ioctl_buffer_real_mode_ptr);
    CHECK_EQ(staged_word(header, 0x12), 5);
    CHECK_EQ(staged_word(header, 0x14), 0);
    CHECK_EQ((long) staged_dword(header, 0x16), 0L);
    CHECK_EQ(header[0x1a], 0x5b);
}

/* Control block code 6 is Device Status.  MOV byte ptr [ESP+0x1c],0x6 is the
   body's only store into the block, so nothing may be asserted about bytes
   1..4 on the way out; the sixth byte of the DOS block is what pins the send
   length at five, because a copy that ran one byte long would take the poison
   with it. */
static void cd_device_status_asks_for_the_device_status_block(void)
{
    read_device_status_from_a_rejected_drive();
    CHECK_EQ(data_fdps_cd_ioctl_buffer[0], 6);
    CHECK_EQ(data_fdps_cd_ioctl_buffer[5], 0x5a);
}

/* Both blocks are copied back out of the DOS memory before anything is read out
   of them, so the two published values have to agree with the bytes still
   sitting in those blocks: the device status with the dword at control block +1
   -- the last four of the five bytes, not the first four, which would start on
   the request code -- and the request status with the word at header+3.  The
   status store is a 16-bit store out of a dword load, so a read a byte early or
   late, or one that let the high half through, would break that equality
   whatever the driver left there.

   The request status is the one this function exists to refresh: its caller
   fdps_cd_audio_is_idle tests bit 0x0200 of it, so a body that stopped at the
   device status dword would leave it holding the previous request's word. */
static void cd_device_status_publishes_both_answers(void)
{
    read_device_status_from_a_rejected_drive();
    CHECK_EQ((long) data_fdps_cdrom_device_status,
             (long) staged_dword(data_fdps_cd_ioctl_buffer, 1));
    CHECK_EQ((int) (data_fdps_cdrom_device_status & 0xff),
             data_fdps_cd_ioctl_buffer[1]);
    CHECK_EQ((int) (data_fdps_cdrom_device_status >> 24),
             data_fdps_cd_ioctl_buffer[4]);
    CHECK_EQ(data_fdps_cd_last_request_status,
             staged_word(data_fdps_cd_request_header_buffer, 3));
}

/* fdps_cd_ioctl_output_command, 0003c51c.
 *
 * Same arrangement as the four request builders above: the request is issued
 * for real at drive letter index 0xff, which MSCDEX rejects on the drive number
 * before it follows ES:BX, and where no CD-ROM drive is mounted there is no
 * MSCDEX handler on INT 2Fh at all.  Neither case touches the two DOS blocks,
 * so what is in them afterwards is exactly what the function staged.
 *
 * Expected values are the immediates in the body -- MOV byte ptr [ESP],0x18,
 * [ESP+1],0 and [ESP+2],0xc at 0003c531..0003c53a, MOV byte ptr [ESP+0xd],0,
 * the transfer address loaded from [0x00069da8], MOV word ptr [ESP+0x12],1, the
 * MOV AL,byte ptr [ESP+0x20] / MOV byte ptr [ESP+0x18],AL pair at 0003c529 that
 * takes the argument a byte at a time, and the three lengths PUSH 0x18 at
 * 0003c554, PUSH 0x1 at 0003c569 and PUSH 0x18 at 0003c583.  The code 5 is what
 * the one caller pushes: PUSH 0x5 at 0003c6c6 in fdps_cd_close_tray.
 *
 * Nothing below asserts what the driver answered.  The status word is checked
 * against the bytes it was read out of, which is what pins the displacement and
 * the width: MOV EAX,[ESP+3] / MOV [0x00069e20],AX.
 */
static void ioctl_output_command_on_a_rejected_drive(int command_code)
{
    if (data_fdps_cd_request_header_real_mode_seg == 0) {
        fdps_cd_alloc_dos_buffers();
    }
    data_fdps_cdrom_drive_letter_index = 0xff;
    data_fdps_cd_ioctl_buffer[1] = 0x5a;
    data_fdps_cd_request_header_buffer[0x18] = 0x5b;
    data_fdps_cd_request_header_buffer[0x19] = 0x5c;
    fdps_cd_ioctl_output_command(command_code);
}

/* Command code 0x0c is IOCTL Output -- the field that makes this the write
   direction -- and the transfer it describes is one byte out of the second DOS
   block, addressed by the packed real-mode far pointer and not by the flat one.
   A byte count of anything but 1 would be a different request. */
static void cd_ioctl_output_stages_an_ioctl_output_request(void)
{
    unsigned char *header;

    ioctl_output_command_on_a_rejected_drive(5);
    header = data_fdps_cd_request_header_buffer;
    CHECK_EQ(header[0], 0x18);
    CHECK_EQ(header[1], 0);
    CHECK_EQ(header[2], 0xc);
    CHECK_EQ(header[0xd], 0);
    CHECK_EQ((long) staged_dword(header, 0xe),
             (long) data_fdps_cd_ioctl_buffer_real_mode_ptr);
    CHECK_EQ(staged_word(header, 0x12), 1);
}

/* The declared length is two short of the record, and the send copies that
   declared length: PUSH 0x18 at 0003c554, against a struct of 0x1a.  So the two
   bytes at the end of the record are never staged, and the poison the setup put
   at 0x18 and 0x19 is still there -- which is the only way to see the
   under-declaration from outside, since what does get sent in start_sector is
   uninitialised stack and cannot be asserted at all. */
static void cd_ioctl_output_sends_two_bytes_short_of_the_record(void)
{
    ioctl_output_command_on_a_rejected_drive(5);
    CHECK_EQ(data_fdps_cd_request_header_buffer[0] + 2,
             (int) sizeof(struct fdps_cd_request_header));
    CHECK_EQ(data_fdps_cd_request_header_buffer[0x18], 0x5b);
    CHECK_EQ(data_fdps_cd_request_header_buffer[0x19], 0x5c);
}

/* The whole control block is the one function-code byte, and PUSH 0x1 at
   0003c569 is the count the staging memcpy runs with, so the second byte of the
   DOS block keeps the poison the setup put there: a copy that ran even one byte
   long would take it with it. */
static void cd_ioctl_output_sends_the_code_as_a_one_byte_block(void)
{
    ioctl_output_command_on_a_rejected_drive(5);
    CHECK_EQ(data_fdps_cd_ioctl_buffer[0], 5);
    CHECK_EQ(data_fdps_cd_ioctl_buffer[1], 0x5a);
}

/* MOV AL,byte ptr [ESP+0x20] reads one byte of the pushed dword, so the block
   carries the argument's low eight bits and nothing else.  0x7f02 and 2 are the
   same order to the drive, Reset Drive, and 0x100 stamps a zero, Eject Disk --
   an argument taken as a full int would put a nonzero byte there for both. */
static void cd_ioctl_output_uses_only_the_low_byte_of_the_argument(void)
{
    ioctl_output_command_on_a_rejected_drive(0x7f02);
    CHECK_EQ(data_fdps_cd_ioctl_buffer[0], 2);
    ioctl_output_command_on_a_rejected_drive(0x100);
    CHECK_EQ(data_fdps_cd_ioctl_buffer[0], 0);
}

/* The header is copied back out of the DOS block before the status is read --
   PUSH 0x18 at 0003c583, a flat literal that covers offset 3 -- so the
   published word has to be the word still sitting at header+3.  MOV EAX,[ESP+3]
   / MOV [0x00069e20],AX is a 16-bit store out of a dword load, so a status read
   a byte early or late, or one that let the high half through, would break the
   equality whatever the driver left there. */
static void cd_ioctl_output_publishes_the_request_status(void)
{
    ioctl_output_command_on_a_rejected_drive(5);
    CHECK_EQ(data_fdps_cd_last_request_status,
             staged_word(data_fdps_cd_request_header_buffer, 3));
}

/* fdps_cdrom_detect, 0003c636.
 *
 * The interrupt is issued for real.  INT 2Fh AX=1500h is the MSCDEX
 * installation check, and the test executable runs under DOS/4GW inside
 * DOSBox-X with the game disc mounted whenever the image is on the host, so it
 * is answered by the same redirector the game asks -- or, on a host without the
 * image, by nobody, which leaves the multiplex registers untouched and is
 * exactly the case the function's first branch is for.
 *
 * Which of those two a run gets is not the test's to decide, so the tests ask
 * MSCDEX the same question themselves, into their own register blocks, and
 * every assertion below is a biconditional against that independent answer.
 * Nothing asserts that a CD-ROM drive was found, and nothing asserts a
 * particular drive count, drive letter or status word.
 *
 * Neither of the module's two shared register blocks can be read back for that
 * purpose.  Past the no-driver branch the body calls fdps_cdrom_read_disk_info,
 * which reaches fdps_cd_device_request, which builds its own DPMI request in
 * the same input block and lets int386x fill the same output block -- so by the
 * time the function returns, both hold the last device request and not the
 * installation check.  That is why the witness here is a local union REGS the
 * test filled itself.
 *
 * Expected values are the immediates and displacements in the body: MOV byte
 * ptr [0x00069dc9],0x15 and [0x00069dc8],0x0 and MOV word ptr [0x00069dcc],0x0
 * for the request, CMP word ptr [0x00069db0],0x0 for the no-driver branch, MOV
 * AX,[0x00069db0] / MOV [0x00069dfc],AX and MOV AL,[0x00069db4] / MOV
 * [0x00069dfe],AL for the two published values, CMP word ptr [0x00069e54],0x0
 * for the allocation guard, and MOVZX EAX,word ptr [0x00069e20] / CMP
 * EAX,0x810c for the result.  The control block code 0xa and the command byte 3
 * the disc-info read stages come from fdps_cdrom_read_disk_info at 0003bfa5.
 */

/* The same question the body asks, asked independently: AH = 0x15, AL = 0 and
   BX = 0 is the MSCDEX installation check, and it answers with the number of
   CD-ROM drives in BX and the drive letter index of the first in CL.  It is a
   pure query, so asking it again either side of a call changes nothing. */
static void mscdex_installation_check(union REGS *answer)
{
    union REGS request;

    memset(&request, 0, sizeof(union REGS));
    request.h.ah = 0x15;
    request.h.al = 0;
    request.w.bx = 0;
    int386(0x2f, &request, answer);
}

/* The module's input register block is poisoned in all four of its 32-bit
   members before each call, because the body writes only AH, AL and BX and
   writes each of them narrower than the register it lands in.  The two globals
   the body publishes are poisoned too, so a call that returned at the no-driver
   branch is visibly one that wrote neither. */
static short detect_with_poisoned_state(void)
{
    data_fdps_cd_int_regs_in.x.eax = 0x99990999;
    data_fdps_cd_int_regs_in.x.ebx = 0x88880777;
    data_fdps_cd_int_regs_in.x.ecx = 0x12345678;
    data_fdps_cd_int_regs_in.x.edx = 0x2468ace0;
    data_fdps_cdrom_drive_count = 0x5a5a;
    data_fdps_cdrom_drive_letter_index = 0x5b;
    return fdps_cdrom_detect();
}

/* The allocation guard is CMP word ptr [0x00069e54],0x0 / JNZ, and it can only
   be seen from outside on a call made while the DOS buffers are still
   unallocated -- which is why this test is registered before every other test
   in the file, and why it asserts that precondition rather than assuming it.
   tests/cd.c is the first test unit in the build's alphabetical order that
   touches any CD global, so the segment word really is the zero-filled bss it
   starts as.

   Allocation sits past the no-driver branch, so the buffers exist afterwards
   exactly when the interrupt reported a drive.  A body that allocated before
   testing BX would leak two DOS blocks on every machine with no CD-ROM
   redirector, and nothing in this module can free them. */
static void cd_detect_allocates_the_dos_buffers_on_the_first_call(void)
{
    union REGS answer;
    int buffers_were_unallocated;

    mscdex_installation_check(&answer);
    buffers_were_unallocated = (data_fdps_cd_request_header_real_mode_seg == 0);
    detect_with_poisoned_state();
    CHECK_EQ(buffers_were_unallocated, 1);
    CHECK_EQ(data_fdps_cd_request_header_real_mode_seg != 0, answer.w.bx != 0);
}

/* AH = 0x15 with AL = 0 is the MSCDEX installation check and BX = 0 is what it
   requires on entry.  The two byte stores land at [0x00069dc9] and
   [0x00069dc8], which are h.ah and h.al of the block that starts at 0x00069dc8
   and nothing else (contract H), and each of the three stores is narrower than
   the register it lands in -- byte, byte, word -- so the halves above them keep
   the poison.

   Those five facts are readable in the shared block afterwards exactly when the
   call returned at the no-driver branch, because that is the only path on which
   nothing runs after the interrupt; a call that found a drive leaves
   fdps_cd_device_request's own DPMI request there instead.  The biconditional
   says both halves of that at once.  On a machine that does have a drive, what
   pins the request is the next test: an installation check built any other way
   would not come back with the answer the test's own one did.

   DX is never written by anything in the chain, so its poison survives on both
   paths. */
static void cd_detect_issues_the_mscdex_installation_check(void)
{
    union REGS answer;
    union REGS probe;

    mscdex_installation_check(&answer);
    detect_with_poisoned_state();
    CHECK_EQ((int) ((char *) &probe.h.al - (char *) &probe), 0);
    CHECK_EQ((int) ((char *) &probe.h.ah - (char *) &probe), 1);
    CHECK_EQ((long) data_fdps_cd_int_regs_in.x.edx, 0x2468ace0L);
    CHECK_EQ(data_fdps_cd_int_regs_in.h.ah == 0x15
             && data_fdps_cd_int_regs_in.h.al == 0
             && data_fdps_cd_int_regs_in.w.bx == 0
             && (data_fdps_cd_int_regs_in.x.eax >> 16) == 0x9999
             && (data_fdps_cd_int_regs_in.x.ebx >> 16) == 0x8888,
             answer.w.bx == 0);
}

/* The count is BX and the letter index is CL: MOV AX,[0x00069db0] is a word out
   of the output block's +4 and MOV AL,[0x00069db4] is a byte out of its +8,
   which is where the 32-bit union REGS puts w.bx and h.cl and nowhere else
   (contract H).  Both stores sit past the no-driver branch, so on a machine
   with no redirector the two globals keep whatever they held -- the test's
   poison, here.

   Nothing asserts what the count or the letter is.  A machine with two CD-ROM
   drives and a machine with one both satisfy this; what is pinned is that the
   two globals hold what an installation check the test issued for itself came
   back with, in the two fields those two displacements name. */
static void cd_detect_publishes_the_drive_count_and_first_letter(void)
{
    union REGS answer;
    union REGS probe;

    mscdex_installation_check(&answer);
    detect_with_poisoned_state();
    CHECK_EQ((int) ((char *) &probe.w.bx - (char *) &probe), 4);
    CHECK_EQ((int) ((char *) &probe.h.cl - (char *) &probe), 8);
    if (answer.w.bx == 0) {
        CHECK_EQ(data_fdps_cdrom_drive_count, 0x5a5a);
        CHECK_EQ(data_fdps_cdrom_drive_letter_index, 0x5b);
    } else {
        CHECK_EQ(data_fdps_cdrom_drive_count, answer.w.bx);
        CHECK_EQ(data_fdps_cdrom_drive_letter_index, answer.h.cl);
    }
}

/* The three results and the two branches that pick between them, as three
   biconditionals: 0 exactly when the multiplex reported no drives, 2 exactly
   when it reported a drive and the disc-info request came back 0x810C, and 1
   exactly when it reported a drive and the request came back anything else.
   Read together they also say the function returns nothing but 0, 1 and 2.

   The 0x810C arm is a single equality and not a test of the driver's error bit
   0x8000, so a drive that refused the request with any other device error is
   reported as 1 -- the same answer as success.  That is in the original: main
   accepts 1 and quits on 0 and 2, so a disc the driver could not read for some
   other reason gets the game started. */
static void cd_detect_maps_the_interrupt_and_status_onto_its_result(void)
{
    union REGS answer;
    short result;

    mscdex_installation_check(&answer);
    result = detect_with_poisoned_state();
    CHECK_EQ(result == 0, answer.w.bx == 0);
    CHECK_EQ(result == 2,
             answer.w.bx != 0 && data_fdps_cd_last_request_status == 0x810c);
    CHECK_EQ(result == 1,
             answer.w.bx != 0 && data_fdps_cd_last_request_status != 0x810c);
}

/* CALL 0x0003bfa5 is the disc-info read, and it too sits past the no-driver
   branch.  Its own staging is what makes the call visible from outside: it
   writes command byte 3, IOCTL Input, into the request header block and control
   block code 0xa, Read Disk Info, into the IOCTL block.  Both are poisoned
   first, so on a machine with no redirector the poison survives -- which is the
   assertion that the call did not happen. */
static void cd_detect_reads_the_disc_info_past_the_no_driver_gate(void)
{
    union REGS answer;

    mscdex_installation_check(&answer);
    if (data_fdps_cd_request_header_real_mode_seg == 0) {
        fdps_cd_alloc_dos_buffers();
    }
    data_fdps_cd_ioctl_buffer[0] = 0x5a;
    data_fdps_cd_request_header_buffer[2] = 0x5b;
    detect_with_poisoned_state();
    CHECK_EQ(data_fdps_cd_ioctl_buffer[0] == 0xa, answer.w.bx != 0);
    CHECK_EQ(data_fdps_cd_request_header_buffer[2] == 3, answer.w.bx != 0);
}

/* MOVZX EAX,word ptr [0x00069e20] / CMP EAX,0x810c is a zero-extending compare,
   which is what an unsigned short promotes to.  A signed short would sign
   extend, giving -32500 where the comparison wants 33036, and the 0x810C arm
   could never be taken on any machine -- the failure would be invisible to
   every test that did not have a drive reporting exactly that status
   (contract C).  So the promotion is asserted directly, on a value the test
   put there. */
static void cd_detect_compares_the_status_word_zero_extended(void)
{
    data_fdps_cd_last_request_status = 0x810c;
    CHECK_EQ((int) sizeof(data_fdps_cd_last_request_status), 2);
    CHECK_EQ(data_fdps_cd_last_request_status == 0x810c, 1);
    CHECK_EQ((long) (data_fdps_cd_last_request_status + 0), 0x810cL);
}

/* fdps_cd_close_tray, 0003c6bc.
 *
 * The order goes out for real, at drive letter index 0xff, on the same
 * reasoning as the fdps_cd_ioctl_output_command cases above: MSCDEX rejects
 * the drive number before it follows ES:BX, and on a host with no CD-ROM
 * mounted nothing answers INT 2Fh at all.  No tray anywhere moves, and the two
 * DOS blocks come back holding exactly what the call staged in them.
 *
 * The expected value that belongs to this function and to no other is the
 * control block code: PUSH 0x5 at 0003c6c6, Close Tray in the MSCDEX IOCTL
 * Output table.  The request shape around it is fdps_cd_ioctl_output_command's
 * -- MOV byte ptr [ESP],0x18, [ESP+1],0, [ESP+2],0xc at 0003c531..0003c53a,
 * MOV word ptr [ESP+0x12],1, PUSH 0x1 at 0003c569 -- and is asserted here to
 * pin that CALL 0x0003c51c at 0003c6c8 is the builder this routine reaches the
 * drive through, rather than one of the other four in the module.
 *
 * The return value is not asserted.  The body has no RET: it runs off its own
 * end at 0003c6d0 into fdps_cd_status_is_not_busy, which this batch has not
 * emitted, so what a call to it answers today is the generated stub module's
 * zero and asserting on that would be testing the stub.
 */
static void close_tray_on_a_rejected_drive(void)
{
    if (data_fdps_cd_request_header_real_mode_seg == 0) {
        fdps_cd_alloc_dos_buffers();
    }
    data_fdps_cdrom_drive_letter_index = 0xff;
    data_fdps_cd_ioctl_buffer[0] = 0x71;
    data_fdps_cd_ioctl_buffer[1] = 0x72;
    data_fdps_cd_last_request_status = 0x7373;
    fdps_cd_close_tray();
}

/* PUSH 0x5 is the whole of the control block, and PUSH 0x1 is the count the
   staging memcpy runs with, so the second byte of the DOS block still holds the
   poison: a code of anything but 5 would be a different order to the drive --
   0 is Eject Disk and 2 is Reset Drive -- and a copy even one byte long would
   have taken the poison with it. */
static void cd_close_tray_sends_the_close_tray_code(void)
{
    close_tray_on_a_rejected_drive();
    CHECK_EQ(data_fdps_cd_ioctl_buffer[0], 5);
    CHECK_EQ(data_fdps_cd_ioctl_buffer[1], 0x72);
}

/* Command code 0x0c is IOCTL Output, the field that makes this the write
   direction, and the transfer it describes is one byte out of the second DOS
   block addressed by the packed real-mode far pointer.  That is
   fdps_cd_ioctl_output_command's request shape and no other builder in the
   module stages it, so these five fields are what say which CALL the body
   makes. */
static void cd_close_tray_goes_out_as_an_ioctl_output_request(void)
{
    unsigned char *header;

    close_tray_on_a_rejected_drive();
    header = data_fdps_cd_request_header_buffer;
    CHECK_EQ(header[0], 0x18);
    CHECK_EQ(header[1], 0);
    CHECK_EQ(header[2], 0xc);
    CHECK_EQ(header[0xd], 0);
    CHECK_EQ((long) staged_dword(header, 0xe),
             (long) data_fdps_cd_ioctl_buffer_real_mode_ptr);
    CHECK_EQ(staged_word(header, 0x12), 1);
}

/* The status word the tail call at 0003c6d0 goes on to test is the one this
   request published, not a leftover: MOV EAX,[ESP+3] / MOV [0x00069e20],AX
   overwrites it out of the header the driver was handed back, so the word in
   the global has to be the word still sitting at header+3.  The setup poisons
   the global first, so a body that never reached the builder at all would have
   to leave 0x7373 there and fail.  Nothing here asserts what that word says -- the driver's answer is the driver's, and on a
   host with no MSCDEX at all the field is never written by anybody. */
static void cd_close_tray_publishes_the_request_status(void)
{
    close_tray_on_a_rejected_drive();
    CHECK_EQ(data_fdps_cd_last_request_status,
             staged_word(data_fdps_cd_request_header_buffer, 3));
}

/* fdps_cd_status_is_not_busy at 0003c6d0 reads one word and returns a
 * predicate on one bit of it, so every case below is the same shape: put a
 * status word in data_fdps_cd_last_request_status, call, and check the answer.
 *
 * The expected answers come from the four instructions that compute them --
 * XOR AL,AL at 0003c6d6, AND AH,0x2 at 0003c6d8, TEST EAX,EAX and SETZ AL at
 * 0003c6de -- which mask the word down to 0x0200 alone and return the
 * inversion of it.  The status words fed in are the test's own: 0x0200 is the
 * DOS device driver request header's busy bit, 0x0100 its done bit, 0x8000 its
 * error flag and the low byte the driver's error code, and 0x810c is the
 * no-readable-disc failure fdps_cdrom_detect compares against.  Nothing here
 * asserts what the global holds on its own -- each case writes the word it
 * tests and puts the previous one back.
 */
static unsigned short not_busy_for(unsigned short status_word)
{
    unsigned short saved_status;
    unsigned short answer;

    saved_status = data_fdps_cd_last_request_status;
    data_fdps_cd_last_request_status = status_word;
    answer = fdps_cd_status_is_not_busy();
    data_fdps_cd_last_request_status = saved_status;
    return answer;
}

/* The busy bit alone decides the answer, and the answer is the inversion of
   it: SETZ on the masked word gives 1 for clear and 0 for set.  A word with
   the done bit up and busy down is the ordinary finished request and still
   answers 1, and the two together still answer 0, so nothing here reads as a
   test of "the request completed". */
static void cd_status_not_busy_inverts_the_busy_bit(void)
{
    CHECK_EQ(not_busy_for(0x0000), 1);
    CHECK_EQ(not_busy_for(0x0200), 0);
    CHECK_EQ(not_busy_for(0x0100), 1);
    CHECK_EQ(not_busy_for(0x0300), 0);
}

/* The mask is exactly 0x0200 and no wider.  XOR AL,AL throws the whole error
   code byte away before the AND, and AND AH,0x2 keeps one bit of the high
   byte, so a word with every other bit in it set is still not busy -- 0xfdff
   is that word -- and the failure status 0x810c, error flag and device error
   12 and all, reports not busy too.  Set the busy bit under a full error byte
   and the answer goes back to 0, which pins that the low byte is discarded
   rather than folded in. */
static void cd_status_not_busy_ignores_every_other_bit(void)
{
    CHECK_EQ(not_busy_for(0xfdff), 1);
    CHECK_EQ(not_busy_for(0x00ff), 1);
    CHECK_EQ(not_busy_for(0x810c), 1);
    CHECK_EQ(not_busy_for(0x02ff), 0);
}

/* fdps_cd_read_media_change_status, 0003c6f9.
 *
 * Same arrangement as the four request builders above: the request is issued
 * for real at drive letter index 0xff, which MSCDEX rejects on the drive number
 * before it follows ES:BX, and where no CD-ROM drive is mounted there is no
 * MSCDEX handler on INT 2Fh at all.  Neither case touches the two DOS blocks,
 * so what is in them afterwards is exactly what the function staged.
 *
 * Expected values are the immediates in the body -- MOV byte ptr [ESP],0x1a,
 * [ESP+1],0 and [ESP+2],3 at 0003c706..0003c70f, MOV dword ptr [ESP+0x16],0 and
 * word ptr [ESP+0x14],0, MOV byte ptr [ESP+0xd],0, the transfer address loaded
 * from [0x00069da8], MOV word ptr [ESP+0x12],2, MOV byte ptr [ESP+0x1c],0x9 for
 * the control block code, and the four PUSH 0x1a / PUSH 0x2 lengths at
 * 0003c73d, 0003c752, 0003c76c and 0003c781.
 *
 * Nothing below asserts what the driver answered.  Byte 1 of the control block
 * is never initialised by the body, so on a rejected request it holds whatever
 * the frame held, and on a machine with a real drive it holds that drive's own
 * tri-state media-change value -- 1 not changed, 0 unknown, 0xff changed.  The
 * two values the function produces are therefore checked against the bytes they
 * were read out of: what is pinned is the displacement and the width, MOV
 * EAX,[ESP+3] / MOV [0x00069e20],AX for the status and MOVZX AX,byte ptr
 * [ESP+0x1d] for the answer.
 */
static unsigned short read_media_change_from_a_rejected_drive(void)
{
    if (data_fdps_cd_request_header_real_mode_seg == 0) {
        fdps_cd_alloc_dos_buffers();
    }
    data_fdps_cdrom_drive_letter_index = 0xff;
    data_fdps_cd_ioctl_buffer[2] = 0x5a;
    data_fdps_cd_request_header_buffer[0x1a] = 0x5b;
    return fdps_cd_read_media_change_status();
}

/* Command code 3 is IOCTL Input and the transfer it describes is two bytes into
   the second DOS block, addressed by the packed real-mode far pointer and not
   by the flat one.  Start sector and volume-ID pointer are zero because an
   IOCTL request moves no disc data.

   The declared length is 0x1a, exactly the record, so the length byte and the
   struct size have to agree; and the byte just past the record keeps the poison
   the setup put there, which is what pins the staging copy at twenty-six rather
   than at the 0x1e fdps_cd_read_head_sector sends. */
static void cd_media_change_stages_an_ioctl_input_request(void)
{
    unsigned char *header;

    read_media_change_from_a_rejected_drive();
    header = data_fdps_cd_request_header_buffer;
    CHECK_EQ(header[0], 0x1a);
    CHECK_EQ(header[0], (int) sizeof(struct fdps_cd_request_header));
    CHECK_EQ(header[1], 0);
    CHECK_EQ(header[2], 3);
    CHECK_EQ(header[0xd], 0);
    CHECK_EQ((long) staged_dword(header, 0xe),
             (long) data_fdps_cd_ioctl_buffer_real_mode_ptr);
    CHECK_EQ(staged_word(header, 0x12), 2);
    CHECK_EQ(staged_word(header, 0x14), 0);
    CHECK_EQ((long) staged_dword(header, 0x16), 0L);
    CHECK_EQ(header[0x1a], 0x5b);
}

/* Control block code 9 is Media Changed, and the byte after it is the driver's
   answer slot.  MOV byte ptr [ESP+0x1c],0x9 is the body's only store into the
   block, so nothing may be asserted about byte 1 on the way out; the third byte
   of the DOS block is what pins the send length at two, because a copy that ran
   one byte long would take the poison with it.

   Two is also what the header's transfer byte count says, so a request that
   staged more bytes than it told the driver about would show as a disagreement
   between these two assertions. */
static void cd_media_change_asks_for_the_media_changed_block(void)
{
    read_media_change_from_a_rejected_drive();
    CHECK_EQ(data_fdps_cd_ioctl_buffer[0], 9);
    CHECK_EQ(staged_word(data_fdps_cd_request_header_buffer, 0x12), 2);
    CHECK_EQ(data_fdps_cd_ioctl_buffer[2], 0x5a);
}

/* Both blocks are copied back out of the DOS memory before anything is read out
   of them, so the two values the function produces have to agree with the bytes
   still sitting in those blocks: the status with the word at header+3, and the
   answer with byte 1 of the control block -- not byte 0, which is the request
   code the function itself stamped and would make the answer a constant 9.  The
   status store is a 16-bit store out of a dword load, so a read a byte early or
   late, or one that let the high half through, would break that equality
   whatever the driver left there. */
static void cd_media_change_publishes_status_and_returns_the_answer_byte(void)
{
    unsigned short media_change_byte;

    media_change_byte = read_media_change_from_a_rejected_drive();
    CHECK_EQ(data_fdps_cd_last_request_status,
             staged_word(data_fdps_cd_request_header_buffer, 3));
    CHECK_EQ(media_change_byte, data_fdps_cd_ioctl_buffer[1]);
}

/* The answer is zero extended into sixteen bits and never sign extended: MOVZX
   AX,byte ptr [ESP+0x1d].  That is load bearing because the value is MSCDEX's
   tri-state media-change byte and its "disc has been changed" case is 0xff --
   read through a signed char it would come back as 0xffff and compare equal to
   neither 0xff nor any of the other two states. */
static void cd_media_change_returns_the_answer_byte_zero_extended(void)
{
    unsigned short media_change_byte;

    media_change_byte = read_media_change_from_a_rejected_drive();
    CHECK_EQ(media_change_byte & 0xff00, 0);
    CHECK_EQ(media_change_byte, media_change_byte & 0xff);
}

/* The fdps_cdrom_detect cases come first, and deliberately: the allocation
   guard at 0003c68e is only observable on a call made before the module's DOS
   buffers exist, and every other test in this file allocates them in its own
   setup. */
void run_cd_tests(void)
{
    RUN_TEST(cd_detect_allocates_the_dos_buffers_on_the_first_call);
    RUN_TEST(cd_detect_issues_the_mscdex_installation_check);
    RUN_TEST(cd_detect_publishes_the_drive_count_and_first_letter);
    RUN_TEST(cd_detect_maps_the_interrupt_and_status_onto_its_result);
    RUN_TEST(cd_detect_reads_the_disc_info_past_the_no_driver_gate);
    RUN_TEST(cd_detect_compares_the_status_word_zero_extended);
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
    RUN_TEST(cd_set_audio_channel_stages_an_ioctl_output_request);
    RUN_TEST(cd_set_audio_channel_sends_two_bytes_short_of_the_record);
    RUN_TEST(cd_set_audio_channel_stamps_the_control_block_code);
    RUN_TEST(cd_set_audio_channel_sends_the_callers_own_nine_bytes);
    RUN_TEST(cd_set_audio_channel_leaves_the_callers_block_alone);
    RUN_TEST(cd_set_audio_channel_publishes_the_request_status);
    RUN_TEST(cd_device_status_stages_an_ioctl_input_request);
    RUN_TEST(cd_device_status_asks_for_the_device_status_block);
    RUN_TEST(cd_device_status_publishes_both_answers);
    RUN_TEST(cd_ioctl_output_stages_an_ioctl_output_request);
    RUN_TEST(cd_ioctl_output_sends_two_bytes_short_of_the_record);
    RUN_TEST(cd_ioctl_output_sends_the_code_as_a_one_byte_block);
    RUN_TEST(cd_ioctl_output_uses_only_the_low_byte_of_the_argument);
    RUN_TEST(cd_ioctl_output_publishes_the_request_status);
    RUN_TEST(cd_close_tray_sends_the_close_tray_code);
    RUN_TEST(cd_close_tray_goes_out_as_an_ioctl_output_request);
    RUN_TEST(cd_close_tray_publishes_the_request_status);
    RUN_TEST(cd_status_not_busy_inverts_the_busy_bit);
    RUN_TEST(cd_status_not_busy_ignores_every_other_bit);
    RUN_TEST(cd_media_change_stages_an_ioctl_input_request);
    RUN_TEST(cd_media_change_asks_for_the_media_changed_block);
    RUN_TEST(cd_media_change_publishes_status_and_returns_the_answer_byte);
    RUN_TEST(cd_media_change_returns_the_answer_byte_zero_extended);
}
