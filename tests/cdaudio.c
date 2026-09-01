/* tests/cdaudio.c -- cover for src/cdaudio.c.
 *
 * So far this covers fdps_cd_seek at 0003c3fa.
 *
 * Every expected value below comes from the twenty instructions of that
 * function, never from the emitted C:
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

void run_cdaudio_tests(void)
{
    RUN_TEST(cdaudio_seek_header_fields_sit_where_the_stores_land);
    RUN_TEST(cdaudio_seek_stages_a_seek_command);
    RUN_TEST(cdaudio_seek_sends_the_sector_as_a_full_dword);
    RUN_TEST(cdaudio_seek_copies_exactly_the_declared_header);
    RUN_TEST(cdaudio_seek_publishes_the_driver_status_word);
}
