/* tests/cdtoc.c -- cover for src/cdtoc.c.
 *
 * So far this covers fdps_cd_unpack_msf at 0003bc3f,
 * fdps_cd_msf_to_sector at 0003bc78, fdps_cdrom_read_upc at 0003bec1,
 * fdps_cdrom_read_disk_info at 0003bfa5, fdps_cdrom_read_track_info at
 * 0003c0c8, fdps_cd_get_track_length_sectors at 0003c1a1,
 * fdps_cd_get_track_length_msf at 0003c217, fdps_cd_get_disk_info_msf at
 * 0003c27c and fdps_cd_sector_to_msf at 0003c2e8.
 *
 * Every expected value below comes from the sixteen instructions of that
 * function and from its two call sites, never from the emitted C:
 *
 *   MOV AL,byte ptr [ESP+0x4] / MOV EDX,dword ptr [ESP+0x10] / MOV [EDX],AL
 *   MOV EAX,[ESP+0x4] / AND EAX,0xff00   / SHR EAX,0x8  / MOV [ESP+0xc]  -> AL
 *   MOV EAX,[ESP+0x4] / AND EAX,0xff0000 / SHR EAX,0x10 / MOV [ESP+0x8]  -> AL
 *
 * __CHK pops its own argument (RET 0x4 at 00043627), so at every one of those
 * loads [ESP+0x4] is the first argument and [ESP+0x8], [ESP+0xc], [ESP+0x10]
 * are the second, third and fourth.  That is what fixes the parameter order:
 * the fourth pointer receives the low byte, the second the bits at 16-23.
 * Both callers clean four dwords off the stack afterwards (ADD ESP,0x10 at
 * 0003bc9f and 0003c0a6), which is the same reading from the outside.
 *
 * The function reads and writes no global, so nothing here depends on data
 * ticket 23 has not emitted yet.
 */
#include <stddef.h>
#include <string.h>
#include "testharn.h"
#include "fdpstype.h"
#include "gamedata.h"
#include "cd.h"
#include "cdtoc.h"

/* Poison bytes on both sides of the three destinations.  0x5a is a value no
   assertion below expects, so a store that lands wide shows up as a changed
   guard rather than as a coincidence. */
static unsigned char msf_bytes[5];

static void unpack_into_poisoned_bytes(unsigned int msf_packed)
{
    int i;

    for (i = 0; i < 5; i++) {
        msf_bytes[i] = 0x5a;
    }
    fdps_cd_unpack_msf(msf_packed, &msf_bytes[1], &msf_bytes[2],
                       &msf_bytes[3]);
}

/* The three masks and shifts, on a value whose three fields are all different
   and none of which is a plausible copy of another.  0x00332c1f is 51 minutes
   32 seconds 31 frames read as a packed address; what matters is that 0x33
   arrives at the minute pointer and 0x1f at the frame pointer, which is the
   parameter order the two call sites clean up after. */
static void cdtoc_unpack_splits_the_three_fields(void)
{
    unpack_into_poisoned_bytes(0x00332c1fu);
    CHECK_EQ(msf_bytes[1], 0x33);
    CHECK_EQ(msf_bytes[2], 0x2c);
    CHECK_EQ(msf_bytes[3], 0x1f);
}

/* AND 0xff0000 is the widest mask the body applies, so bits 24-31 of the
   argument reach nothing.  MSCDEX reports its addresses in the low three bytes
   of a dword whose top byte it does not define, so this is the case the game
   actually feeds the function. */
static void cdtoc_unpack_ignores_the_top_byte(void)
{
    unpack_into_poisoned_bytes(0xff123456u);
    CHECK_EQ(msf_bytes[1], 0x12);
    CHECK_EQ(msf_bytes[2], 0x34);
    CHECK_EQ(msf_bytes[3], 0x56);
}

/* Each field is masked before it is shifted, and the shift is SHR rather than
   SAR, so a field whose top bit is set comes down as itself and nothing above
   it survives.  0x00ffffff sets every bit of all three fields at once; a
   widened or sign-propagating store would have to disturb one of the guards
   to show all three as 0xff. */
static void cdtoc_unpack_carries_full_width_fields(void)
{
    unpack_into_poisoned_bytes(0x00ffffffu);
    CHECK_EQ(msf_bytes[1], 0xff);
    CHECK_EQ(msf_bytes[2], 0xff);
    CHECK_EQ(msf_bytes[3], 0xff);
    CHECK_EQ(msf_bytes[0], 0x5a);
    CHECK_EQ(msf_bytes[4], 0x5a);
}

/* All three stores are MOV byte ptr [EDX],AL, one byte each, and
   fdps_cdrom_read_disk_info relies on it: at 0003c0a1 it passes 0x69e08,
   0x69e09 and 0x69e0a -- three adjacent bss bytes -- as minute, second and
   frame.  A store any wider than a byte would carry one field over the next
   one and over the neighbouring global past it.  A zero argument is the case
   that shows it, because every field it writes is 0x00 and only an untouched
   guard is still 0x5a. */
static void cdtoc_unpack_writes_one_byte_per_pointer(void)
{
    unpack_into_poisoned_bytes(0u);
    CHECK_EQ(msf_bytes[0], 0x5a);
    CHECK_EQ(msf_bytes[1], 0);
    CHECK_EQ(msf_bytes[2], 0);
    CHECK_EQ(msf_bytes[3], 0);
    CHECK_EQ(msf_bytes[4], 0x5a);
}

/* A field of the packed value that is zero has to clear its destination rather
   than leave it: there is no branch anywhere in the body, so all three stores
   happen for every argument.  Here the minute and frame fields are zero and
   only the second field is set. */
static void cdtoc_unpack_always_writes_all_three(void)
{
    unpack_into_poisoned_bytes(0x00004b00u);
    CHECK_EQ(msf_bytes[1], 0);
    CHECK_EQ(msf_bytes[2], 0x4b);
    CHECK_EQ(msf_bytes[3], 0);
}

/* fdps_cd_msf_to_sector at 0003bc78.  Every number below is read off the
   thirty-one instructions of that body, never off the C:

     MOVZX EDX,byte ptr [ESP]      -- the minute byte
     SHL/SUB/SHL/ADD/SHL/MOV/SHL/ADD folds it to EDX = minute * 4500:
       (((m<<5)-m)<<2)+m  = 125m, <<2 = 500m, plus (500m<<3) = 4000m
     MOVZX EBX,byte ptr [ESP+0x8]  -- the second byte
     SHL/ADD/MOV/SHL/SUB folds it to EAX = second * 75:
       ((s<<2)+s) = 5s, (5s<<4) - 5s = 75s
     MOVZX EAX,byte ptr [ESP+0x4]  -- the frame byte, added unscaled
     SUB EAX,0x96                  -- less 150, then RET with EAX

   The three MOVZX loads are what make every field unsigned, and the ADD ESP,4
   after each of the two call sites (0003c0b2 and 0003c180) is what makes the
   argument a single stack dword.  The address 00:02:00 mapping to sector 0 is
   the Red Book lead-in that the 0x96 encodes.

   fdps_cd_unpack_msf is emitted in this same file, so these assertions run
   against real code and not against a stub. */

/* The lead-in itself: 00:02:00 is logical sector 0.  This is the single value
   that fixes the 0x96 -- a body without the SUB would answer 150 here. */
static void cdtoc_sector_of_lead_in_is_zero(void)
{
    CHECK_EQ(fdps_cd_msf_to_sector(0x00000200u), 0);
}

/* The minute scale, on its own: 01:00:00 is 4500 frames from 00:00:00, so 4350
   sectors from 00:02:00.  This pins the whole shift chain, since any other
   multiplier lands somewhere else entirely. */
static void cdtoc_sector_scales_minutes_by_4500(void)
{
    CHECK_EQ(fdps_cd_msf_to_sector(0x00010000u), 4350);
}

/* The second scale, on its own: 00:03:00 is one second past the lead-in, and
   one second is the 75 frames the second chain multiplies by. */
