/* audio.c -- digital sound effects through the Miles AIL library.
 *
 * See audio.h for what a caller has to know, resource_info/saf.md for the
 * container the clips come out of, and rebuild_info/ail_link.md for how the
 * vendor library is linked.  This file owns the sample-handle table and the
 * driver-available flag; it holds no other state.
 */
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
