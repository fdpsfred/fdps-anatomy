/* tests/cdaudio.c -- cover for src/cdaudio.c.
 *
 * So far this covers fdps_cd_seek at 0003c3fa, fdps_cd_play_audio_range at
 * 0003c452, fdps_cd_stop_audio at 0003c4a7 and fdps_cd_resume_audio at
 * 0003c4ff.
 *
 * Every expected value below comes from the instructions of those functions,
 * never from the emitted C.  For fdps_cd_seek:
 *
 *   MOV byte  ptr [ESP],     0x18   MOV byte  ptr [ESP+0x1],  0x0
 *   MOV byte  ptr [ESP+0x2], 0x83   MOV byte  ptr [ESP+0xd],  0x0
 *   MOV dword ptr [ESP+0xe], 0x0    MOV word  ptr [ESP+0x12], 0x0
 *   MOV EAX,dword ptr [ESP+0x1c] /  MOV dword ptr [ESP+0x14], EAX
 *   PUSH 0x18 / LEA EAX,[ESP+0x4] / PUSH EAX / PUSH dword ptr [0x00069de8]
 *   PUSH 0x18 / JMP 0003bea0, whose body is
 *     PUSH dword ptr [0x00069de8] / LEA EAX,[ESP+0x8] / PUSH EAX / CALL memcpy
 *     MOV EAX,dword ptr [ESP+0x3] / MOV [0x00069e20],AX
 *
 * and for fdps_cd_play_audio_range:
 *
 *   MOV byte  ptr [ESP],     0x16   MOV byte  ptr [ESP+0x1],  0x0
 *   MOV byte  ptr [ESP+0x2], 0x84   MOV byte  ptr [ESP+0xd],  0x0
 *   MOV EAX,dword ptr [ESP+0x1c] /  MOV dword ptr [ESP+0xe],  EAX
 *   MOV EAX,dword ptr [ESP+0x20] /  SUB EAX,dword ptr [ESP+0x1c] /
 *     MOV dword ptr [ESP+0x12], EAX
 *   PUSH 0x16 / LEA EAX,[ESP+0x4] / PUSH EAX / PUSH dword ptr [0x00069de8]
 *   PUSH 0x16 / JMP 0003bea0, the same shared epilogue
 *
 * and for fdps_cd_stop_audio, whose whole body this is:
 *
 *   MOV byte  ptr [ESP],     0xd    MOV byte  ptr [ESP+0x1],  0x0
 *   MOV byte  ptr [ESP+0x2], 0x85
 *   PUSH 0xd / LEA EAX,[ESP+0x4] / PUSH EAX / PUSH dword ptr [0x00069de8] /
 *     CALL memcpy / ADD ESP,0xc / CALL 0003bb7d
 *   PUSH 0xd / PUSH dword ptr [0x00069de8] / LEA EAX,[ESP+0x8] / PUSH EAX /
 *     CALL memcpy / ADD ESP,0xc
 *   MOV EAX,dword ptr [ESP+0x3] / MOV [0x00069e20],AX
 *
 * and for fdps_cd_resume_audio, whose whole body is three stores and a jump
 * into the trailer just quoted:
 *
 *   MOV byte  ptr [ESP],     0xd    MOV byte  ptr [ESP+0x1],  0x0
 *   MOV byte  ptr [ESP+0x2], 0x88   JMP 0003c4c2
 *
 * The request is issued for real, exactly as tests/cd.c issues its own: the
 * test executable runs under DOS/4GW inside DOSBox-X, which is the same DPMI
 * host the game has, and the drive named is
 * data_fdps_cdrom_drive_letter_index 0xff -- past every drive letter there is,
 * so MSCDEX rejects the request on the drive number before it follows ES:BX
 * into the request header, and where no CD-ROM drive is mounted at all there
 * is no MSCDEX handler on INT 2Fh and the multiplex returns untouched.  Either
 * way nothing outside the module writes the header block, so the bytes sitting
 * in it afterwards are exactly the ones the function staged.
 *
 * Nothing below asserts what any global holds on its own -- ticket 23 owns
 * their contents.  Every expectation is an immediate from the body, a poison
 * byte the test put there, or a relationship between two values the function
 * itself wrote.
 */
#include <stddef.h>
#include <string.h>

#include "testharn.h"
#include "fdpstype.h"
#include "gamedata.h"
#include "cd.h"
#include "cdaudio.h"

