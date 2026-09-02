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
 * Note on the original build flags: the request-staging routines here come
 * from the block from 0003bade to 0003c96x, which was not compiled with the
 * flag set the rest of the game was -- every function in that block opens with
 * PUSH <frame size> / CALL __CHK, the stack probe that -s removes and that no
 * other game function carries.  The rebuild has one flag set for every unit
 * (rebuild_info/build_flags.md), so what it builds from this file is the same
 * code without the probe, and the probe is not written out below.
 *
 * fdps_cd_set_music_track at 00030bf0 and fdps_cd_music_repeat_poll at
 * 00030c50 are the exceptions in the other direction: both sit outside that
 * block, were built with the game's own flag set and carry no probe of their
 * own.  Routing puts them here because they are the music layer's front door
 * onto these commands -- the one that starts a track and the one that keeps it
 * going -- not because they shared a translation unit with them.
 *
 * memcpy comes from <string.h> and is a real call in the image; the request
 * header layout comes from fdpstype.h, the DOS block pointer and the published
 * status word from gamedata.h, the driver request path from cd.h, and the
 * table-of-contents query the play range is resolved out of from cdtoc.h.
 */
#include <string.h>

#include "fdpstype.h"
#include "gamedata.h"
#include "cd.h"
#include "cdtoc.h"
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

/* 0003c452.  The MSCDEX Play Audio request -- command 0x84 -- that starts
   CD-DA playback over one range of disc sectors.  Like the seek above it
   carries nothing through the IOCTL block: the whole request is the header,
   and the header is the 22-byte Play Audio shape, shorter than either of the
   two shapes the rest of the module builds.

   That shape overlaps struct fdps_cd_request_header at three of its fields
   rather than matching it.  Play Audio has the addressing mode where the IOCTL
   shape has the media descriptor, at offset 0x0d; the starting sector as a
   full 32 bits at 0x0e, where the IOCTL shape has its 32-bit transfer address;
   and the sector count as a full 32 bits at 0x12, where the IOCTL shape has a
   16-bit byte count followed by a 16-bit starting sector.  The count is
   therefore written through the address of transfer_byte_count with a 32-bit
   store -- MOV dword ptr [ESP+0x12],EAX at 0003c482 -- which covers exactly
   those two 16-bit fields and stops short of volume_id_ptr.  Writing the
   16-bit field on its own would send the drive a two-byte count with two bytes
   of stack garbage above it: a track longer than 65535 sectors, which is
   anything past 14:33, would play the wrong length, and shorter ones would
   still be wrong whenever the garbage was not zero.  The offsets the two casts
   depend on are asserted in tests/cdaudio.c.

   Offsets 3 to 0x0c -- the status word and the eight reserved bytes -- are
   never initialised, here as in the original.

   There is no comparison and no branch: the count is end_sector minus
   start_sector, stored whole, with nothing checking that the range runs
   forwards.  The staged and read-back lengths are both the literal 0x16 the
   body pushes at 0003c486 and 0003c4a0, and the status word is the word at
   header offset 3.  This function never looks at that word itself; playback is
   asynchronous, so a caller learns nothing from it beyond whether the driver
   took the request. */
void fdps_cd_play_audio_range(unsigned int start_sector,
                              unsigned int end_sector)
{
    struct fdps_cd_request_header request_header;

    request_header.header_length = 0x16;
    request_header.subunit = 0;
    request_header.command = 0x84;
    request_header.media_descriptor = 0;
    request_header.transfer_address = start_sector;
    *(unsigned int *) &request_header.transfer_byte_count =
        end_sector - start_sector;

    memcpy(data_fdps_cd_request_header_buffer, &request_header, 0x16);
    fdps_cd_device_request();
    memcpy(&request_header, data_fdps_cd_request_header_buffer, 0x16);

    data_fdps_cd_last_request_status = request_header.status;
}

