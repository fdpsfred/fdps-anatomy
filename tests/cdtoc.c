/* tests/cdtoc.c -- cover for src/cdtoc.c.
 *
 * So far this covers fdps_cd_unpack_msf at 0003bc3f and
 * fdps_cd_msf_to_sector at 0003bc78.
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
}