/* 0xa5 is a value no assertion below expects, so a byte the function did not
   write shows up as itself rather than as a coincidence. */
#define HEADER_POISON 0xa5

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
   module's own "already allocated" flag, and allocating again would leak the
   previous pair, which nothing in the module can free.

   Eight bytes past the end of the 0x18 the request declares are poisoned too,
   so a copy that ran for the 0x1a of the IOCTL shape, or for the whole struct,
   would show. */
static unsigned char *seek_with_a_poisoned_header(unsigned int sector)
{
    int i;

    if (data_fdps_cd_request_header_real_mode_seg == 0) {
        fdps_cd_alloc_dos_buffers();
    }
    for (i = 0; i < 0x20; i++) {
        data_fdps_cd_request_header_buffer[i] = HEADER_POISON;
    }
    data_fdps_cdrom_drive_letter_index = 0xff;
    fdps_cd_seek(sector);
    return data_fdps_cd_request_header_buffer;
}

/* Every store the body makes into the request header is an ESP displacement,
   so the struct's offsets are what decides which field each one lands on
   (contract H).  The sector store is the one that matters most here: it is a
   dword at 0x14, which is where the IOCTL shape this struct describes puts a
   16-bit field, and the emitted C reaches those four bytes through the address
   of that field.  If the field moved, the store would move with it. */
static void cdaudio_seek_header_fields_sit_where_the_stores_land(void)
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
}

/* Command code 0x83 is Seek and the header declares itself 24 bytes long, the
   extended shape those commands use.  Subunit 0 is the module's only drive.
   The byte at 0x0d is the addressing mode, and 0 there is what makes the
   sector field an HSG logical sector instead of a packed Red Book address --
   the whole reason this routine takes a sector number.

   Address and count are both zero because a seek transfers nothing: it is not
   staged through the IOCTL block the way every request in cd.c and cdtoc.c is,
   so the address field carries no real-mode far pointer at all. */
static void cdaudio_seek_stages_a_seek_command(void)
{
    unsigned char *header;

    header = seek_with_a_poisoned_header(0x00051234u);
    CHECK_EQ(header[0], 0x18);
    CHECK_EQ(header[1], 0);
    CHECK_EQ(header[2], 0x83);
    CHECK_EQ(header[0xd], 0);
    CHECK_EQ((long) staged_dword(header, 0xe), 0L);
    CHECK_EQ(staged_word(header, 0x12), 0);
}

/* MOV dword ptr [ESP+0x14],EAX is four bytes wide, so a sector past 65535 has
   to arrive whole.  0x00051234 is sector 332852, an hour and a quarter into a
   disc, chosen because none of its four bytes is the poison and its high half
   is neither zero nor the poison: a 16-bit store would leave 0xa5a5 above the
   low word.

   The second seek then names sector 1, which shares no byte with the first, to
   show the field is rewritten whole rather than having its low half updated
   over the previous request's high half. */
static void cdaudio_seek_sends_the_sector_as_a_full_dword(void)
{
    unsigned char *header;

    header = seek_with_a_poisoned_header(0x00051234u);
    CHECK_EQ((long) staged_dword(header, 0x14), 0x00051234L);

    header = seek_with_a_poisoned_header(1u);
    CHECK_EQ((long) staged_dword(header, 0x14), 1L);
}

/* PUSH 0x18 is the copy length in both directions, an immediate and not the
   header's own declared length, so the two bytes above the header stay as the
   test left them.  Those two are where the IOCTL shape's volume-ID pointer
   would still be running: a request built and copied at the 0x1a length the
   rest of the module uses would have overwritten them. */
static void cdaudio_seek_copies_exactly_the_declared_header(void)
{
    unsigned char *header;

    header = seek_with_a_poisoned_header(0x00051234u);
    CHECK_EQ(header[0x18], HEADER_POISON);
    CHECK_EQ(header[0x19], HEADER_POISON);
    CHECK_EQ(header[0x1f], HEADER_POISON);
}

/* The header is copied back out of the DOS block before anything is read out
   of it, so the published status word has to agree with the word still sitting
   at header offset 3.  What the driver leaves there is not the test's to
   decide -- a rejected request leaves the bytes the stack frame held, since
   offsets 3 to 0x0c are never initialised -- so what is pinned is the
   displacement and the width: reading a byte later, or taking the whole dword
   the load fetches instead of its low half, would break this. */