static void cdtoc_sector_scales_seconds_by_75(void)
{
    CHECK_EQ(fdps_cd_msf_to_sector(0x00000300u), 75);
}

/* The frame field is added unscaled, and 74 is the largest a real address
   carries: 00:02:74 is 74 sectors past the lead-in. */
static void cdtoc_sector_adds_frames_unscaled(void)
{
    CHECK_EQ(fdps_cd_msf_to_sector(0x0000024au), 74);
}

/* All three fields at once, none of them a plausible copy of another:
   0x00332c1f is 51 minutes 44 seconds 31 frames, so
   51*4500 + 44*75 + 31 - 150 = 229500 + 3300 + 31 - 150. */
static void cdtoc_sector_sums_all_three_fields(void)
{
    CHECK_EQ(fdps_cd_msf_to_sector(0x00332c1fu), 232681);
}

/* Below the lead-in the result is negative and is not clamped: there is no
   branch anywhere in the body and no CDQ or AND after the SUB.  00:00:00 is
   the case the game can actually reach, because it is what an unanswered
   driver query leaves in the reply block, and 00:01:00 shows the sign is not
   a special case of zero. */
static void cdtoc_sector_goes_negative_below_the_lead_in(void)
{
    CHECK_EQ(fdps_cd_msf_to_sector(0x00000000u), -150);
    CHECK_EQ(fdps_cd_msf_to_sector(0x00000100u), -75);
}

/* Bits 24-31 of the argument reach nothing, because fdps_cd_unpack_msf masks
   the minute field with AND 0xff0000 before it stores it.  MSCDEX leaves that
   byte undefined in the replies both callers pass through. */
static void cdtoc_sector_ignores_the_top_byte(void)
{
    CHECK_EQ(fdps_cd_msf_to_sector(0xff000200u), 0);
}

/* Full-width fields, well past anything a real disc carries: the three MOVZX
   loads zero-extend, so 0xff arrives as 255 and not as -1, and the sum stays
   in the 32-bit EAX the function returns.  255*4500 + 255*75 + 255 - 150. */
static void cdtoc_sector_carries_full_width_fields(void)
{
    CHECK_EQ(fdps_cd_msf_to_sector(0x00ffffffu), 1166730L);
}

/* fdps_cdrom_read_upc, 0003bec1.
 *
 * Same arrangement as tests/cd.c uses for the other three request builders in
 * this module: the request is issued for real through fdps_cd_device_request,
 * which is emitted code and not a stub, at drive letter index 0xff.  That is
 * past every drive letter there is, so MSCDEX rejects the request on the drive
 * number before it ever follows ES:BX into the request header, and on a
 * machine with no CD-ROM mounted at all there is no MSCDEX handler on INT 2Fh
 * and the multiplex returns untouched.  Either way nothing writes into the two
 * DOS blocks, so what is sitting in them after the call is exactly what the
 * function staged there.
 *
 * The expected values are the immediates in the body: MOV byte ptr [ESP],0x1a,
 * [ESP+1],0 and [ESP+2],3 at 0003bece..0003bed7, MOV dword ptr [ESP+0x16],0
 * and word ptr [ESP+0x14],0, MOV byte ptr [ESP+0xd],0, the transfer address
 * loaded from [0x00069da8], MOV word ptr [ESP+0x12],0xb, and MOV byte ptr
 * [ESP+0x1c],0xe / [ESP+0x1d],0x2 for the control block.  The four staging
 * copies are PUSH 0x1a, PUSH 0xb, PUSH 0xb and MOVZX EAX,byte ptr [ESP].
 *
 * The published status word and the published catalog number are checked
 * against the bytes they were read out of rather than against fixed values,
 * because what a driver leaves in those fields is not the test's to decide:
 * what is being pinned is the displacement and the width -- MOV EAX,[ESP+0x3]
 * / MOV [0x00069e20],AX for the status, and LEA EAX,[ESP+0x22] / PUSH 0x69e0f
 * / PUSH 0x7 for the seven bytes at control block +2.
 *
 * The memset arm at 0003bf77 is not reachable from here.  It fires only when
 * the driver writes 0 over the CONTROL/ADR byte, and a request the driver
 * never looked at comes back holding the 2 the function itself staged -- which
 * is the assertion below, and is the same fact from the other side: the arm
 * stays shut unless a driver actively reports "no catalog number".
 */
static unsigned int upc_staged_dword(unsigned char *block, int offset)
{
    unsigned int value;

    memcpy(&value, block + offset, 4);
    return value;
}

static unsigned short upc_staged_word(unsigned char *block, int offset)
{
    unsigned short value;

    memcpy(&value, block + offset, 2);
    return value;
}

/* The DOS blocks are allocated the way the module allocates them, and only if
   they are not there yet -- data_fdps_cd_request_header_real_mode_seg is the
   module's own "already allocated" flag.  Allocating again would work but
   would leak the previous pair, which nothing in the module can free. */
static int read_upc_from_a_rejected_drive(void)
{
    if (data_fdps_cd_request_header_real_mode_seg == 0) {
        fdps_cd_alloc_dos_buffers();
    }
    data_fdps_cdrom_drive_letter_index = 0xff;
    return fdps_cdrom_read_upc();
}

/* Every store the body makes into the request header is an ESP displacement,
   so the struct's offsets are what decides which field each one lands on
   (contract H).  Twenty-six is also the length the header declares itself to
   be here and the length both header copies run for, so a struct that had
   grown would stage bytes this request does not send. */
static void cdtoc_upc_header_fields_sit_where_the_stores_land(void)
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

/* Command code 3 is IOCTL Input, and the transfer it describes is eleven bytes
   into the second DOS block -- the same 0xb both control block copies run for.
   The address field carries the packed real-mode far pointer the module keeps
   for exactly this and not the flat pointer, because the driver that follows
   it runs in real mode; using the flat one would send the driver somewhere
   above the first megabyte.  Start sector and volume-ID pointer are zero
   because an IOCTL request transfers no disc data. */
static void cdtoc_upc_stages_an_ioctl_input_request(void)
{
    unsigned char *header;

    read_upc_from_a_rejected_drive();
    header = data_fdps_cd_request_header_buffer;
    CHECK_EQ(header[0], 0x1a);
    CHECK_EQ(header[1], 0);
    CHECK_EQ(header[2], 3);
    CHECK_EQ(header[0xd], 0);
    CHECK_EQ((long) upc_staged_dword(header, 0xe),
             (long) data_fdps_cd_ioctl_buffer_real_mode_ptr);
    CHECK_EQ(upc_staged_word(header, 0x12), 0xb);
    CHECK_EQ(upc_staged_word(header, 0x14), 0);
    CHECK_EQ((long) upc_staged_dword(header, 0x16), 0L);
}

/* Control block code 0x0e is UPC Code, and the byte after it is CONTROL/ADR.
   It goes out as 2 -- ADR = media catalog number -- and not as 0, which is the
   whole reason the zero test after the request means "the driver says there is
   no catalog number" rather than "nobody wrote anything here".  A body that
   cleared the block before sending it would leave a 0 in this byte instead. */
static void cdtoc_upc_asks_for_the_catalog_number(void)
{
    read_upc_from_a_rejected_drive();
    CHECK_EQ(data_fdps_cd_ioctl_buffer[0], 0xe);
    CHECK_EQ(data_fdps_cd_ioctl_buffer[1], 2);
}

/* Both blocks are copied back out of the DOS memory before anything is read
   out of them, so the two values the function publishes have to agree with the
   bytes still sitting in those blocks: the status with the word at header+3,
   and the seven catalog bytes with the block's bytes 2 to 8.  Reading the
   status one byte later, or copying the catalog field from the block's start,
   would break one or the other. */
static void cdtoc_upc_publishes_status_and_the_block_field(void)
{
    read_upc_from_a_rejected_drive();
    CHECK_EQ(data_fdps_cd_last_request_status,
             upc_staged_word(data_fdps_cd_request_header_buffer, 3));
    CHECK_EQ(memcmp(data_fdps_cd_media_catalog_number,
                    data_fdps_cd_ioctl_buffer + 2, 7), 0);
}

