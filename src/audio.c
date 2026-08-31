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