static void cdaudio_seek_publishes_the_driver_status_word(void)
{
    unsigned char *header;

    header = seek_with_a_poisoned_header(0x00051234u);
    CHECK_EQ(data_fdps_cd_last_request_status, staged_word(header, 3));
}

/* The play request is staged the same way the seek is, and for the same
   reason: the drive named is past every drive letter there is, so nothing
   outside the module writes the header block and the bytes left in it are the
   ones the function put there.  A range that is already playing is not a
   hazard here -- the driver never sees the request. */
static unsigned char *play_with_a_poisoned_header(unsigned int start_sector,
                                                  unsigned int end_sector)
{
    int i;

    if (data_fdps_cd_request_header_real_mode_seg == 0) {
        fdps_cd_alloc_dos_buffers();
    }
    for (i = 0; i < 0x20; i++) {
        data_fdps_cd_request_header_buffer[i] = HEADER_POISON;
    }
    data_fdps_cdrom_drive_letter_index = 0xff;
    fdps_cd_play_audio_range(start_sector, end_sector);
    return data_fdps_cd_request_header_buffer;
}

/* The sector count is a 32-bit store at header offset 0x12 (contract H), and
   the struct describes the IOCTL shape, which has two 16-bit fields there:
   transfer_byte_count at 0x12 and start_sector at 0x14.  The emitted C reaches
   those four bytes through the address of the first of them, so what keeps the
   store inside the pair is that the two are adjacent, four bytes together, and
   that volume_id_ptr does not begin until 0x16.  If any of the three moved,
   the store would move with it or spill past the pair. */
static void cdaudio_play_count_field_spans_the_pair_it_overlaps(void)
{
    CHECK_EQ((int) offsetof(struct fdps_cd_request_header, transfer_address),
             0xe);
    CHECK_EQ((int) offsetof(struct fdps_cd_request_header,
                            transfer_byte_count), 0x12);
    CHECK_EQ((int) offsetof(struct fdps_cd_request_header, start_sector), 0x14);
    CHECK_EQ((int) offsetof(struct fdps_cd_request_header, volume_id_ptr),
             0x16);
    CHECK_EQ((int) (sizeof(((struct fdps_cd_request_header *) 0)->
                           transfer_byte_count) +
                    sizeof(((struct fdps_cd_request_header *) 0)->
                           start_sector)), 4);
}

/* Command code 0x84 is Play Audio and the header declares itself 22 bytes
   long, the shape that command uses -- shorter than the 24 the seek declares
   and the 26 the IOCTL requests do.  Subunit 0 is the module's only drive, and
   the byte at 0x0d is the addressing mode, 0 for HSG logical sectors rather
   than packed Red Book addresses. */
static void cdaudio_play_stages_a_play_audio_command(void)
{
    unsigned char *header;

    header = play_with_a_poisoned_header(0x00012345u, 0x00051234u);
    CHECK_EQ(header[0], 0x16);
    CHECK_EQ(header[1], 0);
    CHECK_EQ(header[2], 0x84);
    CHECK_EQ(header[0xd], 0);
}

/* MOV dword ptr [ESP+0xe],EAX takes the first argument whole into the field
   the IOCTL shape uses for a 32-bit transfer address, so a start sector past
   65535 arrives intact.  0x00012345 is sector 74565, past the 16-bit ceiling,
   and shares no byte with the poison. */
static void cdaudio_play_sends_the_start_sector_as_a_full_dword(void)
{
    unsigned char *header;

    header = play_with_a_poisoned_header(0x00012345u, 0x00051234u);
    CHECK_EQ((long) staged_dword(header, 0xe), 0x00012345L);
}

/* What the drive is given is the count, not the end: SUB EAX,dword ptr
   [ESP+0x1c] before the store.  0x00051234 - 0x00012345 is 0x0003eeef, whose
   high half is neither zero nor the poison, so a 16-bit store would leave
   0xa5a5 above the low word and a store of the end sector instead of the
   difference would read back as 0x00051234.

   The second range starts at zero and ends at 0x00040000, a count whose low
   half is zero: a store that only wrote the low half of the field would leave
   the poison in the high half, and one that only wrote the high half would
   leave it in the low. */
static void cdaudio_play_sends_the_sector_count_as_a_full_dword(void)
{
    unsigned char *header;

    header = play_with_a_poisoned_header(0x00012345u, 0x00051234u);
    CHECK_EQ((long) staged_dword(header, 0x12), 0x0003eeefL);

    header = play_with_a_poisoned_header(0u, 0x00040000u);
    CHECK_EQ((long) staged_dword(header, 0x12), 0x00040000L);
}