/* MOV EAX,0x1 is the only thing that reaches the RET, so the answer is 1 on a
   request the driver refused exactly as on one it answered -- there is no
   branch anywhere between the status store and the return.  A caller cannot
   learn anything from it. */
static void cdtoc_upc_always_returns_one(void)
{
    CHECK_EQ(read_upc_from_a_rejected_drive(), 1);
}

/* fdps_cdrom_read_disk_info, 0003bfa5.
 *
 * Issued for real through the same rejected drive the UPC cases above use, and
 * for the same reason: MSCDEX turns the request away on the drive number, or
 * there is no MSCDEX at all, and either way nothing writes into the two DOS
 * blocks.  What is in them after the call is what the function staged there.
 *
 * This function is the one case in the module where that makes every published
 * value known rather than merely self-consistent.  It clears its whole control
 * block before stamping the code byte -- memset(...,0,7) at 0003bfed, then MOV
 * byte ptr [ESP+0x1c],0xa -- so the six reply bytes go out as zero and come
 * back as zero, which fixes both track numbers at 0, all three lead-out MSF
 * bytes at 0, and the lead-out sector at 0 - 150.
 *
 * Every destination is poisoned with 0x5a before the call, because zero is
 * also what an unwritten bss global holds: without the poison a store that
 * never happened and a store of the driver's zero look identical.  There is no
 * branch anywhere in the body, so all seven destinations must lose their
 * poison on every call.
 *
 * The expected header bytes are the immediates at 0003bfb2..0003bfe4: 0x1a, 0,
 * 3, media descriptor 0 at +0xd, start sector 0 at +0x14, volume ID 0 at
 * +0x16, the transfer address loaded from [0x00069da8] and byte count 7 at
 * +0x12.  The staging copies are PUSH 0x1a and three PUSH 0x7, and the
 * read-back header length is the literal PUSH 0x1a at 0003c053.
 *
 * What this arrangement cannot separate is which byte of an all-zero reply
 * each published field was taken from; the displacements themselves are read
 * off the assembly -- LEA EAX,[ESP+0x21] for the six-byte copy, MOV
 * AL,[ESP+0x1d] and [ESP+0x1e] for the two track numbers, and the dword at
 * [ESP+0x1f] for the lead-out address -- and only a driver that answers with
 * distinguishable bytes would let a test tell them apart.
 *
 * fdps_cd_device_request is emitted in src/cd.c and fdps_cd_unpack_msf and
 * fdps_cd_msf_to_sector in the file under test, so none of the three is a
 * stub and nothing below rests on a stubbed return.
 */
static void poison_the_disk_info_globals(void)
{
    int i;

    for (i = 0; i < 6; i++) {
        data_fdps_cd_disk_info_reply[i] = 0x5a;
    }
    data_fdps_cd_lowest_track_number = 0x5a;
    data_fdps_cd_highest_track_number = 0x5a;
    data_fdps_cd_leadout_msf_minute = 0x5a;
    data_fdps_cd_leadout_second = 0x5a;
    data_fdps_cd_leadout_frame = 0x5a;
    data_fdps_cd_leadout_sector = 0x5a5a5a5aUL;
    data_fdps_cd_last_request_status = 0x5a5a;
}

static void read_disk_info_from_a_rejected_drive(void)
{
    if (data_fdps_cd_request_header_real_mode_seg == 0) {
        fdps_cd_alloc_dos_buffers();
    }
    data_fdps_cdrom_drive_letter_index = 0xff;
    poison_the_disk_info_globals();
    fdps_cdrom_read_disk_info();
}

/* Command code 3 is IOCTL Input again, and the transfer it describes is the
   seven bytes of the Read Disk Info block -- the same 7 all three control
   block copies run for, and the length the header's byte count field carries.
   The address field is the packed real-mode far pointer, not the flat one, for
   the same reason as in the UPC request: the driver that follows it runs in
   real mode. */
static void cdtoc_disk_info_stages_an_ioctl_input_request(void)
{
    unsigned char *header;

    read_disk_info_from_a_rejected_drive();
    header = data_fdps_cd_request_header_buffer;
    CHECK_EQ(header[0], 0x1a);
    CHECK_EQ(header[1], 0);
    CHECK_EQ(header[2], 3);
    CHECK_EQ(header[0xd], 0);
    CHECK_EQ((long) upc_staged_dword(header, 0xe),
             (long) data_fdps_cd_ioctl_buffer_real_mode_ptr);
    CHECK_EQ(upc_staged_word(header, 0x12), 7);
    CHECK_EQ(upc_staged_word(header, 0x14), 0);
    CHECK_EQ((long) upc_staged_dword(header, 0x16), 0L);
}

/* Control block code 0x0a is Read Disk Info, and the six bytes behind it are
   the reply area, sent as zero.  This is the difference from the UPC block
   next door, which presets its second byte and relies on the driver having to
   overwrite it: here the memset runs first and the code byte is written after
   it, so a body that stamped the code before clearing would send the driver a
   block whose first byte is 0. */
static void cdtoc_disk_info_asks_for_read_disk_info(void)
{
    int i;

    read_disk_info_from_a_rejected_drive();
    CHECK_EQ(data_fdps_cd_ioctl_buffer[0], 0xa);
    for (i = 1; i < 7; i++) {
        CHECK_EQ(data_fdps_cd_ioctl_buffer[i], 0);
    }
}

/* The six reply bytes are published whole and then the first two of them again
   as the track numbers, so all three have to agree with the block that came
   back out of DOS memory.  The copy starts at the block's byte 1, past the
   code byte: a copy from the block's start would put 0x0a into the reply's
   first byte and shift every field along one. */
static void cdtoc_disk_info_publishes_the_reply_block(void)
{
    read_disk_info_from_a_rejected_drive();
    CHECK_EQ(memcmp(data_fdps_cd_disk_info_reply,
                    data_fdps_cd_ioctl_buffer + 1, 6), 0);
    CHECK_EQ(data_fdps_cd_disk_info_reply[0], 0);
    CHECK_EQ(data_fdps_cd_lowest_track_number, data_fdps_cd_ioctl_buffer[1]);
    CHECK_EQ(data_fdps_cd_highest_track_number, data_fdps_cd_ioctl_buffer[2]);
    CHECK_EQ(data_fdps_cd_lowest_track_number, 0);
    CHECK_EQ(data_fdps_cd_highest_track_number, 0);
}

/* The lead-out address is split into its three fields on every call, so all
   three lose the poison even when every field is zero -- fdps_cd_unpack_msf
   has no branch either.  The bytes they are split out of are the block's 3 to
   6, which the rejected request left at zero. */
static void cdtoc_disk_info_splits_the_leadout_address(void)
{
    read_disk_info_from_a_rejected_drive();
    CHECK_EQ(data_fdps_cd_leadout_msf_minute, data_fdps_cd_ioctl_buffer[5]);
    CHECK_EQ(data_fdps_cd_leadout_second, data_fdps_cd_ioctl_buffer[4]);
    CHECK_EQ(data_fdps_cd_leadout_frame, data_fdps_cd_ioctl_buffer[3]);
    CHECK_EQ(data_fdps_cd_leadout_msf_minute, 0);
    CHECK_EQ(data_fdps_cd_leadout_second, 0);
    CHECK_EQ(data_fdps_cd_leadout_frame, 0);
}

/* The same four bytes go through fdps_cd_msf_to_sector as well, and its result
   is stored into an unsigned global without being clamped or tested: 00:00:00
   is 150 frames before logical sector 0, so what lands in the global is the
   bit pattern of -150.  A body that clamped, or that published the frame count
   without the SUB EAX,0x96, would put 0 or 150 here instead.  The comparison
   is written against the unsigned value rather than through the harness's long
   because the global is what carries the sign, and it is unsigned. */
static void cdtoc_disk_info_converts_the_leadout_to_a_sector(void)
{
    read_disk_info_from_a_rejected_drive();
    CHECK_EQ(data_fdps_cd_leadout_sector == 0xffffff6aUL, 1);
}