/* 0003c4a7.  The MSCDEX Stop Audio request -- command 0x85 -- that halts
   CD-DA playback.  It is the shortest request the module builds: the header
   declares itself 13 bytes long, which is the bare DOS device request header
   and nothing else -- length, subunit, command, the status word the driver
   writes back, and the eight reserved bytes.  There is no addressing-mode
   byte, no transfer address and no sector range, because the command takes no
   parameters at all; the drive is simply told to stop wherever it is.

   Three stores is the whole of the request: MOV byte ptr [ESP],0xd, MOV byte
   ptr [ESP+0x1],0x0 and MOV byte ptr [ESP+0x2],0x85 at 0003c4b4-0003c4c2.
   Offsets 3 to 0x0c -- the status word and the eight reserved bytes -- are
   never initialised, here as in the original: they go out to the driver
   holding whatever the stack frame held, and the driver overwrites the status
   word with its answer.  That is why the local is a whole struct rather than
   three bytes: the read-back at 0003c4f1 takes a word from offset 3, so the
   thirteen bytes have to be one object even though only three of them are
   written.

   Because the header stops at 13 bytes, none of the fields the struct declares
   from media_descriptor onwards is part of this request -- the copy in both
   directions is the literal 0xd pushed at 0003c4c2 and 0003c4dc, and the bytes
   above it in the DOS block are left holding whatever the previous request put
   there.  Nothing here needs the casts the seek and the play request need,
   since every field it does touch is a plain byte at its declared offset.

   The status word is the word at header offset 3 -- MOV EAX,[ESP+3] / MOV
   [0x00069e20],AX, a dword load of which only the low half is stored.  This
   function never looks at that word itself, and neither does any of its eight
   callers: every one of them calls this and moves straight on, so stopping
   playback is unconditional and a drive that refuses the command is not
   noticed. */
void fdps_cd_stop_audio(void)
{
    struct fdps_cd_request_header request_header;

    request_header.header_length = 0xd;
    request_header.subunit = 0;
    request_header.command = 0x85;

    memcpy(data_fdps_cd_request_header_buffer, &request_header, 0xd);
    fdps_cd_device_request();
    memcpy(&request_header, data_fdps_cd_request_header_buffer, 0xd);

    data_fdps_cd_last_request_status = request_header.status;
}

/* 0003c4ff.  The MSCDEX Resume Audio Play request -- command 0x88 -- that
   restarts playback the drive was told to hold.  It is the stop request with
   one byte changed: the same bare 13-byte device request header, the same
   three stores, the same absence of any addressing mode, transfer address or
   sector range, because this command takes no parameters either -- the drive
   resumes from wherever its own pause left it.

   The whole body is MOV byte ptr [ESP],0xd, MOV byte ptr [ESP+0x1],0x0 and MOV
   byte ptr [ESP+0x2],0x88 at 0003c50c-0003c51a, and then JMP 0003c4c2 into the
   middle of fdps_cd_stop_audio: the two routines differ only in that command
   byte, so the compiler tail-merged everything from the outbound memcpy
   onwards and the resume body owns no epilogue of its own.  What follows the
   jump is therefore literally the stop request's trailer, and the two are
   written here as two whole functions because the merge is codegen rather than
   behaviour (ADR-0001).

   Offsets 3 to 0x0c -- the status word and the eight reserved bytes -- are
   never initialised here either: they go out to the driver holding whatever
   the frame held.  The local is a whole struct for the same reason it is one
   in the stop request, because the read-back at 0003c4f1 takes a word from
   offset 3 and the thirteen bytes have to be one object.

   Nothing in the image calls this.  A sweep for the entry address -- xrefs,
   an operand search over all 89,420 instructions and a byte search for the
   little-endian pointer -- finds it referenced from nowhere, so the pause it
   pairs with is not issued anywhere either and no caller pins its behaviour
   further.  Whether the drive accepted the request is visible only in
   data_fdps_cd_last_request_status, which this leaves holding the status word
   the driver wrote back. */
void fdps_cd_resume_audio(void)
{
    struct fdps_cd_request_header request_header;

    request_header.header_length = 0xd;
    request_header.subunit = 0;
    request_header.command = 0x88;

    memcpy(data_fdps_cd_request_header_buffer, &request_header, 0xd);
    fdps_cd_device_request();
    memcpy(&request_header, data_fdps_cd_request_header_buffer, 0xd);

    data_fdps_cd_last_request_status = request_header.status;
}

