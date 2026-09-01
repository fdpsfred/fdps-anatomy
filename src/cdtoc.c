/* cdtoc.c -- the CD table-of-contents layer: the MSCDEX disk- and track-info
 * queries and the Red Book MSF/sector arithmetic built on their answers.
 *
 * cd.c owns the device request path this module's queries go out through, and
 * the register and request blocks they share; this file owns what the driver's
 * replies mean.
 *
 * Note on this module's original build flags: like the rest of the block from
 * 0003bade to 0003c96x it was not compiled with the flag set the rest of the
 * game was -- every function in it opens with PUSH <frame size> / CALL __CHK,
 * the stack probe that -s removes and that no other game function carries.
 * The rebuild has one flag set for every unit (rebuild_info/build_flags.md),
 * so what it builds from this file is the same code without the probe, and
 * the probe is not written out below.
 *
 * memcpy and memset come from <string.h> and are real calls in the image; the
 * request header layout comes from fdpstype.h, the two DOS block pointers and
 * the published status word from gamedata.h, and the driver request path
 * itself from cd.h.
 */
#include <string.h>

#include "fdpstype.h"
#include "gamedata.h"
#include "cd.h"
#include "cdtoc.h"

/* 0003bc3f.  Three byte stores straight out of the packed argument, in
   least-significant-field-first order: the low byte to *frame, bits 8-15 to
   *second, bits 16-23 to *minute.  There is no branch and no arithmetic beyond
   the mask and shift of each field, and bits 24-31 never leave the register.

   Each field is isolated with AND and then brought down with SHR, a logical
   shift, so nothing here depends on the sign of the argument -- the mask has
   already cleared every bit above the field by the time the shift happens.

   The stores are byte-wide: MOV byte ptr [EDX],AL for each of the three, so a
   caller may point the three parameters at three adjacent bytes -- which
   fdps_cdrom_read_disk_info does, at the lead-out minute, second and frame
   globals -- without the writes reaching past them. */
void fdps_cd_unpack_msf(unsigned int msf_packed, unsigned char *minute,
                        unsigned char *second, unsigned char *frame)
{
    *frame = (unsigned char) msf_packed;
    *second = (unsigned char) ((msf_packed & 0xff00) >> 8);
    *minute = (unsigned char) ((msf_packed & 0xff0000) >> 16);
}

/* 0003bc78.  Straight-line arithmetic on the three fields fdps_cd_unpack_msf
   writes out; there is no branch in the body at all.  The three destinations
   are three separate stack bytes and the call cleans four dwords off the stack
   at 0003bc9f, so the pointers go out in the same most-significant-first order
   the callee declares.

   The frame counts come out of MOVZX loads -- MOVZX EDX,byte ptr [ESP] for the
   minute, MOVZX EBX,byte ptr [ESP+0x8] for the second, MOVZX EAX,byte ptr
   [ESP+0x4] for the frame -- so every field is zero-extended and none of the
   three can carry a sign into the sum.  The sum itself is signed: the result
   goes back as EAX with nothing clamping it, and the two constants are folded
   as shift chains, 4500 as ((m*31)*4+m)*4 plus that times 8 and 75 as
   (s*5)*16-(s*5).

   The 150 at the end is what makes this a sector number rather than a frame
   count: Red Book puts a two-second lead-in ahead of logical sector 0, so
   00:02:00 is sector 0 and anything earlier is a negative sector.  Both call
   sites store the result into a dword global and neither tests its sign. */
int fdps_cd_msf_to_sector(unsigned int msf_packed)
{
    unsigned char minute;
    unsigned char second;
    unsigned char frame;

    fdps_cd_unpack_msf(msf_packed, &minute, &second, &frame);
    return minute * 4500 + second * 75 + frame - 150;
}

