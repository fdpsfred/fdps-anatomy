/* tests/cdaudio.c -- cover for src/cdaudio.c.
 *
 * So far this covers fdps_cd_seek at 0003c3fa, fdps_cd_play_audio_range at
 * 0003c452, fdps_cd_stop_audio at 0003c4a7, fdps_cd_resume_audio at 0003c4ff,
 * fdps_cd_read_q_channel at 0003c5a6, fdps_cd_audio_is_idle at 0003c6e8,
 * fdps_cd_resolve_track_range at 0003c803, fdps_cd_play_track at 0003c85b,
 * fdps_cd_play_track_range at 0003c892, fdps_cd_set_music_track at 00030bf0 and
 * fdps_cd_music_repeat_poll at 00030c50.
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

/* fdps_cd_read_q_channel at 0003c5a6 is the only request in this file that
 * carries data in both directions, so unlike the four above it stages a control
 * block through the second DOS block as well as a header through the first.
 * Its immediates are
 *
 *   MOV byte  ptr [ESP],     0x1a   MOV byte  ptr [ESP+0x1],  0x0
 *   MOV byte  ptr [ESP+0x2], 0x3    MOV dword ptr [ESP+0x16], 0x0
 *   MOV word  ptr [ESP+0x14],0x0    MOV byte  ptr [ESP+0xd],  0x0
 *   MOV EAX,[0x00069da8] / MOV dword ptr [ESP+0xe],EAX
 *   MOV word  ptr [ESP+0x12],0x6
 *   MOV EAX,dword ptr [ESP+0x20] /  MOV byte ptr [EAX],0xc
 *   PUSH 0x1a / ... / CALL memcpy   PUSH 0xb / ... / CALL memcpy
 *   CALL 0003bb7d
 *   PUSH 0x1a / ... / CALL memcpy   PUSH 0xb / JMP 0003be27, which pushes the
 *     block pointers and jumps on to 0003c590 for the second copy and
 *     MOV EAX,dword ptr [ESP+0x3] / MOV [0x00069e20],AX
 *
 * The drive named is data_fdps_cdrom_drive_letter_index 0xff for the same
 * reason it is above: MSCDEX rejects the request on the drive number before it
 * follows ES:BX, so neither DOS block is written by anything outside the
 * module and what is sitting in them afterwards is exactly what the function
 * staged.  That is also why nothing below asserts a track or a position -- the
 * reply is the question coming back.
 *
 * One thing this cannot pin from outside: with no driver writing the IOCTL
 * block, the eleven bytes copied back into the caller's block are the eleven
 * that were just copied out of it, so the read-back direction is invisible
 * except at its far edge.  What is asserted is that edge -- the guard byte
 * after the caller's block is untouched, and the poison byte after the DOS
 * block's eleven is untouched -- which is what catches a copy that ran for the
 * struct's size or for one of the other lengths in this family.
 */

/* The caller's block with five guard bytes welded to it, so a copy that
   overran the eleven has somewhere to show up.  They are one object rather
   than two globals because Watcom promises nothing about the order or the
   neighbourliness of separate uninitialised globals (contract B). */
struct q_channel_probe_block {
    struct fdps_cd_q_channel_block block;
    unsigned char guard[5];
};

static struct q_channel_probe_block q_channel_probe;

/* The ramp starts at 0xa0 so every byte of the block is distinct and none of
   them is 0x0c, the control code the function stamps into byte 0 -- otherwise
   the stamp would be invisible.  Byte 0x0b of the IOCTL block is poisoned
   separately because it is the first byte the send memcpy must not reach. */
static void read_q_channel_from_a_rejected_drive(void)
{
    int i;

    if (data_fdps_cd_request_header_real_mode_seg == 0) {
        fdps_cd_alloc_dos_buffers();
    }
    for (i = 0; i < 0x20; i++) {
        data_fdps_cd_request_header_buffer[i] = HEADER_POISON;
    }
    q_channel_probe.block.control_code = 0xa0;
    q_channel_probe.block.control_adr = 0xa1;
    q_channel_probe.block.track_number = 0xa2;
    q_channel_probe.block.point_index = 0xa3;
    q_channel_probe.block.minute = 0xa4;
    q_channel_probe.block.second = 0xa5;
    q_channel_probe.block.frame = 0xa6;
    q_channel_probe.block.zero = 0xa7;
    q_channel_probe.block.absolute_minute = 0xa8;
    q_channel_probe.block.absolute_second = 0xa9;
    q_channel_probe.block.absolute_frame = 0xaa;
    for (i = 0; i < 5; i++) {
        q_channel_probe.guard[i] = (unsigned char) (0xab + i);
    }
    data_fdps_cd_ioctl_buffer[0xb] = 0x5a;
    data_fdps_cdrom_drive_letter_index = 0xff;
    fdps_cd_read_q_channel(&q_channel_probe.block);
}

/* Every field the caller reads out of the reply is a byte at a fixed offset in
   the block the driver filled, and the two memcpy lengths are the flat 0xb the
   body pushes, so the struct's size and its offsets are what decides which
   byte means what (contract H).  fdps_cd_read_audio_position takes the track
   from offset 2 -- MOV AL,[0x00069e58] against the block based at 0x00069e56 --
   and the position from 4, 5 and 6.

   The guard offset is asserted alongside them because the overrun checks below
   are worthless if the guard is not sitting immediately past the eleven. */
static void cdaudio_q_channel_block_fields_sit_where_the_driver_writes_them(void)
{
    CHECK_EQ((int) sizeof(struct fdps_cd_q_channel_block), 11);
    CHECK_EQ((int) offsetof(struct fdps_cd_q_channel_block, control_code), 0);
    CHECK_EQ((int) offsetof(struct fdps_cd_q_channel_block, track_number), 2);
    CHECK_EQ((int) offsetof(struct fdps_cd_q_channel_block, minute), 4);
    CHECK_EQ((int) offsetof(struct fdps_cd_q_channel_block, second), 5);
    CHECK_EQ((int) offsetof(struct fdps_cd_q_channel_block, frame), 6);
    CHECK_EQ((int) offsetof(struct q_channel_probe_block, guard), 11);
}

/* Command code 3 is IOCTL Input and the header declares itself 0x1a bytes, the
   full record -- this is the one request in the file whose declared length and
   struct size agree, so a struct that had grown or shrunk would break that
   equality rather than silently sending live stack or truncating the request.

   The addressing byte at 0x0d, the starting sector at 0x14 and the volume-ID
   pointer at 0x16 are all zero because an IOCTL request moves no disc data.
   The transfer address is the packed real-mode far pointer of the IOCTL block,
   not its flat linear address -- the driver runs in real mode and cannot use
   the flat one. */
static void cdaudio_q_channel_stages_an_ioctl_input_request(void)
{
    unsigned char *header;

    read_q_channel_from_a_rejected_drive();
    header = data_fdps_cd_request_header_buffer;
    CHECK_EQ(header[0], 0x1a);
    CHECK_EQ(header[0], (int) sizeof(struct fdps_cd_request_header));
    CHECK_EQ(header[1], 0);
    CHECK_EQ(header[2], 3);
    CHECK_EQ(header[0xd], 0);
    CHECK_EQ((long) staged_dword(header, 0xe),
             (long) data_fdps_cd_ioctl_buffer_real_mode_ptr);
    CHECK_EQ(staged_word(header, 0x14), 0);
    CHECK_EQ((long) staged_dword(header, 0x16), 0L);
}

/* MOV word ptr [ESP+0x12],0x6 asks the driver for six bytes while the block
   staged around it is eleven, and fdps_cd_read_audio_position then reads the
   frame byte at offset 6 -- one past the last byte the request declared.  The
   count is therefore neither sizeof the block nor the 0xb the two memcpy's use,
   and this is the assertion that says so: writing either of those instead would
   send the drive a different request and nothing else in the body would look
   wrong. */
static void cdaudio_q_channel_asks_for_six_bytes_of_an_eleven_byte_block(void)
{
    unsigned char *header;

    read_q_channel_from_a_rejected_drive();
    header = data_fdps_cd_request_header_buffer;
    CHECK_EQ(staged_word(header, 0x12), 6);
    CHECK_EQ((int) sizeof(struct fdps_cd_q_channel_block), 11);
    CHECK_EQ((int) offsetof(struct fdps_cd_q_channel_block, frame), 6);
}

/* MOV EAX,dword ptr [ESP+0x20] / MOV byte ptr [EAX],0xc stamps the control
   block code into the caller's own block before anything is copied, so the 0x0c
   has to be visible in both places afterwards -- in the caller's block because
   that is where the store landed, and in the staged block because the stamp is
   what was sent.  The ramp put 0xa0 in that byte, so an unstamped block would
   read back as 0xa0. */
static void cdaudio_q_channel_stamps_the_control_block_code(void)
{
    read_q_channel_from_a_rejected_drive();
    CHECK_EQ(q_channel_probe.block.control_code, 0xc);
    CHECK_EQ(data_fdps_cd_ioctl_buffer[0], 0xc);
}

/* PUSH 0xb / PUSH [ESP+0x24] / PUSH [0x00069da4] / CALL memcpy sends the
   caller's block as it stands: bytes 1..10 are never cleared or rewritten, so
   the ramp the test put there is what reaches the driver.  Byte 0x0b of the
   IOCTL block keeps its own poison, which is what pins the length at eleven
   rather than at the nine or the one the sibling requests in cd.c stage. */
static void cdaudio_q_channel_sends_the_callers_own_eleven_bytes(void)
{
    int i;

    read_q_channel_from_a_rejected_drive();
    for (i = 1; i < 0xb; i++) {
        CHECK_EQ(data_fdps_cd_ioctl_buffer[i], 0xa0 + i);
    }
    CHECK_EQ(data_fdps_cd_ioctl_buffer[0xb], 0x5a);
}

/* PUSH 0xb / PUSH [0x00069da4] / PUSH [ESP+0x28] in the shared epilogue copies
   the block back the other way, into the caller's block and nowhere else.  All
   eleven bytes have to match the block they came from, and the caller's guard
   bytes have to still hold their ramp values: a read-back of the struct's size,
   or of anything past eleven, would reach into them. */
static void cdaudio_q_channel_copies_eleven_bytes_back_to_the_caller(void)
{
    int i;

    read_q_channel_from_a_rejected_drive();
    CHECK_EQ(memcmp(&q_channel_probe.block, data_fdps_cd_ioctl_buffer, 0xb), 0);
    for (i = 0; i < 5; i++) {
        CHECK_EQ(q_channel_probe.guard[i], 0xab + i);
    }
}

/* PUSH 0x1a is the header copy length in both directions -- a literal, not the
   header's own length byte, though here the two agree -- so the six bytes above
   the record are left as the test poisoned them.  This is the longest header
   the file stages, so nothing else in it could have reached them either. */
static void cdaudio_q_channel_copies_exactly_the_declared_header(void)
{
    unsigned char *header;

    read_q_channel_from_a_rejected_drive();
    header = data_fdps_cd_request_header_buffer;
    CHECK_EQ(header[0x1a], HEADER_POISON);
    CHECK_EQ(header[0x1b], HEADER_POISON);
    CHECK_EQ(header[0x1f], HEADER_POISON);
}

/* The header is copied back out of the DOS block before the status word is read
   out of it, so the published word has to agree with the word still at header
   offset 3.  What the driver leaves there is not the test's to decide --
   offsets 3 to 0x0c are never initialised -- so what is pinned is the
   displacement and the width: MOV EAX,[ESP+3] / MOV [0x00069e20],AX stores only
   the low half of the dword it loads. */
static void cdaudio_q_channel_publishes_the_driver_status_word(void)
{
    unsigned char *header;

    read_q_channel_from_a_rejected_drive();
    header = data_fdps_cd_request_header_buffer;
    CHECK_EQ(data_fdps_cd_last_request_status, staged_word(header, 3));
}