/* MOV EAX,dword ptr [ESP+0x3] / MOV [0x00069e20],AX: the status is the word at
   the header's offset 3 and only the word, so it has to agree with the two
   bytes still sitting at that displacement in the DOS block.  What a refused
   request leaves there is not the test's to decide -- the header goes out with
   that field uninitialised -- so this pins the displacement and the width and
   not a value.  It also has to differ from the poison for the store to have
   happened at all, which the block's own bytes decide. */
static void cdtoc_disk_info_publishes_the_status_word(void)
{
    read_disk_info_from_a_rejected_drive();
    CHECK_EQ(data_fdps_cd_last_request_status,
             upc_staged_word(data_fdps_cd_request_header_buffer, 3));
}

/* fdps_cdrom_read_track_info, 0003c0c8.
 *
 * Issued for real through the same rejected drive the two cases above use, and
 * for the same reason: nothing writes into the two DOS blocks, so what is in
 * them after the call is what the function staged there.
 *
 * This request is the one in the module that does NOT clear its control block
 * -- there is no memset anywhere in the body, only MOV byte ptr [ESP+0x1c],0xb
 * and the track byte at [ESP+0x1d] -- so bytes 2..6 go out as stack content and
 * come back as the same stack content.  Two of the three published values are
 * therefore not knowable in advance here, and the assertions on them are
 * written against the block that came back rather than against fixed numbers:
 * what is being pinned is the displacement, the width and the mask.  The two
 * values that ARE knowable, the track number in both of its widths, are pinned
 * against fixed numbers.
 *
 * The expected header bytes are the immediates at 0003c0d5..0003c100: 0x1a, 0
 * and 3 in the first three bytes, media descriptor 0 at +0xd, transfer address
 * loaded from [0x00069da8] at +0xe, byte count 7 at +0x12, start sector 0 at
 * +0x14 and volume ID 0 at +0x16.  The staging copies are PUSH 0x1a and PUSH
 * 0x7 in each direction, and the read-back header length is the literal 0x1a at
 * 0003c143 rather than the header's own length byte.
 *
 * fdps_cd_device_request is emitted in src/cd.c and fdps_cd_msf_to_sector in
 * the file under test, so neither is a stub and nothing below rests on a
 * stubbed return.
 */
static void poison_the_track_info_globals(void)
{
    data_fdps_cd_track_info_track_number = 0x5a5a;
    data_fdps_cd_track_start_sector = 0x5a5a5a5aUL;
    data_fdps_cd_track_info_control_flags = 0x5a;
}

/* Fills a stretch of stack below the current frame, so that the control block
   bytes the request sends out are not left holding whatever happened to be
   there.  The pattern is chosen, not arbitrary; it repeats every four bytes as
   0xa3, 0xb3, 0xe3, 0xf3, and each of the three properties below is what makes
   one of the assertions able to fail:

     - bit 5 is set in every one of them.  Bit 5 is the ONLY bit by which the
       0xd0 the body masks with and the 0xf0 an obvious rewrite would use
       differ, so a fill byte with bit 5 clear makes both masks produce the same
       answer and the mask assertions cannot see the difference.  A uniform 0x5a
       fill was exactly such a byte: 0x5a & 0xd0 and 0x5a & 0xf0 are both 0x50.
     - the low bits 0x03 are set in every one of them, so a mask that kept any
       of the low nibble is caught by the & 0x2f assertion.
     - all four differ from each other, and they differ inside the 0xd0 mask as
       well as outside it (they mask to 0x80, 0x90, 0xc0 and 0xd0).  A uniform
       fill makes every displacement into the block look alike, so the dword at
       block+2 and the dword at block+3 -- the offset the Read Disk Info reply
       uses -- would convert to the same sector, and the masked byte at block+6
       would equal the one at block+5.  With these four, both displacements are
       pinned.

   Nothing asserted here depends on this landing on the request's frame: every
   assertion is written against the block that actually came back, so a run
   where the fill does not reach it still holds.  What the fill buys is that a
   wrong mask or a wrong displacement is visible when it does. */
static void scribble_the_stack_below(void)
{
    volatile unsigned char scratch[512];
    int i;

    for (i = 0; i < 512; i++) {
        scratch[i] = (unsigned char) (0xa3 | ((i & 1) << 4) | ((i & 2) << 5));
    }
}

static void read_track_info_from_a_rejected_drive(int track)
{
    if (data_fdps_cd_request_header_real_mode_seg == 0) {
        fdps_cd_alloc_dos_buffers();
    }
    data_fdps_cdrom_drive_letter_index = 0xff;
    poison_the_track_info_globals();
    scribble_the_stack_below();
    fdps_cdrom_read_track_info(track);
}

/* Command code 3 is IOCTL Input again, and the transfer it describes is the
   seven bytes of the Read Audio Track Info block -- the same 7 both control
   block copies run for.  The address field is the packed real-mode far pointer
   and not the flat one, for the same reason as in the other two requests. */
static void cdtoc_track_info_stages_an_ioctl_input_request(void)
{
    unsigned char *header;

    read_track_info_from_a_rejected_drive(3);
    header = data_fdps_cd_request_header_buffer;
    CHECK_EQ(header[0], 0x1a);
    CHECK_EQ(header[1], 0);
    CHECK_EQ(header[2], 3);
    CHECK_EQ(header[0xd], 0);
    CHECK_EQ((long) upc_staged_dword(header, 0xe),
             (long) data_fdps_cd_ioctl_buffer_real_mode_ptr);
    CHECK_EQ(upc_staged_word(header, 0x12), 7);
    CHECK_EQ(upc_staged_word(header, 0x14), 0);
    CHECK_EQ((long) upc_staged_dword(header, 0x16), 0L);
}

/* Control block code 0x0b is Read Audio Track Info and the byte behind it is
   the track being asked about.  Only the low byte of the argument gets there --
   MOV AL,byte ptr [ESP+0x28], a byte load -- which 0x51234 shows: the driver is
   asked for track 0x34.  A body that stored the whole argument would put 0x34
   in the same place but would also walk over the reply area behind it. */
static void cdtoc_track_info_asks_for_one_track(void)
{
    read_track_info_from_a_rejected_drive(3);
    CHECK_EQ(data_fdps_cd_ioctl_buffer[0], 0xb);
    CHECK_EQ(data_fdps_cd_ioctl_buffer[1], 3);
    read_track_info_from_a_rejected_drive(0x51234);
    CHECK_EQ(data_fdps_cd_ioctl_buffer[0], 0xb);
    CHECK_EQ(data_fdps_cd_ioctl_buffer[1], 0x34);
}

/* The published track number is the caller's argument and not anything out of
   the reply, and it is sixteen bits of it: MOV EAX,dword ptr [ESP+0x28] then
   MOV [0x00069dff],AX.  0x51234 is the case that separates the two widths --
   0x1234 reaches the global while only 0x34 reached the control block above --
   and -2 is the case that fixes the sign, since both readers widen the loaded
   word with CWDE. */
static void cdtoc_track_info_publishes_the_track_number(void)
{
    read_track_info_from_a_rejected_drive(3);
    CHECK_EQ(data_fdps_cd_track_info_track_number, 3);
    read_track_info_from_a_rejected_drive(0x51234);
    CHECK_EQ(data_fdps_cd_track_info_track_number, 0x1234);
    read_track_info_from_a_rejected_drive(-2);
    CHECK_EQ(data_fdps_cd_track_info_track_number, -2);
    CHECK_EQ(data_fdps_cd_ioctl_buffer[1], 0xfe);
}

/* The start sector is the dword at the control block's byte 2 run through
   fdps_cd_msf_to_sector -- PUSH dword ptr [ESP+0x1e] off a frame whose block
   begins at +0x1c -- so it has to agree with the four bytes still sitting at
   that offset in the block that came back.  Reading the address from the
   block's start, or from +3 the way the Read Disk Info reply carries it, would
   name a position elsewhere on the disc -- which this can see because the fill
   above changes every four bytes, so the dword at +2 and the dword at +3 carry
   different minute, second and frame bytes and convert to different sectors.
   The poison is what shows the store happened at all: the conversion cannot
   produce 0x5a5a5a5a, whose value is far past the 1166730 sectors the widest
   possible address converts to. */