/* 0003c5a6.  The one request in this file that asks the drive a question
   instead of ordering it about: MSCDEX IOCTL Input -- device command 3 -- with
   control code 0x0c, Audio Q-Channel Info, which comes back saying which CD-DA
   track is playing and how far into that track the head has got.

   Because it carries data both ways it is built in the 26-byte IOCTL shape
   struct fdps_cd_request_header describes -- the full record, so the declared
   length and the struct size agree here and no field of it is reached through a
   cast, unlike the seek and the play request above.  The transfer address is
   data_fdps_cd_ioctl_buffer_real_mode_ptr, the packed real-mode far pointer of
   the second DOS block, while the two memcpy's that fill and drain that block
   go through data_fdps_cd_ioctl_buffer, the flat linear address of the same
   memory.  The two are not interchangeable (rebuild_info/pitfalls.md).

   Offsets 3 to 0x0c -- the status word and the eight reserved bytes -- are
   never initialised, here as everywhere else in the module.

   The declared transfer is six bytes -- MOV word ptr [ESP+0x12],0x6 at
   0003c5de -- while the control block staged and drained around it is eleven,
   the PUSH 0xb at 0003c601 and 0003c62f.  That is not a slip to be tidied up:
   fdps_cd_read_audio_position, the only caller, reads the frame field at block
   offset 6, one byte past what the request declares, and it works because a
   real MSCDEX driver fills the whole Q-channel block regardless.  Setting the
   count to sizeof the block, or to the 0x0b both memcpy's use, changes what the
   game asks the driver for.

   Both read-back lengths are literals rather than the header's own length byte
   -- PUSH 0x1a at 0003c61a, where fdps_cd_read_audio_channel_info reads that
   byte back instead -- which comes to the same 0x1a either way.

   The body ends in a tail jump: PUSH 0xb / JMP 0003be27, into the middle of
   fdps_cd_read_audio_channel_info, which pushes the two remaining memcpy
   arguments and jumps on into 0003c590 inside fdps_cd_ioctl_output_command,
   where the drain, the status store and the RET live.  Three routines building
   the same request with different control codes share one epilogue; the merge
   is codegen rather than behaviour (ADR-0001), so what is written here is the
   whole of what this address does.

   The status word is published in data_fdps_cd_last_request_status because the
   function returns nothing: a drive that refused the request and a drive that
   really is sitting at track 1 minute 0 are otherwise indistinguishable, and
   bit 15 of that word is the driver's error flag. */
void fdps_cd_read_q_channel(struct fdps_cd_q_channel_block *q_channel)
{
    struct fdps_cd_request_header request_header;

    request_header.header_length = 0x1a;
    request_header.subunit = 0;
    request_header.command = 3;
    request_header.volume_id_ptr = 0;
    request_header.start_sector = 0;
    request_header.media_descriptor = 0;
    request_header.transfer_address = data_fdps_cd_ioctl_buffer_real_mode_ptr;
    request_header.transfer_byte_count = 6;
    q_channel->control_code = 0xc;

    memcpy(data_fdps_cd_request_header_buffer, &request_header, 0x1a);
    memcpy(data_fdps_cd_ioctl_buffer, q_channel, 0xb);
    fdps_cd_device_request();
    memcpy(&request_header, data_fdps_cd_request_header_buffer, 0x1a);
    memcpy(q_channel, data_fdps_cd_ioctl_buffer, 0xb);

    data_fdps_cd_last_request_status = request_header.status;
}

/* 0003c6e8.  Answers whether the CD-DA the game started has finished playing:
   ask the drive for its device status, then report whether the busy bit of the
   status word that request left behind is clear.  1 is idle, 0 is still
   playing.

   The whole body after the stack probe is two instructions -- CALL 0003c34f
   then JMP 0003c6d0 -- so it takes nothing, keeps nothing, and branches
   nowhere.  Its work is entirely in the order of the two: the question has to
   go out to the driver before the answer is read, because what is read is the
   status word the request itself deposits.

   Neither call hands a value back through a register.  The device status
   request is declared void and the value it does leave in EAX is dead the
   moment the predicate starts, whose first instruction is MOV AX,[0x00069e20]
   -- a reload of data_fdps_cd_last_request_status.  So the link between the
   two calls is that global and nothing else: fdps_cdrom_read_device_status
   refreshes it as a side effect of the request, and the four-byte device
   status the same routine publishes in data_fdps_cdrom_device_status, the
   answer the request was nominally asking for, is never looked at by anybody.
   The request is issued for the status word alone.

   The busy bit is bit 9, 0x0200, of the DOS device driver request header's
   status word.  A request the driver refused sets bit 15 instead and is not
   distinguished here -- see fdps_cd_status_is_not_busy -- and a request that
   never reached a driver at all leaves the word holding whatever the caller's
   frame had in it, since nothing in this module initialises the header's
   status field.  So the answer means "not busy" only where an MSCDEX driver
   answered.

   The result is 16 bits: the predicate zero-extends 0 or 1 into AX, and the
   one caller, fdps_cd_music_repeat_poll at 00030c8c, reads it as a word --
   TEST AX,AX / JNZ.  That caller polls this once every 0x4b of its own ticks,
   and only while a track is selected and its enable byte is set, which is what
   keeps a real device request off the per-frame path: every call here issues
   one.

   The tail jump is a call written out.  fdps_cd_close_tray reaches the same
   predicate the same way, by running off its own end into it, and both hand
   back the value the predicate computes, so both are written as a call to it
   (ADR-0001). */
