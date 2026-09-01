/* audio.c -- digital sound effects through the Miles AIL library.
 *
 * See audio.h for what a caller has to know, resource_info/saf.md for the
 * container the clips come out of, and rebuild_info/ail_link.md for how the
 * vendor library is linked.  This file owns the sample-handle table and the
 * driver-available flag; it holds no other state.
 *
 * rand comes from <stdlib.h>.  It is a real call in the original -- CALL
 * 0x00042cf8 from the timer handler at 000307a1 -- because the flag set
 * carries no -oi, so __INLINE_FUNCTIONS__ is not defined and the header leaves
 * rand a call.  printf comes from <stdio.h> and is a real call too, CALL
 * 0x00042deb from the timer installer at 000307de.
 */
#include <stdio.h>
#include <stdlib.h>
#include "ailv3.h"
#include "gamedata.h"
#include "vfs.h"
#include "audio.h"

/* Sound is section 3 of a .SAF, so its descriptor is the last of the four
   10-byte section descriptors that begin at header offset 0x0c: u16 item count
   at +0x2a, u32 section start at +0x2c.  Both are addressed as byte offsets
   from the image base rather than through a header struct, for the reason
   src/saf.h gives -- the descriptors are byte-packed and a struct pads them
   apart.  A section opens with `count` u32 offsets, and every offset stored
   anywhere in a .SAF is measured from the start of the file, so it is rebased
   on the image base and not on the table it was read out of. */
#define SAF_SOUND_COUNT_OFFSET 0x2a
#define SAF_SOUND_SECTION_START_OFFSET 0x2c

/* One sound item: u8 channel count, u8 bits per sample, u16 sample rate, u32
   sample byte count, then that many unsigned PCM bytes. */
#define SFX_CLIP_CHANNELS_OFFSET 0
#define SFX_CLIP_BITS_OFFSET 1
#define SFX_CLIP_RATE_OFFSET 2
#define SFX_CLIP_LENGTH_OFFSET 4
#define SFX_CLIP_SAMPLES_OFFSET 8

/* Miles' digital sample format enumeration, the second argument of
   AIL_set_sample_type: mono/stereo crossed with 8/16 bits, in that order. */
#define AIL_SAMPLE_FORMAT_MONO_8 0
#define AIL_SAMPLE_FORMAT_MONO_16 1
#define AIL_SAMPLE_FORMAT_STEREO_8 2
#define AIL_SAMPLE_FORMAT_STEREO_16 3

/* The flags word that goes with the format.  Bit 0 is Miles' signed-PCM flag
   and is deliberately clear here: .SAF sample data is unsigned PCM. */
#define AIL_SAMPLE_TYPE_FLAGS 2

/* AIL_sample_status answers with the handle's state; 4 is the playing state.
   The scan below asks only whether a slot is that, so a handle that has never
   been allocated -- status 0 -- counts as free, which is what lets the first
   effect of a session play on slot 0. */
#define AIL_SAMPLE_STATUS_PLAYING 4

/* Every effect is played once at a fixed volume; the mix is balanced in the
   sample data, and nothing in the program varies either. */
#define SFX_SAMPLE_LOOP_COUNT 1
#define SFX_SAMPLE_VOLUME 0x28

/* The volume the .WAV path substitutes when the caller asked for the default
   with SFX_WAV_VOLUME_FROM_DEFAULT.  It is louder than the .SAF path's fixed
   0x28 above, and it is a separate literal in the image (PUSH 0x3c at
   00030bba) rather than a shared constant: the two paths are not required to
   agree and in the shipped game they do not. */
#define SFX_WAV_SAMPLE_VOLUME 0x3c

/* A .WAV image opens with the 12-byte RIFF wrapper -- the four id bytes, the
   32-bit size of everything after them, and the "WAVE" form type -- and the
   chunk list starts right behind it.  Every chunk is an 8-byte header, four id
   bytes and a 32-bit payload size, followed by that many payload bytes. */
#define RIFF_WRAPPER_SIZE 0x0c
#define RIFF_CHUNK_SIZE_OFFSET 4
#define RIFF_CHUNK_PAYLOAD_OFFSET 8

/* The three fields the header parser lifts out of a 'fmt ' chunk, as byte
   offsets from the chunk header rather than from the WAVEFORMATEX payload the
   standard describes: the assembly addresses them straight off the chunk
   pointer it kept, so payload+2, +4 and +0xe read as +0xa, +0xc and +0x16.
   Channels is taken as the low byte of the 16-bit nChannels field and the
   sample rate as the full 32-bit nSamplesPerSec. */
#define WAV_FMT_CHANNELS_OFFSET 0x0a
#define WAV_FMT_RATE_OFFSET 0x0c
#define WAV_FMT_BITS_OFFSET 0x16