static void cdtoc_track_info_publishes_the_start_sector(void)
{
    unsigned int staged_msf;

    read_track_info_from_a_rejected_drive(3);
    staged_msf = upc_staged_dword(data_fdps_cd_ioctl_buffer, 2);
    CHECK_EQ((long) data_fdps_cd_track_start_sector,
             (long) fdps_cd_msf_to_sector(staged_msf));
    CHECK_EQ(data_fdps_cd_track_start_sector == 0x5a5a5a5aUL, 0);
}

/* AND AL,0xd0 on the byte at the control block's byte 6.  Two things are pinned
   here: the byte it comes from, by comparing against the block that came back,
   and the mask, by requiring every bit outside 0xd0 to be clear.  Bit 5 is the
   one that matters -- it is copy-permitted, and the 0xf0 an obvious rewrite
   would use keeps it, which would make fdps_cd_track_is_audio's exact compare
   against 0x40 report a copyable audio track as data.  The fill above is what
   lets that be seen: every byte it writes has bit 5 set, so under 0xf0 the
   published byte would come back 0x20 higher and both the & 0x2f and the & 0x20
   assertion would fail.  Its four values also mask to four different results,
   so reading block+5 instead of block+6 breaks the first assertion.  The poison
   cannot survive the mask either, since 0x5a has bits at 0x0a. */
static void cdtoc_track_info_masks_the_control_byte(void)
{
    read_track_info_from_a_rejected_drive(3);
    CHECK_EQ(data_fdps_cd_track_info_control_flags,
             data_fdps_cd_ioctl_buffer[6] & 0xd0);
    CHECK_EQ(data_fdps_cd_track_info_control_flags & 0x2f, 0);
    CHECK_EQ(data_fdps_cd_track_info_control_flags & 0x20, 0);
}

/* MOV EAX,dword ptr [ESP+0x3] / MOV [0x00069e20],AX, exactly as in the other
   two requests: the status is the word at the header's offset 3 and only the
   word, so it has to agree with the two bytes still at that displacement in the
   DOS block.  What a refused request leaves there is not the test's to decide,
   so this pins the displacement and the width and not a value. */
static void cdtoc_track_info_publishes_the_status_word(void)
{
    read_track_info_from_a_rejected_drive(3);
    CHECK_EQ(data_fdps_cd_last_request_status,
             upc_staged_word(data_fdps_cd_request_header_buffer, 3));
}

/* fdps_cd_get_track_length_sectors, 0003c1a1.
 *
 * Driven through the same rejected drive as everything above, which decides
 * the whole shape of what can be seen here.  The function opens with
 * fdps_cdrom_read_disk_info, and a refused Read Disk Info clears its control
 * block before sending it, so on this drive the refresh always publishes
 * highest track 0 and lead-out sector 0 - 150.  A track number is an unsigned
 * byte, so nothing is ever below 0: only the lead-out arm of the branch at
 * 0003c1de runs here, and the next-track arm needs a drive that answers.
 *
 * Both endpoints then collapse onto the same number, and it is worth being
 * exact about why, because it is what the assertions below can and cannot see.
 * The Read Audio Track Info block is the one in the module that is not cleared,
 * so bytes 2..6 of its reply are whatever the frame it was staged in already
 * held.  The frame it is staged in here is the one fdps_cdrom_read_disk_info
 * used a moment earlier, from the same ESP -- and that function memsets its own
 * seven-byte block to zero and gets it back from the refused request unchanged.
 * So the start address the track query reports is 00:00:00, the same address
 * the refused disc query reported for the lead-out, and both convert to the
 * same 0 - 150.  The length is therefore 0 on this drive whichever arm of the
 * branch runs, which is what makes the arm itself, and the direction of the
 * subtraction at 0003c20f, invisible from here; separating them needs a drive
 * that answers, and the emit verdict records that as an open issue.  What the
 * length being exactly 0 does pin is that the answer is a difference of the two
 * endpoints and not either endpoint on its own, since either alone is
 * 0xffffff6a.
 *
 * The restoring third query at 0003c205 is what the rest pins.  The track
 * number it re-asks for is the word this function saved at 0003c1b4, before
 * the first query overwrote it, sign-extended by MOVSX EAX,word ptr [ESP+0x4];
 * so the value left in data_fdps_cd_track_info_track_number after the call has
 * to be the one that was there before it, and the low byte of that value has to
 * be what the control block came to rest holding.  0x1234 separates the two
 * widths -- sixteen bits reach the global, eight the block -- and -2 fixes the
 * sign of the word load.
 *
 * fdps_cdrom_read_disk_info and fdps_cdrom_read_track_info are both emitted in
 * the file under test, so neither is a stub and nothing below rests on a
 * stubbed return.
 */
static unsigned int track_length_from_a_rejected_drive(unsigned char track,
                                                       short selected_track)
{
    if (data_fdps_cd_request_header_real_mode_seg == 0) {
        fdps_cd_alloc_dos_buffers();
    }
    data_fdps_cdrom_drive_letter_index = 0xff;
    poison_the_disk_info_globals();
    poison_the_track_info_globals();
    data_fdps_cd_track_info_track_number = selected_track;
    scribble_the_stack_below();
    return fdps_cd_get_track_length_sectors(track);
}

/* CALL 0x0003bfa5 at 0003c1af, before anything else: both globals the branch
   and the lead-out arm read are this call's answers.  The poison is 0x5a in
   both, so a body that skipped the refresh would leave them holding it. */
static void cdtoc_track_length_refreshes_the_disc_summary(void)
{
    track_length_from_a_rejected_drive(1, 1);
    CHECK_EQ(data_fdps_cd_highest_track_number, 0);
    CHECK_EQ(data_fdps_cd_leadout_sector == 0xffffff6aUL, 1);
}

/* SUB EAX,dword ptr [ESP] at 0003c20f: the answer is one endpoint less the
   other and not an endpoint on its own.  The two middle assertions are what
   make the last one mean that -- both endpoints are pinned at 0xffffff6a
   first, against their poison and against the value a refused query converts
   to, so a body that returned either of them, or that dropped the subtraction,
   would answer 0xffffff6a where 0 is required.  An off-by-150 anywhere in the
   two conversions would show here as well, since the lead-in is what has to
   cancel for a length of zero to come out. */
static void cdtoc_track_length_subtracts_the_two_endpoints(void)
{
    unsigned int length;

    length = track_length_from_a_rejected_drive(1, 1);
    CHECK_EQ(data_fdps_cd_highest_track_number, 0);
    CHECK_EQ(data_fdps_cd_track_start_sector == 0x5a5a5a5aUL, 0);
    CHECK_EQ(data_fdps_cd_track_start_sector == 0xffffff6aUL, 1);
    CHECK_EQ(data_fdps_cd_leadout_sector == 0xffffff6aUL, 1);
    CHECK_EQ(length == data_fdps_cd_leadout_sector
                       - data_fdps_cd_track_start_sector, 1);
    CHECK_EQ(length == 0, 1);
}

/* The third query re-asks for the track the globals described on entry, so the
   track number survives a call that queried a different track: 7 goes in, 3 is
   asked about, 7 is what is left.  The control block's track byte is the same
   fact from the driver's side -- the last request the driver saw named 7 and
   not 3, which is what keeps fdps_cd_resolve_track_range reading the right
   track's globals afterwards.  0x1234 shows the two widths apart and -2 shows
   the word is sign-extended on the way back out. */
static void cdtoc_track_length_restores_the_selected_track(void)
{
    track_length_from_a_rejected_drive(3, 7);
    CHECK_EQ(data_fdps_cd_track_info_track_number, 7);
    CHECK_EQ(data_fdps_cd_ioctl_buffer[1], 7);

    track_length_from_a_rejected_drive(3, 0x1234);
    CHECK_EQ(data_fdps_cd_track_info_track_number, 0x1234);
    CHECK_EQ(data_fdps_cd_ioctl_buffer[1], 0x34);

    track_length_from_a_rejected_drive(3, -2);
    CHECK_EQ(data_fdps_cd_track_info_track_number, -2);
    CHECK_EQ(data_fdps_cd_ioctl_buffer[1], 0xfe);
}