/* 0003bec1.  An MSCDEX IOCTL Input request -- command code 3 -- carrying the
   eleven-byte control block 0Eh, UPC Code, which asks the drive for the disc's
   media catalog number.

   Only two bytes of the control block are initialised.  Byte 0 is the control
   block code and byte 1 is the CONTROL/ADR nibble pair, preset here to 2 --
   ADR = media catalog number -- and that preset is what the branch after the
   request tests against: the driver answers by overwriting that byte, and a
   driver that reports "this disc carries no catalog number" writes 0 there.
   Clearing the whole block before the request would make the zero test fire on
   every drive that leaves the byte alone, and blank the published catalog
   number that a drive had actually answered with.  Bytes 2..10 go out to the
   driver as whatever the frame held, exactly as in the original.

   The two DOS block pointers are two views of the same second block: the
   memcpy pair stages through the flat linear address in
   data_fdps_cd_ioctl_buffer, while the header's transfer address field carries
   the packed real-mode far pointer in data_fdps_cd_ioctl_buffer_real_mode_ptr,
   because the driver that follows it runs in real mode.

   The read-back header length is the local header's own declared 0x1a --
   MOVZX EAX,byte ptr [ESP] at 0003bf4e, zero-extended -- and not anything the
   driver wrote, since the local is read before it is overwritten.

   The single branch in the body is the CONTROL/ADR test at 0003bf70.  The
   status word is published but never tested, so the catalog number global is
   rewritten whether or not the driver accepted the request, and a refused
   request leaves it holding whatever the staged block came back with.

   Returns 1 unconditionally -- MOV EAX,0x1 with nothing else reaching the RET
   -- so the value says nothing about whether the request worked.  What the
   routine produces is data_fdps_cd_media_catalog_number and
   data_fdps_cd_last_request_status.  Nothing in the image calls it and nothing
   in the image reads the catalog number it publishes. */
int fdps_cdrom_read_upc(void)
{
    struct fdps_cd_request_header request_header;
    unsigned char control_block[11];

    request_header.header_length = 0x1a;
    request_header.subunit = 0;
    request_header.command = 3;
    request_header.volume_id_ptr = 0;
    request_header.start_sector = 0;
    request_header.media_descriptor = 0;
    request_header.transfer_address = data_fdps_cd_ioctl_buffer_real_mode_ptr;
    request_header.transfer_byte_count = 0xb;
    control_block[0] = 0xe;
    control_block[1] = 2;

    memcpy(data_fdps_cd_request_header_buffer, &request_header, 0x1a);
    memcpy(data_fdps_cd_ioctl_buffer, control_block, 0xb);
    fdps_cd_device_request();
    memcpy(control_block, data_fdps_cd_ioctl_buffer, 0xb);
    memcpy(&request_header, data_fdps_cd_request_header_buffer,
           request_header.header_length);

    data_fdps_cd_last_request_status = request_header.status;
    if (control_block[1] == 0) {
        memset(&control_block[2], 0, 7);
    }
    memcpy(data_fdps_cd_media_catalog_number, &control_block[2], 7);
    return 1;
}

/* 0003bfa5.  An MSCDEX IOCTL Input request -- command code 3 again -- carrying
   the seven-byte control block 0Ah, Read Disk Info, which asks the drive for
   the first and last track numbers on the disc and where the lead-out starts.

   Unlike the UPC request above, the control block is cleared whole before the
   code byte is written -- MOV byte ptr [ESP+0x1c],0xa comes after the memset
   at 0003bfed -- so the six reply bytes go out to the driver as zero.  That is
   what makes every value this routine publishes deterministic when no driver
   answers: the block comes back as it went out, the two track numbers read 0
   and the lead-out address reads 00:00:00, which fdps_cd_msf_to_sector turns
   into -150.

   The control block is staged into the DOS block and immediately copied back
   before the request is sent (0003c00f and 0003c024, the same seven bytes in
   each direction).  The round trip cannot change anything -- nothing else runs
   between the two copies -- and it is written out here because it is what the
   original does, not because the value depends on it.

   The read-back header length is the literal 0x1a at 0003c053 rather than the
   header's own length byte, so the copy back is fixed at the size the request
   went out as whatever the driver wrote into byte 0.

   There is no branch anywhere in the body: every one of the seven destinations
   is written on every call, including a call the driver refused, and the
   status word is published without being tested.  What the routine produces is
   data_fdps_cd_disk_info_reply (the six reply bytes verbatim),
   data_fdps_cd_lowest_track_number, data_fdps_cd_highest_track_number, the
   three lead-out MSF bytes, data_fdps_cd_leadout_sector and
   data_fdps_cd_last_request_status.

   The lead-out address is read twice out of the same four bytes at control
   block +3, once for the MSF split and once for the sector conversion
   (0003c09d and 0003c0a9); fdps_cd_unpack_msf writes only through the three
   pointers it is given, so the second read cannot see anything the first call
   changed, and one local below stands for both reads.

   That read is a plain dword load in the original -- PUSH dword ptr [ESP+0x1f]
   -- off an offset that is three bytes into the block and so not aligned; the
   386 does not care and neither does the cast below, which is what keeps this
   from becoming a memcpy call the original does not make. */