/* 000142d0.  Three guards in the order the assembly has them, all of them
   plain returns to the shared epilogue rather than an error path.

   The driver-available flag is read twice -- CMP byte ptr [0x00069d71],0x0 at
   000142dc and again at 00014315, with nothing between them that could change
   it.  The second read is reproduced because the original has it; it costs one
   compare and removing it would be a judgement about what the original meant
   rather than what it does.

   The bound is a zero-extended 16-bit count compared with a signed compare:
   MOV AX,word ptr [EAX+0x2a] / AND EAX,0xffff / CMP EAX,dword ptr [EBP+0x18] /
   JG.  Both halves matter.  Reading the count as a signed short would make a
   count of 0x8000 negative and reject every index; comparing unsigned would
   let the caller's -1 through, and -1 is exactly what the caller passes -- a
   frame whose script has no sound stores sound id -1 and 00014140 sign-extends
   it into this argument (resource_info/saf.md).

   The scan stops at the first handle whose status is not "playing" and gives
   up when all eight are, which is a silent drop and the reason a burst of
   effects thins out rather than queueing.  The status comes back in EAX from
   the CALL at 00014349 and is compared with 4 there; nothing else in this
   function uses a value a CALL returned. */
void fdps_sfx_play(void *saf, int sound_index)
{
    unsigned char *saf_base;
    unsigned char *clip;
    unsigned int sound_section_start;
    int sample_format;
    int slot;

    if (data_fdps_audio_sfx_driver_available_flag == 0
        || data_fdps_audio_sfx_enabled_flag == 0) {
        return;
    }
    saf_base = (unsigned char *) saf;
    if (sound_index < 0
        || sound_index
           >= *(unsigned short *) (saf_base + SAF_SOUND_COUNT_OFFSET)) {
        return;
    }
    if (data_fdps_audio_sfx_driver_available_flag == 0) {
        return;
    }

    slot = 0;
    while (slot < SFX_SAMPLE_SLOT_COUNT
           && AIL_sample_status(data_fdps_audio_sample_handle_table[slot])
              == AIL_SAMPLE_STATUS_PLAYING) {
        slot++;
    }
    if (slot == SFX_SAMPLE_SLOT_COUNT) {
        return;
    }

    sound_section_start =
        *(unsigned int *) (saf_base + SAF_SOUND_SECTION_START_OFFSET);
    clip = saf_base + *(unsigned int *)
        (saf_base + sound_section_start + sound_index * 4);

    /* The channel and bit-depth bytes are read zero-extended (MOV AL,byte ptr
       [EAX] / AND EAX,0xff) and tested as a chain of two-field comparisons, so
       every combination the .SAF format does not use -- all 525 files are mono
       8-bit -- falls through to the stereo 16-bit code rather than being
       rejected. */
    if (clip[SFX_CLIP_CHANNELS_OFFSET] == 1 && clip[SFX_CLIP_BITS_OFFSET] == 8) {
        sample_format = AIL_SAMPLE_FORMAT_MONO_8;
    } else if (clip[SFX_CLIP_CHANNELS_OFFSET] == 1
               && clip[SFX_CLIP_BITS_OFFSET] == 16) {
        sample_format = AIL_SAMPLE_FORMAT_MONO_16;
    } else if (clip[SFX_CLIP_CHANNELS_OFFSET] == 2
               && clip[SFX_CLIP_BITS_OFFSET] == 8) {
        sample_format = AIL_SAMPLE_FORMAT_STEREO_8;
    } else {
        sample_format = AIL_SAMPLE_FORMAT_STEREO_16;
    }

    AIL_init_sample(data_fdps_audio_sample_handle_table[slot]);
    AIL_set_sample_address(data_fdps_audio_sample_handle_table[slot],
                           (unsigned int) (clip + SFX_CLIP_SAMPLES_OFFSET),
                           *(unsigned int *) (clip + SFX_CLIP_LENGTH_OFFSET));
    AIL_set_sample_type(data_fdps_audio_sample_handle_table[slot],
                        sample_format, AIL_SAMPLE_TYPE_FLAGS);
    AIL_set_sample_loop_count(data_fdps_audio_sample_handle_table[slot],
                              SFX_SAMPLE_LOOP_COUNT);
    AIL_set_sample_volume(data_fdps_audio_sample_handle_table[slot],
                          SFX_SAMPLE_VOLUME);
    AIL_set_sample_playback_rate(data_fdps_audio_sample_handle_table[slot],
                                 *(unsigned short *)
                                 (clip + SFX_CLIP_RATE_OFFSET));
    AIL_start_sample(data_fdps_audio_sample_handle_table[slot]);
}