/* fdps_cd_get_track_length_msf, 0003c217.
 *
 * Driven through the same rejected drive as everything above, and here that
 * arrangement is not a limitation but the one case that pins the whole chain.
 * The case immediately above establishes that on this drive
 * fdps_cd_get_track_length_sectors answers exactly 0 -- both endpoints are the
 * refused query's 00:00:00 and the subtraction cancels them -- so the input to
 * the arithmetic below is a known number and not whatever a disc happens to
 * hold.  Nothing here rests on a stub: the length query is emitted in the file
 * under test and so is everything it calls except fdps_cd_device_request, which
 * is emitted in src/cd.c.
 *
 * From that 0 the eighteen instructions between 0003c23a and 0003c276 decide
 * every byte, and this is what they compute, read off them and not off the C:
 *
 *   LEA EBX,[EAX + 0xffffff6a]  -- 0 - 150, and EBX is an unsigned dividend
 *                                  from here on, so this is 4294967146
 *   MOV ECX,0x4b / XOR EDX,EDX / DIV ECX
 *                               -- 4294967146 = 57266228*75 + 46, so DL is 46
 *   MOV byte ptr [ESI],DL       -- ESI is [ESP+0x1c], the fourth argument
 *   SUB EBX,EAX (the remainder) / DIV ECX again
 *                               -- 57266228 whole seconds
 *   MOV ESI,0x3c / XOR EDX,EDX / DIV ESI
 *                               -- 57266228 = 954437*60 + 8, so DL is 8
 *   MOV byte ptr [EDI],DL       -- EDI is [ESP+0x18], the third argument
 *   SUB EBX,EAX / DIV ESI again -- 954437 minutes
 *   MOV EAX,[ESP+0x14] / MOV byte ptr [EAX],BL
 *                               -- the second argument, and BL is the low byte
 *                                  of 954437 = 0xe9045, so 0x45
 *
 * That triple is what makes the two facts in the rebuild note visible.  Written
 * with a signed intermediate the subtraction would give -150 and the answer
 * would be a small negative triple instead of this one, and written without the
 * SUB 0x96 at all the length of 0 would convert to 0/0/0.  The minute byte is
 * the third: 954437 does not fit in a byte and nothing clamps it, so 0x45 is
 * what a plain byte store leaves.
 *
 * The three values are also all different from each other, which is what pins
 * the parameter order -- the fourth pointer takes the frames and the second the
 * minutes -- and different from the 0x5a poison, which is what shows each store
 * happened at all.
 *
 * What this cannot see is a track whose length is not zero, because that needs
 * a drive that answers; the emit verdict records it as an open issue.
 */
static unsigned char msf_out[5];

static void track_length_msf_from_a_rejected_drive(unsigned char track)
{
    int i;

    if (data_fdps_cd_request_header_real_mode_seg == 0) {
        fdps_cd_alloc_dos_buffers();
    }
    data_fdps_cdrom_drive_letter_index = 0xff;
    poison_the_disk_info_globals();
    poison_the_track_info_globals();
    scribble_the_stack_below();
    for (i = 0; i < 5; i++) {
        msf_out[i] = 0x5a;
    }
    fdps_cd_get_track_length_msf(track, &msf_out[1], &msf_out[2],
                                 &msf_out[3]);
}

/* The whole chain at once, on the one input this drive makes knowable.  Any of
   the four things the rebuild note warns about -- dropping the 0x96, using a
   signed intermediate, dividing by anything but 75 then 60, or clamping the
   minute field -- moves at least one of these three bytes. */
static void cdtoc_track_length_msf_converts_the_wrapped_length(void)
{
    track_length_msf_from_a_rejected_drive(1);
    CHECK_EQ(msf_out[1], 0x45);
    CHECK_EQ(msf_out[2], 8);
    CHECK_EQ(msf_out[3], 46);
}

/* The input the triple above was computed from, asserted separately so that a
   failure says which half moved: the length query answering something other
   than 0 on this drive, or the arithmetic converting the 0 differently. */
static void cdtoc_track_length_msf_starts_from_a_zero_length(void)
{
    unsigned int length;

    if (data_fdps_cd_request_header_real_mode_seg == 0) {
        fdps_cd_alloc_dos_buffers();
    }
    data_fdps_cdrom_drive_letter_index = 0xff;
    scribble_the_stack_below();
    length = fdps_cd_get_track_length_sectors(1);
    CHECK_EQ(length == 0, 1);
}

/* Three byte-wide stores and no branch: each guard on either side of the three
   destinations keeps its poison, and none of the three destinations keeps it.
   A store wider than a byte would carry one field over its neighbour, which is
   what a caller aiming the three at three adjacent bytes relies on not
   happening. */
static void cdtoc_track_length_msf_writes_one_byte_per_pointer(void)
{
    track_length_msf_from_a_rejected_drive(1);
    CHECK_EQ(msf_out[0], 0x5a);
    CHECK_EQ(msf_out[4], 0x5a);
    CHECK_EQ(msf_out[1] == 0x5a, 0);
    CHECK_EQ(msf_out[2] == 0x5a, 0);
    CHECK_EQ(msf_out[3] == 0x5a, 0);
}

/* The length comes from the query and not from a global this function reads
   itself: the query refreshes the disc summary on its way in, so the poison in
   both of those globals is gone afterwards and the lead-out sector holds the
   -150 a refused Read Disk Info converts to.  A body that skipped the CALL at
   0003c232 would leave both holding 0x5a. */
static void cdtoc_track_length_msf_delegates_to_the_sector_query(void)
{
    track_length_msf_from_a_rejected_drive(1);
    CHECK_EQ(data_fdps_cd_highest_track_number, 0);
    CHECK_EQ(data_fdps_cd_leadout_sector == 0xffffff6aUL, 1);
    CHECK_EQ(data_fdps_cd_track_start_sector == 0xffffff6aUL, 1);
}

/* fdps_cd_get_disk_info_msf, 0003c27c.
 *
 * Driven through the same rejected drive as everything above, and for the same
 * reason it is the one case that makes the arithmetic knowable: a refused Read
 * Disk Info clears its control block before sending it and gets it back
 * unchanged, so after the refresh at 0003c291 the highest-track byte at
 * 00069e07 is 0 and the lead-out sector at 00069e0b is 0 - 150.  Those two are
 * the function's only inputs, and the cases above have already pinned both of
 * them against this drive.
 *
 * From there the twenty-two instructions between 0003c296 and 0003c2e2 decide
 * every byte, and this is what they compute, read off them and not off the C:
 *
 *   MOV DL,byte ptr [0x00069e07] / MOV EBX,[ESP+0x10] / MOV byte ptr [EBX],DL
 *                               -- the highest track, whole, to the FIRST
 *                                  argument, and before any arithmetic
 *   MOV EBX,dword ptr [0x00069e0b] / SUB EBX,0x96
 *                               -- 4294967146 - 150 = 4294966996, and EBX is
 *                                  an unsigned dividend from here on
 *   MOV ECX,0x4b / XOR EDX,EDX / DIV ECX
 *                               -- 4294966996 = 57266226*75 + 46, so DL is 46
 *   MOV byte ptr [EDI],DL       -- EDI is [ESP+0x1c], the fourth argument
 *   MOVZX EDX,DL / SUB EAX,EDX / DIV ECX again
 *                               -- 57266226 whole seconds
 *   MOV ECX,0x3c / XOR EDX,EDX / DIV ECX
 *                               -- 57266226 = 954437*60 + 6, so DL is 6
 *   MOV byte ptr [ESI],DL       -- ESI is [ESP+0x18], the third argument
 *   MOVZX EDX,DL / SUB EAX,EDX / DIV ECX again
 *                               -- 954437 minutes
 *   MOV EDX,[ESP+0x14] / MOV byte ptr [EDX],AL
 *                               -- the second argument, and AL is the low byte
 *                                  of 954437 = 0xe9045, so 0x45
 *
 * The four displacements are [ESP+0x10], [ESP+0x14], [ESP+0x18] and [ESP+0x1c]
 * after PUSH EBX / PUSH ESI / PUSH EDI, and __CHK pops its own argument (RET
 * 0x4 at 00043627), so they are the first, second, third and fourth arguments
 * in that order.
 *
 * The four expected bytes are all different from each other and all different
 * from the 0x5a poison, which is what lets one arrangement pin the parameter
 * order and the fact that every store happened.  The triple also differs from
 * the one fdps_cd_get_track_length_msf produces on this drive -- 0x45, 8, 46 --
 * in its seconds field, because the input differs by exactly the 150 that
 * function's own SUB 0x96 has already taken off; a body that read the wrong
 * global, or that dropped its own subtraction, lands on a different second.
 *
 * Nothing here rests on a stub: fdps_cdrom_read_disk_info is emitted in the
 * file under test, and everything it calls except fdps_cd_device_request, which
 * is emitted in src/cd.c.
 *
 * What this cannot see is a disc that reports a track count and a lead-out of
 * its own, because that needs a drive that answers; the emit verdict records it
 * as an open issue.
 */