void fdps_cdrom_read_disk_info(void)
{
    struct fdps_cd_request_header request_header;
    unsigned char control_block[7];
    unsigned int leadout_msf_packed;

    request_header.header_length = 0x1a;
    request_header.subunit = 0;
    request_header.command = 3;
    request_header.media_descriptor = 0;
    request_header.start_sector = 0;
    request_header.volume_id_ptr = 0;
    request_header.transfer_address = data_fdps_cd_ioctl_buffer_real_mode_ptr;
    request_header.transfer_byte_count = 7;
    memset(control_block, 0, 7);
    control_block[0] = 0xa;

    memcpy(data_fdps_cd_request_header_buffer, &request_header, 0x1a);
    memcpy(data_fdps_cd_ioctl_buffer, control_block, 7);
    memcpy(control_block, data_fdps_cd_ioctl_buffer, 7);
    fdps_cd_device_request();
    memcpy(control_block, data_fdps_cd_ioctl_buffer, 7);
    memcpy(&request_header, data_fdps_cd_request_header_buffer, 0x1a);

    memcpy(data_fdps_cd_disk_info_reply, &control_block[1], 6);
    data_fdps_cd_lowest_track_number = control_block[1];
    data_fdps_cd_highest_track_number = control_block[2];
    leadout_msf_packed = *(unsigned int *) &control_block[3];
    fdps_cd_unpack_msf(leadout_msf_packed, &data_fdps_cd_leadout_msf_minute,
                       &data_fdps_cd_leadout_second,
                       &data_fdps_cd_leadout_frame);
    data_fdps_cd_leadout_sector =
        (unsigned int) fdps_cd_msf_to_sector(leadout_msf_packed);
    data_fdps_cd_last_request_status = request_header.status;
}

/* 0003c0c8.  An MSCDEX IOCTL Input request -- command code 3 once more --
   carrying the seven-byte control block 0Bh, Read Audio Track Info, which asks
   the drive where one track starts and what its control field says.

   The header is built exactly as the other two build theirs, and with the same
   seven-byte transfer the Read Disk Info block uses.  The control block is not
   cleared: only the code byte and the track byte are written, so bytes 2..6 go
   out to the driver as whatever the frame held, and a request no driver
   answers brings that same stack content back.  Every value published below a
   refused request is therefore whatever was on the stack, and unlike
   fdps_cdrom_read_disk_info next door nothing here makes it deterministic.
   Adding a memset would change what the driver is sent.

   The track number reaches two different places at two different widths.  Only
   its low byte goes into the control block -- MOV AL,byte ptr [ESP+0x28] at
   0003c10c, a byte load -- because that is all the field is; but the copy that
   is published takes the low sixteen bits, MOV EAX,dword ptr [ESP+0x28] with
   MOV [0x00069dff],AX at 0003c188.  The argument itself arrives as a full
   dword: each of the ten call sites, spread over six callers, pushes one and
   cleans four bytes afterwards, and between them they push a zero-extended
   byte, a sign-extended word and the literal 1.

   There is no branch anywhere in the body.  The status word is published
   without being tested, so all three track globals are rewritten on a refused
   request as well as an accepted one, and a caller reading them cannot tell
   the two apart.

   The start address is read out of the control block as a plain dword at
   block+2 -- PUSH dword ptr [ESP+0x1e] -- an offset that is two bytes into the
   block and so unaligned; as in fdps_cdrom_read_disk_info the cast below is
   what keeps this from becoming a memcpy the original does not make.

   The control byte is masked with 0xd0 and not with the 0xf0 that taking "the
   control nibble" would suggest.  Bit 5 is copy-permitted, and
   fdps_cd_track_is_audio compares the published byte against 0x40 exactly, so
   masking with 0xf0 would make every audio track that permits copying read
   back as 0x60 and be reported as a data track. */
void fdps_cdrom_read_track_info(int track)
{
    struct fdps_cd_request_header request_header;
    unsigned char control_block[7];

    request_header.header_length = 0x1a;
    request_header.subunit = 0;
    request_header.command = 3;
    request_header.volume_id_ptr = 0;
    request_header.start_sector = 0;
    request_header.media_descriptor = 0;
    request_header.transfer_address = data_fdps_cd_ioctl_buffer_real_mode_ptr;
    request_header.transfer_byte_count = 7;
    control_block[0] = 0xb;
    control_block[1] = (unsigned char) track;

    memcpy(data_fdps_cd_request_header_buffer, &request_header, 0x1a);
    memcpy(data_fdps_cd_ioctl_buffer, control_block, 7);
    fdps_cd_device_request();
    memcpy(&request_header, data_fdps_cd_request_header_buffer, 0x1a);
    memcpy(control_block, data_fdps_cd_ioctl_buffer, 7);

    data_fdps_cd_last_request_status = request_header.status;
    data_fdps_cd_track_start_sector = (unsigned int)
        fdps_cd_msf_to_sector(*(unsigned int *) &control_block[2]);
    data_fdps_cd_track_info_track_number = (short) track;
    data_fdps_cd_track_info_control_flags =
        (unsigned char) (control_block[6] & 0xd0);
}