/* fdps_cd_audio_is_idle at 0003c6e8 has no body of its own -- after the stack
 * probe it is
 *
 *   CALL 0003c34f / JMP 0003c6d0
 *
 * so what there is to pin is which request goes out, that it goes out before
 * the answer is read, and that the answer is the busy-bit predicate applied to
 * the word that request published.  The immediates quoted below are the
 * callee's, fdps_cdrom_read_device_status at 0003c34f:
 *
 *   MOV byte ptr [ESP],      0x1a   MOV byte ptr [ESP+0x2],  0x3
 *   MOV byte ptr [ESP+0x1c], 0x6
 *
 * and the predicate is fdps_cd_status_is_not_busy at 0003c6d0, whose whole body
 * is
 *
 *   MOV AX,[0x00069e20] / XOR AL,AL / AND AH,0x2 / MOVZX EAX,AX /
 *     TEST EAX,EAX / SETZ AL / MOVZX AX,AL / RET
 *
 * The drive named is 0xff for the same reason as everywhere above, so nothing
 * outside the module writes the request header and the status word that comes
 * back is whatever the callee's frame held at header offset 3 -- offsets 3 to
 * 0x0c are never initialised by anything in the module.  That word is not the
 * test's to predict, so nothing here asserts which way the answer comes out;
 * what is asserted is that the answer is the predicate of the word this call
 * itself published.  Which branch of the predicate runs for a given word is
 * settled in tests/cd.c, which reaches fdps_cd_status_is_not_busy directly and
 * can hand it a status word of its choosing.  From here it cannot: the only
 * route to the predicate goes through a request that overwrites the word first,
 * which is the whole behaviour under test.
 */

/* The block is poisoned across 0x20 the way the other helpers poison it, and a
   stop request is issued first so that the header the assertions read has a
   real earlier request in it rather than only poison: a stop declares 0x0d and
   carries command 0x85, neither of which the device status request uses.  The
   IOCTL block's first byte is poisoned separately, because the control block
   code is the one byte that says which IOCTL question was asked. */
static void audio_is_idle_after_a_stop_request(void)
{
    int i;

    if (data_fdps_cd_request_header_real_mode_seg == 0) {
        fdps_cd_alloc_dos_buffers();
    }
    data_fdps_cdrom_drive_letter_index = 0xff;
    fdps_cd_stop_audio();
    for (i = 0; i < 0x20; i++) {
        data_fdps_cd_request_header_buffer[i] = HEADER_POISON;
    }
    data_fdps_cd_ioctl_buffer[0] = HEADER_POISON;
}

/* The request that goes out is the Device Status IOCTL and not one of the four
   audio commands this file sends: header length 0x1a, command 3 -- IOCTL Input,
   against the 0x83, 0x84, 0x85 and 0x88 the others carry -- and control block
   code 6.  All three have to be there together, because the length alone is
   shared with the Q-channel request in this file and the command byte alone is
   shared with every IOCTL request in the module; the control code is what
   separates Device Status from them.

   The header was poisoned after the stop, so these bytes can only have got
   there through the call under test. */
static void cdaudio_audio_is_idle_issues_a_device_status_request(void)
{
    unsigned char *header;

    audio_is_idle_after_a_stop_request();
    fdps_cd_audio_is_idle();

    header = data_fdps_cd_request_header_buffer;
    CHECK_EQ(header[0], 0x1a);
    CHECK_EQ(header[1], 0);
    CHECK_EQ(header[2], 3);
    CHECK_EQ(data_fdps_cd_ioctl_buffer[0], 6);
}

/* The request runs before the answer is read, so the word the call leaves in
   data_fdps_cd_last_request_status is the one sitting at header offset 3 when
   it returns -- the same relationship the four request functions above are held
   to, and here it is what says a request ran at all.  The global is set to a
   value the poison cannot produce first: a call that answered from the previous
   request's word without issuing one of its own would leave 0x1234 standing
   against the 0xa5a5 in the header. */
static void cdaudio_audio_is_idle_publishes_a_fresh_status_word(void)
{
    unsigned char *header;

    audio_is_idle_after_a_stop_request();
    data_fdps_cd_last_request_status = 0x1234;
    fdps_cd_audio_is_idle();

    header = data_fdps_cd_request_header_buffer;
    CHECK_EQ(data_fdps_cd_last_request_status, staged_word(header, 3));
}

/* The answer is the inversion of bit 0x0200 of that word and of nothing else.
   AND AH,0x2 over XOR AL,AL is the mask, SETZ the inversion, so whichever way
   the driver left the word the returned flag has to agree with the test written
   out here.  Reading the global after the call rather than before is the point:
   it is this call's word, not the stop request's. */
static void cdaudio_audio_is_idle_answers_the_busy_bit_of_that_word(void)
{
    unsigned short idle_flag;

    audio_is_idle_after_a_stop_request();
    idle_flag = fdps_cd_audio_is_idle();

    CHECK_EQ(idle_flag, (data_fdps_cd_last_request_status & 0x0200) == 0);
}

/* Nothing touches the status word between the two calls, so the tail jump's
   target has to hand back what a direct call to it hands back.  This is what
   says the JMP 0003c6d0 was written out as the predicate it names and not as a
   test of its own that happens to agree on the value the driver left. */
static void cdaudio_audio_is_idle_returns_what_the_predicate_returns(void)
{
    unsigned short idle_flag;

    audio_is_idle_after_a_stop_request();
    idle_flag = fdps_cd_audio_is_idle();

    CHECK_EQ(idle_flag, fdps_cd_status_is_not_busy());
}

/* MOVZX AX,AL over SETZ AL is a zero-extended 0 or 1 in a 16-bit register, and
   the only caller, fdps_cd_music_repeat_poll at 00030c91, reads it as a word --
   TEST AX,AX.  So the flag is one of exactly two values and the return type is
   two bytes wide; anything else would hand that caller a value it only half
   looks at. */
static void cdaudio_audio_is_idle_returns_a_zero_or_one_word(void)
{
    unsigned short idle_flag;

    audio_is_idle_after_a_stop_request();
    idle_flag = fdps_cd_audio_is_idle();

    CHECK_EQ(idle_flag == 0 || idle_flag == 1, 1);
    CHECK_EQ((int) sizeof(fdps_cd_audio_is_idle()), 2);
}

/* Every call issues its own request: there is no caching and no guard in the
   body, so a second call after the header has been poisoned again has to put
   the same three bytes back.  This is what the caller's 0x4b-tick throttle
   exists for, and a body that asked once and then answered from the stored word
   would pass every assertion above. */
static void cdaudio_audio_is_idle_asks_the_drive_again_on_every_call(void)
{
    unsigned char *header;
    int i;

    audio_is_idle_after_a_stop_request();
    fdps_cd_audio_is_idle();

    for (i = 0; i < 0x20; i++) {
        data_fdps_cd_request_header_buffer[i] = HEADER_POISON;
    }
    data_fdps_cd_ioctl_buffer[0] = HEADER_POISON;
    fdps_cd_audio_is_idle();

    header = data_fdps_cd_request_header_buffer;
    CHECK_EQ(header[0], 0x1a);
    CHECK_EQ(header[2], 3);
    CHECK_EQ(data_fdps_cd_ioctl_buffer[0], 6);
}

/* fdps_cd_resolve_track_range at 0003c803 sends no request of its own.  What it
 * does is read four globals the table-of-contents queries publish and write the
 * two play-range globals, with one branch and up to two calls to
 * fdps_cdrom_read_track_info in between:
 *
 *   MOV EAX,[0x00069e01] / MOV [0x00069dec],EAX
 *   MOV AX,[0x00069dff]  / MOVSX EBX,AX
 *   MOVZX EDX,byte ptr [0x00069e07]
 *   LEA ECX,[EBX + 0x1] / CMP ECX,EDX / JLE 0003c83b
 *   MOV EAX,[0x00069e0b] / MOV [0x00069de4],EAX / RET
 *  0003c83b:
 *   INC EAX / CWDE / PUSH EAX / CALL 0003c0c8 / ADD ESP,0x4
 *   MOV EAX,[0x00069e01] / MOV [0x00069de4],EAX
 *   PUSH EBX / CALL 0003c0c8 / ADD ESP,0x4 / RET
 *
 * So the globals are the whole interface, and the tests below set them, call,
 * and read them back.  Setting one is not an assertion about what it holds --
 * ticket 23 owns their contents -- it is how the branch under test is reached.
 *
 * The two queries are real MSCDEX IOCTL Input requests, and the drive named is
 * data_fdps_cdrom_drive_letter_index 0xff for the same reason as everywhere
 * above: MSCDEX rejects the request on the drive number before it follows
 * ES:BX, so the bytes left in the two DOS blocks are exactly the ones
 * fdps_cdrom_read_track_info staged, and whether a query ran at all is visible
 * in them.  What a rejected query then publishes in
 * data_fdps_cd_track_start_sector is not the test's to predict: it is
 * fdps_cd_msf_to_sector applied to the uninitialised bytes of the reply block.
 *
 * That undefined value is still bounded, and the bound is what several
 * assertions below rest on.  fdps_cd_msf_to_sector is
 * minute * 4500 + second * 75 + frame - 150 over three zero-extended bytes, so
 * whatever a query publishes lies in 0..1166730 or, where the reply block held
 * a low address, in 0xffffff6a..0xffffffff.  The three sentinel sector values
 * below are outside both ranges, so a play-range global holding one of them
 * cannot have come out of a query.
 */

/* 0x00456789 is 4,548,489 and 0x00900000 is 9,437,184: both past the largest
   sector fdps_cd_msf_to_sector can return, so a query result can never be
   mistaken for either.  0xa5a5a5a5 is past it as well, which is what makes
   "the function wrote this global" checkable. */
#define RESOLVE_TRACK_START_SECTOR 0x00456789UL
#define RESOLVE_LEADOUT_SECTOR     0x00900000UL
#define RANGE_POISON               0xa5a5a5a5UL

/* Puts the four globals the body reads into a known state, poisons both DOS
   blocks and the two globals the body writes, and calls.  The header block is
   poisoned across 0x20 so that a request of any of the module's lengths shows
   up; the IOCTL block's first two bytes are poisoned because they are the
   control block code and the track number of a Read Audio Track Info query,
   which is how a query that did run says which track it asked about. */
static void resolve_the_range_of_track(short track_number,
                                       unsigned char highest_track)
{
    int i;

    if (data_fdps_cd_request_header_real_mode_seg == 0) {
        fdps_cd_alloc_dos_buffers();
    }
    for (i = 0; i < 0x20; i++) {
        data_fdps_cd_request_header_buffer[i] = HEADER_POISON;
    }
    data_fdps_cd_ioctl_buffer[0] = HEADER_POISON;
    data_fdps_cd_ioctl_buffer[1] = HEADER_POISON;

    data_fdps_cd_track_info_track_number = track_number;
    data_fdps_cd_highest_track_number = highest_track;
    data_fdps_cd_track_start_sector = (unsigned int) RESOLVE_TRACK_START_SECTOR;
    data_fdps_cd_leadout_sector = (unsigned int) RESOLVE_LEADOUT_SECTOR;
    data_fdps_cd_play_range_start_sector = (unsigned int) RANGE_POISON;
    data_fdps_cd_play_range_end_sector = (unsigned int) RANGE_POISON;

    data_fdps_cdrom_drive_letter_index = 0xff;
    fdps_cd_resolve_track_range();
}

/* MOV EAX,[0x00069e01] / MOV [0x00069dec],EAX is the first pair of
   instructions in the body, and on the last-track arm
   MOV EAX,[0x00069e0b] / MOV [0x00069de4],EAX is the whole of the rest: the
   range is the selected track's start address and the disc's lead-out, copied
   across whole.  Track 20 of a disc whose highest track is 20 is the last one,
   so 21 > 20 falls through to that arm. */