static unsigned char disk_info_out[6];

static void disk_info_msf_from_a_rejected_drive(void)
{
    int i;

    if (data_fdps_cd_request_header_real_mode_seg == 0) {
        fdps_cd_alloc_dos_buffers();
    }
    data_fdps_cdrom_drive_letter_index = 0xff;
    poison_the_disk_info_globals();
    scribble_the_stack_below();
    for (i = 0; i < 6; i++) {
        disk_info_out[i] = 0x5a;
    }
    fdps_cd_get_disk_info_msf(&disk_info_out[1], &disk_info_out[2],
                              &disk_info_out[3], &disk_info_out[4]);
}

/* The highest-track byte reaches the first pointer, unmasked and before the
   arithmetic.  On this drive it is 0, which the poison of 0x5a distinguishes
   from a store that never happened, and which no other destination holds. */
static void cdtoc_disk_info_msf_reports_the_highest_track(void)
{
    disk_info_msf_from_a_rejected_drive();
    CHECK_EQ(data_fdps_cd_highest_track_number, 0);
    CHECK_EQ(disk_info_out[1], 0);
}

/* The whole chain at once, on the one input this drive makes knowable, and the
   parameter order with it: minutes to the second pointer, seconds to the third,
   frames to the fourth.  Any of the things the note on the definition warns
   about -- dropping the 0x96, using a signed intermediate, dividing by anything
   but 75 then 60, or clamping the minute field -- moves at least one of these
   three bytes. */
static void cdtoc_disk_info_msf_converts_the_playing_time(void)
{
    disk_info_msf_from_a_rejected_drive();
    CHECK_EQ(disk_info_out[2], 0x45);
    CHECK_EQ(disk_info_out[3], 6);
    CHECK_EQ(disk_info_out[4], 46);
}

/* Four byte-wide stores and no branch: the guards on either side of the four
   destinations keep their poison, and none of the four destinations keeps it.
   A store wider than a byte would carry one field over its neighbour, which is
   what a caller aiming the four at four adjacent bytes relies on not
   happening. */
static void cdtoc_disk_info_msf_writes_one_byte_per_pointer(void)
{
    disk_info_msf_from_a_rejected_drive();
    CHECK_EQ(disk_info_out[0], 0x5a);
    CHECK_EQ(disk_info_out[5], 0x5a);
    CHECK_EQ(disk_info_out[1] == 0x5a, 0);
    CHECK_EQ(disk_info_out[2] == 0x5a, 0);
    CHECK_EQ(disk_info_out[3] == 0x5a, 0);
    CHECK_EQ(disk_info_out[4] == 0x5a, 0);
}

/* CALL 0x0003bfa5 at 0003c291, before either global is read: both are this
   call's answers.  The poison is 0x5a in both, so a body that skipped the
   refresh would leave them holding it -- and this is also where the input the
   triple above was computed from is asserted, so that a failure says which half
   moved, the refresh or the arithmetic. */
static void cdtoc_disk_info_msf_refreshes_the_disc_summary(void)
{
    disk_info_msf_from_a_rejected_drive();
    CHECK_EQ(data_fdps_cd_highest_track_number == 0x5a, 0);
    CHECK_EQ(data_fdps_cd_leadout_sector == 0x5a5a5a5aUL, 0);
    CHECK_EQ(data_fdps_cd_leadout_sector == 0xffffff6aUL, 1);
}

/* fdps_cd_sector_to_msf at 0003c2e8.  Nothing in the image calls it and it
 * reads no global, so every expected value below is computed from its
 * thirty-two instructions alone:
 *
 *   MOV EBX,[ESP+0x10] / SUB EBX,0x96
 *                               -- the first argument, less 150, unsigned from
 *                                  here on: SUB with no test, then DIV and
 *                                  never IDIV
 *   MOV dword [ESP],0x4b / XOR EDX,EDX / MOV EAX,EBX / DIV dword ptr [ESP]
 *   MOV byte ptr [ECX],DL       -- ECX is [ESP+0x1c], the fourth argument, and
 *                                  DL is the frame remainder
 *   MOVZX EDX,DL / MOV EAX,EBX / SUB EAX,EDX / MOV ECX,0x4b / DIV ECX
 *                               -- whole seconds, the quotient recomputed from
 *                                  (value - remainder)
 *   MOV ECX,0x3c / XOR EDX,EDX / DIV ECX
 *   MOV byte ptr [ESI],DL       -- ESI is [ESP+0x18], the third argument
 *   MOVZX EDX,DL / SUB EAX,EDX / DIV ECX again
 *   MOV EDX,[ESP+0x14] / MOV byte ptr [EDX],BL
 *                               -- the second argument, low byte of the minute
 *                                  quotient, no clamp
 *
 * The displacements are [ESP+0x10], [ESP+0x14], [ESP+0x18] and [ESP+0x1c] after
 * PUSH EBX / PUSH ESI / SUB ESP,0x4, and __CHK pops its own argument (RET 0x4
 * at 00043627), so they are the first, second, third and fourth arguments in
 * that order: sector, minute, second, frame -- and the stores run frame,
 * second, minute, the reverse of the parameter order.
 *
 * All three stores are byte-wide, so the poison guards on either side of the
 * three destinations must survive every case.
 */
static unsigned char sector_msf_bytes[5];

static void sector_to_msf_into_poisoned_bytes(unsigned int sector)
{
    int i;

    for (i = 0; i < 5; i++) {
        sector_msf_bytes[i] = 0x5a;
    }
    fdps_cd_sector_to_msf(sector, &sector_msf_bytes[1], &sector_msf_bytes[2],
                          &sector_msf_bytes[3]);
}

/* Sector 150 is exactly the 0x96 the body takes off, so the dividend is 0 and
   every field is 0.  The guards distinguish three stores of zero from three
   stores that never happened. */
static void cdtoc_sector_to_msf_lead_in_is_all_zero(void)
{
    sector_to_msf_into_poisoned_bytes(150);
    CHECK_EQ(sector_msf_bytes[1], 0);
    CHECK_EQ(sector_msf_bytes[2], 0);
    CHECK_EQ(sector_msf_bytes[3], 0);
    CHECK_EQ(sector_msf_bytes[0], 0x5a);
    CHECK_EQ(sector_msf_bytes[4], 0x5a);
}

/* SUB EBX,0x96 and not ADD: 300 - 150 is 150 frames, two seconds.  A body that
   dropped the adjustment would answer four seconds and one that added the
   pregap the textbook way would answer six. */
static void cdtoc_sector_to_msf_subtracts_the_pregap(void)
{
    sector_to_msf_into_poisoned_bytes(300);
    CHECK_EQ(sector_msf_bytes[1], 0);
    CHECK_EQ(sector_msf_bytes[2], 2);
    CHECK_EQ(sector_msf_bytes[3], 0);
}