/* 0003c1a1.  How long one track plays, counted in CD frames: the sector the
   track after it starts at -- or, for the last track on the disc, the sector
   the lead-out starts at -- less the sector this track starts at.  Both
   endpoints come out of fdps_cd_msf_to_sector, which takes the 150-frame
   lead-in off each of them, so the offset cancels in the subtraction and what
   is left is a plain frame count at the Red Book's 75 frames per second.

   The disc summary is refreshed first, so the highest track number the branch
   below tests against and the lead-out sector the last track ends at are both
   this call's answers and not whatever an earlier query left behind.

   The branch is unsigned -- CMP AL,byte ptr [0x00069e07] then JNC at 0003c1d8
   -- and both sides of it are unsigned bytes anyway.  Falling through, the
   track is not the last one and the end point is where track + 1 starts;
   taking the jump, it is the last one and the end point is the lead-out.  A
   track number above the disc's highest takes the lead-out arm too, and the
   length it gets back is meaningless rather than refused: nothing here range
   checks the argument.

   Three queries, not two.  The third one at 0003c205 re-asks for the track
   the track-info globals described when this function was entered, saved off
   the word at 0003c1b4 before the first query overwrote it, so
   data_fdps_cd_track_info_track_number, data_fdps_cd_track_start_sector and
   data_fdps_cd_track_info_control_flags are left naming the same track they
   named before the call.  fdps_cd_resolve_track_range reads exactly those
   globals to decide which track's range to publish, so writing the obvious
   two-query version leaves the CD layer pointing at the next track and a
   following play or seek addresses the wrong one.

   The saved track number is sixteen bits wide and sign-extended when it is
   passed back -- MOVSX EAX,word ptr [ESP+0x4] -- matching the width the global
   is stored at; the next track's number is computed sixteen bits wide as well,
   MOVZX BX,AL / INC EBX / MOVSX EAX,BX, which for a track number that arrived
   as a byte cannot differ from the full-width sum.

   The result is unsigned: it is the difference of two unsigned globals, and
   the one caller, fdps_cd_get_track_length_msf, divides what it gets back by
   75 with DIV rather than IDIV. */
unsigned int fdps_cd_get_track_length_sectors(unsigned char track)
{
    short saved_track_number;
    unsigned int track_start_sector;
    unsigned int track_end_sector;

    fdps_cdrom_read_disk_info();
    saved_track_number = data_fdps_cd_track_info_track_number;
    fdps_cdrom_read_track_info(track);
    track_start_sector = data_fdps_cd_track_start_sector;
    if (track < data_fdps_cd_highest_track_number) {
        fdps_cdrom_read_track_info((short) (track + 1));
        track_end_sector = data_fdps_cd_track_start_sector;
    } else {
        track_end_sector = data_fdps_cd_leadout_sector;
    }
    fdps_cdrom_read_track_info(saved_track_number);
    return track_end_sector - track_start_sector;
}

/* 0003c217.  The same track length as above, restated as a Red Book
   minute/second/frame triple.  There is no branch anywhere in the body: one
   call, a subtraction and two divide-and-remainder pairs, and all three results
   leave through the out-pointers as single bytes -- MOV byte ptr [ESI],DL, MOV
   byte ptr [EDI],DL and MOV byte ptr [EAX],BL, so a caller may aim the three at
   three adjacent bytes without the stores reaching past them.

   Every division is DIV and never IDIV, and the value they divide arrives as
   LEA EBX,[EAX + 0xffffff6a] -- the length less 150 -- with no test of the
   result.  That matters because the 150 is taken off a value that is already a
   duration: fdps_cd_msf_to_sector has removed the lead-in from both endpoints
   before fdps_cd_get_track_length_sectors subtracts them, so the offset has
   already cancelled and this second subtraction takes off two seconds that are
   not there.  It is the module's fixed convention rather than a correction --
   the identical SUB 0x96 opens the same /75, /60 chain in fdps_cd_sector_to_msf
   and in fdps_cd_get_disk_info_msf -- so it stays, and the intermediate stays
   unsigned: a length below 150 frames wraps to just under 2^32 here and comes
   out as a huge triple, where a signed intermediate would give a small negative
   one and a different answer in all three bytes.

   The original writes each division as (value - remainder) / divisor and so
   computes each quotient twice; that is written out below because it is what
   the body does, and it cannot change the value, since the remainder is exactly
   what truncation drops.

   The quotient of the last division is stored as a byte with no clamp, so a
   playing time past 255 minutes -- which the wrapped case above reaches -- wraps
   in the minute field alone. */