static void cdaudio_resolve_range_ends_the_last_track_at_the_lead_out(void)
{
    resolve_the_range_of_track(20, 20);

    CHECK_EQ((long) data_fdps_cd_play_range_start_sector,
             (long) RESOLVE_TRACK_START_SECTOR);
    CHECK_EQ((long) data_fdps_cd_play_range_end_sector,
             (long) RESOLVE_LEADOUT_SECTOR);
}

/* That arm ends at the RET at 0003c83a, before either CALL 0003c0c8, so no
   device request goes out and nothing the queries publish is disturbed.  The
   header block still holds the poison across every offset a request of any of
   the module's lengths would have written, the IOCTL block still holds its
   own, and the track globals still name the track the caller selected. */
static void cdaudio_resolve_range_asks_the_drive_nothing_for_the_last_track(void)
{
    unsigned char *header;

    resolve_the_range_of_track(20, 20);

    header = data_fdps_cd_request_header_buffer;
    CHECK_EQ(header[0], HEADER_POISON);
    CHECK_EQ(header[2], HEADER_POISON);
    CHECK_EQ(header[0x19], HEADER_POISON);
    CHECK_EQ(data_fdps_cd_ioctl_buffer[0], HEADER_POISON);
    CHECK_EQ(data_fdps_cd_ioctl_buffer[1], HEADER_POISON);
    CHECK_EQ(data_fdps_cd_track_info_track_number, 20);
    CHECK_EQ((long) data_fdps_cd_track_start_sector,
             (long) RESOLVE_TRACK_START_SECTOR);
}

/* Nothing in the body range checks the track number against the disc, so a
   track above the highest one takes the same arm the last track does -- 26 > 20
   -- and gets the lead-out as its end.  This is what says the test is > and not
   ==: a body that ended the range at the lead-out only for the highest track
   would query a track that is not there for every number above it. */
static void cdaudio_resolve_range_takes_a_track_past_the_disc_to_the_lead_out(void)
{
    unsigned char *header;

    resolve_the_range_of_track(25, 20);

    CHECK_EQ((long) data_fdps_cd_play_range_end_sector,
             (long) RESOLVE_LEADOUT_SECTOR);
    header = data_fdps_cd_request_header_buffer;
    CHECK_EQ(header[0], HEADER_POISON);
    CHECK_EQ(data_fdps_cd_ioctl_buffer[0], HEADER_POISON);
}

/* LEA ECX,[EBX + 0x1] / CMP ECX,EDX / JLE takes the jump when track + 1 is at
   or below the highest track number, so track 19 of a 20-track disc has a
   track after it and is queried for, while track 20 does not and is not.  The
   boundary is the one place the two arms are a single condition apart, and JL
   instead of JLE would send the second-to-last track to the lead-out.

   A Read Audio Track Info query is header length 0x1a with command 3, IOCTL
   Input, and control block code 0x0b in the IOCTL block -- the immediates
   MOV byte ptr [ESP],0x1a, MOV byte ptr [ESP+0x2],0x3 and
   MOV byte ptr [ESP+0x1c],0xb of fdps_cdrom_read_track_info at 0003c0c8. */
static void cdaudio_resolve_range_boundary_is_track_plus_one(void)
{
    unsigned char *header;

    resolve_the_range_of_track(19, 20);
    header = data_fdps_cd_request_header_buffer;
    CHECK_EQ(header[0], 0x1a);
    CHECK_EQ(header[1], 0);
    CHECK_EQ(header[2], 3);
    CHECK_EQ(data_fdps_cd_ioctl_buffer[0], 0xb);

    resolve_the_range_of_track(20, 20);
    header = data_fdps_cd_request_header_buffer;
    CHECK_EQ(header[0], HEADER_POISON);
    CHECK_EQ(data_fdps_cd_ioctl_buffer[0], HEADER_POISON);
}

/* MOVZX EDX,byte ptr [0x00069e07] zero-extends the highest track number, so a
   disc that reports 255 tracks bounds the walk at 255 and track 5 is queried
   for like any other.  Read as a signed byte it would be -1, 6 > -1 would fall
   through, and every track on that disc would end at the lead-out with no
   query at all -- which is exactly what the poison here would show
   (contract C). */
static void cdaudio_resolve_range_reads_the_highest_track_unsigned(void)
{
    unsigned char *header;

    resolve_the_range_of_track(5, 0xff);

    header = data_fdps_cd_request_header_buffer;
    CHECK_EQ(header[0], 0x1a);
    CHECK_EQ(header[2], 3);
    CHECK_EQ(data_fdps_cd_ioctl_buffer[0], 0xb);
}

/* MOV AX,[0x00069dff] / MOVSX EBX,AX sign-extends the track number, and the
   compare that follows is signed, JLE and not JBE.  A track number of -1 --
   which no published caller can reach, but which is the width and the sign the
   two loads work in -- makes track + 1 zero, and zero is at or below the
   highest track number of a disc that reports none, so the query arm runs.
   Read as an unsigned word it would be 65535, 65536 > 0 would fall through to
   the lead-out arm, and the poison below would still be standing.

   The track byte the last query left in the IOCTL block is 0xff, the low byte
   of -1 -- MOV AL,byte ptr [ESP+0x28] / MOV byte ptr [ESP+0x1d],AL in
   fdps_cdrom_read_track_info takes only that byte -- and the published track
   number is the full -1 the restoring query was handed. */
static void cdaudio_resolve_range_reads_the_track_number_signed(void)
{
    unsigned char *header;

    resolve_the_range_of_track(-1, 0);

    header = data_fdps_cd_request_header_buffer;
    CHECK_EQ(header[0], 0x1a);
    CHECK_EQ(data_fdps_cd_ioctl_buffer[0], 0xb);
    CHECK_EQ(data_fdps_cd_ioctl_buffer[1], 0xff);
    CHECK_EQ(data_fdps_cd_track_info_track_number, -1);
}

/* The range start is copied at 0003c80e-0003c813, before either query, and the
   queries republish data_fdps_cd_track_start_sector underneath it.  The
   sentinel is past every sector fdps_cd_msf_to_sector can return, so a copy
   made after a query could not produce it -- which is what pins the order.  It
   also has to be there on both arms, since the copy is above the branch. */
static void cdaudio_resolve_range_copies_the_start_before_it_queries(void)
{
    resolve_the_range_of_track(5, 20);
    CHECK_EQ((long) data_fdps_cd_play_range_start_sector,
             (long) RESOLVE_TRACK_START_SECTOR);

    resolve_the_range_of_track(20, 20);
    CHECK_EQ((long) data_fdps_cd_play_range_start_sector,
             (long) RESOLVE_TRACK_START_SECTOR);
}

/* On the query arm the range end is MOV EAX,[0x00069e01] / MOV [0x00069de4],EAX
   at 0003c846 -- the start address the query for the next track published --
   and never the lead-out.  Both sentinels are outside the range a query can
   publish, so the end holding either of them would say the wrong source was
   read: the lead-out means the branch went the wrong way, and the poison means
   nothing was written at all. */
static void cdaudio_resolve_range_ends_a_queried_track_where_the_next_starts(void)
{
    resolve_the_range_of_track(5, 20);

    CHECK_EQ((long) data_fdps_cd_play_range_end_sector ==
             (long) RESOLVE_LEADOUT_SECTOR, 0);
    CHECK_EQ((long) data_fdps_cd_play_range_end_sector ==
             (long) RANGE_POISON, 0);
}

/* PUSH EBX / CALL 0003c0c8 at 0003c850 is the closing query, which re-asks for
   the track this was entered on so that the track-info globals are left naming
   it.  EBX is the sign-extended word loaded at 0003c818, before the first query
   overwrote it, so what goes back is the caller's own track and not the next
   one.  Both the published track number and the track byte of the last request
   staged in the IOCTL block have to be 5 rather than the 6 the first query
   asked about; a body that dropped this call as unobserved -- nothing in the
   image reads what it republishes -- would leave both at 6, and would cost the
   drive one device request where the original costs two. */
static void cdaudio_resolve_range_restores_the_track_it_was_entered_on(void)
{
    resolve_the_range_of_track(5, 20);

    CHECK_EQ(data_fdps_cd_track_info_track_number, 5);
    CHECK_EQ(data_fdps_cd_ioctl_buffer[0], 0xb);
    CHECK_EQ(data_fdps_cd_ioctl_buffer[1], 5);
}

/* fdps_cd_play_track at 0003c85b is four calls and nothing else:
 *
 *   CALL 0003c4a7                     fdps_cd_stop_audio
 *   MOVSX EAX,word ptr [ESP + 0x4] / PUSH EAX / CALL 0003c0c8 / ADD ESP,0x4
 *   CALL 0003c803                     fdps_cd_resolve_track_range
 *   PUSH dword ptr [0x00069de4] / PUSH dword ptr [0x00069dec] /
 *     CALL 0003c452 / ADD ESP,0x8     fdps_cd_play_audio_range
 *
 * so what there is to pin down is the order of those four and which global
 * reaches which argument.  Three of them leave a mark: the query stages a
 * 0x1a-byte request header and stamps the IOCTL block, the resolve writes the
 * two play-range globals, and the play request stages a 0x16-byte header over
 * the top of the query's.  The stop request leaves none -- it stages thirteen
 * bytes into the same header block that the two later requests then overwrite
 * whole, and its status word is the same rejected-request word every other
 * request here publishes -- so no assertion below distinguishes it, and its
 * place at the head of the body rests on the disassembly alone.
 *
 * The sentinels and the poison are the ones the resolve tests above set up,
 * and for the same reasons: RESOLVE_TRACK_START_SECTOR, RESOLVE_LEADOUT_SECTOR
 * and RANGE_POISON all sit outside the 0..1166730 and 0xffffff6a..0xffffffff
 * that fdps_cd_msf_to_sector can produce, so a global holding one of them
 * cannot have come out of a query.  Every request here goes out for real to
 * drive index 0xff, which MSCDEX refuses on the drive number before it follows
 * ES:BX, so the bytes left in both DOS blocks are exactly the ones the module
 * staged.
 *
 * Header offsets 0x16 to 0x19 are the lever the ordering rests on.  They are
 * the volume_id_ptr field, which fdps_cdrom_read_track_info sets to zero
 * inside its 0x1a-byte request and which fdps_cd_play_audio_range's 0x16-byte
 * request does not reach: poisoned before the call, they say afterwards both
 * that a query ran and that the play request came after it.
 */

/* Puts the globals the chain reads into a known state and calls.
   preset_track_number is what data_fdps_cd_track_info_track_number holds on
   the way in: the query inside the function is what replaces it with track, so
   choosing the two to disagree is how the tests below tell whether the query
   really ran before fdps_cd_resolve_track_range read that global. */
static void play_the_track(short track, short preset_track_number,
                           unsigned char highest_track)
{
    int i;

    if (data_fdps_cd_request_header_real_mode_seg == 0) {
        fdps_cd_alloc_dos_buffers();
    }
    for (i = 0; i < 0x20; i++) {
        data_fdps_cd_request_header_buffer[i] = HEADER_POISON;
    }
    data_fdps_cd_ioctl_buffer[0] = HEADER_POISON;
    data_fdps_cd_ioctl_buffer[1] = HEADER_POISON;

    data_fdps_cd_track_info_track_number = preset_track_number;
    data_fdps_cd_highest_track_number = highest_track;
    data_fdps_cd_track_start_sector = (unsigned int) RESOLVE_TRACK_START_SECTOR;
    data_fdps_cd_leadout_sector = (unsigned int) RESOLVE_LEADOUT_SECTOR;
    data_fdps_cd_play_range_start_sector = (unsigned int) RANGE_POISON;
    data_fdps_cd_play_range_end_sector = (unsigned int) RANGE_POISON;

    data_fdps_cdrom_drive_letter_index = 0xff;
    fdps_cd_play_track(track);
}