/* There is no comparison anywhere in the body, so nothing keeps the range
   running forwards: a reversed pair is subtracted and stored exactly as it
   comes out, and the driver is asked for a count near 2^32.  0x0000000a -
   0x00000064 is 0xffffffa6. */
static void cdaudio_play_subtracts_without_checking_the_range(void)
{
    unsigned char *header;

    header = play_with_a_poisoned_header(0x00000064u, 0x0000000au);
    CHECK_EQ((long) staged_dword(header, 0x12), (long) 0xffffffa6UL);
}

/* PUSH 0x16 is the copy length in both directions, so the header ends two
   bytes below where the seek's does and four below the IOCTL requests'.  The
   bytes at 0x16 and 0x17 are where the seek's own header is still running:
   a request staged at any of the module's other lengths would have
   overwritten them. */
static void cdaudio_play_copies_exactly_the_declared_header(void)
{
    unsigned char *header;

    header = play_with_a_poisoned_header(0x00012345u, 0x00051234u);
    CHECK_EQ(header[0x16], HEADER_POISON);
    CHECK_EQ(header[0x17], HEADER_POISON);
    CHECK_EQ(header[0x1f], HEADER_POISON);
}

/* The header is copied back out of the DOS block before the status word is
   read out of it, so the published word has to agree with the word still at
   header offset 3.  What the driver leaves there is not the test's to decide
   -- offsets 3 to 0x0c are never initialised -- so what is pinned is the
   displacement and the width. */
static void cdaudio_play_publishes_the_driver_status_word(void)
{
    unsigned char *header;

    header = play_with_a_poisoned_header(0x00012345u, 0x00051234u);
    CHECK_EQ(data_fdps_cd_last_request_status, staged_word(header, 3));
}

/* The stop request is staged the same way the other two are, and for the same
   reason.  The block is poisoned across the whole 0x20 so that everything
   above the thirteen bytes this request declares shows up as untouched. */
static unsigned char *stop_with_a_poisoned_header(void)
{
    int i;

    if (data_fdps_cd_request_header_real_mode_seg == 0) {
        fdps_cd_alloc_dos_buffers();
    }
    for (i = 0; i < 0x20; i++) {
        data_fdps_cd_request_header_buffer[i] = HEADER_POISON;
    }
    data_fdps_cdrom_drive_letter_index = 0xff;
    fdps_cd_stop_audio();
    return data_fdps_cd_request_header_buffer;
}

/* The three stores the body makes are byte stores at ESP displacements 0, 1
   and 2, and the read-back is a word at displacement 3, so those four offsets
   are what the emitted C's field accesses have to land on (contract H).  The
   sizes matter as much as the offsets: header_length, subunit and command are
   each one byte, and status is two, which is what makes the fields the C names
   cover exactly the bytes the ESP displacements do. */
static void cdaudio_stop_header_fields_sit_where_the_stores_land(void)
{
    CHECK_EQ((int) offsetof(struct fdps_cd_request_header, header_length), 0);
    CHECK_EQ((int) offsetof(struct fdps_cd_request_header, subunit), 1);
    CHECK_EQ((int) offsetof(struct fdps_cd_request_header, command), 2);
    CHECK_EQ((int) offsetof(struct fdps_cd_request_header, status), 3);
    CHECK_EQ((int) sizeof(((struct fdps_cd_request_header *) 0)->
                          header_length), 1);
    CHECK_EQ((int) sizeof(((struct fdps_cd_request_header *) 0)->subunit), 1);
    CHECK_EQ((int) sizeof(((struct fdps_cd_request_header *) 0)->command), 1);
    CHECK_EQ((int) sizeof(((struct fdps_cd_request_header *) 0)->status), 2);
}

/* Command code 0x85 is Stop Audio and the header declares itself 13 bytes
   long, the bare device request header with no command block after it -- the
   shortest of the three lengths this file's requests use, against the seek's
   0x18 and the play's 0x16.  Subunit 0 is the module's only drive.

   There is deliberately no assertion on offset 0x0d here, unlike the seek and
   the play: this request has no addressing-mode byte, because 13 bytes is
   where its header ends. */
static void cdaudio_stop_stages_a_stop_audio_command(void)
{
    unsigned char *header;

    header = stop_with_a_poisoned_header();
    CHECK_EQ(header[0], 0xd);
    CHECK_EQ(header[1], 0);
    CHECK_EQ(header[2], 0x85);
}