/* 000304e0.  Straight-line bring-up with two optional halves, each an
   `if (handle != 0)` off what an install answered, and a tail call that is
   outside both: the JZ at 00030517 skips the sequence allocation, the JZ at
   00030544 skips the flag and the eight-slot loop, and both land on 00030584,
   which is MOV EAX,dword ptr [EBP+0x14] -- the argument load for the timer
   install.  So the clock goes on the air on a machine with no sound card at
   all, which is what makes every wait, animation step and blink in the game
   independent of the audio hardware.

   Four values come back from a CALL and every one of them is stored:
   AIL_install_MDI_INI's EAX into 0x69d6c at 0003050b, AIL_allocate_sequence_-
   handle's into 0x69d60 at 0003052e, AIL_install_DIG_INI's into 0x69d68 at
   00030538, and AIL_allocate_sample_handle's into [EDX+0x69d30] at 0003057c.
   AIL_startup returns nothing and fdps_audio_timer_install returns nothing;
   EAX is dead at the RET, and neither caller reads it (both do PUSH 0x19 /
   CALL / ADD ESP,0x4 and go straight on).

   Each handle is re-read out of its global for the test and for the argument
   that follows -- CMP dword ptr [0x00069d6c],0x0 then PUSH dword ptr
   [0x00069d6c] -- rather than kept in a register, which is what -od does with
   a plain assignment to a global.

   The three flag stores are unconditional and come before either install.
   Setting data_fdps_audio_sfx_enabled_flag to 1 here is a real effect and not
   an initialisation of something the caller has not written yet: it is the
   player's own "sound effects on" toggle, which the options menu writes and a
   save file carries (gamedata.h), and start-up turns it back on whatever the
   last session left it at.

   The MDI half is write-only state.  0x69d72, 0x69d60 and 0x69d6c have two,
   one and three references in the whole image and every one of them is in this
   function, so nothing ever asks whether a music driver installed, nothing
   ever plays the sequence handle, and nothing uninstalls the driver by name --
   the game's music comes off the CD (program_info/cd_audio.md) and this half
   of the bring-up leaves nothing behind that the rest of the program reads.
   The DIG driver handle at 0x69d68 is the same: its three references are the
   store, the test and the PUSH below, and the eight sample handles are what
   the rest of the file works with.

   The loop is the ordinary -od for-statement shape -- MOV [EBP-0x4],0x0 / CMP
   with 0x8 / JL to the body / JMP to the exit, with the increment block at
   0003055c reached by a JMP from the bottom of the body -- and the emitted
   object reproduces that layout branch for branch (WDISASM over the AUDIO.OBJ
   this file compiles to).  One instruction differs and it is a dead one: the
   original's increment block is MOV EAX,dword ptr [EBP-0x4] followed by INC
   dword ptr [EBP-0x4], where wcc386 gives the bare INC here.  The loaded value
   is discarded in the original -- EAX is overwritten by the next call before
   anything reads it -- so what spelling of the third clause makes 10.0a
   materialise it is not settled by this function, and it is register
   allocation either way (ADR-0001).  It is not the volatile case the tick
   handler below documents: slot is a plain local.

   The bound is a signed compare over the array's real extent, and the index
   reaches the table as LEA EDX,[EDX*0x4+0x0] / MOV dword ptr [EDX + 0x69d30],
   EAX -- base 0x69d30 with nothing folded into it, last slot 0x69d4c, and the
   next global at 0x69d50. */
void fdps_audio_init(int tick_rate_hz)
{
    int slot;

    AIL_startup();
    data_fdps_audio_bgm_driver_available_flag = 0;
    data_fdps_audio_sfx_driver_available_flag = 0;
    data_fdps_audio_sfx_enabled_flag = 1;

    data_fdps_audio_bgm_driver_handle = AIL_install_MDI_INI();
    if (data_fdps_audio_bgm_driver_handle != 0) {
        data_fdps_audio_bgm_driver_available_flag = 1;
        data_fdps_audio_bgm_sequence_handle =
            AIL_allocate_sequence_handle(data_fdps_audio_bgm_driver_handle);
    }

    data_fdps_audio_sfx_dig_driver_handle = AIL_install_DIG_INI();
    if (data_fdps_audio_sfx_dig_driver_handle != 0) {
        data_fdps_audio_sfx_driver_available_flag = 1;
        for (slot = 0; slot < SFX_SAMPLE_SLOT_COUNT; slot++) {
            data_fdps_audio_sample_handle_table[slot] =
                AIL_allocate_sample_handle(
                    data_fdps_audio_sfx_dig_driver_handle);
        }
    }

    fdps_audio_timer_install(tick_rate_hz);
}

/* 000305a0.  A Watcom frame around one call and nothing else: PUSH
   EBX/ESI/EDI/EBP, MOV EBP,ESP, SUB ESP,0x0, CALL AIL_shutdown, the mirrored
   POP sequence, RET.  There is no argument, no local, no branch and not one
   memory access in the body, so nothing the file owns changes: the eight
   sample handles keep the values fdps_audio_init put in them, both audio flags
   keep theirs, and the DIG driver handle is never passed to an uninstall call
   -- AIL_shutdown walks its own driver slots at 00045af9 and releases whatever
   is installed.  Nothing uses a value that came back from the CALL: EAX is
   dead at the RET, and the sole caller main@000293bd goes straight into the
   next CALL without reading it. */
void fdps_audio_shutdown(void)
{
    AIL_shutdown();
}

/* 000305c0.  Two paths off one equality test, CMP dword ptr [EBP+0x14],-0x1 /
   JZ: not the sentinel means one AIL_stop_sample on that slot and a jump
   straight to the epilogue, the sentinel means the walk.  The walk's bound is
   a signed compare, CMP dword ptr [EBP-0x4],0x8 / JL, over the array's real
   extent rather than a count somebody passed in.

   The index is scaled and folded into the displacement -- LEA EAX,[EAX*0x4+0x0]
   then PUSH dword ptr [EAX+0x69d30] -- so 0x69d30 is the base of the
   eight-entry handle table this file owns, not a neighbour's address reached
   by a fixed offset: index 7 lands on 0x69d4c, and the next global begins at
   0x69d50.

   There is no bounds check on sample_index, and neither audio flag is looked
   at, which is what separates this from its siblings: fdps_sfx_play and the
   voice allocator at 00030a00 both return early when the DIG driver did not
   install, and this one hands the eight table slots over whatever they hold.
   Handing AIL a handle that is still its BSS zero is harmless -- the vendor's
   worker at 00047470 opens with TEST EAX,EAX / JZ to its RET -- so adding the
   guard would be a judgement about what the original meant, and the emitted
   code does what it does instead.

   Nothing here uses a value that came back from a CALL: AIL_stop_sample
   returns void, and EAX is dead at every one of the three exits. */