/* The last CALL in the body is fdps_cd_play_audio_range at 0003c889, so the
   header block is left holding its request and not the query's or the stop's.
   The four immediates are that function's own -- MOV byte ptr [ESP],0x16,
   MOV byte ptr [ESP+0x1],0x0, MOV byte ptr [ESP+0x2],0x84 and
   MOV byte ptr [ESP+0xd],0x0 -- and 0x16 rather than 0x1a is what says the
   0x1a-byte query did not come last. */
static void cdaudio_play_track_leaves_a_play_request_in_the_header(void)
{
    unsigned char *header;

    play_the_track(20, 20, 20);

    header = data_fdps_cd_request_header_buffer;
    CHECK_EQ(header[0], 0x16);
    CHECK_EQ(header[1], 0);
    CHECK_EQ(header[2], 0x84);
    CHECK_EQ(header[0xd], 0);
}

/* CALL 0003c0c8 at 0003c870 is a Read Audio Track Info query, header length
   0x1a with volume_id_ptr zero at offset 0x16 and control block code 0x0b in
   the IOCTL block.  Those four zeroes stand where the poison was, which says
   the query ran; the poison still standing at 0x1a and 0x1f says nothing
   longer than 0x1a bytes was ever staged. */
static void cdaudio_play_track_queries_before_it_plays(void)
{
    unsigned char *header;

    play_the_track(20, 20, 20);

    header = data_fdps_cd_request_header_buffer;
    CHECK_EQ(header[0x16], 0);
    CHECK_EQ(header[0x17], 0);
    CHECK_EQ(header[0x18], 0);
    CHECK_EQ(header[0x19], 0);
    CHECK_EQ(header[0x1a], HEADER_POISON);
    CHECK_EQ(header[0x1f], HEADER_POISON);
    CHECK_EQ(data_fdps_cd_ioctl_buffer[0], 0xb);
}

/* MOVSX EAX,word ptr [ESP + 0x4] / PUSH EAX is the argument going into that
   query, so the track the CD layer is left pointing at is the one this
   function was handed.  fdps_cdrom_read_track_info publishes the low sixteen
   bits of it and stages the low byte of it as the control block's track
   field. */
static void cdaudio_play_track_selects_the_track_it_was_given(void)
{
    play_the_track(20, 20, 20);

    CHECK_EQ(data_fdps_cd_track_info_track_number, 20);
    CHECK_EQ(data_fdps_cd_ioctl_buffer[1], 20);
}

/* PUSH dword ptr [0x00069de4] at 0003c87d comes before
   PUSH dword ptr [0x00069dec] at 0003c883, so the start sector is the last
   pushed and therefore fdps_cd_play_audio_range's first argument.  That
   function puts its first argument at header offset 0x0e and the difference of
   the pair at 0x12, so a swapped pair would show up as the lead-out sentinel
   sitting in the transfer address.  Track 20 of a 20-track disc takes the
   lead-out arm of the resolve, which is what makes the two ends differ by a
   known amount here. */
static void cdaudio_play_track_sends_the_range_start_first(void)
{
    unsigned char *header;

    play_the_track(20, 20, 20);

    header = data_fdps_cd_request_header_buffer;
    CHECK_EQ((long) staged_dword(header, 0x0e),
             (long) data_fdps_cd_play_range_start_sector);
    CHECK_EQ((long) staged_dword(header, 0x0e) ==
             (long) RESOLVE_LEADOUT_SECTOR, 0);
    CHECK_EQ((long) staged_dword(header, 0x12),
             (long) (data_fdps_cd_play_range_end_sector -
                     data_fdps_cd_play_range_start_sector));
}

/* CALL 0003c803 at 0003c878 is what fills the pair in, and on the lead-out arm
   the end it publishes is data_fdps_cd_leadout_sector copied whole.  The
   preset track number is 5 and the track asked for is 20, so only the query at
   0003c870 running first can put the disc's last track in front of the
   resolve; the sentinel arriving in the play request's count field is what
   says the resolve ran between the query and the play. */
static void cdaudio_play_track_ends_the_last_track_at_the_lead_out(void)
{
    unsigned char *header;

    play_the_track(20, 5, 20);

    header = data_fdps_cd_request_header_buffer;
    CHECK_EQ((long) data_fdps_cd_play_range_end_sector,
             (long) RESOLVE_LEADOUT_SECTOR);
    CHECK_EQ((long) staged_dword(header, 0x12),
             (long) ((unsigned int) RESOLVE_LEADOUT_SECTOR -
                     data_fdps_cd_play_range_start_sector));
}

/* The range start the resolve copies is data_fdps_cd_track_start_sector, which
   the query at 0003c870 republishes on its way past.  The sentinel that global
   was loaded with cannot survive that, and the poison in the play-range global
   cannot survive the copy, so what is left has to be a query's answer -- and a
   query's answer is fdps_cd_msf_to_sector over three zero-extended bytes,
   minute * 4500 + second * 75 + frame - 150, which is bounded by -150 below
   and 255 * 4500 + 255 * 75 + 255 - 150 = 1166730 above.  On the lead-out arm
   no later query runs, so the global it was copied from still holds the same
   value afterwards. */
static void cdaudio_play_track_resolves_the_range_from_its_own_query(void)
{
    unsigned int start_sector;

    play_the_track(20, 20, 20);

    start_sector = data_fdps_cd_play_range_start_sector;
    CHECK_EQ((long) start_sector == (long) RESOLVE_TRACK_START_SECTOR, 0);
    CHECK_EQ((long) start_sector == (long) RANGE_POISON, 0);
    CHECK_EQ((long) start_sector, (long) data_fdps_cd_track_start_sector);
    CHECK_EQ(start_sector <= 1166730UL || start_sector >= 0xffffff6aUL, 1);
}

/* The other direction of the same ordering.  The preset track number is 20,
   which on a 20-track disc would send the resolve down its lead-out arm; the
   track asked for is 5, which has a track after it and takes the query arm
   instead.  So an end that is neither the lead-out sentinel nor the poison
   says the resolve read the query's track number and not the one that was
   standing there before the call, and the restore inside the resolve leaves
   the layer naming track 5. */
static void cdaudio_play_track_points_the_layer_before_it_resolves(void)
{
    play_the_track(5, 20, 20);

    CHECK_EQ(data_fdps_cd_track_info_track_number, 5);
    CHECK_EQ(data_fdps_cd_ioctl_buffer[1], 5);
    CHECK_EQ((long) data_fdps_cd_play_range_end_sector ==
             (long) RESOLVE_LEADOUT_SECTOR, 0);
    CHECK_EQ((long) data_fdps_cd_play_range_end_sector ==
             (long) RANGE_POISON, 0);
}

/* There is no compare and no branch anywhere in the body, and none of the four
   callees refuses a track number either: track 25 of a 20-track disc is
   queried for like any other, ends at the lead-out because 26 > 20, and the
   play request goes out on that range.  A body that guarded the track number
   would leave the header holding the query rather than the play request. */
static void cdaudio_play_track_does_not_refuse_a_track_past_the_disc(void)
{
    unsigned char *header;

    play_the_track(25, 5, 20);

    header = data_fdps_cd_request_header_buffer;
    CHECK_EQ(data_fdps_cd_track_info_track_number, 25);
    CHECK_EQ((long) data_fdps_cd_play_range_end_sector,
             (long) RESOLVE_LEADOUT_SECTOR);
    CHECK_EQ(header[0], 0x16);
    CHECK_EQ(header[2], 0x84);
}

/* fdps_cd_set_music_track at 00030bf0 is one store and two branches:
 *
 *   CMP byte ptr [0x00060008],0x0 / JNZ 00030c0c
 *   MOV dword ptr [EBP+0x14],0xffffffff       the music-off override
 *   MOV EAX,dword ptr [EBP+0x14] / MOV [0x00069d54],EAX
 *   CMP dword ptr [0x00069d54],-0x1 / JNZ 00030c24
 *   CALL 0003c4a7 / JMP 00030c3c              fdps_cd_stop_audio
 *   CMP byte ptr [0x00060008],0x0 / JZ 00030c3c
 *   MOV EAX,[0x00069d54] / INC EAX / PUSH EAX / CALL 0003c85b / ADD ESP,0x4
 *
 * so there are three things to pin down: which value reaches
 * data_fdps_audio_cd_current_music_index, which of the two callees runs, and
 * that the track handed to fdps_cd_play_track is one more than the index.
 *
 * The two callees leave different marks in the request header block, which is
 * what the arms are told apart by.  fdps_cd_stop_audio stages thirteen bytes
 * -- header[0] 0xd and header[2] 0x85 -- and touches nothing above offset
 * 0x0c and nothing in the IOCTL block at all.  fdps_cd_play_track ends in a
 * Play Audio request, header[0] 0x16 and header[2] 0x84, and on the way there
 * runs fdps_cdrom_read_track_info at least twice, each of which stamps 0x0b
 * into the IOCTL block and the track's low byte beside it.  So header[0]
 * separates the arms, and a poisoned IOCTL block still holding its poison says
 * no query ran -- which is the evidence that the stop arm did not fall through
 * into the play arm.
 *
 * Which track was asked for is read back out of
 * data_fdps_cd_track_info_track_number and data_fdps_cd_ioctl_buffer[1]:
 * fdps_cd_resolve_track_range re-queries the track it was entered on as its
 * last act, so after the chain both name the track fdps_cd_play_track was
 * handed.  That is what makes the +1 visible from outside.
 *
 * Every request goes out for real to drive index 0xff, refused on the drive
 * number before MSCDEX follows ES:BX, exactly as the tests above issue theirs.
 */

/* A music index nothing below expects and no arm can produce, so the global
   still holding it afterwards would mean the store at 00030c0f never ran. */
#define CURRENT_MUSIC_POISON 0x5a5a5a5aL

/* What data_fdps_cd_track_info_track_number holds on the way in.  It is past
   every track any case below asks for, so the global still holding it says no
   query ran and the play arm was not taken. */
#define MUSIC_PRESET_TRACK 20

/* Puts the music setting, the published index and everything the chain under
   fdps_cd_play_track reads into a known state, then calls. */
static void set_the_music_track(int music_index, unsigned char music_enabled)
{
    int i;

    if (data_fdps_cd_request_header_real_mode_seg == 0) {
        fdps_cd_alloc_dos_buffers();
    }
    for (i = 0; i < 0x20; i++) {
        data_fdps_cd_request_header_buffer[i] = HEADER_POISON;
    }
    data_fdps_cd_ioctl_buffer[0] = HEADER_POISON;
    data_fdps_cd_ioctl_buffer[1] = HEADER_POISON;

    data_fdps_cd_track_info_track_number = MUSIC_PRESET_TRACK;
    data_fdps_cd_highest_track_number = 20;
    data_fdps_cd_track_start_sector = (unsigned int) RESOLVE_TRACK_START_SECTOR;
    data_fdps_cd_leadout_sector = (unsigned int) RESOLVE_LEADOUT_SECTOR;
    data_fdps_cd_play_range_start_sector = (unsigned int) RANGE_POISON;
    data_fdps_cd_play_range_end_sector = (unsigned int) RANGE_POISON;

    data_fdps_cdrom_drive_letter_index = 0xff;
    data_fdps_audio_bgm_enabled_flag = music_enabled;
    data_fdps_audio_cd_current_music_index = (int) CURRENT_MUSIC_POISON;

    fdps_cd_set_music_track(music_index);
}

/* MOV EAX,dword ptr [EBP+0x14] / MOV [0x00069d54],EAX at 00030c0c is
   unconditional on this arm, so the index the caller asked for is what gets
   published; the poison standing afterwards would say nothing was stored.
   fdps_title_screen and fdps_play_ending_credit_roll both push 1. */