/* PUSH 0xd is the copy length in both directions, so the request ends at
   offset 0x0d and everything above it is left as it was.  0x0d is where the
   play request's addressing-mode byte sits and 0x0e where its start sector
   begins: a stop staged at either of the other two lengths this file uses
   would have reached across them. */
static void cdaudio_stop_copies_exactly_the_declared_header(void)
{
    unsigned char *header;

    header = stop_with_a_poisoned_header();
    CHECK_EQ(header[0xd], HEADER_POISON);
    CHECK_EQ(header[0xe], HEADER_POISON);
    CHECK_EQ(header[0x15], HEADER_POISON);
    CHECK_EQ(header[0x17], HEADER_POISON);
    CHECK_EQ(header[0x1f], HEADER_POISON);
}

/* The same boundary again, but pinned against a real longer request instead of
   against poison, so it holds even if the poison fill were ever to change: a
   play request stages 0x16 bytes and puts its start sector as a full dword at
   offset 0x0e, and a stop issued straight afterwards must leave that dword
   standing.  0x00012345 is the same start sector the play tests above use. */
static void cdaudio_stop_leaves_the_previous_requests_tail_alone(void)
{
    unsigned char *header;

    header = play_with_a_poisoned_header(0x00012345u, 0x00051234u);
    CHECK_EQ((long) staged_dword(header, 0xe), 0x00012345L);

    data_fdps_cdrom_drive_letter_index = 0xff;
    fdps_cd_stop_audio();
    CHECK_EQ(header[0], 0xd);
    CHECK_EQ(header[2], 0x85);
    CHECK_EQ((long) staged_dword(header, 0xe), 0x00012345L);
}

/* The header is copied back out of the DOS block before the status word is
   read out of it, so the published word has to agree with the word still at
   header offset 3.  What the driver leaves there is not the test's to decide
   -- offsets 3 to 0x0c are never initialised -- so what is pinned is the
   displacement and the width: MOV EAX,[ESP+3] / MOV [0x00069e20],AX stores
   only the low half of the dword it loads, so a store of the whole dword, or a
   read from a different displacement, would break this. */
static void cdaudio_stop_publishes_the_driver_status_word(void)
{
    unsigned char *header;

    header = stop_with_a_poisoned_header();
    CHECK_EQ(data_fdps_cd_last_request_status, staged_word(header, 3));
}

/* The resume request stages through the same block as the other three, and is
   poisoned across the whole 0x20 for the same reason the stop request is. */
static unsigned char *resume_with_a_poisoned_header(void)
{
    int i;

    if (data_fdps_cd_request_header_real_mode_seg == 0) {
        fdps_cd_alloc_dos_buffers();
    }
    for (i = 0; i < 0x20; i++) {
        data_fdps_cd_request_header_buffer[i] = HEADER_POISON;
    }
    data_fdps_cdrom_drive_letter_index = 0xff;
    fdps_cd_resume_audio();
    return data_fdps_cd_request_header_buffer;
}

/* Command code 0x88 is Resume Audio Play and the header declares itself 13
   bytes long -- the same bare device request header the stop command uses, so
   the only thing separating the two requests on the wire is this one byte.
   Subunit 0 is the module's only drive.

   0x88 rather than 0x85 is the whole point of the assertion: the two functions
   share every instruction from 0003c4c2 onwards, so a resume that reached the
   driver carrying 0x85 would stop the music instead of restarting it and
   nothing else in either body would look wrong.

   As with the stop request there is deliberately no assertion on offset 0x0d:
   13 bytes is where this header ends, and it has no addressing-mode byte. */
static void cdaudio_resume_stages_a_resume_audio_command(void)
{
    unsigned char *header;

    header = resume_with_a_poisoned_header();
    CHECK_EQ(header[0], 0xd);
    CHECK_EQ(header[1], 0);
    CHECK_EQ(header[2], 0x88);
}

/* PUSH 0xd is the copy length in both directions in the shared trailer, so the
   resume request ends at offset 0x0d exactly as the stop request does and
   everything above it is left as it was. */
static void cdaudio_resume_copies_exactly_the_declared_header(void)
{
    unsigned char *header;

    header = resume_with_a_poisoned_header();
    CHECK_EQ(header[0xd], HEADER_POISON);
    CHECK_EQ(header[0xe], HEADER_POISON);
    CHECK_EQ(header[0x15], HEADER_POISON);
    CHECK_EQ(header[0x17], HEADER_POISON);
    CHECK_EQ(header[0x1f], HEADER_POISON);
}