unsigned short fdps_cd_audio_is_idle(void)
{
    fdps_cdrom_read_device_status();
    return fdps_cd_status_is_not_busy();
}

/* 0003c803.  Turns the track the CD layer is currently pointed at into the
   pair of disc sectors a Play Audio request takes, and publishes it in
   data_fdps_cd_play_range_start_sector and data_fdps_cd_play_range_end_sector.

   There is no argument.  The track acted on is whichever one
   fdps_cdrom_read_track_info queried last, which is why both callers --
   fdps_cd_play_track at 0003c878 and fdps_cd_play_track_range at 0003c8b1 --
   issue that query themselves and then call straight in.  The two globals read
   to find that track, data_fdps_cd_track_info_track_number and
   data_fdps_cd_track_start_sector, are that query's own answers.

   The range start is copied first, at 0003c80e-0003c813, ahead of either query
   below: those queries republish data_fdps_cd_track_start_sector, so a copy
   made after one of them would name the wrong track's start.

   The branch is the last-track test.  The track number is loaded as a signed
   word -- MOV AX,[0x00069dff] / MOVSX EBX,AX -- and the disc's highest track
   number as an unsigned byte -- MOVZX EDX,byte ptr [0x00069e07] -- and the
   compare of track + 1 against it is signed, CMP ECX,EDX / JLE.  Taking the
   jump, another track follows this one and the range ends where that one
   starts; falling through, this is the last track -- or a track number past
   the disc's highest, which nothing here refuses -- and the range ends at the
   lead-out.  Reading the highest track number as a signed byte instead would
   make a 255-track disc read as -1 and send every track down the lead-out arm.

   The end sector is one past the last sector to play, not the last one:
   fdps_cd_play_audio_range takes the pair and sends end - start as the sector
   count of its request.

   The last statement is a second query for the track this was entered on, and
   it is not dead code.  Nothing in the image reads the globals it republishes
   -- data_fdps_cd_track_start_sector, data_fdps_cd_track_info_track_number and
   data_fdps_cd_track_info_control_flags each have a reader only inside a
   routine that queries first -- but the call is a real MSCDEX IOCTL Input
   request to the drive, so dropping it changes how many device requests a
   track change costs.  fdps_cd_get_track_length_sectors carries the identical
   restore at 0003c205 for the identical reason, and between them they are the
   CD block's convention: the track-info globals are caller-owned state a
   helper puts back.  It also makes the non-last-track path cost two device
   requests that the last-track path does not.

   Neither call hands anything back.  fdps_cdrom_read_track_info is void, and
   the MOV EAX,[0x00069e01] at 0003c846 that follows the first one is a fresh
   load of the global that call republished, not a value it left in EAX.

   The argument of that first call is computed sixteen bits wide -- INC EAX /
   CWDE over the word loaded into AX at 0003c818, rather than the
   LEA ECX,[EBX + 0x1] the compare works from -- so the cast below is what
   keeps the sum in the width the assembly does it in.  Every reachable caller
   arrives with a small positive track number, where the two widths cannot
   differ. */
void fdps_cd_resolve_track_range(void)
{
    int selected_track;

    data_fdps_cd_play_range_start_sector = data_fdps_cd_track_start_sector;
    selected_track = data_fdps_cd_track_info_track_number;
    if (selected_track + 1 > data_fdps_cd_highest_track_number) {
        data_fdps_cd_play_range_end_sector = data_fdps_cd_leadout_sector;
        return;
    }
    fdps_cdrom_read_track_info((short) (selected_track + 1));
    data_fdps_cd_play_range_end_sector = data_fdps_cd_track_start_sector;
    fdps_cdrom_read_track_info(selected_track);
}