static void cdaudio_set_music_publishes_the_index_it_was_given(void)
{
    set_the_music_track(1, 1);

    CHECK_EQ(data_fdps_audio_cd_current_music_index, 1);
}

/* The else arm ends in CALL 0003c85b, whose last request is a Play Audio --
   MOV byte ptr [ESP],0x16 and MOV byte ptr [ESP+0x2],0x84 inside
   fdps_cd_play_audio_range.  0x16 rather than 0xd is what says the stop arm
   was not the one taken. */
static void cdaudio_set_music_plays_when_the_music_is_on(void)
{
    unsigned char *header;

    set_the_music_track(1, 1);

    header = data_fdps_cd_request_header_buffer;
    CHECK_EQ(header[0], 0x16);
    CHECK_EQ(header[2], 0x84);
}

/* MOV EAX,[0x00069d54] / INC EAX / PUSH EAX at 00030c2d-00030c33: the track
   number is one more than the music index, because the game counts its music
   from 0 while the disc's audio tracks start at 2.  The chain leaves both
   data_fdps_cd_track_info_track_number and the IOCTL block's track byte naming
   the track it was handed, so index 1 has to come out as track 2 and index 2
   as track 3 -- two points, which no fixed track number and no other offset
   fits. */
static void cdaudio_set_music_plays_the_track_one_past_the_index(void)
{
    set_the_music_track(1, 1);
    CHECK_EQ(data_fdps_cd_track_info_track_number, 2);
    CHECK_EQ(data_fdps_cd_ioctl_buffer[1], 2);

    set_the_music_track(2, 1);
    CHECK_EQ(data_fdps_cd_track_info_track_number, 3);
    CHECK_EQ(data_fdps_cd_ioctl_buffer[1], 3);
}

/* CMP dword ptr [0x00069d54],-0x1 is the only comparison the published value
   meets, so 0 is an ordinary music index and not a second way of saying "no
   music": it plays track 1.  A body that tested for zero as well would take
   the stop arm and leave a 0xd in the header. */
static void cdaudio_set_music_treats_index_zero_as_a_real_track(void)
{
    unsigned char *header;

    set_the_music_track(0, 1);

    header = data_fdps_cd_request_header_buffer;
    CHECK_EQ(data_fdps_audio_cd_current_music_index, 0);
    CHECK_EQ(header[0], 0x16);
    CHECK_EQ(data_fdps_cd_track_info_track_number, 1);
    CHECK_EQ(data_fdps_cd_ioctl_buffer[1], 1);
}

/* CALL 0003c4a7 on the -1 arm is fdps_cd_stop_audio, whose whole request is
   thirteen bytes: header[0] 0xd, header[1] 0 and header[2] 0x85, with offset
   0x0d never written.  The poison still standing there says nothing longer was
   staged over it, and the poison still in the IOCTL block says no
   Read Audio Track Info query ran -- together, that the play arm was not
   reached.  fdps_run_village_phase pushes -1 on leaving the village. */
static void cdaudio_set_music_stops_the_drive_on_minus_one(void)
{
    unsigned char *header;

    set_the_music_track(-1, 1);

    header = data_fdps_cd_request_header_buffer;
    CHECK_EQ(data_fdps_audio_cd_current_music_index, -1);
    CHECK_EQ(header[0], 0xd);
    CHECK_EQ(header[1], 0);
    CHECK_EQ(header[2], 0x85);
    CHECK_EQ(header[0xd], HEADER_POISON);
    CHECK_EQ(data_fdps_cd_ioctl_buffer[0], HEADER_POISON);
    CHECK_EQ(data_fdps_cd_track_info_track_number, MUSIC_PRESET_TRACK);
}

/* The music-off test at 00030bfc sits ABOVE the store, and what it does is
   MOV dword ptr [EBP+0x14],0xffffffff -- it rewrites the request rather than
   skipping it.  So with music off the published index is -1 and not the 2 the
   caller asked for, and the drive is stopped.  This is the pitfall the whole
   function turns on: an early-out would leave 2 standing here, and
   fdps_cd_music_repeat_poll would keep restarting track 3 after the player
   switched the music off. */
static void cdaudio_set_music_off_discards_the_requested_index(void)
{
    unsigned char *header;

    set_the_music_track(2, 0);

    header = data_fdps_cd_request_header_buffer;
    CHECK_EQ(data_fdps_audio_cd_current_music_index, -1);
    CHECK_EQ(header[0], 0xd);
    CHECK_EQ(header[2], 0x85);
    CHECK_EQ(data_fdps_cd_ioctl_buffer[0], HEADER_POISON);
    CHECK_EQ(data_fdps_cd_track_info_track_number, MUSIC_PRESET_TRACK);
}

/* Music off and -1 asked for is the same arm reached from the other side: the
   override writes the -1 that was already there, and the stop request goes out
   just the same. */
static void cdaudio_set_music_off_with_minus_one_still_stops(void)
{
    unsigned char *header;

    set_the_music_track(-1, 0);

    header = data_fdps_cd_request_header_buffer;
    CHECK_EQ(data_fdps_audio_cd_current_music_index, -1);
    CHECK_EQ(header[0], 0xd);
    CHECK_EQ(header[2], 0x85);
    CHECK_EQ(data_fdps_cd_ioctl_buffer[0], HEADER_POISON);
}

/* The override is applied to the argument slot and to nothing that outlives
   the call, so a music-off call does not disable the next one: switching the
   setting back on and asking again plays, and the second call's own reading of
   the setting at 00030bfc is what decides it. */
static void cdaudio_set_music_off_does_not_latch(void)
{
    unsigned char *header;

    set_the_music_track(2, 0);
    CHECK_EQ(data_fdps_audio_cd_current_music_index, -1);

    set_the_music_track(2, 1);

    header = data_fdps_cd_request_header_buffer;
    CHECK_EQ(data_fdps_audio_cd_current_music_index, 2);
    CHECK_EQ(header[0], 0x16);
    CHECK_EQ(data_fdps_cd_track_info_track_number, 3);
}

/* ---------------------------------------------------------------------------
 * fdps_cd_music_repeat_poll at 00030c50.
 *
 * The whole body, which every expectation below is read off:
 *
 *   MOV EAX,[0x00069d74] / CMP EAX,dword ptr [0x00069d64] / JZ 00030cbb
 *   INC dword ptr [0x00060170]
 *   CMP dword ptr [0x00060170],0x4b / JNZ 00030cb1
 *   CMP dword ptr [0x00069d54],-0x1  / JZ  00030c8a
 *   CMP byte  ptr [0x00060008],0x0   / JNZ 00030c8c
 *   00030c8a: JMP 00030c96
 *   00030c8c: CALL 0003c6e8 / TEST AX,AX / JNZ 00030c98
 *   00030c96: JMP 00030ca7
 *   00030c98: MOV EAX,[0x00069d54] / INC EAX / PUSH EAX / CALL 0003c85b /
 *             ADD ESP,0x4
 *   00030ca7: MOV dword ptr [0x00060170],0x0
 *   00030cb1: MOV EAX,[0x00069d64] / MOV [0x00069d74],EAX
 *   00030cbb: POP EBP / POP EDI / POP ESI / POP EBX / RET
 *
 * Two of the branches turn on a value that came back from a CALL, and the CALL
 * is fdps_cd_audio_is_idle, whose answer is the busy bit 0x0200 of the status
 * word its own device request leaves behind.  No driver answers that request
 * here -- drive index 0xff is refused on the drive number, as everywhere above
 * -- so the word that comes back is the one that went out, and the one that
 * went out is fdps_cdrom_read_device_status's uninitialised local.  That local
 * is stack, so painting the stack the poll's callees are about to use decides
 * the answer: 0x00 leaves the busy bit clear and the drive reads as idle, 0xff
 * sets it and the drive reads as still playing.  Both arms are therefore
 * reachable on demand, and the paint is asserted to work before anything is
 * built on it.
 * ------------------------------------------------------------------------ */

/* A tick number nothing else in the file uses.  The latch is armed one above it
   so that the poll always sees a new tick unless a case says otherwise. */
#define POLL_TICK 5u

/* One short of the 0x4b the body fires on, so a single call reaches the
   period. */
#define POLL_PERIOD_MINUS_ONE 0x4a

/* Fills the stack the poll's callees are about to run in with one byte, so
   that fdps_cdrom_read_device_status's uninitialised status field -- the word
   fdps_cd_audio_is_idle reads bit 0x0200 out of -- holds that byte repeated
   rather than whatever the previous call chain left.  The array is far larger
   than the four frames involved, so nothing here depends on where any of them
   lands. */
static void paint_the_request_frame(unsigned char value)
{
    unsigned char paint[2048];

    memset(paint, value, sizeof(paint));
}

/* Puts the poll's five globals and everything the chain under it reads into a
   known state, with the tick latch armed one above the counter so that the
   outer guard lets the body through.  The request header and the IOCTL block
   are poisoned, so anything found in them afterwards was staged by this call. */
static void arm_the_music_poll(int counter_preset, int music_index,
                               unsigned char music_enabled)
{
    int i;

    if (data_fdps_cd_request_header_real_mode_seg == 0) {
        fdps_cd_alloc_dos_buffers();
    }
    for (i = 0; i < 0x20; i++) {
        data_fdps_cd_request_header_buffer[i] = HEADER_POISON;
    }
    data_fdps_cd_ioctl_buffer[0] = HEADER_POISON;
    data_fdps_cd_ioctl_buffer[1] = HEADER_POISON;

    data_fdps_cd_track_info_track_number = MUSIC_PRESET_TRACK;
    data_fdps_cd_highest_track_number = 20;
    data_fdps_cd_track_start_sector = (unsigned int) RESOLVE_TRACK_START_SECTOR;
    data_fdps_cd_leadout_sector = (unsigned int) RESOLVE_LEADOUT_SECTOR;
    data_fdps_cd_play_range_start_sector = (unsigned int) RANGE_POISON;
    data_fdps_cd_play_range_end_sector = (unsigned int) RANGE_POISON;

    data_fdps_cdrom_drive_letter_index = 0xff;
    data_fdps_audio_bgm_enabled_flag = music_enabled;
    data_fdps_audio_cd_current_music_index = music_index;

    data_fdps_audio_cd_repeat_tick_counter = counter_preset;
    data_fdps_timer_tick_counter = POLL_TICK;
    data_fdps_audio_cd_repeat_last_tick = POLL_TICK + 1u;
}

/* The paint has to decide the drive's answer for the two arms below to be
   reachable on demand, so that is asserted on its own rather than assumed: a
   frame painted 0x00 carries no busy bit and fdps_cd_status_is_not_busy's
   (status & 0x0200) == 0 has to come out 1, a frame painted 0xff sets the bit
   and it has to come out 0.  If this ever stops holding it is this test that
   says so, instead of the play-arm tests quietly passing on the wrong arm. */
static void cdaudio_repeat_poll_paint_decides_what_the_drive_says(void)
{
    if (data_fdps_cd_request_header_real_mode_seg == 0) {
        fdps_cd_alloc_dos_buffers();
    }
    data_fdps_cdrom_drive_letter_index = 0xff;

    paint_the_request_frame(0x00);
    CHECK_EQ(fdps_cd_audio_is_idle(), 1);

    paint_the_request_frame(0xff);
    CHECK_EQ(fdps_cd_audio_is_idle(), 0);
}

/* MOV EAX,[0x00069d74] / CMP EAX,dword ptr [0x00069d64] / JZ 00030cbb goes
   straight to the epilogue, so a tick the poll has already finished on costs
   nothing at all: the counter does not move, the latch does not move and no
   request is staged.  This is what lets the game loops call this every pass. */