void fdps_audio_stop_sample(int sample_index)
{
    int slot;

    if (sample_index != SFX_STOP_ALL_SLOTS) {
        AIL_stop_sample(data_fdps_audio_sample_handle_table[sample_index]);
        return;
    }
    for (slot = 0; slot < SFX_SAMPLE_SLOT_COUNT; slot++) {
        AIL_stop_sample(data_fdps_audio_sample_handle_table[slot]);
    }
}

/* 00030630.  The same voice allocator as fdps_sfx_play, with the container
   reading taken away: both audio flags, then the scan for a slot whose
   AIL_sample_status is not 4, then the AIL calls that load the handle.  Both
   failures leave through the same MOV dword ptr [EBP-0x4],0xffffffff, so a
   silenced audio system and eight busy voices are one answer, not two.
   [EBP-0x4] is the shared return slot -od gives a function with three exits;
   the returns below are that slot read at 00030730.

   Which AIL calls are absent is the point of the function.  fdps_sfx_play makes
   seven calls on the handle it claimed and this one makes five: there is no
   AIL_set_sample_type and no AIL_set_sample_volume between 000306b0 and
   00030722, so the format, the flags word and the volume stay as
   AIL_init_sample's worker at 000471e0 left them -- 0 at +0x34, 0 at +0x38, and
   the library's master volume at +0x40.  Nor is there any arithmetic on
   pcm_data: 000306bc PUSHes [EBP+0x14] itself, where the sibling adds 8 to step
   over a .SAF item header.  The caller hands over samples, not a file.

   The rate is the only argument that does not come from the caller: PUSH dword
   ptr [0x00069d5c] at 000306f4 reads the global unconditionally, so whatever it
   holds is what AIL is told, zero included.

   The scan's bound is a signed compare, CMP dword ptr [EBP-0x8],0x8 / JL, and
   the index is folded into the displacement as LEA EAX,[EAX*0x4+0x0] / PUSH
   dword ptr [EAX+0x69d30] -- the same eight-entry table this file owns, whose
   last slot is 0x69d4c with the next global at 0x69d50.

   One value comes back from a CALL: EAX from AIL_sample_status at 00030681, and
   the CMP EAX,0x4 at 00030689 is the whole use of it.  Every other CALL here
   returns void and EAX is dead after each one; the slot index the function
   returns comes from [EBP-0x8], not from AIL_start_sample. */
int fdps_audio_start_sample(void *pcm_data, unsigned int pcm_len,
                            unsigned int loop_count)
{
    int slot;

    if (data_fdps_audio_sfx_driver_available_flag == 0
        || data_fdps_audio_sfx_enabled_flag == 0) {
        return SFX_NO_SAMPLE_SLOT;
    }

    slot = 0;
    while (slot < SFX_SAMPLE_SLOT_COUNT
           && AIL_sample_status(data_fdps_audio_sample_handle_table[slot])
              == AIL_SAMPLE_STATUS_PLAYING) {
        slot++;
    }
    if (slot == SFX_SAMPLE_SLOT_COUNT) {
        return SFX_NO_SAMPLE_SLOT;
    }

    AIL_init_sample(data_fdps_audio_sample_handle_table[slot]);
    AIL_set_sample_address(data_fdps_audio_sample_handle_table[slot],
                           (unsigned int) pcm_data, pcm_len);
    AIL_set_sample_loop_count(data_fdps_audio_sample_handle_table[slot],
                              loop_count);
    AIL_set_sample_playback_rate(data_fdps_audio_sample_handle_table[slot],
                                 data_fdps_audio_sample_playback_rate);
    AIL_start_sample(data_fdps_audio_sample_handle_table[slot]);
    return slot;
}

/* 00030740.  One AIL call and one equality test on what it answered: MOV EAX,
   dword ptr [EBP+0x14] / LEA EAX,[EAX*0x4+0x0] / PUSH dword ptr [EAX+0x69d30]
   at 00030756 for the indexed handle, CALL AIL_sample_status at 0003075c, then
   CMP EAX,0x4 / JNZ at 00030764 with the two arms writing 1 and 0 into the
   shared return slot at [EBP-0x4].  The single return below is that slot, read
   at 00030779.  The test is an equality, so the status word's signedness never
   comes into it.

   One value comes back from a CALL and that is the whole body: EAX from
   AIL_sample_status, used only by the CMP.

   sample_index is used with no bound of any kind, and that is a live path
   rather than an oversight.  Both callers hand over whatever
   fdps_audio_start_wav@00030a00 answered with, and that is SFX_NO_SAMPLE_SLOT
   when it started nothing, so in FDPS.LE the PUSH really does read the dword at
   0x00069d2c.  Nothing in the image writes 0x00069d28 or 0x00069d2c -- they are
   the padding between the palette-cycle phase at 0x00069d24 and this table --
   so a -1 index fetches a null handle, and AIL_sample_status's worker at
   00047290 opens with TEST EAX,EAX / JZ and answers 0 for one.  The spinning
   caller therefore leaves its wait loop at once. */