/* 0003c85b.  Plays one CD-DA track from its beginning to its end: four calls
   in a row, no branch, no local and nothing tested.

   The order is the whole of it.  The stop at 0003c865 comes first because the
   drive may still be playing the previous track and Play Audio does not
   replace a range in progress.  The query at 0003c870 is what points the CD
   layer's track-info globals at the requested track, and
   fdps_cd_resolve_track_range at 0003c878 reads exactly those globals to
   decide the range, so the query cannot be moved after it -- it also
   republishes data_fdps_cd_track_start_sector, which is the range start the
   resolve then copies.  The play request at 0003c889 comes last and takes the
   pair the resolve published.

   The argument is a signed word: MOVSX EAX,word ptr [ESP + 0x4] at 0003c86a is
   the only read of it, so the sixteen bits it is fetched at are the width the
   track number travels in even though every call site pushes a full dword.
   fdps_cdrom_read_track_info takes an int, and the sign extension is what the
   cast to it is.

   The two globals go out in the order the pushes name them -- PUSH dword ptr
   [0x00069de4] at 0003c87d, then PUSH dword ptr [0x00069dec] at 0003c883, so
   the last pushed is the first argument and the start sector is the one
   fdps_cd_play_audio_range receives first.

   Nothing here reports anything.  All four callees are void, EAX is never set
   deliberately and the RET at 0003c891 follows the ADD ESP,0x8 that cleans the
   play request's arguments, so this returns nothing.  A track number past the
   disc is not refused anywhere along the chain: the query answers with
   whatever the driver says, the resolve sends it down its lead-out arm, and
   the play request goes out on the range that produces. */
void fdps_cd_play_track(short track)
{
    fdps_cd_stop_audio();
    fdps_cdrom_read_track_info(track);
    fdps_cd_resolve_track_range();
    fdps_cd_play_audio_range(data_fdps_cd_play_range_start_sector,
                             data_fdps_cd_play_range_end_sector);
}

/* 0003c892.  Plays a run of consecutive CD-DA tracks as one range -- from where
   the first track starts through to where the last one ends -- and reports how
   long that run is in disc sectors.

   It is fdps_cd_play_track above with the range resolved twice instead of once.
   The stop at 0003c89e comes first for the same reason it does there: Play
   Audio does not replace a range already in progress.  Then the pair
   fdps_cdrom_read_track_info / fdps_cd_resolve_track_range runs once for
   first_track and once for last_track, because the resolve takes no argument
   and acts on whichever track the query left the CD layer pointing at.

   The order of the two loads is the whole of the function's difficulty.
   fdps_cd_resolve_track_range rewrites both data_fdps_cd_play_range_start_sector
   and data_fdps_cd_play_range_end_sector on every call, so the start sector is
   taken at 0003c8b6, between the two resolves, and only the end sector is taken
   after the second one at 0003c8cf.  Issuing both resolves and then reading the
   two globals -- the shorter way to write it, and the one the pair of globals
   invites -- would send last_track's start sector instead and play the wrong
   range.  That is this function's own trap rather than a pattern that repeats,
   so it lives here and in the plate comment rather than in
   rebuild_info/pitfalls.md.

   Both arguments are signed words: MOVSX EAX,word ptr [ESP + 0xc] at 0003c8a3
   and MOVSX EAX,word ptr [ESP + 0x10] at 0003c8bc are the only reads of them,
   and fdps_cdrom_read_track_info takes an int, so the sign extension is what
   the promotion is.

   There is no comparison and no branch anywhere in the body, so nothing checks
   that last_track follows first_track and nothing checks either against the
   disc.  A track number past the disc's highest goes down
   fdps_cd_resolve_track_range's lead-out arm like any other, and a reversed
   pair produces an end below the start: fdps_cd_play_audio_range then sends the
   difference to the driver as a sector count near 2^32, and the length returned
   here comes out negative.

   Neither of the two loads is a value a CALL handed back.  All four callees are
   void, and MOV ESI,dword ptr [0x00069dec] and MOV EBX,dword ptr [0x00069de4]
   are fresh loads of the globals the resolve just published, not of anything
   left in a register.  The returned length is the difference of those two
   loads, SUB EAX,ESI at 0003c8e1, and not a value read back out of the globals
   afterwards. */