/* The frame field is the remainder of the first DIV, so it runs 0 to 74 and the
   75th frame is the first second: the divisor is 0x4b and not 0x4a or 0x50. */
static void cdtoc_sector_to_msf_frames_roll_over_at_75(void)
{
    sector_to_msf_into_poisoned_bytes(150 + 74);
    CHECK_EQ(sector_msf_bytes[3], 74);
    CHECK_EQ(sector_msf_bytes[2], 0);
    sector_to_msf_into_poisoned_bytes(150 + 75);
    CHECK_EQ(sector_msf_bytes[3], 0);
    CHECK_EQ(sector_msf_bytes[2], 1);
}

/* The second divisor is 0x3c, so a minute is 60 seconds and 4500 frames. */
static void cdtoc_sector_to_msf_seconds_roll_over_at_60(void)
{
    sector_to_msf_into_poisoned_bytes(150 + 4500);
    CHECK_EQ(sector_msf_bytes[1], 1);
    CHECK_EQ(sector_msf_bytes[2], 0);
    CHECK_EQ(sector_msf_bytes[3], 0);
}

/* The whole chain at once on three fields that are all different from each
   other and from the poison: 15716 - 150 = 15566 = 3*4500 + 27*75 + 41.  This
   is also what pins the parameter order, since the stores run in the opposite
   order to the parameters -- swapping the minute and frame pointers swaps 3 and
   41 here. */
static void cdtoc_sector_to_msf_splits_all_three_fields(void)
{
    sector_to_msf_into_poisoned_bytes(15716);
    CHECK_EQ(sector_msf_bytes[1], 3);
    CHECK_EQ(sector_msf_bytes[2], 27);
    CHECK_EQ(sector_msf_bytes[3], 41);
}

/* Every store is byte-wide -- MOV byte ptr [ECX],DL, MOV byte ptr [ESI],DL and
   MOV byte ptr [EDX],BL -- which is what a caller aiming the three at three
   adjacent bytes relies on.  The guards keep their poison and none of the three
   destinations does. */
static void cdtoc_sector_to_msf_writes_one_byte_per_pointer(void)
{
    sector_to_msf_into_poisoned_bytes(15716);
    CHECK_EQ(sector_msf_bytes[0], 0x5a);
    CHECK_EQ(sector_msf_bytes[4], 0x5a);
    CHECK_EQ(sector_msf_bytes[1] == 0x5a, 0);
    CHECK_EQ(sector_msf_bytes[2] == 0x5a, 0);
    CHECK_EQ(sector_msf_bytes[3] == 0x5a, 0);
}

/* Sector 0 wraps: 0 - 150 as an unsigned 32-bit value is 4294967146, which is
   57266228*75 + 46 and 57266228 seconds is 954437*60 + 8, so the triple is
   0x45, 8, 46 -- 954437 is 0xe9045 and only its low byte is stored.  A signed
   intermediate would divide -150 instead and answer 0, -2, 0 in all three
   fields.  This is the same wrap fdps_cd_get_track_length_msf reaches from a
   zero-length track, and it lands on the same three bytes. */
static void cdtoc_sector_to_msf_wraps_below_the_pregap(void)
{
    sector_to_msf_into_poisoned_bytes(0);
    CHECK_EQ(sector_msf_bytes[1], 0x45);
    CHECK_EQ(sector_msf_bytes[2], 8);
    CHECK_EQ(sector_msf_bytes[3], 46);
}

/* The minute quotient is stored with MOV byte ptr [EDX],BL and no clamp, so 257
   minutes comes back as 1.  1156880 - 150 = 257*4500 + 3*75 + 5. */
static void cdtoc_sector_to_msf_minutes_wrap_at_256(void)
{
    sector_to_msf_into_poisoned_bytes(1156880);
    CHECK_EQ(sector_msf_bytes[1], 1);
    CHECK_EQ(sector_msf_bytes[2], 3);
    CHECK_EQ(sector_msf_bytes[3], 5);
}

void run_cdtoc_tests(void)
{
    RUN_TEST(cdtoc_unpack_splits_the_three_fields);
    RUN_TEST(cdtoc_unpack_ignores_the_top_byte);
    RUN_TEST(cdtoc_unpack_carries_full_width_fields);
    RUN_TEST(cdtoc_unpack_writes_one_byte_per_pointer);
    RUN_TEST(cdtoc_unpack_always_writes_all_three);
    RUN_TEST(cdtoc_sector_of_lead_in_is_zero);
    RUN_TEST(cdtoc_sector_scales_minutes_by_4500);
    RUN_TEST(cdtoc_sector_scales_seconds_by_75);
    RUN_TEST(cdtoc_sector_adds_frames_unscaled);
    RUN_TEST(cdtoc_sector_sums_all_three_fields);
    RUN_TEST(cdtoc_sector_goes_negative_below_the_lead_in);
    RUN_TEST(cdtoc_sector_ignores_the_top_byte);
    RUN_TEST(cdtoc_sector_carries_full_width_fields);
    RUN_TEST(cdtoc_upc_header_fields_sit_where_the_stores_land);
    RUN_TEST(cdtoc_upc_stages_an_ioctl_input_request);
    RUN_TEST(cdtoc_upc_asks_for_the_catalog_number);
    RUN_TEST(cdtoc_upc_publishes_status_and_the_block_field);
    RUN_TEST(cdtoc_upc_always_returns_one);
    RUN_TEST(cdtoc_disk_info_stages_an_ioctl_input_request);
    RUN_TEST(cdtoc_disk_info_asks_for_read_disk_info);
    RUN_TEST(cdtoc_disk_info_publishes_the_reply_block);
    RUN_TEST(cdtoc_disk_info_splits_the_leadout_address);
    RUN_TEST(cdtoc_disk_info_converts_the_leadout_to_a_sector);
    RUN_TEST(cdtoc_disk_info_publishes_the_status_word);
    RUN_TEST(cdtoc_track_info_stages_an_ioctl_input_request);
    RUN_TEST(cdtoc_track_info_asks_for_one_track);
    RUN_TEST(cdtoc_track_info_publishes_the_track_number);
    RUN_TEST(cdtoc_track_info_publishes_the_start_sector);
    RUN_TEST(cdtoc_track_info_masks_the_control_byte);
    RUN_TEST(cdtoc_track_info_publishes_the_status_word);
    RUN_TEST(cdtoc_track_length_refreshes_the_disc_summary);
    RUN_TEST(cdtoc_track_length_subtracts_the_two_endpoints);
    RUN_TEST(cdtoc_track_length_restores_the_selected_track);
    RUN_TEST(cdtoc_track_length_msf_starts_from_a_zero_length);
    RUN_TEST(cdtoc_track_length_msf_converts_the_wrapped_length);
    RUN_TEST(cdtoc_track_length_msf_writes_one_byte_per_pointer);
    RUN_TEST(cdtoc_track_length_msf_delegates_to_the_sector_query);
    RUN_TEST(cdtoc_disk_info_msf_reports_the_highest_track);
    RUN_TEST(cdtoc_disk_info_msf_converts_the_playing_time);
    RUN_TEST(cdtoc_disk_info_msf_writes_one_byte_per_pointer);
    RUN_TEST(cdtoc_disk_info_msf_refreshes_the_disc_summary);
    RUN_TEST(cdtoc_sector_to_msf_lead_in_is_all_zero);
    RUN_TEST(cdtoc_sector_to_msf_subtracts_the_pregap);
    RUN_TEST(cdtoc_sector_to_msf_frames_roll_over_at_75);
    RUN_TEST(cdtoc_sector_to_msf_seconds_roll_over_at_60);
    RUN_TEST(cdtoc_sector_to_msf_splits_all_three_fields);
    RUN_TEST(cdtoc_sector_to_msf_writes_one_byte_per_pointer);
    RUN_TEST(cdtoc_sector_to_msf_wraps_below_the_pregap);
    RUN_TEST(cdtoc_sector_to_msf_minutes_wrap_at_256);
}