int fdps_audio_sample_is_playing(int sample_index)
{
    return AIL_sample_status(data_fdps_audio_sample_handle_table[sample_index])
           == AIL_SAMPLE_STATUS_PLAYING;
}

/* 00030790.  Straight-line, no argument, no local and no branch: a Watcom
   frame (PUSH ESI/EDI/EBP, MOV EBP,ESP, SUB ESP,0x0 -- the original leaves EBX
   out of the save set, where the rebuild's prologue keeps it, which is
   register allocation and outside ADR-0001) around exactly two effects, then
   the mirrored POP sequence and RET.

   The increment is written ++x rather than x++ on purpose.  The counter is
   volatile, so post-increment makes the compiler materialise the old value it
   then throws away: wcc386 emits a dead MOV EAX,[counter] ahead of the INC for
   x++ and the bare INC for ++x, which is the original's single instruction.

   INC dword ptr [0x00069d64] at 0003079b is the whole of the first effect and
   the only memory operand anywhere in the body: nothing else this file owns is
   read or written, no audio flag is consulted and no AIL entry point is
   reached.  It is also the only write to that counter in the image -- one
   READ_WRITE among its references, against some fifty routines that only read
   it -- so the game's clock advances here and nowhere else, and it advances by
   exactly one per tick with a 32-bit wrap and no reset path.

   CALL rand at 000307a1 is the second effect and its result is thrown away:
   EAX still holds it at the RET but the function is reached through a pointer
   AIL calls from its timer interrupt, so there is no caller to read it.  The
   draw is the point -- one turn of the CRT's LCG per tick, which is what keeps
   the game's rolls from repeating between two runs that press the same keys.
   Removing the call would compile, and would change every random outcome in
   the game.

   Stack probes have to stay off in this translation unit.  The original's
   prologue is SUB ESP,0x0 with no CALL __CHK, which is the -s the game code is
   built with (rebuild_info/build_flags.md); a probe here would run on the
   interrupt's own stack, which is exactly the case the all-on setting kills. */
void fdps_timer_tick_handler(void)
{
    ++data_fdps_timer_tick_counter;
    rand();
}

/* 000307b0.  One branch and three vendor calls, with no local and no answer.
   The branch is CMP dword ptr [0x00069d50],-0x1 / JNZ 0x000307e6 at 000307cf
   and it guards the message alone: both arms land on the same PUSH at 000307e6,
   so a registration that failed is announced and then used, and the function
   returns normally either way.  There is no exit path, no retry and no second
   answer for the caller to read.

   One value comes back from a CALL and it is the whole of the state this
   function leaves behind: EAX from AIL_register_timer at 000307c2, stored into
   the global at 000307ca.  The two calls after it return void and EAX is dead
   at each of them.  The handle is then re-read out of memory for all three of
   its uses -- CMP dword ptr [0x00069d50] at 000307cf, PUSH dword ptr
   [0x00069d50] at 000307ea and again at 000307f8 -- rather than kept in a
   register, and that global is the only data operand anywhere in the body:
   neither audio flag is looked at and no sample handle is touched.

   What gets registered is the address of the tick handler above, MOV EAX,
   0x30790 / PUSH EAX at 000307bc with a fixup to it -- a number here would
   register whatever happened to sit at that address in the rebuild.  AIL files
   it in its own timer table and calls it from the timer interrupt from
   AIL_start_timer onwards, so the clock the whole game paces on starts on the
   last line of this function.

   The rate is the caller's argument handed straight to the library, [EBP+0x14]
   PUSHed at 000307e9 as AIL_set_timer_frequency's second argument with no
   arithmetic on it.  The library divides into it -- MOV EAX,0xf4240 / DIV EBX
   at 000450f0 -- so the period is 1000000/rate microseconds and a rate of zero
   would fault there; 25 is the only value the image ever passes.

   The failed handle really is passed on, and the vendor is not uniform about
   it: AIL_start_timer's worker tolerates -1 (CMP EBX,-0x1 / JZ at 00045016)
   while AIL_set_timer_period's does not (000450b0 writes through
   [handle + 0x60520] with no test at all), so that path puts one dword a byte
   below the library's period table.  It is a latent vendor bug on a path this
   game never reaches -- FDPS registers one timer out of the fifteen slots at
   00044f5e -- and guarding it here would be a judgement about what the original
   meant instead of what it does. */
void fdps_audio_timer_install(int tick_rate_hz)
{
    data_fdps_audio_timer_handle =
        (int) AIL_register_timer((unsigned int) fdps_timer_tick_handler);
    if (data_fdps_audio_timer_handle == AIL_TIMER_REGISTER_FAILED) {
        printf(" Timer fail !!!\n");
    }
    AIL_set_timer_frequency(data_fdps_audio_timer_handle, tick_rate_hz);
    AIL_start_timer(data_fdps_audio_timer_handle);
}