/* The same boundary pinned against a real longer request instead of against
   poison: a play request stages 0x16 bytes with its start sector as a full
   dword at offset 0x0e, and a resume issued straight afterwards must leave
   that dword standing.  0x00012345 is the start sector the play tests use. */
static void cdaudio_resume_leaves_the_previous_requests_tail_alone(void)
{
    unsigned char *header;

    header = play_with_a_poisoned_header(0x00012345u, 0x00051234u);
    CHECK_EQ((long) staged_dword(header, 0xe), 0x00012345L);

    data_fdps_cdrom_drive_letter_index = 0xff;
    fdps_cd_resume_audio();
    CHECK_EQ(header[0], 0xd);
    CHECK_EQ(header[2], 0x88);
    CHECK_EQ((long) staged_dword(header, 0xe), 0x00012345L);
}

/* The header is copied back out of the DOS block before the status word is
   read out of it, so the published word has to agree with the word still at
   header offset 3.  What the driver leaves there is not the test's to decide
   -- offsets 3 to 0x0c are never initialised -- so what is pinned is the
   displacement and the width: MOV EAX,[ESP+3] / MOV [0x00069e20],AX stores
   only the low half of the dword it loads. */
static void cdaudio_resume_publishes_the_driver_status_word(void)
{
    unsigned char *header;

    header = resume_with_a_poisoned_header();
    CHECK_EQ(data_fdps_cd_last_request_status, staged_word(header, 3));
}

/* A stop and a resume issued back to back reach the driver as the same
   thirteen bytes apart from the command: the tail merge means everything else
   about them is literally the same instructions, and this is what says the two
   emitted bodies did not collapse into one behaviour.  Only the fields the
   bodies actually write are compared -- offsets 3 to 0x0c hold whatever the
   frame held and are not the test's to predict. */
static void cdaudio_resume_and_stop_differ_only_in_the_command_byte(void)
{
    unsigned char stop_header[3];
    unsigned char *header;

    header = stop_with_a_poisoned_header();
    stop_header[0] = header[0];
    stop_header[1] = header[1];
    stop_header[2] = header[2];

    header = resume_with_a_poisoned_header();
    CHECK_EQ(header[0], stop_header[0]);
    CHECK_EQ(header[1], stop_header[1]);
    CHECK_EQ(header[2] - stop_header[2], 0x88 - 0x85);
}

void run_cdaudio_tests(void)
{
    RUN_TEST(cdaudio_seek_header_fields_sit_where_the_stores_land);
    RUN_TEST(cdaudio_seek_stages_a_seek_command);
    RUN_TEST(cdaudio_seek_sends_the_sector_as_a_full_dword);
    RUN_TEST(cdaudio_seek_copies_exactly_the_declared_header);
    RUN_TEST(cdaudio_seek_publishes_the_driver_status_word);
    RUN_TEST(cdaudio_play_count_field_spans_the_pair_it_overlaps);
    RUN_TEST(cdaudio_play_stages_a_play_audio_command);
    RUN_TEST(cdaudio_play_sends_the_start_sector_as_a_full_dword);
    RUN_TEST(cdaudio_play_sends_the_sector_count_as_a_full_dword);
    RUN_TEST(cdaudio_play_subtracts_without_checking_the_range);
    RUN_TEST(cdaudio_play_copies_exactly_the_declared_header);
    RUN_TEST(cdaudio_play_publishes_the_driver_status_word);
    RUN_TEST(cdaudio_stop_header_fields_sit_where_the_stores_land);
    RUN_TEST(cdaudio_stop_stages_a_stop_audio_command);
    RUN_TEST(cdaudio_stop_copies_exactly_the_declared_header);
    RUN_TEST(cdaudio_stop_leaves_the_previous_requests_tail_alone);
    RUN_TEST(cdaudio_stop_publishes_the_driver_status_word);
    RUN_TEST(cdaudio_resume_stages_a_resume_audio_command);
    RUN_TEST(cdaudio_resume_copies_exactly_the_declared_header);
    RUN_TEST(cdaudio_resume_leaves_the_previous_requests_tail_alone);
    RUN_TEST(cdaudio_resume_publishes_the_driver_status_word);
    RUN_TEST(cdaudio_resume_and_stop_differ_only_in_the_command_byte);
}