static void cdaudio_repeat_poll_ignores_a_tick_it_has_already_seen(void)
{
    arm_the_music_poll(10, 1, 1);
    data_fdps_timer_tick_counter = 1234;
    data_fdps_audio_cd_repeat_last_tick = 1234;

    paint_the_request_frame(0x00);
    fdps_cd_music_repeat_poll();

    CHECK_EQ(data_fdps_audio_cd_repeat_tick_counter, 10);
    CHECK_EQ(data_fdps_audio_cd_repeat_last_tick, 1234);
    CHECK_EQ(data_fdps_cd_request_header_buffer[0], HEADER_POISON);
    CHECK_EQ(data_fdps_cd_track_info_track_number, MUSIC_PRESET_TRACK);
}

/* INC dword ptr [0x00060170] at 00030c69 and MOV EAX,[0x00069d64] /
   MOV [0x00069d74],EAX at 00030cb1: a tick the poll has not seen steps the
   counter by one and refreshes the latch onto the live counter. */
static void cdaudio_repeat_poll_steps_the_counter_on_a_new_tick(void)
{
    arm_the_music_poll(10, -1, 0);

    fdps_cd_music_repeat_poll();

    CHECK_EQ(data_fdps_audio_cd_repeat_tick_counter, 11);
    CHECK_EQ(data_fdps_audio_cd_repeat_last_tick, POLL_TICK);
}

/* The counter is inside the latch's arm, so it counts ticks and not calls:
   three calls in one tick step it once, and it moves again only when the timer
   does.  A body that stepped it per call would reach the period 75 times
   sooner and put a real device request on the per-frame path. */
static void cdaudio_repeat_poll_counts_ticks_and_not_calls(void)
{
    arm_the_music_poll(0, -1, 0);

    fdps_cd_music_repeat_poll();
    fdps_cd_music_repeat_poll();
    fdps_cd_music_repeat_poll();
    CHECK_EQ(data_fdps_audio_cd_repeat_tick_counter, 1);

    data_fdps_timer_tick_counter = POLL_TICK + 1u;
    fdps_cd_music_repeat_poll();
    CHECK_EQ(data_fdps_audio_cd_repeat_tick_counter, 2);
    CHECK_EQ(data_fdps_audio_cd_repeat_last_tick, POLL_TICK + 1u);
}

/* The guard is JZ and not a signed or unsigned order test, so a latch above the
   live counter still services the tick.  That is what makes the tick counter's
   wrap harmless here, the same as it is for fdps_cycle_ui_palette. */
static void cdaudio_repeat_poll_latches_on_inequality_not_order(void)
{
    arm_the_music_poll(0, -1, 0);
    data_fdps_timer_tick_counter = 10;
    data_fdps_audio_cd_repeat_last_tick = 4000000000u;

    fdps_cd_music_repeat_poll();

    CHECK_EQ(data_fdps_audio_cd_repeat_tick_counter, 1);
    CHECK_EQ(data_fdps_audio_cd_repeat_last_tick, 10);
}

/* CMP dword ptr [0x00060170],0x4b / JNZ 00030cb1 skips everything below it, so
   a tick that leaves the counter short of the period asks the drive nothing --
   the header is still poison and no track-info query has run -- while the latch
   is still refreshed.  The frame is painted idle so that a body without the
   period test would have played. */
static void cdaudio_repeat_poll_asks_the_drive_nothing_before_the_period(void)
{
    arm_the_music_poll(0x49, 1, 1);

    paint_the_request_frame(0x00);
    fdps_cd_music_repeat_poll();

    CHECK_EQ(data_fdps_audio_cd_repeat_tick_counter, 0x4a);
    CHECK_EQ(data_fdps_cd_request_header_buffer[0], HEADER_POISON);
    CHECK_EQ(data_fdps_cd_ioctl_buffer[0], HEADER_POISON);
    CHECK_EQ(data_fdps_cd_track_info_track_number, MUSIC_PRESET_TRACK);
    CHECK_EQ(data_fdps_audio_cd_repeat_last_tick, POLL_TICK);
}

/* The tick that carries the counter to 0x4b is the one that reaches the drive.
   With the frame painted busy the chain stops at fdps_cd_audio_is_idle's own
   request, so what stands in the header is the Device Status IOCTL -- length
   0x1a, command 3, control block code 6 -- and nothing longer was staged over
   it.  0x4b is 75 ticks, and the tick runs at the 25 Hz main asks
   AIL_set_timer_frequency for, so the drive is asked once every three
   seconds. */
static void cdaudio_repeat_poll_asks_the_drive_on_the_seventy_fifth_tick(void)
{
    arm_the_music_poll(POLL_PERIOD_MINUS_ONE, 1, 1);

    paint_the_request_frame(0xff);
    fdps_cd_music_repeat_poll();

    CHECK_EQ(data_fdps_cd_request_header_buffer[0], 0x1a);
    CHECK_EQ(data_fdps_cd_request_header_buffer[2], 3);
    CHECK_EQ(data_fdps_cd_ioctl_buffer[0], 6);
}

/* The period test is equality and not order: a counter already standing at
   0x4b steps to 0x4c, which the JNZ takes past everything, so it goes right
   round rather than firing on the spot.  A body written with >= would have
   fired here and reset -- two values apart, which no off-by-one in the
   constant produces. */
static void cdaudio_repeat_poll_tests_the_period_for_equality(void)
{
    arm_the_music_poll(0x4b, 1, 1);

    paint_the_request_frame(0x00);
    fdps_cd_music_repeat_poll();

    CHECK_EQ(data_fdps_audio_cd_repeat_tick_counter, 0x4c);
    CHECK_EQ(data_fdps_cd_request_header_buffer[0], HEADER_POISON);
    CHECK_EQ(data_fdps_cd_track_info_track_number, MUSIC_PRESET_TRACK);
}

/* MOV dword ptr [0x00060170],0x0 at 00030ca7 is below the join of all three
   inner arms, so the counter is cleared whether the drive was played, found
   busy, or never asked at all.  A reset that only happened on the play arm
   would leave a silent drive being asked once per tick from then on. */
static void cdaudio_repeat_poll_clears_the_counter_on_every_arm(void)
{
    arm_the_music_poll(POLL_PERIOD_MINUS_ONE, -1, 1);
    paint_the_request_frame(0x00);
    fdps_cd_music_repeat_poll();
    CHECK_EQ(data_fdps_audio_cd_repeat_tick_counter, 0);

    arm_the_music_poll(POLL_PERIOD_MINUS_ONE, 1, 0);
    paint_the_request_frame(0x00);
    fdps_cd_music_repeat_poll();
    CHECK_EQ(data_fdps_audio_cd_repeat_tick_counter, 0);

    arm_the_music_poll(POLL_PERIOD_MINUS_ONE, 1, 1);
    paint_the_request_frame(0xff);
    fdps_cd_music_repeat_poll();
    CHECK_EQ(data_fdps_audio_cd_repeat_tick_counter, 0);

    arm_the_music_poll(POLL_PERIOD_MINUS_ONE, 1, 1);
    paint_the_request_frame(0x00);
    fdps_cd_music_repeat_poll();
    CHECK_EQ(data_fdps_audio_cd_repeat_tick_counter, 0);
}

/* TEST AX,AX / JNZ 00030c98: a zero from fdps_cd_audio_is_idle means the busy
   bit was set and the track is still playing, so nothing is restarted.  The
   Device Status request is still standing in the header, the track-info
   globals still name the track they were armed with and the play range is
   still its poison, which together say the chain stopped at the query. */
static void cdaudio_repeat_poll_leaves_a_playing_track_alone(void)
{
    arm_the_music_poll(POLL_PERIOD_MINUS_ONE, 1, 1);

    paint_the_request_frame(0xff);
    fdps_cd_music_repeat_poll();

    CHECK_EQ(data_fdps_cd_request_header_buffer[0], 0x1a);
    CHECK_EQ(data_fdps_cd_track_info_track_number, MUSIC_PRESET_TRACK);
    CHECK_EQ(data_fdps_cd_play_range_start_sector,
             (unsigned int) RANGE_POISON);
}

/* A non-zero answer takes the JNZ into CALL 0003c85b, whose last request is a
   Play Audio -- header length 0x16 and command 0x84 -- staged over the Device
   Status request that asked the question.  What the request carries is the
   range fdps_cd_resolve_track_range published on the way: the sector at header
   offset 0x0e is data_fdps_cd_play_range_start_sector and the count at 0x12 is
   the width of the pair, which is what says the whole of fdps_cd_play_track
   ran and not just its stop.  The range itself is not pinned to a number here
   -- the track-info query the resolve reads is answered by no driver, so what
   it publishes is the reply block's own leftovers. */
static void cdaudio_repeat_poll_restarts_a_finished_track(void)
{
    unsigned char *header;

    arm_the_music_poll(POLL_PERIOD_MINUS_ONE, 1, 1);

    paint_the_request_frame(0x00);
    fdps_cd_music_repeat_poll();

    header = data_fdps_cd_request_header_buffer;
    CHECK_EQ(header[0], 0x16);
    CHECK_EQ(header[2], 0x84);
    CHECK_EQ((long) staged_dword(header, 0x0e),
             (long) data_fdps_cd_play_range_start_sector);
    CHECK_EQ((long) staged_dword(header, 0x12),
             (long) (data_fdps_cd_play_range_end_sector -
                     data_fdps_cd_play_range_start_sector));
}

/* MOV EAX,[0x00069d54] / INC EAX / PUSH EAX at 00030c98: the track restarted is
   one past the published music index, the same shift fdps_cd_set_music_track
   applies, because the game counts music from 0 while the disc's audio tracks
   start at 2.  Index 1 has to come out as track 2 and index 2 as track 3 --
   two points, which no fixed track number and no other offset fits.  The index
   is read back out of the global, not carried from anywhere. */
static void cdaudio_repeat_poll_restarts_the_track_one_past_the_index(void)
{
    arm_the_music_poll(POLL_PERIOD_MINUS_ONE, 1, 1);
    paint_the_request_frame(0x00);
    fdps_cd_music_repeat_poll();
    CHECK_EQ(data_fdps_cd_track_info_track_number, 2);
    CHECK_EQ(data_fdps_cd_ioctl_buffer[1], 2);

    arm_the_music_poll(POLL_PERIOD_MINUS_ONE, 2, 1);
    paint_the_request_frame(0x00);
    fdps_cd_music_repeat_poll();
    CHECK_EQ(data_fdps_cd_track_info_track_number, 3);
    CHECK_EQ(data_fdps_cd_ioctl_buffer[1], 3);
}

/* CMP dword ptr [0x00069d54],-0x1 is the only comparison the index meets, so 0
   is an ordinary music index and not a second way of saying "no music": it
   restarts track 1.  A body that tested for zero as well would have left the
   header poisoned. */
static void cdaudio_repeat_poll_treats_index_zero_as_a_real_track(void)
{
    arm_the_music_poll(POLL_PERIOD_MINUS_ONE, 0, 1);

    paint_the_request_frame(0x00);
    fdps_cd_music_repeat_poll();

    CHECK_EQ(data_fdps_cd_request_header_buffer[0], 0x16);
    CHECK_EQ(data_fdps_cd_track_info_track_number, 1);
    CHECK_EQ(data_fdps_cd_ioctl_buffer[1], 1);
}

/* CMP dword ptr [0x00069d54],-0x1 / JZ 00030c8a jumps over the CALL, so with
   no music selected the drive is not even asked: the header is still poison
   although the frame is painted idle, which is what says the guard is above
   the query and not below it.  Every device request costs real time, and this
   is the state the game sits in whenever the music is off. */