/* 00030810.  The whole body is MOV EAX,dword ptr [EBP+0x14] at 0003081c and MOV
   [0x00069d5c],EAX at 0003081f: one argument read, one store, no branch, no
   CALL and no second data operand.  The frame is the plain -od one -- PUSH
   EBX/ESI/EDI/EBP, MOV EBP,ESP, SUB ESP,0x0 -- so the argument sits at
   [EBP+0x14], the first slot above the four saved registers and the return
   address, and the RET at 00030828 carries no immediate: the caller clears it.

   Nothing between the load and the store touches the value.  There is no CMP
   anywhere in the body, so no range is enforced and no sign is tested; the old
   contents of the global are not read, so a second call simply replaces the
   first.  The store is a dword and the global is the same int
   AIL_set_sample_playback_rate takes.

   The reader is fdps_audio_start_sample, which PUSHes [0x00069d5c] at 000306f4
   on every call it gets past its guards.  Those two instructions are the only
   references to the global in the image, and no instruction anywhere calls this
   function -- so in a real session the rate stays the BSS zero and this setter
   is dead code that the game shipped with. */
void fdps_audio_set_sample_playback_rate(int samples_per_sec)
{
    data_fdps_audio_sample_playback_rate = samples_per_sec;
}

/* 00030830.  Two tag tests, one unbounded chunk walk and five stores, with no
   CALL anywhere in the body and no global touched: everything it reads comes
   through its two arguments and everything it writes goes into the caller's
   descriptor.

   The frame is the plain -od one -- PUSH EBX/ESI/EDI/EBP, MOV EBP,ESP, SUB
   ESP,0x14 -- so the two arguments sit at [EBP+0x14] and [EBP+0x18] and the RET
   at 000309fb carries no immediate.  The sole caller pushes them right to left
   and clears them itself: LEA EAX,[EBP-0x1c] / PUSH EAX for the descriptor,
   PUSH [EBP+0x14] for the image, CALL, ADD ESP,0x8 at 00030a7d.

   Both tag tests are chains of four zero-extended byte compares (MOV AL,byte
   ptr [EAX+n] / AND EAX,0xff / CMP EAX,imm) that fall into a shared store of
   -1 at 00030885 and 000308d5; only the first four bytes and bytes 8..0xb are
   looked at, so the RIFF size field at +4 is never read and never used to bound
   anything.  Neither pointer is checked against NULL.

   The walk keeps two slots, [EBP-0xc] for 'fmt ' and [EBP-0x8] for 'data',
   both cleared at 000308e5/000308ec before the first pass.  Each pass runs both
   id tests -- so a later chunk of the same id overwrites the slot an earlier
   one filled -- and only then asks whether both slots are non-NULL: CMP
   [EBP-0xc],0x0 / JZ to the advance at 0003099f, CMP [EBP-0x8],0x0 / JNZ to the
   stores at 000309b0.  The advance is ADD EAX,0x8 onto the payload size read at
   [cursor+4] and nothing else.

   That advance is the part a correct RIFF reader gets differently, and it must
   not be corrected here (rebuild_info/pitfalls.md).  There is no pad-to-even
   step, so an odd-sized chunk leaves the cursor one byte short of the next
   header and the walk reads ids out of the middle of the file from then on;
   there is no end-of-buffer test and no RIFF-size bound, so a .WAV missing
   either chunk walks off the end of the buffer instead of failing.  Both are
   safe for the images the game actually plays and neither can be guarded
   without changing what the function does.

   The five stores are the descriptor, in the assembly's order, and the three
   dwords land on odd offsets 2, 6 and 0xa -- the caller reads them back from
   [EBP-0x1a], [EBP-0x16] and [EBP-0x12] -- so it is a byte-packed record and
   not a naturally aligned one.  The last of them is not a field of the file at
   all: it is the address of the data chunk's payload, data+8, computed here and
   handed to AIL_set_sample_address by the caller.

   The answer is 0 on the store path and -1 from either tag test, and the caller
   throws it away -- XOR EAX,EAX at 00030a80 overwrites it before anything reads
   it, and it decides nothing there. */
int fdps_wav_parse_header(void *wav_data, void *info_out)
{
    unsigned char *riff_image;
    unsigned char *chunk;
    unsigned char *fmt_chunk;
    unsigned char *data_chunk;
    unsigned char *info;

    riff_image = (unsigned char *) wav_data;
    if (riff_image[0] != 'R' || riff_image[1] != 'I' || riff_image[2] != 'F'
        || riff_image[3] != 'F') {
        return -1;
    }
    if (riff_image[8] != 'W' || riff_image[9] != 'A' || riff_image[10] != 'V'
        || riff_image[11] != 'E') {
        return -1;
    }

    chunk = riff_image + RIFF_WRAPPER_SIZE;
    fmt_chunk = 0;
    data_chunk = 0;
    for (;;) {
        if (chunk[0] == 'f' && chunk[1] == 'm' && chunk[2] == 't'
            && chunk[3] == ' ') {
            fmt_chunk = chunk;
        }
        if (chunk[0] == 'd' && chunk[1] == 'a' && chunk[2] == 't'
            && chunk[3] == 'a') {
            data_chunk = chunk;
        }
        if (fmt_chunk != 0 && data_chunk != 0) {
            break;
        }
        chunk += *(unsigned int *) (chunk + RIFF_CHUNK_SIZE_OFFSET)
                 + RIFF_CHUNK_PAYLOAD_OFFSET;
    }

    info = (unsigned char *) info_out;
    info[WAV_INFO_CHANNELS_OFFSET] = fmt_chunk[WAV_FMT_CHANNELS_OFFSET];
    info[WAV_INFO_BITS_OFFSET] = fmt_chunk[WAV_FMT_BITS_OFFSET];
    *(unsigned int *) (info + WAV_INFO_RATE_OFFSET) =
        *(unsigned int *) (fmt_chunk + WAV_FMT_RATE_OFFSET);
    *(unsigned int *) (info + WAV_INFO_PCM_LENGTH_OFFSET) =
        *(unsigned int *) (data_chunk + RIFF_CHUNK_SIZE_OFFSET);
    *(unsigned char **) (info + WAV_INFO_PCM_DATA_OFFSET) =
        data_chunk + RIFF_CHUNK_PAYLOAD_OFFSET;
    return 0;
}