void fdps_cd_get_track_length_msf(unsigned char track, unsigned char *minutes,
                                  unsigned char *seconds, unsigned char *frames)
{
    unsigned int adjusted_length_frames;
    unsigned int frames_in_second;
    unsigned int total_seconds;
    unsigned int seconds_in_minute;

    adjusted_length_frames = fdps_cd_get_track_length_sectors(track) - 0x96;
    frames_in_second = adjusted_length_frames % 75;
    *frames = (unsigned char) frames_in_second;
    total_seconds = (adjusted_length_frames - frames_in_second) / 75;
    seconds_in_minute = total_seconds % 60;
    *seconds = (unsigned char) seconds_in_minute;
    *minutes = (unsigned char) ((total_seconds - seconds_in_minute) / 60);
}

/* 0003c27c.  The two facts about the disc as a whole: how many tracks it has
   and how long it plays.  There is no branch in the body -- one call, a byte
   copy, a subtraction and the same two divide-and-remainder pairs as the
   function above -- and all four results leave through the out-pointers as
   single bytes: MOV byte ptr [EBX],DL, MOV byte ptr [EDI],DL, MOV byte ptr
   [ESI],DL and MOV byte ptr [EDX],AL, so a caller may aim the four at four
   adjacent bytes without the stores reaching past them.

   The disc summary is refreshed first, at 0003c291, so both globals read
   below are this call's answers rather than whatever an earlier query left
   behind.  Both are also rewritten on a refused request, which is what makes
   the answer on a drive that did not respond track 0 and a playing time
   converted from the -150 that a 00:00:00 lead-out becomes.

   The highest-track byte is copied out first, before any of the arithmetic,
   and it is copied whole: MOV DL,byte ptr [0x00069e07] then a byte store, with
   no mask and no range check, so whatever the driver put in that field is what
   the caller gets.

   The playing length is the lead-out sector less 0x96, and both DIVs are
   unsigned and never IDIV.  As in the function above the 150 comes off a value
   that has already had the lead-in removed -- fdps_cd_msf_to_sector took it off
   the lead-out address before fdps_cdrom_read_disk_info stored the sector
   number -- so this subtraction takes off two seconds that are not there and
   every answer is two seconds short of the disc's real playing time.  It is
   the module's fixed convention rather than a correction, the same SUB 0x96
   that opens the same /75, /60 chain in fdps_cd_get_track_length_msf and in
   fdps_cd_sector_to_msf, and the intermediate stays unsigned: a lead-out
   sector below 150 wraps to just under 2^32 and comes out as a huge triple,
   where a signed intermediate would give a small negative one and a different
   answer in all three bytes.

   Each division is written as (value - remainder) / divisor, computing each
   quotient twice, because that is what the body does -- MOVZX EDX,DL then SUB
   EAX,EDX before the second DIV of each pair.  It cannot change the value: the
   remainder is exactly what truncation drops, and it is below 75 and below 60
   respectively, so the byte it is carried through is wide enough for it.

   The minute quotient is stored as a byte with no clamp, so a playing time
   past 255 minutes -- which the wrapped case above reaches -- wraps in the
   minute field alone.  Nothing in the image calls this function. */
void fdps_cd_get_disk_info_msf(unsigned char *highest_track_out,
                               unsigned char *minutes_out,
                               unsigned char *seconds_out,
                               unsigned char *frames_out)
{
    unsigned int playing_length_frames;
    unsigned int frames_in_second;
    unsigned int total_seconds;
    unsigned int seconds_in_minute;

    fdps_cdrom_read_disk_info();
    *highest_track_out = data_fdps_cd_highest_track_number;
    playing_length_frames = data_fdps_cd_leadout_sector - 0x96;
    frames_in_second = playing_length_frames % 75;
    *frames_out = (unsigned char) frames_in_second;
    total_seconds = (playing_length_frames - frames_in_second) / 75;
    seconds_in_minute = total_seconds % 60;
    *seconds_out = (unsigned char) seconds_in_minute;
    *minutes_out = (unsigned char) ((total_seconds - seconds_in_minute) / 60);
}