int fdps_cd_play_track_range(short first_track, short last_track)
{
    unsigned int start_sector;
    unsigned int end_sector;

    fdps_cd_stop_audio();

    fdps_cdrom_read_track_info(first_track);
    fdps_cd_resolve_track_range();
    start_sector = data_fdps_cd_play_range_start_sector;

    fdps_cdrom_read_track_info(last_track);
    fdps_cd_resolve_track_range();
    end_sector = data_fdps_cd_play_range_end_sector;

    fdps_cd_play_audio_range(start_sector, end_sector);
    return (int) (end_sector - start_sector);
}

/* 0003c8e6.  Plays the whole CD as audio: one Play Audio request that begins at
   the disc's very first sector and runs for as many sectors as the lead-out
   sits at.  Four calls, no local, no branch and nothing tested.

   The start sector is the literal 0 the body pushes at 0003c90a -- PUSH 0x0 --
   and not where track 1's audio begins.  This is the function's own trap and
   the reason to read the pushes rather than the call order: the query two lines
   above resolves track 1 and republishes data_fdps_cd_track_start_sector, which
   is exactly the value the obvious rewrite reaches for, and on this game's
   mixed-mode disc the two are not the same place -- sector 0 is the data track.
   Writing the resolved start instead would move the beginning of playback.  It
   is this function's own trap rather than a pattern that repeats, so it lives
   here and in the plate comment rather than in rebuild_info/pitfalls.md.

   The track query at 0003c8f7 is a call whose every result is discarded.
   PUSH 0x1 / CALL 0003c0c8 / ADD ESP,0x4 asks fdps_cdrom_read_track_info about
   track 1, which republishes data_fdps_cd_track_info_track_number,
   data_fdps_cd_track_start_sector and data_fdps_cd_track_info_control_flags;
   nothing below reads any of the three, and no fdps_cd_resolve_track_range
   call follows to read them either.  Dropping it would still leave the play
   request identical and would still change the program: it sends a real IOCTL
   Input to the driver and it leaves the CD layer's track-info globals naming
   track 1, which every later reader of them sees.

   The stop at 0003c8f0 comes first for the reason it does in
   fdps_cd_play_track: Play Audio does not replace a range already in progress.
   The disc summary at 0003c8ff is what makes the count current -- it is the
   call that writes data_fdps_cd_leadout_sector, so the push of that global at
   0003c904 is a fresh load of what fdps_cdrom_read_disk_info just published
   and not a value the call left in a register.

   The two arguments go out as PUSH dword ptr [0x00069e0b] then PUSH 0x0, so
   the last pushed is the first argument: 0 is the start sector and the lead-out
   sector is the end.  fdps_cd_play_audio_range subtracts one from the other, so
   what the driver is asked to play is data_fdps_cd_leadout_sector sectors from
   sector 0.  Nothing checks that the lead-out is above 0, and on a drive that
   refused the summary query it is not -- the reply block reads 00:00:00, which
   fdps_cd_msf_to_sector turns into -150, and the count goes out near 2^32.

   Returns nothing and reports nothing: all four callees are void, EAX is never
   set deliberately, and the RET is reached through the tail
   fdps_cd_play_track's body ends with -- the JMP 0x0003c889 at 0003c90c lands
   on the CALL / ADD ESP,0x8 / RET the compiler merged between the two.  That
   merge is instruction selection and not behaviour (ADR-0001), so what is
   written below is the call itself.

   Nothing in the image calls it.  A sweep for the entry address -- xrefs and an
   operand search over all 89,420 instructions -- finds it referenced from
   nowhere, so no caller pins its behaviour further. */
void fdps_cd_play_whole_disc(void)
{
    fdps_cd_stop_audio();
    fdps_cdrom_read_track_info(1);
    fdps_cdrom_read_disk_info();
    fdps_cd_play_audio_range(0, data_fdps_cd_leadout_sector);
}