static void cdaudio_repeat_poll_asks_nothing_with_no_track_selected(void)
{
    arm_the_music_poll(POLL_PERIOD_MINUS_ONE, -1, 1);

    paint_the_request_frame(0x00);
    fdps_cd_music_repeat_poll();

    CHECK_EQ(data_fdps_cd_request_header_buffer[0], HEADER_POISON);
    CHECK_EQ(data_fdps_cd_ioctl_buffer[0], HEADER_POISON);
    CHECK_EQ(data_fdps_cd_track_info_track_number, MUSIC_PRESET_TRACK);
}

/* CMP byte ptr [0x00060008],0x0 / JNZ 00030c8c is read each firing rather than
   latched, so clearing the player's music setting stops the restarts from the
   next firing on even with an index still published.  Nothing here is asked of
   the drive either.  This is the second half of the pair that keeps a switched
   off music setting from being undone by the poll -- the other half is
   fdps_cd_set_music_track publishing -1. */
static void cdaudio_repeat_poll_asks_nothing_with_the_music_switched_off(void)
{
    arm_the_music_poll(POLL_PERIOD_MINUS_ONE, 1, 0);

    paint_the_request_frame(0x00);
    fdps_cd_music_repeat_poll();

    CHECK_EQ(data_fdps_cd_request_header_buffer[0], HEADER_POISON);
    CHECK_EQ(data_fdps_cd_ioctl_buffer[0], HEADER_POISON);
    CHECK_EQ(data_fdps_cd_track_info_track_number, MUSIC_PRESET_TRACK);
}

/* MOV EAX,[0x00069d64] / MOV [0x00069d74],EAX sits after the whole of the CD
   work, so a pass that reached the drive still refreshes the latch, and the
   next call in the same tick is refused by it: the counter that firing cleared
   stays cleared and no second request is staged.  Without the refresh on this
   arm every later call in the tick would step the counter again. */
static void cdaudio_repeat_poll_latches_the_tick_it_finished_on(void)
{
    int i;

    arm_the_music_poll(POLL_PERIOD_MINUS_ONE, 1, 1);

    paint_the_request_frame(0x00);
    fdps_cd_music_repeat_poll();
    CHECK_EQ(data_fdps_audio_cd_repeat_last_tick, POLL_TICK);
    CHECK_EQ(data_fdps_audio_cd_repeat_tick_counter, 0);

    for (i = 0; i < 0x20; i++) {
        data_fdps_cd_request_header_buffer[i] = HEADER_POISON;
    }
    fdps_cd_music_repeat_poll();
    CHECK_EQ(data_fdps_audio_cd_repeat_tick_counter, 0);
    CHECK_EQ(data_fdps_cd_request_header_buffer[0], HEADER_POISON);
}

/* ---------------------------------------------------------------------------
 * fdps_cd_play_track_range at 0003c892.
 *
 * The whole body after the stack probe:
 *
 *   CALL 0003c4a7                                fdps_cd_stop_audio
 *   MOVSX EAX,word ptr [ESP + 0xc] / PUSH EAX / CALL 0003c0c8 / ADD ESP,0x4
 *   CALL 0003c803                                fdps_cd_resolve_track_range
 *   MOV ESI,dword ptr [0x00069dec]               the start sector, latched here
 *   MOVSX EAX,word ptr [ESP + 0x10] / PUSH EAX / CALL 0003c0c8 / ADD ESP,0x4
 *   CALL 0003c803                                fdps_cd_resolve_track_range
 *   MOV EBX,dword ptr [0x00069de4]               the end sector
 *   PUSH EBX / PUSH ESI / CALL 0003c452 / ADD ESP,0x8
 *   MOV EAX,EBX / SUB EAX,ESI / RET
 *
 * so what there is to pin down is which argument reaches which end of the
 * range, that the play request goes out after both resolves, and that the
 * returned length is the difference of the pair that was sent.
 *
 * The globals, the sentinels and the poison are the ones the fdps_cd_play_track
 * cases above set up, and for the same reasons: RESOLVE_TRACK_START_SECTOR,
 * RESOLVE_LEADOUT_SECTOR and RANGE_POISON all sit outside the 0..1166730 and
 * 0xffffff6a..0xffffffff that fdps_cd_msf_to_sector can produce, so a sector a
 * query answered with can never be mistaken for one of them.  Every request
 * goes out for real to drive index 0xff, which MSCDEX refuses on the drive
 * number before it follows ES:BX, so the bytes left in both DOS blocks are
 * exactly the ones the module staged.
 *
 * One thing about this function is not reachable from outside and rests on the
 * disassembly alone: the stop request at 0003c89e leaves no mark, exactly as it
 * leaves none in fdps_cd_play_track, because it stages thirteen bytes into the
 * same header block the four later requests overwrite whole.  The latch order
 * is reachable, and the case that reaches it says how.
 * ------------------------------------------------------------------------ */

/* What data_fdps_cd_track_info_track_number holds on the way in.  It is past
   every track any case below asks for, so the global still holding it would say
   no query ran at all. */
#define RANGE_PRESET_TRACK 40

/* Puts the four globals the chain reads into a known state, poisons both DOS
   blocks and the two play-range globals, and calls.  leadout_sector is a
   parameter rather than the file's sentinel because one case below needs the
   range's end to fall under its start, which is the only way this function
   returns a negative length without a driver. */
static int play_the_range(short first_track, short last_track,
                          unsigned char highest_track,
                          unsigned int leadout_sector)
{
    int i;

    if (data_fdps_cd_request_header_real_mode_seg == 0) {
        fdps_cd_alloc_dos_buffers();
    }
    for (i = 0; i < 0x20; i++) {
        data_fdps_cd_request_header_buffer[i] = HEADER_POISON;
    }
    data_fdps_cd_ioctl_buffer[0] = HEADER_POISON;
    data_fdps_cd_ioctl_buffer[1] = HEADER_POISON;

    data_fdps_cd_track_info_track_number = RANGE_PRESET_TRACK;
    data_fdps_cd_highest_track_number = highest_track;
    data_fdps_cd_track_start_sector = (unsigned int) RESOLVE_TRACK_START_SECTOR;
    data_fdps_cd_leadout_sector = leadout_sector;
    data_fdps_cd_play_range_start_sector = (unsigned int) RANGE_POISON;
    data_fdps_cd_play_range_end_sector = (unsigned int) RANGE_POISON;

    data_fdps_cdrom_drive_letter_index = 0xff;
    return fdps_cd_play_track_range(first_track, last_track);
}

/* CALL 0003c452 at 0003c8d7 is the last request the body issues, so the header
   block is left holding a Play Audio and not one of the four 0x1a-byte queries
   that ran before it.  The immediates are fdps_cd_play_audio_range's own --
   MOV byte ptr [ESP],0x16, MOV byte ptr [ESP+0x1],0x0,
   MOV byte ptr [ESP+0x2],0x84 and MOV byte ptr [ESP+0xd],0x0 -- and 0x16
   rather than 0x1a is what says no query came last. */
static void cdaudio_play_range_leaves_a_play_request_in_the_header(void)
{
    unsigned char *header;

    play_the_range(5, 20, 20, (unsigned int) RESOLVE_LEADOUT_SECTOR);

    header = data_fdps_cd_request_header_buffer;
    CHECK_EQ(header[0], 0x16);
    CHECK_EQ(header[1], 0);
    CHECK_EQ(header[2], 0x84);
    CHECK_EQ(header[0xd], 0);
}

/* A Read Audio Track Info query is 0x1a bytes with volume_id_ptr zeroed at
   offset 0x16, and the 0x16-byte play request that follows does not reach those
   four bytes: zeroes standing where the poison was say a query ran, and the
   poison still standing at 0x1a and 0x1f says nothing longer than 0x1a bytes
   was ever staged.  The control block code 0x0b in the IOCTL block is the same
   evidence from the other side. */
static void cdaudio_play_range_queries_before_it_plays(void)
{
    unsigned char *header;

    play_the_range(5, 20, 20, (unsigned int) RESOLVE_LEADOUT_SECTOR);

    header = data_fdps_cd_request_header_buffer;
    CHECK_EQ(header[0x16], 0);
    CHECK_EQ(header[0x17], 0);
    CHECK_EQ(header[0x18], 0);
    CHECK_EQ(header[0x19], 0);
    CHECK_EQ(header[0x1a], HEADER_POISON);
    CHECK_EQ(header[0x1f], HEADER_POISON);
    CHECK_EQ(data_fdps_cd_ioctl_buffer[0], 0xb);
}

/* MOVSX EAX,word ptr [ESP + 0x10] / PUSH EAX / CALL 0003c0c8 at 0003c8bc is the
   second of the two selections, so the track the CD layer is left pointing at
   is last_track and never first_track.  Track 20 of a 20-track disc is the last
   one, so its resolve makes no query of its own and the selection that survives
   is the one this function made; track 5 has a track after it, so its resolve
   queries track 6 and restores 5, which is the selection that survives there.
   Either way both readings name the second argument, and the two cases have the
   arguments the other way round from each other, so a body that selected
   first_track second would come out at 5 where this expects 20. */
static void cdaudio_play_range_selects_the_last_track_second(void)
{
    play_the_range(5, 20, 20, (unsigned int) RESOLVE_LEADOUT_SECTOR);
    CHECK_EQ(data_fdps_cd_track_info_track_number, 20);
    CHECK_EQ(data_fdps_cd_ioctl_buffer[1], 20);

    play_the_range(20, 5, 20, (unsigned int) RESOLVE_LEADOUT_SECTOR);
    CHECK_EQ(data_fdps_cd_track_info_track_number, 5);
    CHECK_EQ(data_fdps_cd_ioctl_buffer[1], 5);
}

/* MOV EBX,dword ptr [0x00069de4] at 0003c8cf is below the second
   CALL 0003c803, so the range's end is what last_track's resolve published.
   Track 20 of a 20-track disc takes that resolve's lead-out arm, and the
   lead-out sentinel is past every sector a query can answer with, so the end
   holding it says the second resolve was for the second argument.  With the
   arguments the other way round the last resolve is track 5's, which takes the
   query arm, and the end is that query's answer instead -- neither the sentinel
   nor the poison. */
static void cdaudio_play_range_ends_where_the_last_track_ends(void)
{
    play_the_range(5, 20, 20, (unsigned int) RESOLVE_LEADOUT_SECTOR);
    CHECK_EQ((long) data_fdps_cd_play_range_end_sector,
             (long) RESOLVE_LEADOUT_SECTOR);

    play_the_range(20, 5, 20, (unsigned int) RESOLVE_LEADOUT_SECTOR);
    CHECK_EQ((long) data_fdps_cd_play_range_end_sector ==
             (long) RESOLVE_LEADOUT_SECTOR, 0);
    CHECK_EQ((long) data_fdps_cd_play_range_end_sector ==
             (long) RANGE_POISON, 0);
}

/* The start sector that goes out is what a resolve copied out of
   data_fdps_cd_track_start_sector, which this call's own query republished:
   neither the sentinel that global was loaded with nor the poison the
   play-range global was loaded with can survive, and what is left has to be
   fdps_cd_msf_to_sector over three zero-extended bytes,
   minute * 4500 + second * 75 + frame - 150, bounded by -150 below and
   255 * 4500 + 255 * 75 + 255 - 150 = 1166730 above.  It is also not the
   lead-out, which is the far end of the range and not this one.

   PUSH EBX / PUSH ESI at 0003c8d5 puts the start sector last on the stack and
   therefore first in fdps_cd_play_audio_range's arguments, where it lands at
   header offset 0x0e; a swapped pair would put the lead-out sentinel there. */