/* 00030a00.  The .WAV twin of fdps_sfx_play: the same two flag guards onto a
   shared -1, the same scan for the first slot whose AIL_sample_status is not 4,
   and the same four-arm format ladder -- with a header parse in place of the
   .SAF item lookup, and with three of the six values AIL is given coming from
   the caller instead of from the file.

   The frame is the plain -od one, PUSH EBX/ESI/EDI/EBP / MOV EBP,ESP / SUB
   ESP,0x1c, so the four arguments sit at [EBP+0x14], [EBP+0x18], [EBP+0x1c] and
   [EBP+0x20] and the caller clears them: all six call sites in the image push
   right to left and follow the CALL with ADD ESP,0x10 (0002a22a, 0001dfe9,
   0002648d, 00028a23, 00036946, 00021a4f).  Every one of the six pushes the same
   triple -- PUSH -0x1 for the volume, PUSH -0x1 for the rate, PUSH 0x1 for the
   loop count -- so the only argument that varies in the shipped game is the
   image itself, and the two default paths below are the ones that always run.
   Two callers keep the answer (MOV dword ptr [EBP-0x1c],EAX at 0001dfec and at
   00036949); the other four drop it.

   The 14-byte descriptor is this function's own stack local at [EBP-0x1c], and
   the three dwords are read back from the odd offsets fdps_wav_parse_header
   wrote them to -- [EBP-0x1a] for the rate, [EBP-0x16] for the byte count and
   [EBP-0x12] for the PCM address -- which is why it is a byte array here and not
   a struct.  The two byte fields are read zero-extended (XOR EAX,EAX / MOV AL,
   byte ptr [EBP-0x1c] / CMP EAX,0x1), so the ladder's comparisons are unsigned
   and every pair the first three tests miss falls into the last arm rather than
   being rejected.

   The parse's answer is thrown away, and it must stay thrown away
   (rebuild_info/pitfalls.md).  XOR EAX,EAX at 00030a80 overwrites it before
   anything reads it, so on a buffer that is not RIFF/WAVE -- where the parser
   returns -1 without writing a byte of the descriptor -- this function goes on
   to hand AIL whatever the stack slots happened to hold as the format, the
   address and the length.  Adding the obvious `if (parse failed) return -1`
   would make the rebuild behave differently from the original.

   Two arguments carry a -1 sentinel and each is tested exactly once, before any
   AIL call: CMP dword ptr [EBP+0x1c],-0x1 / JNZ at 00030ae4 replaces the rate
   with the descriptor's, and CMP dword ptr [EBP+0x20],-0x1 / JZ at 00030b96
   picks between the caller's volume and 0x3c.  The rate substitution writes back
   into the argument slot, so the value that reaches AIL is the same variable
   either way.

   The call order is the point of comparison with fdps_sfx_play.  Both make seven
   calls on the claimed handle, but this one puts AIL_set_sample_volume last, at
   00030bb0/00030bcc -- after AIL_start_sample at 00030b8e -- where the .SAF path
   sets it before the start.  The sound is therefore already running when its
   volume is set, and AIL's worker at 00047330 rebuilds the voice's volume table
   on a live sample.

   One value comes back from a CALL and it is used once: EAX from
   AIL_sample_status at 00030a51, compared with 4 at 00030a59.  Every other CALL
   here returns void or has its answer discarded, and the slot number the
   function returns comes from [EBP-0xc] via the shared return slot at
   [EBP-0x4], not from AIL_start_sample.

   The index reaches the table as LEA EAX,[EAX*0x4+0x0] / PUSH dword ptr
   [EAX+0x69d30] at all seven call sites -- base 0x69d30 with nothing folded into
   it, last slot 0x69d4c, next global at 0x69d50. */