/* 0003c93a.  Asks the drive for the audio Q-channel and copies four bytes of
   the reply out through the caller's pointers: block+2, the TNO field, is the
   track playing now, and block+4, +5 and +6 are the minutes, seconds and
   frames of the running time inside that track.  The absolute position on the
   disc, which the driver reports at block+8 to block+10, is not read.

   The block is data_fdps_cd_q_channel_block, pushed as the literal 0x69e56 at
   0003c944 -- a block this file owns rather than anything the caller supplies
   -- so the four pointers are the whole of what a call hands back, and each
   call overwrites what the previous one left there.

   The order below is the order the body stores in -- track, frame, second,
   minute -- and it is kept because it is observable: nothing stops a caller
   passing one address twice, and then the last store is what survives.  The
   minute store is not spelled out in this function's own instructions.
   0003c972 loads AL from block+4 and 0003c977 jumps to 0003bc71, the
   MOV EDX,[ESP+8] / MOV [EDX],AL / RET tail sitting inside
   fdps_cd_unpack_msf, which stores through the same second-argument slot and
   returns.  Sharing an epilogue is instruction selection and not behaviour
   (ADR-0001), so what is written below is the store itself.

   There is no branch and no error path.  fdps_cd_read_q_channel returns
   nothing and nothing is read back from the CALL -- the reply is read out of
   the block the callee filled -- so a request the driver refused is
   indistinguishable here from a real answer: the block still holds whatever
   was in it, and this routine copies those bytes out regardless.  Only
   data_fdps_cd_last_request_status, which the callee publishes, says which of
   the two happened.

   The frame byte at offset 6 is read although the request the callee issues
   declares a six-byte transfer, so it is one byte past what the driver was
   asked to fill.  Neither half of that mismatch may be tidied up: a real
   MSCDEX driver fills the whole Q-channel block, and changing the count on the
   fdps_cd_read_q_channel side or guarding the read here changes what the game
   asks the drive for and what it reports back.

   Nothing in the image calls it.  A sweep for the entry address -- xrefs and
   an operand search over all 89,420 instructions -- finds it referenced from
   nowhere, so no caller pins its behaviour further. */
void fdps_cd_read_audio_position(unsigned char *track, unsigned char *minute,
                                 unsigned char *second, unsigned char *frame)
{
    fdps_cd_read_q_channel(&data_fdps_cd_q_channel_block);

    *track = data_fdps_cd_q_channel_block.track_number;
    *frame = data_fdps_cd_q_channel_block.frame;
    *second = data_fdps_cd_q_channel_block.second;
    *minute = data_fdps_cd_q_channel_block.minute;
}


/* 00030bf0.  Settles which background music the game should be playing and
   makes the drive match: the requested index goes into
   data_fdps_audio_cd_current_music_index, and the drive is then either sent to
   the matching CD track or stopped.

   The music-enabled setting is what makes this more than a two-line wrapper.
   CMP byte ptr [0x00060008],0x0 / JNZ at 00030bfc-00030c03 is above the store,
   not around the play call: with music off the argument is overwritten with -1
   at 00030c05 and it is that -1 which is published, so the setting rewrites
   the request rather than skipping it.  Writing the obvious early-out instead
   would leave the previous index standing in the global, and
   fdps_cd_music_repeat_poll -- which restarts whatever that global names when
   the drive falls idle -- would go on restarting the old track after the
   player switched music off.  This is the function's own trap and not a
   pattern that repeats, so it lives here and in the plate comment's Rebuild
   note rather than in rebuild_info/pitfalls.md.

   The store at 00030c0f is what the two tests below it read: CMP dword ptr
   [0x00069d54],-0x1 at 00030c14 and MOV EAX,[0x00069d54] at 00030c2d both go
   back to the global rather than to the argument slot, which is why the body
   below names the global on both.

   The second test of the music-enabled setting at 00030c24 is in the original
   and can never fail: reaching it means the global is not -1, and the only way
   for it not to be -1 is for the first test to have found the setting on.  It
   is kept because it is a branch the original executes; it costs one compare
   and decides nothing.

   The +1 at 00030c32 is the whole mapping between the two numbering schemes:
   the game counts background music from 0 and the disc's audio tracks start at
   2, track 1 being the data track.  fdps_cd_play_track fetches the number back
   as a signed word.

   Returns nothing, and reports nothing.  Both callees are void, no request
   status is looked at here, and a music index whose track is not on the disc
   goes out like any other -- nothing along the chain refuses it. */
void fdps_cd_set_music_track(int music_index)
{
    if (data_fdps_audio_bgm_enabled_flag == 0) {
        music_index = -1;
    }
    data_fdps_audio_cd_current_music_index = music_index;
    if (data_fdps_audio_cd_current_music_index == -1) {
        fdps_cd_stop_audio();
    } else if (data_fdps_audio_bgm_enabled_flag != 0) {
        fdps_cd_play_track(data_fdps_audio_cd_current_music_index + 1);
    }
}