static void cdaudio_play_range_starts_from_its_own_query(void)
{
    unsigned char *header;
    unsigned int start_sector;

    play_the_range(5, 20, 20, (unsigned int) RESOLVE_LEADOUT_SECTOR);

    header = data_fdps_cd_request_header_buffer;
    start_sector = staged_dword(header, 0x0e);
    CHECK_EQ((long) start_sector == (long) RESOLVE_TRACK_START_SECTOR, 0);
    CHECK_EQ((long) start_sector == (long) RANGE_POISON, 0);
    CHECK_EQ((long) start_sector == (long) RESOLVE_LEADOUT_SECTOR, 0);
    CHECK_EQ(start_sector <= 1166730UL || start_sector >= 0xffffff6aUL, 1);
}

/* MOV EAX,EBX / SUB EAX,ESI at 0003c8df is the two latched sectors and nothing
   else, so the length reported is exactly the count the play request went out
   with -- fdps_cd_play_audio_range computes the same difference of the same
   pair into header offset 0x12.  The two are asserted against each other and
   the count against the lead-out sentinel, so neither can drift without the
   other.

   The length is positive here because the lead-out sentinel is above every
   sector a query can answer with, on either side of the wrap. */
static void cdaudio_play_range_returns_the_sector_length_it_sent(void)
{
    unsigned char *header;
    int range_length;

    range_length = play_the_range(5, 20, 20,
                                  (unsigned int) RESOLVE_LEADOUT_SECTOR);

    header = data_fdps_cd_request_header_buffer;
    CHECK_EQ((long) range_length, (long) staged_dword(header, 0x12));
    CHECK_EQ((long) staged_dword(header, 0x12),
             (long) ((unsigned int) RESOLVE_LEADOUT_SECTOR -
                     staged_dword(header, 0x0e)));
    CHECK_EQ(range_length > 0, 1);
}

/* Nothing compares the two ends and nothing clamps the difference, so a range
   whose end falls below its start is subtracted exactly as it stands and the
   length comes out negative.  A genuinely reversed pair of track numbers cannot
   be used to show it: both ends would then be driver answers this test cannot
   predict.  What can be arranged is the same arithmetic with one end known --
   the lead-out is the test's own value and is what track 20's resolve publishes
   as the end, so the first call is made only to read back the start sector this
   chain produces, and the second is given a lead-out a hundred sectors under
   it.

   The start sector is the same on both calls because nothing between them
   changes what the queries stage, and that is asserted rather than assumed:
   if it ever stops holding, this says so instead of the length quietly
   disagreeing by an unexplained amount. */
static void cdaudio_play_range_length_goes_negative_below_the_start(void)
{
    unsigned char *header;
    unsigned int start_sector;
    int range_length;

    range_length = play_the_range(5, 20, 20, 0xfffffc18u);

    header = data_fdps_cd_request_header_buffer;
    start_sector = staged_dword(header, 0x0e);
    CHECK_EQ(start_sector <= 1166730UL || start_sector >= 0xffffff6aUL, 1);
    CHECK_EQ((long) range_length, (long) (0xfffffc18u - start_sector));
    CHECK_EQ((long) range_length, (long) staged_dword(header, 0x12));
    CHECK_EQ(range_length < 0, 1);
}

/* MOV ESI,dword ptr [0x00069dec] at 0003c8b6 sits between the two
   CALL 0003c803, so the start sector that goes out is the one first_track's
   resolve published and not the one last_track's resolve leaves standing in the
   global afterwards.  Reading both globals after both resolves -- the shorter
   way to write the body, and the one the pair of globals invites -- would send
   last_track's start instead and play from the wrong place.

   The two are different numbers here for a reason that only holds without a
   driver, and it is worth saying plainly because it is what this case rests on.
   Every start sector is fdps_cd_msf_to_sector over the three address bytes of a
   Read Audio Track Info reply, and a refused request leaves the reply block
   holding the bytes that were staged into it -- which are
   fdps_cdrom_read_track_info's own uninitialised frame.  The two queries this
   function makes do not run over the same frame contents: track 5's resolve
   takes the query arm and makes two nested calls of its own in between, so the
   query for track 20 finds different bytes there than the query for track 5
   did.  Both answers are still inside what fdps_cd_msf_to_sector can produce,
   which is asserted first, so both are real query answers and not a global left
   unwritten.

   That makes this the one arrangement where the wrong order is visible from
   outside: neither number is predictable, but they are not each other. */
static void cdaudio_play_range_latches_the_start_before_the_second_resolve(void)
{
    unsigned char *header;
    unsigned int sent_start_sector;
    unsigned int last_track_start_sector;

    play_the_range(5, 20, 20, (unsigned int) RESOLVE_LEADOUT_SECTOR);

    header = data_fdps_cd_request_header_buffer;
    sent_start_sector = staged_dword(header, 0x0e);
    last_track_start_sector = data_fdps_cd_play_range_start_sector;

    CHECK_EQ(sent_start_sector <= 1166730UL ||
             sent_start_sector >= 0xffffff6aUL, 1);
    CHECK_EQ(last_track_start_sector <= 1166730UL ||
             last_track_start_sector >= 0xffffff6aUL, 1);
    CHECK_EQ((long) sent_start_sector == (long) last_track_start_sector, 0);
}

/* There is no compare and no branch anywhere in the body, and neither
   fdps_cdrom_read_track_info nor fdps_cd_resolve_track_range refuses a track
   number either: tracks 25 and 30 of a 20-track disc are queried for like any
   other and both resolves take the lead-out arm, so the range ends at the
   lead-out and the play request goes out on it.  A body that guarded either
   argument would leave the header holding a query rather than the play
   request. */
static void cdaudio_play_range_does_not_refuse_a_track_past_the_disc(void)
{
    unsigned char *header;
    int range_length;

    range_length = play_the_range(25, 30, 20,
                                  (unsigned int) RESOLVE_LEADOUT_SECTOR);

    header = data_fdps_cd_request_header_buffer;
    CHECK_EQ(data_fdps_cd_track_info_track_number, 30);
    CHECK_EQ((long) data_fdps_cd_play_range_end_sector,
             (long) RESOLVE_LEADOUT_SECTOR);
    CHECK_EQ(header[0], 0x16);
    CHECK_EQ(header[2], 0x84);
    CHECK_EQ((long) range_length, (long) staged_dword(header, 0x12));
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
    RUN_TEST(cdaudio_q_channel_block_fields_sit_where_the_driver_writes_them);
    RUN_TEST(cdaudio_q_channel_stages_an_ioctl_input_request);
    RUN_TEST(cdaudio_q_channel_asks_for_six_bytes_of_an_eleven_byte_block);
    RUN_TEST(cdaudio_q_channel_stamps_the_control_block_code);
    RUN_TEST(cdaudio_q_channel_sends_the_callers_own_eleven_bytes);
    RUN_TEST(cdaudio_q_channel_copies_eleven_bytes_back_to_the_caller);
    RUN_TEST(cdaudio_q_channel_copies_exactly_the_declared_header);
    RUN_TEST(cdaudio_q_channel_publishes_the_driver_status_word);
    RUN_TEST(cdaudio_audio_is_idle_issues_a_device_status_request);
    RUN_TEST(cdaudio_audio_is_idle_publishes_a_fresh_status_word);
    RUN_TEST(cdaudio_audio_is_idle_answers_the_busy_bit_of_that_word);
    RUN_TEST(cdaudio_audio_is_idle_returns_what_the_predicate_returns);
    RUN_TEST(cdaudio_audio_is_idle_returns_a_zero_or_one_word);
    RUN_TEST(cdaudio_audio_is_idle_asks_the_drive_again_on_every_call);
    RUN_TEST(cdaudio_resolve_range_ends_the_last_track_at_the_lead_out);
    RUN_TEST(cdaudio_resolve_range_asks_the_drive_nothing_for_the_last_track);
    RUN_TEST(cdaudio_resolve_range_takes_a_track_past_the_disc_to_the_lead_out);
    RUN_TEST(cdaudio_resolve_range_boundary_is_track_plus_one);
    RUN_TEST(cdaudio_resolve_range_reads_the_highest_track_unsigned);
    RUN_TEST(cdaudio_resolve_range_reads_the_track_number_signed);
    RUN_TEST(cdaudio_resolve_range_copies_the_start_before_it_queries);
    RUN_TEST(cdaudio_resolve_range_ends_a_queried_track_where_the_next_starts);
    RUN_TEST(cdaudio_resolve_range_restores_the_track_it_was_entered_on);
    RUN_TEST(cdaudio_play_track_leaves_a_play_request_in_the_header);
    RUN_TEST(cdaudio_play_track_queries_before_it_plays);
    RUN_TEST(cdaudio_play_track_selects_the_track_it_was_given);
    RUN_TEST(cdaudio_play_track_sends_the_range_start_first);
    RUN_TEST(cdaudio_play_track_ends_the_last_track_at_the_lead_out);
    RUN_TEST(cdaudio_play_track_resolves_the_range_from_its_own_query);
    RUN_TEST(cdaudio_play_track_points_the_layer_before_it_resolves);
    RUN_TEST(cdaudio_play_track_does_not_refuse_a_track_past_the_disc);
    RUN_TEST(cdaudio_play_range_leaves_a_play_request_in_the_header);
    RUN_TEST(cdaudio_play_range_queries_before_it_plays);
    RUN_TEST(cdaudio_play_range_selects_the_last_track_second);
    RUN_TEST(cdaudio_play_range_ends_where_the_last_track_ends);
    RUN_TEST(cdaudio_play_range_starts_from_its_own_query);
    RUN_TEST(cdaudio_play_range_returns_the_sector_length_it_sent);
    RUN_TEST(cdaudio_play_range_length_goes_negative_below_the_start);
    RUN_TEST(cdaudio_play_range_latches_the_start_before_the_second_resolve);
    RUN_TEST(cdaudio_play_range_does_not_refuse_a_track_past_the_disc);
    RUN_TEST(cdaudio_set_music_publishes_the_index_it_was_given);
    RUN_TEST(cdaudio_set_music_plays_when_the_music_is_on);
    RUN_TEST(cdaudio_set_music_plays_the_track_one_past_the_index);
    RUN_TEST(cdaudio_set_music_treats_index_zero_as_a_real_track);
    RUN_TEST(cdaudio_set_music_stops_the_drive_on_minus_one);
    RUN_TEST(cdaudio_set_music_off_discards_the_requested_index);
    RUN_TEST(cdaudio_set_music_off_with_minus_one_still_stops);
    RUN_TEST(cdaudio_set_music_off_does_not_latch);
    RUN_TEST(cdaudio_repeat_poll_paint_decides_what_the_drive_says);
    RUN_TEST(cdaudio_repeat_poll_ignores_a_tick_it_has_already_seen);
    RUN_TEST(cdaudio_repeat_poll_steps_the_counter_on_a_new_tick);
    RUN_TEST(cdaudio_repeat_poll_counts_ticks_and_not_calls);
    RUN_TEST(cdaudio_repeat_poll_latches_on_inequality_not_order);
    RUN_TEST(cdaudio_repeat_poll_asks_the_drive_nothing_before_the_period);
    RUN_TEST(cdaudio_repeat_poll_asks_the_drive_on_the_seventy_fifth_tick);
    RUN_TEST(cdaudio_repeat_poll_tests_the_period_for_equality);
    RUN_TEST(cdaudio_repeat_poll_clears_the_counter_on_every_arm);
    RUN_TEST(cdaudio_repeat_poll_leaves_a_playing_track_alone);
    RUN_TEST(cdaudio_repeat_poll_restarts_a_finished_track);
    RUN_TEST(cdaudio_repeat_poll_restarts_the_track_one_past_the_index);
    RUN_TEST(cdaudio_repeat_poll_treats_index_zero_as_a_real_track);
    RUN_TEST(cdaudio_repeat_poll_asks_nothing_with_no_track_selected);
    RUN_TEST(cdaudio_repeat_poll_asks_nothing_with_the_music_switched_off);
    RUN_TEST(cdaudio_repeat_poll_latches_the_tick_it_finished_on);
}