int fdps_audio_start_wav(void *wav_data, int loop_count, int playback_rate,
                         int volume)
{
    unsigned char wav_info[WAV_INFO_SIZE];
    int sample_format;
    int slot;

    if (data_fdps_audio_sfx_driver_available_flag == 0
        || data_fdps_audio_sfx_enabled_flag == 0) {
        return SFX_NO_SAMPLE_SLOT;
    }

    slot = 0;
    while (slot < SFX_SAMPLE_SLOT_COUNT
           && AIL_sample_status(data_fdps_audio_sample_handle_table[slot])
              == AIL_SAMPLE_STATUS_PLAYING) {
        slot++;
    }
    if (slot == SFX_SAMPLE_SLOT_COUNT) {
        return SFX_NO_SAMPLE_SLOT;
    }

    fdps_wav_parse_header(wav_data, wav_info);

    if (wav_info[WAV_INFO_CHANNELS_OFFSET] == 1
        && wav_info[WAV_INFO_BITS_OFFSET] == 8) {
        sample_format = AIL_SAMPLE_FORMAT_MONO_8;
    } else if (wav_info[WAV_INFO_CHANNELS_OFFSET] == 1
               && wav_info[WAV_INFO_BITS_OFFSET] == 16) {
        sample_format = AIL_SAMPLE_FORMAT_MONO_16;
    } else if (wav_info[WAV_INFO_CHANNELS_OFFSET] == 2
               && wav_info[WAV_INFO_BITS_OFFSET] == 8) {
        sample_format = AIL_SAMPLE_FORMAT_STEREO_8;
    } else {
        sample_format = AIL_SAMPLE_FORMAT_STEREO_16;
    }

    if (playback_rate == SFX_WAV_RATE_FROM_HEADER) {
        playback_rate = *(int *) (wav_info + WAV_INFO_RATE_OFFSET);
    }

    AIL_init_sample(data_fdps_audio_sample_handle_table[slot]);
    AIL_set_sample_address(data_fdps_audio_sample_handle_table[slot],
                           *(unsigned int *)
                           (wav_info + WAV_INFO_PCM_DATA_OFFSET),
                           *(unsigned int *)
                           (wav_info + WAV_INFO_PCM_LENGTH_OFFSET));
    AIL_set_sample_type(data_fdps_audio_sample_handle_table[slot],
                        sample_format, AIL_SAMPLE_TYPE_FLAGS);
    AIL_set_sample_loop_count(data_fdps_audio_sample_handle_table[slot],
                              loop_count);
    AIL_set_sample_playback_rate(data_fdps_audio_sample_handle_table[slot],
                                 playback_rate);
    AIL_start_sample(data_fdps_audio_sample_handle_table[slot]);
    if (volume != SFX_WAV_VOLUME_FROM_DEFAULT) {
        AIL_set_sample_volume(data_fdps_audio_sample_handle_table[slot],
                              volume);
    } else {
        AIL_set_sample_volume(data_fdps_audio_sample_handle_table[slot],
                              SFX_WAV_SAMPLE_VOLUME);
    }
    return slot;
}

/* 0002a1f0.  The whole of the game's "play that sound" entry point, and the
   only reader of the BaseWav.vfs image besides the loader that fills the
   pointer and the shutdown that frees it: one lookup, one NULL test, one call.

   The frame is the plain -od one, PUSH EBX/ESI/EDI/EBP / MOV EBP,ESP / SUB
   ESP,0x8, so the single argument sits at [EBP+0x14]; all 49 call sites push
   one pointer and follow the CALL with ADD ESP,0x4, so the caller clears it.
   Nothing is returned -- EAX on exit is whatever the last call left there --
   and every call site drops it.

   The two stack dwords are the member pointer at [EBP-0x4] and the member's
   byte count at [EBP-0x8].  The count exists only because the lookup insists
   on somewhere to put it: no instruction in this function reads [EBP-0x8]
   back, and fdps_audio_start_wav takes the length out of the .WAV's own data
   chunk rather than from the container's directory.

   The container base is loaded from the global every call, PUSH dword ptr
   [0x000643a0] at 0002a204, so a name looked up before fdps_load_global_
   resources has run is looked up through a null pointer.  Nothing here guards
   that, and nothing needs to: the loader runs before the first effect.

   The three literals handed to fdps_audio_start_wav are PUSH 0x1 for the loop
   count at 0002a21f and PUSH -0x1 twice at 0002a21b and 0002a21d, which are
   its rate and volume sentinels -- so a clip plays once, at the rate its own
   header declares, at the .WAV path's fixed volume of 0x3c.  Neither -1 is a
   pan or a slot.

   A name that is not in the container is dropped in silence: TEST/JZ over the
   call at 0002a219 and no diagnostic, no fallback and no return value to tell
   a caller apart from one whose sound played.

   The name reaches strupr inside the lookup and is upper-cased in the caller's
   own storage (vfs.h), which is why every call site in the image passes a
   pointer to a writable data-segment string -- "Beep.wav" at 0x61b04,
   "Chess.wav" at 0x61550 -- and not a read-only literal
   (rebuild_info/pitfalls.md). */
void fdps_play_sfx(char *name)
{
    void *clip;
    unsigned int clip_bytes;

    clip = fdps_vfs_image_get_entry(
        (struct fdps_vfs_image_header *) data_fdps_audio_basewav_sfx_bank_buf_ptr,
        name, &clip_bytes);
    if (clip != NULL) {
        fdps_audio_start_wav(clip, SFX_SAMPLE_LOOP_COUNT,
                             SFX_WAV_RATE_FROM_HEADER,
                             SFX_WAV_VOLUME_FROM_DEFAULT);
    }
}