/* 00030c50.  The background music's keep-alive: the game loops call this on
   every pass and it restarts the selected CD track once the drive has run off
   the end of it.  A CD-DA track plays once and stops -- nothing in the MSCDEX
   command set loops one -- so the looping the player hears is this poll
   noticing the silence and issuing the play command again.

   Takes nothing and reports nothing.  Everything it works on is a global, no
   argument register or stack slot is read, and SUB ESP,0x0 at 00030c56 says
   the frame carries no local either.

   Two throttles sit in front of the drive, and both are the point of the
   function.  The outer one is the tick latch: CMP EAX,dword ptr [0x00069d64] /
   JZ at 00030c61 returns at once unless the timer has moved since the last
   pass, so the body runs at most once per tick however many times a loop spins
   through it in that tick.  The inner one is the counter at 0x00060170, which
   only the outer test's survivors reach, so it counts ticks and not calls; at
   0x4b of them the drive is asked a question.  That is three seconds of wall
   clock: main pushes 25 into fdps_audio_init at 0002930c, which hands it down
   through fdps_audio_timer_install to AIL_set_timer_frequency, so the counter
   advances 25 times a second and 75 of its ticks are three of them.  Without
   both throttles, fdps_cd_audio_is_idle's real device request would go out on
   every frame of every menu.

   The counter is compared for equality and not for order: CMP dword ptr
   [0x00060170],0x4b / JNZ at 00030c6f.  It is exact because the only path that
   raises it is the INC directly above and the only path that clears it is
   inside the arm the test guards, so it cannot step over 0x4b -- but a counter
   that somehow started above it would count all the way round rather than fire
   at once, which is what an order test would have done instead.  The image
   ships it holding 0.

   The reset at 00030ca7 is below the join of all three inner arms, so the
   counter is cleared whether or not anything was played: a poll that found no
   track selected, or the music switched off, or the drive still playing waits
   another 0x4b ticks before asking again.

   The three inner tests are ordered as the cheapest first.  CMP dword ptr
   [0x00069d54],-0x1 asks whether any music is selected at all, CMP byte ptr
   [0x00060008],0x0 asks whether the player has music on, and only if both
   answer does the drive get a real device request -- both branches jump over
   the CALL at 00030c8c to the reset, so a poll on those arms costs no request.
   The two tests are not redundant with each other even though
   fdps_cd_set_music_track publishes -1 whenever the setting is off: the
   setting can be turned off between two polls without anybody republishing the
   index, and it is this test that stops the drive being restarted after that.

   TEST AX,AX / JNZ at 00030c91 is a 16-bit read of what fdps_cd_audio_is_idle
   handed back, which is that routine's zero-extended 0 or 1 -- non-zero means
   the drive is no longer busy, so the track has finished and is due to be
   played again.  Nothing else is taken from the call.

   The +1 at 00030c9d is the same numbering shift fdps_cd_set_music_track
   applies: data_fdps_audio_cd_current_music_index is the game's 0-based music
   index and the disc's audio tracks start at 2, track 1 being the data track.
   The index is read back out of the global here rather than remembered from
   anywhere, so what gets restarted is whatever the setter last published.

   The latch is refreshed last, at 00030cb1, and from a second read of the live
   counter rather than from the value the entry test compared -- the CD work in
   between can take long enough for the timer to move, and what is latched is
   the tick the pass finished on.  data_fdps_timer_tick_counter is volatile, so
   both reads stay in the rebuild. */
void fdps_cd_music_repeat_poll(void)
{
    if (data_fdps_audio_cd_repeat_last_tick != data_fdps_timer_tick_counter) {
        data_fdps_audio_cd_repeat_tick_counter++;

        if (data_fdps_audio_cd_repeat_tick_counter == 0x4b) {
            if (data_fdps_audio_cd_current_music_index != -1 &&
                data_fdps_audio_bgm_enabled_flag != 0 &&
                fdps_cd_audio_is_idle() != 0) {
                fdps_cd_play_track(data_fdps_audio_cd_current_music_index + 1);
            }
            data_fdps_audio_cd_repeat_tick_counter = 0;
        }

        data_fdps_audio_cd_repeat_last_tick = data_fdps_timer_tick_counter;
    }
}
