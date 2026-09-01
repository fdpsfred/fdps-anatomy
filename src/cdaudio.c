/* cdaudio.c -- the CD-DA playback layer: the MSCDEX audio commands the game
 * drives the music with.
 *
 * These are the commands that move the drive and start and stop sound, as
 * opposed to the ones that ask it questions: cd.c owns the device request path
 * they are handed to the driver through and the two DOS conventional-memory
 * blocks they are staged in, and cdtoc.c owns the table-of-contents queries
 * and the Red Book arithmetic that turns a track number into the sector range
 * a play command takes.
 *
 * Note on this module's original build flags: like the rest of the block from
 * 0003bade to 0003c96x it was not compiled with the flag set the rest of the
 * game was -- every function in it opens with PUSH <frame size> / CALL __CHK,
 * the stack probe that -s removes and that no other game function carries.
 * The rebuild has one flag set for every unit (rebuild_info/build_flags.md),
 * so what it builds from this file is the same code without the probe, and
 * the probe is not written out below.
 *
 * memcpy comes from <string.h> and is a real call in the image; the request
 * header layout comes from fdpstype.h, the DOS block pointer and the published
 * status word from gamedata.h, and the driver request path from cd.h.
 */
#include <string.h>

#include "fdpstype.h"
#include "gamedata.h"
#include "cd.h"
#include "cdaudio.h"

/* 0003c3fa.  A bare MSCDEX device request -- command 0x83, Seek -- with no
   transfer attached: address field zero, sector count zero, and the drive
   asked only to put its head on the sector named at header offset 0x14.
   Nothing is staged through the IOCTL block, because a seek moves the drive
   and returns no data.

   The header is the 24-byte extended shape rather than the 26-byte IOCTL one
   the same struct describes elsewhere in the module.  The two differ exactly
   at offset 0x14: the IOCTL shape has a 16-bit starting sector there followed
   by a 32-bit volume-ID pointer, while the extended shape ends at 0x18 with a
   full 32-bit starting sector in that slot, and MOV dword ptr [ESP+0x14],EAX
   at 0003c42d is that 32-bit store.  fdpstype.h carries the IOCTL shape, so
   the store is written through the address of the field the extended shape
   overlaps -- writing the 16-bit field instead would drop the high half of any
   sector past 65535, which on a disc of up to 74 minutes is anything past
   14:33.  The offset the cast depends on is asserted in tests/cdaudio.c.

   Offsets 3 to 0x0c -- the status word and the eight reserved bytes -- are
   never initialised, here as in the original: they go out to the driver
   holding whatever the stack frame held, and the driver overwrites the status
   word with its answer.

   The read-back length is the literal 0x18 the body pushes at 0003c44b, not
   the header's own declared length, and the status word is the word at header
   offset 3 -- MOV EAX,[ESP+3] / MOV [0x00069e20],AX, a dword load of which
   only the low half is stored.  This function never looks at that status word
   itself. */
void fdps_cd_seek(unsigned int sector)
{
    struct fdps_cd_request_header request_header;

    request_header.header_length = 0x18;
    request_header.subunit = 0;
    request_header.command = 0x83;
    request_header.media_descriptor = 0;
    request_header.transfer_address = 0;
    request_header.transfer_byte_count = 0;
    *(unsigned int *) &request_header.start_sector = sector;

    memcpy(data_fdps_cd_request_header_buffer, &request_header, 0x18);
    fdps_cd_device_request();
    memcpy(&request_header, data_fdps_cd_request_header_buffer, 0x18);

    data_fdps_cd_last_request_status = request_header.status;
}
