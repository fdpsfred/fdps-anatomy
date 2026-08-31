/* tests/audio.c -- cover for src/audio.c.
 *
 * Expected values come from the assembly at 000142d0 -- MOV AX,word ptr
 * [EAX+0x2a] / AND EAX,0xffff / CMP EAX,dword ptr [EBP+0x18] / JG for the
 * bound, CMP dword ptr [EBP+0x18],0x0 / JL for the floor, the two CMP byte ptr
 * [0x00069d71]/[0x00069d70] guards, the CALL/CMP EAX,0x4 scan over the eight
 * handles, the three adds that rebase the stored offset onto the image base,
 * and the PUSH sequence of each AIL call -- and from the .SAF layout in
 * resource_info/saf.md (sound is section 3, so its descriptor is the fourth
 * 10-byte one: u16 count at +0x2a, u32 section start at +0x2c; a sound item is
 * u8 channels, u8 bits, u16 rate, u32 sample bytes, then the samples).  None
 * of them is read off the emitted C.
 *
 * The image is staged here rather than read from a game file because a .SAF is
 * never a loose file -- all 525 live inside .VFS containers and reach this
 * function only as a block already unpacked into memory -- so a byte buffer is
 * exactly the shape the function is handed.
 *
 * WHAT PLAYBACK IS OBSERVED THROUGH.  This function's whole effect is the
 * arguments it hands the AIL library, and the real library is linked into this
 * executable (rebuild_info/ail_link.md), so the arguments are read back out of
 * the sample structure AIL itself writes them into.  The field offsets below
 * are the vendor's, taken from the library's own code as it sits in FDPS.LE:
 *
 *   +0x00 driver     read by 00047426 (AIL_start_sample's worker)
 *   +0x04 status     returned by 00047290, set to 2 by 000471ec, to 4 by 00047423
 *   +0x08 address    written by 000472ba (AIL_set_sample_address's worker)
 *   +0x10 length     written by 000472c1
 *   +0x30 loop count written by 000473ec
 *   +0x34 format     written by 000472ea
 *   +0x38 flags      written by 000472ee
 *   +0x3c rate       written by 0004731c
 *   +0x40 volume     written by 00047342
 *
 * A handle is therefore a plain structure here, zero-filled and 0x854 bytes --
 * the largest offset AIL_init_sample writes is +0x850 -- with two fields set:
 * the status the scan is meant to see, and a driver pointer, because
 * AIL_init_sample's volume-table rebuild at 00045f50 reads through it.  The
 * driver structure is zero except at +0x54, which AIL_start_sample reads at
 * 0004742c and returns on when it is non-zero: the mixer is never started, no
 * DMA is programmed, and nothing here needs a real sound card.  The first test
 * checks that premise against the linked library instead of assuming it.
 */
#include <stdlib.h>
#include "testharn.h"
#include "ailv3.h"
#include "gamedata.h"
#include "audio.h"

/* Vendor field offsets, as documented above.  All are 4-byte aligned, so the
   fixtures are arrays of unsigned int and a field is one element. */
#define SAMPLE_DRIVER 0x00
#define SAMPLE_STATUS 0x04
#define SAMPLE_ADDRESS 0x08
#define SAMPLE_LENGTH 0x10
#define SAMPLE_LOOP_COUNT 0x30
#define SAMPLE_FORMAT 0x34
#define SAMPLE_FLAGS 0x38
#define SAMPLE_RATE 0x3c
#define SAMPLE_VOLUME 0x40
#define DRIVER_STARTED 0x54

#define SAMPLE_WORDS (0x854 / 4 + 1)
#define DRIVER_WORDS (0x80 / 4)

/* The two states the scan distinguishes.  4 is Miles' playing state, the one
   value 000142d0 compares against; 2 is the state AIL_init_sample leaves a
   finished handle in, so it is what a free slot looks like in the game. */
#define STATUS_PLAYING 4
#define STATUS_DONE 2

static unsigned int busy_sample[SAMPLE_WORDS];
static unsigned int free_sample[SAMPLE_WORDS];
static unsigned int fake_driver[DRIVER_WORDS];

/* The staged .SAF image: header, then the sound section's offset table, then
   three clips.  Every stored offset is file-relative, as in the real thing. */
#define IMAGE_SIZE 0x10420
#define SOUND_SECTION_START 0x100
/* Past 0xffff, so a 16-bit read of the section start cannot reach it. */
#define HIGH_SECTION_START 0x10000
#define CLIP_0_OFFSET 0x200
#define CLIP_1_OFFSET 0x260
#define CLIP_2_OFFSET 0x2c0

static unsigned char stage_image[IMAGE_SIZE];

static unsigned int sample_field(unsigned int *sample, int offset)
{
    return sample[offset / 4];
}

/* Where the address AIL was given lands inside the staged image, or -1 when
   nothing was handed over at all.  Reporting the offset rather than the
   pointer keeps the expected values readable and independent of where the
   loader put the fixture. */
static long played_offset(void)
{
    unsigned int address;

    address = sample_field(free_sample, SAMPLE_ADDRESS);
    if (address == 0) {
        return -1;
    }
    return (long) (address - (unsigned int) stage_image);
}

static void stage_driver(void)
{
    int i;

    for (i = 0; i < DRIVER_WORDS; i++) {
        fake_driver[i] = 0;
    }
    fake_driver[DRIVER_STARTED / 4] = 1;
}

/* Puts every handle slot on a sample that is playing, except free_slot, which
   gets one that is not.  free_slot -1 leaves all eight busy. */
static void stage_slots(int free_slot)
{
    int i;

    stage_driver();
    for (i = 0; i < SAMPLE_WORDS; i++) {
        busy_sample[i] = 0;
        free_sample[i] = 0;
    }
    busy_sample[SAMPLE_STATUS / 4] = STATUS_PLAYING;
    free_sample[SAMPLE_STATUS / 4] = STATUS_DONE;
    busy_sample[SAMPLE_DRIVER / 4] = (unsigned int) fake_driver;
    free_sample[SAMPLE_DRIVER / 4] = (unsigned int) fake_driver;
    for (i = 0; i < SFX_SAMPLE_SLOT_COUNT; i++) {
        data_fdps_audio_sample_handle_table[i] = busy_sample;
    }
    if (free_slot >= 0) {
        data_fdps_audio_sample_handle_table[free_slot] = free_sample;
    }
}

static void write_u16(unsigned long offset, unsigned long value)
{
    stage_image[offset] = (unsigned char) (value & 0xff);
    stage_image[offset + 1] = (unsigned char) ((value >> 8) & 0xff);
}

static void write_u32(unsigned long offset, unsigned long value)
{
    stage_image[offset] = (unsigned char) (value & 0xff);
    stage_image[offset + 1] = (unsigned char) ((value >> 8) & 0xff);
    stage_image[offset + 2] = (unsigned char) ((value >> 16) & 0xff);
    stage_image[offset + 3] = (unsigned char) ((value >> 24) & 0xff);
}

/* The sound section's descriptor: u16 item count at +0x2a, u32 section start
   at +0x2c.  Written a byte at a time, so the fixture makes no alignment
   assumption of its own about fields the code reads out of a packed header. */
static void stage_sound_section(unsigned long count, unsigned long start)
{
    write_u16(0x2a, count);
    write_u32(0x2c, start);
}

static void stage_offset_entry(unsigned long section_start, int index,
                               unsigned long clip_offset)
{
    write_u32(section_start + index * 4, clip_offset);
}

static void stage_clip(unsigned long clip_offset, int channels, int bits,
                       unsigned long rate, unsigned long length)
{
    stage_image[clip_offset] = (unsigned char) channels;
    stage_image[clip_offset + 1] = (unsigned char) bits;
    write_u16(clip_offset + 2, rate);
    write_u32(clip_offset + 4, length);
}

/* The image every test starts from: three clips, all reachable, with three
   different rates and lengths so an index that lands on the wrong entry says
   so instead of matching by accident.  Magic and version are the real ones --
   a reader that lost the table base and landed on offset 0 would otherwise
   find four zeroes there and look plausible. */
static void stage_default_image(void)
{
    int i;

    for (i = 0; i < IMAGE_SIZE; i++) {
        stage_image[i] = 0;
    }
    stage_image[0] = 'S';
    stage_image[1] = 'A';
    stage_image[2] = 'F';
    stage_image[3] = 6;
    stage_sound_section(3, SOUND_SECTION_START);
    stage_offset_entry(SOUND_SECTION_START, 0, CLIP_0_OFFSET);
    stage_offset_entry(SOUND_SECTION_START, 1, CLIP_1_OFFSET);
    stage_offset_entry(SOUND_SECTION_START, 2, CLIP_2_OFFSET);
    stage_clip(CLIP_0_OFFSET, 1, 8, 22050, 0x11);
    stage_clip(CLIP_1_OFFSET, 1, 8, 11025, 0x30);
    stage_clip(CLIP_2_OFFSET, 1, 8, 8000, 0x40);
}

/* Both flags set, one free slot, the default image: the state in which an
   effect is expected to play. */
static void stage_ready(int free_slot)
{
    stage_default_image();
    stage_slots(free_slot);
    data_fdps_audio_sfx_driver_available_flag = 1;
    data_fdps_audio_sfx_enabled_flag = 1;
}

/* The premise everything else rests on, checked against the linked library
   rather than assumed: AIL_sample_status answers with the word at +0x04, and a
   handle that has never been started is not "playing". */
static void the_fixture_looks_like_a_handle_to_ail(void)
{
    stage_slots(2);
    CHECK_EQ(AIL_sample_status(busy_sample), STATUS_PLAYING);
    CHECK_EQ(AIL_sample_status(free_sample), STATUS_DONE);
}

/* The whole play path in one go: the scan skips the two busy slots and stops
   on the third, and every argument handed to AIL is read back out of that
   handle.  0x28 for the volume and 1 for the loop count are the literals
   PUSHed at 0001447d and 00014463; the flags word 2 is the PUSH at 00014445.
   The address is the clip's samples, eight bytes past the item header, and the
   length is the u32 at +4 of that header -- the item is index 1, so an
   off-by-one in the table walk lands on clip 0 or clip 2 and misses both. */
static void plays_on_the_first_slot_that_is_not_playing(void)
{
    stage_ready(2);
    fdps_sfx_play(stage_image, 1);
    CHECK_EQ(played_offset(), CLIP_1_OFFSET + 8);
    CHECK_EQ(sample_field(free_sample, SAMPLE_LENGTH), 0x30);
    CHECK_EQ(sample_field(free_sample, SAMPLE_RATE), 11025);
    CHECK_EQ(sample_field(free_sample, SAMPLE_VOLUME), 0x28);
    CHECK_EQ(sample_field(free_sample, SAMPLE_LOOP_COUNT), 1);
    CHECK_EQ(sample_field(free_sample, SAMPLE_FLAGS), 2);
    CHECK_EQ(sample_field(free_sample, SAMPLE_STATUS), STATUS_PLAYING);
    CHECK_EQ(sample_field(busy_sample, SAMPLE_ADDRESS), 0);
}

/* The scan runs 0..7 and takes the first free slot, whichever it is. */
static void the_last_slot_is_reachable(void)
{
    stage_ready(SFX_SAMPLE_SLOT_COUNT - 1);
    fdps_sfx_play(stage_image, 0);
    CHECK_EQ(played_offset(), CLIP_0_OFFSET + 8);
    CHECK_EQ(sample_field(free_sample, SAMPLE_RATE), 22050);
}

/* All eight busy: the request is dropped, and dropped without touching the
   handles.  If the scan's compare went the other way it would take slot 0 and
   the busy handle would come back holding a clip. */
static void all_slots_playing_drops_the_request(void)
{
    stage_ready(-1);
    fdps_sfx_play(stage_image, 1);
    CHECK_EQ(sample_field(busy_sample, SAMPLE_ADDRESS), 0);
    CHECK_EQ(sample_field(busy_sample, SAMPLE_LENGTH), 0);
    CHECK_EQ(sample_field(busy_sample, SAMPLE_RATE), 0);
}

/* (1,8) -> 0, (1,16) -> 1, (2,8) -> 2, everything else -> 3.  The flags word
   is checked alongside format 0 because AIL_init_sample zeroes the format
   field too: flags 2 is what proves AIL_set_sample_type ran at all. */
static void format_comes_from_the_channel_and_bit_depth_bytes(void)
{
    stage_ready(0);
    stage_clip(CLIP_1_OFFSET, 1, 8, 11025, 0x30);
    fdps_sfx_play(stage_image, 1);
    CHECK_EQ(sample_field(free_sample, SAMPLE_FORMAT), 0);
    CHECK_EQ(sample_field(free_sample, SAMPLE_FLAGS), 2);

    stage_ready(0);
    stage_clip(CLIP_1_OFFSET, 1, 16, 11025, 0x30);
    fdps_sfx_play(stage_image, 1);
    CHECK_EQ(sample_field(free_sample, SAMPLE_FORMAT), 1);

    stage_ready(0);
    stage_clip(CLIP_1_OFFSET, 2, 8, 11025, 0x30);
    fdps_sfx_play(stage_image, 1);
    CHECK_EQ(sample_field(free_sample, SAMPLE_FORMAT), 2);

    stage_ready(0);
    stage_clip(CLIP_1_OFFSET, 2, 16, 11025, 0x30);
    fdps_sfx_play(stage_image, 1);
    CHECK_EQ(sample_field(free_sample, SAMPLE_FORMAT), 3);
}

/* An unlisted pair is not rejected and not treated as mono 8-bit: it falls
   through to 3, which is the last else of the chain and not a default anyone
   chose. */
static void an_unknown_channel_bit_pair_falls_through_to_three(void)
{
    stage_ready(0);
    stage_clip(CLIP_1_OFFSET, 1, 4, 11025, 0x30);
    fdps_sfx_play(stage_image, 1);
    CHECK_EQ(sample_field(free_sample, SAMPLE_FORMAT), 3);

    stage_ready(0);
    stage_clip(CLIP_1_OFFSET, 0, 8, 11025, 0x30);
    fdps_sfx_play(stage_image, 1);
    CHECK_EQ(sample_field(free_sample, SAMPLE_FORMAT), 3);
}

/* MOV AX,word ptr [EAX+0x2] / AND EAX,0xffff: the rate is a 16-bit field read
   without sign extension, so a rate above 0x7fff stays positive. */
static void rate_is_a_16_bit_zero_extended_read(void)
{
    stage_ready(0);
    stage_clip(CLIP_1_OFFSET, 1, 8, 0xf000, 0x30);
    fdps_sfx_play(stage_image, 1);
    CHECK_EQ(sample_field(free_sample, SAMPLE_RATE), 0xf000);
}

/* PUSH dword ptr [EAX+0x4]: the sample byte count is a full 32-bit field.  A
   16-bit read would report 0x2345 for this clip. */
static void length_is_a_full_32_bit_read(void)
{
    stage_ready(0);
    stage_clip(CLIP_1_OFFSET, 1, 8, 11025, 0x12345);
    fdps_sfx_play(stage_image, 1);
    CHECK_EQ(sample_field(free_sample, SAMPLE_LENGTH), 0x12345);
}

/* -1 is what a frame with no sound stores, sign-extended into this argument by
   the caller at 000142b5, so the floor check is the one that runs on almost
   every frame the game draws. */
static void a_negative_index_plays_nothing(void)
{
    stage_ready(0);
    fdps_sfx_play(stage_image, -1);
    CHECK_EQ(played_offset(), -1);
    CHECK_EQ(sample_field(free_sample, SAMPLE_STATUS), STATUS_DONE);
}

/* The bound is exclusive, and the count is 16 bits: the section start sits
   immediately after it, so a 32-bit read of +0x2a would see 0x01000003 and
   accept index 3.  Index 2 is played first to show the fixture is live. */
static void the_count_is_an_exclusive_16_bit_bound(void)
{
    stage_ready(0);
    fdps_sfx_play(stage_image, 2);
    CHECK_EQ(played_offset(), CLIP_2_OFFSET + 8);

    stage_ready(0);
    fdps_sfx_play(stage_image, 3);
    CHECK_EQ(played_offset(), -1);
}

/* A count of zero accepts nothing, index 0 included. */
static void a_zero_count_plays_nothing(void)
{
    stage_ready(0);
    stage_sound_section(0, SOUND_SECTION_START);
    fdps_sfx_play(stage_image, 0);
    CHECK_EQ(played_offset(), -1);
}

/* The two flags are separate answers and either one alone silences the effect:
   the player's toggle at 00069d70 and whether a DIG driver was installed at
   00069d71. */
static void either_audio_flag_clear_plays_nothing(void)
{
    stage_ready(0);
    data_fdps_audio_sfx_enabled_flag = 0;
    fdps_sfx_play(stage_image, 1);
    CHECK_EQ(played_offset(), -1);

    stage_ready(0);
    data_fdps_audio_sfx_driver_available_flag = 0;
    fdps_sfx_play(stage_image, 1);
    CHECK_EQ(played_offset(), -1);

    stage_ready(0);
    data_fdps_audio_sfx_enabled_flag = 0;
    data_fdps_audio_sfx_driver_available_flag = 0;
    fdps_sfx_play(stage_image, 1);
    CHECK_EQ(played_offset(), -1);
}

/* The stored offset is rebased on the image base, not on the table it was read
   out of: an offset that would land inside the image either way proves
   nothing, so entry 1 points at a clip that is before the table. */
static void the_stored_offset_is_rebased_on_the_image_base(void)
{
    stage_ready(0);
    stage_offset_entry(SOUND_SECTION_START, 1, 0x40);
    stage_clip(0x40, 1, 8, 16000, 0x20);
    fdps_sfx_play(stage_image, 1);
    CHECK_EQ(played_offset(), 0x40 + 8);
    CHECK_EQ(sample_field(free_sample, SAMPLE_RATE), 16000);
}

/* The section start is a full 32-bit field: with the offset table put past
   0xffff, a 16-bit read of +0x2c would see 0 and walk the file header as
   though it were the table. */
static void the_section_start_is_a_full_32_bit_field(void)
{
    stage_ready(0);
    stage_sound_section(3, HIGH_SECTION_START);
    stage_offset_entry(HIGH_SECTION_START, 1, CLIP_1_OFFSET);
    fdps_sfx_play(stage_image, 1);
    CHECK_EQ(played_offset(), CLIP_1_OFFSET + 8);
    CHECK_EQ(sample_field(free_sample, SAMPLE_RATE), 11025);
}

/* ---- fdps_audio_shutdown @ 000305a0 --------------------------------------
 *
 * The body is a Watcom frame around one CALL to AIL_shutdown and holds no
 * other instruction, so there are exactly two things to establish: that the
 * call really happens, and that nothing else does.  Both are read out of the
 * linked library and out of the game state the file owns, never off the
 * emitted C.
 *
 * WHY EVERY CASE HERE STARTS WITH AIL_startup.  AIL_shutdown finishes by
 * putting the timer vector back with INT 21h AH=25h at 00044a96, from the
 * copy at 0x000605e0 -- and that copy is written on the startup path, at
 * 00044a2e.  The image holds zero there, so a library that was never started
 * would have the timer vector pointed at nothing.  The game pairs the two the
 * same way round: fdps_audio_init@000304e0 opens with CALL AIL_startup at
 * 000304ec, and this function is the other end of that pair.  With AIL_DEBUG
 * unset -- the string is at 0x0006227b, read at 0003d731 -- startup takes its
 * short path and no log file is opened.
 *
 * WHAT PROVES THE CALL HAPPENED.  AIL's timer table is fifteen four-byte
 * slots at 0x000604a0: the allocator at 00044f4e hands back the byte offset
 * of the lowest free slot, or -1 when all fifteen are taken, and the shared
 * exit at 0003eb4a returns it.  AIL_shutdown reaches AIL_release_all_timers
 * at 0003e7be, whose loop at 00044ff1 frees handles 0x38 down to 0.  So a
 * handle number that drops back to the bottom of the table across the call is
 * the library's own record that the shutdown ran.  Nothing here ever starts a
 * timer, so the registered callback is never entered and a null one is all
 * the allocator needs: it only stores the value at 0x00060460 + handle.
 */
#define TIMER_SLOT_STRIDE 4
#define NO_TIMER_CALLBACK 0

/* Registering twice, shutting down, then registering again: the third handle
   is the first slot of the table, which it can only be if AIL_shutdown
   released the two that were out. */
static void shutdown_releases_the_ail_timer_handles(void)
{
    HTIMER first;
    HTIMER second;
    HTIMER after_shutdown;

    AIL_startup();
    first = AIL_register_timer(NO_TIMER_CALLBACK);
    second = AIL_register_timer(NO_TIMER_CALLBACK);
    CHECK_EQ(first, 0);
    CHECK_EQ(second, TIMER_SLOT_STRIDE);

    fdps_audio_shutdown();

    after_shutdown = AIL_register_timer(NO_TIMER_CALLBACK);
    CHECK_EQ(after_shutdown, 0);
    AIL_release_timer_handle(after_shutdown);
}

/* The body has no memory access at all, so tearing the audio system down
   leaves every piece of state this file owns exactly as it stood: the two
   flags keep their values, the eight handle-table entries keep their
   pointers, and the sample structures themselves are neither stopped nor
   re-initialised -- AIL_shutdown is never handed one.  Each of those is
   something an intuitive rewrite would plausibly add. */
static void shutdown_leaves_the_game_audio_state_alone(void)
{
    stage_ready(2);
    AIL_startup();
    fdps_audio_shutdown();
    CHECK_EQ(data_fdps_audio_sfx_driver_available_flag, 1);
    CHECK_EQ(data_fdps_audio_sfx_enabled_flag, 1);
    CHECK_EQ((unsigned int) data_fdps_audio_sample_handle_table[0],
             (unsigned int) busy_sample);
    CHECK_EQ((unsigned int) data_fdps_audio_sample_handle_table[2],
             (unsigned int) free_sample);
    CHECK_EQ((unsigned int)
             data_fdps_audio_sample_handle_table[SFX_SAMPLE_SLOT_COUNT - 1],
             (unsigned int) busy_sample);
    CHECK_EQ(sample_field(busy_sample, SAMPLE_STATUS), STATUS_PLAYING);
    CHECK_EQ(sample_field(free_sample, SAMPLE_STATUS), STATUS_DONE);
    CHECK_EQ(sample_field(free_sample, SAMPLE_ADDRESS), 0);
}

/* ---- fdps_audio_stop_sample @ 000305c0 -----------------------------------
 *
 * Expected values come from 000305c0 -- CMP dword ptr [EBP+0x14],-0x1 / JZ for
 * the sentinel, CMP dword ptr [EBP-0x4],0x8 / JL for the walk's bound, and
 * LEA EAX,[EAX*0x4+0x0] / PUSH dword ptr [EAX+0x69d30] for how a slot number
 * reaches a handle -- and from the two call sites, 00019122 and 0001b8c6, both
 * of which PUSH -0x1.
 *
 * WHAT SILENCE IS OBSERVED THROUGH.  The function's whole effect is which
 * handles it hands to AIL_stop_sample, and the real library is linked in, so
 * the answer is read back out of the handles themselves.  AIL_stop_sample at
 * 0003f1df does its logging test against a flag the image leaves zero and then
 * tail-calls the worker at 00047470, which is seven instructions long:
 *
 *   MOV EAX,[ESP+0x4] / TEST EAX,EAX / JZ ret   -- a null handle is tolerated
 *   CMP [EAX+0x4],0x4 / JNZ ret                 -- only a playing sample moves
 *   MOV [EAX+0x4],0x8                           -- and it moves to state 8
 *
 * So a handle that was stopped reads 8 at +0x04 and one that was not still
 * reads what it held.  Nothing on that path touches any other field, which is
 * why these fixtures are two words rather than the 0x854 the play tests need.
 */
#define STATUS_STOPPED 8
#define STOP_HANDLE_WORDS 2

static unsigned int stop_handles[SFX_SAMPLE_SLOT_COUNT][STOP_HANDLE_WORDS];

/* Eight distinct handles, all playing, one per slot -- distinct because every
   assertion below is about which slot moved, and a table whose entries all
   point at one structure cannot tell them apart.  Both audio flags are set so
   that a test which clears one is clearing it from a known state. */
static void stage_stop_slots(void)
{
    int slot;
    int word;

    for (slot = 0; slot < SFX_SAMPLE_SLOT_COUNT; slot++) {
        for (word = 0; word < STOP_HANDLE_WORDS; word++) {
            stop_handles[slot][word] = 0;
        }
        stop_handles[slot][SAMPLE_STATUS / 4] = STATUS_PLAYING;
        data_fdps_audio_sample_handle_table[slot] = stop_handles[slot];
    }
    data_fdps_audio_sfx_driver_available_flag = 1;
    data_fdps_audio_sfx_enabled_flag = 1;
}

static unsigned int stop_status(int slot)
{
    return stop_handles[slot][SAMPLE_STATUS / 4];
}

/* The sentinel walks the whole table: every one of the eight is asserted
   separately, because the bound is the one number in the function that a
   rewrite could plausibly get wrong and a spot check at slot 0 would not
   notice a walk that stopped at 4. */
static void the_sentinel_stops_all_eight_slots(void)
{
    stage_stop_slots();
    fdps_audio_stop_sample(SFX_STOP_ALL_SLOTS);
    CHECK_EQ(stop_status(0), STATUS_STOPPED);
    CHECK_EQ(stop_status(1), STATUS_STOPPED);
    CHECK_EQ(stop_status(2), STATUS_STOPPED);
    CHECK_EQ(stop_status(3), STATUS_STOPPED);
    CHECK_EQ(stop_status(4), STATUS_STOPPED);
    CHECK_EQ(stop_status(5), STATUS_STOPPED);
    CHECK_EQ(stop_status(6), STATUS_STOPPED);
    CHECK_EQ(stop_status(7), STATUS_STOPPED);
}

/* Any other value silences that slot and only that slot: the neighbours on
   both sides are checked, so an index scaled by the wrong stride or folded
   onto the wrong base lands on one of them and says so. */
static void an_index_stops_only_that_slot(void)
{
    stage_stop_slots();
    fdps_audio_stop_sample(3);
    CHECK_EQ(stop_status(3), STATUS_STOPPED);
    CHECK_EQ(stop_status(0), STATUS_PLAYING);
    CHECK_EQ(stop_status(1), STATUS_PLAYING);
    CHECK_EQ(stop_status(2), STATUS_PLAYING);
    CHECK_EQ(stop_status(4), STATUS_PLAYING);
    CHECK_EQ(stop_status(7), STATUS_PLAYING);
}

/* Both ends of the table are reachable through the index path, which pins the
   base and the stride together: slot 0 needs the base right, slot 7 needs the
   stride right as well. */
static void the_first_and_last_slots_are_both_addressable(void)
{
    stage_stop_slots();
    fdps_audio_stop_sample(0);
    CHECK_EQ(stop_status(0), STATUS_STOPPED);
    CHECK_EQ(stop_status(1), STATUS_PLAYING);

    stage_stop_slots();
    fdps_audio_stop_sample(SFX_SAMPLE_SLOT_COUNT - 1);
    CHECK_EQ(stop_status(SFX_SAMPLE_SLOT_COUNT - 1), STATUS_STOPPED);
    CHECK_EQ(stop_status(SFX_SAMPLE_SLOT_COUNT - 2), STATUS_PLAYING);
}

/* Neither audio flag is tested, on either path.  This is the assertion that
   separates the emitted routine from the guarded shape its siblings have: with
   both flags clear, fdps_sfx_play does nothing at all and this one still
   silences all eight. */
static void neither_audio_flag_is_consulted(void)
{
    stage_stop_slots();
    data_fdps_audio_sfx_driver_available_flag = 0;
    data_fdps_audio_sfx_enabled_flag = 0;
    fdps_audio_stop_sample(SFX_STOP_ALL_SLOTS);
    CHECK_EQ(stop_status(0), STATUS_STOPPED);
    CHECK_EQ(stop_status(7), STATUS_STOPPED);

    stage_stop_slots();
    data_fdps_audio_sfx_driver_available_flag = 0;
    data_fdps_audio_sfx_enabled_flag = 0;
    fdps_audio_stop_sample(5);
    CHECK_EQ(stop_status(5), STATUS_STOPPED);
    CHECK_EQ(stop_status(4), STATUS_PLAYING);
}

/* A slot still holding its BSS zero is what the table looks like when the DIG
   driver never installed, and the walk goes through it rather than stopping
   there: slot 5 is the only live handle and it is behind five null ones. */
static void a_null_slot_does_not_end_the_walk(void)
{
    int slot;

    stage_stop_slots();
    for (slot = 0; slot < SFX_SAMPLE_SLOT_COUNT; slot++) {
        if (slot != 5) {
            data_fdps_audio_sample_handle_table[slot] = 0;
        }
    }
    fdps_audio_stop_sample(SFX_STOP_ALL_SLOTS);
    CHECK_EQ(stop_status(5), STATUS_STOPPED);
}

/* Deciding whether a sample is in a state that can be stopped belongs to AIL,
   not here: a slot that is not playing comes back exactly as it went in, and
   the walk carries on past it to the slots after. */
static void a_slot_that_is_not_playing_is_left_as_it_stands(void)
{
    stage_stop_slots();
    stop_handles[4][SAMPLE_STATUS / 4] = STATUS_DONE;
    fdps_audio_stop_sample(SFX_STOP_ALL_SLOTS);
    CHECK_EQ(stop_status(4), STATUS_DONE);
    CHECK_EQ(stop_status(3), STATUS_STOPPED);
    CHECK_EQ(stop_status(5), STATUS_STOPPED);
    CHECK_EQ(stop_status(7), STATUS_STOPPED);
}

/* ---- fdps_audio_start_sample @ 00030630 ----------------------------------
 *
 * Expected values come from 00030630 -- the two CMP byte ptr
 * [0x00069d71]/[0x00069d70] guards onto the shared -1, the CMP dword ptr
 * [EBP-0x8],0x8 / JL scan bound with CMP EAX,0x4 on what AIL_sample_status
 * returned, the PUSH order of the five AIL calls between 000306b0 and 00030722,
 * PUSH dword ptr [0x00069d5c] at 000306f4, and MOV EAX,dword ptr [EBP-0x8] at
 * 0003072a for the answer -- and from AIL_init_sample's worker at 000471e0,
 * which is what decides whether a field the function never writes can be told
 * apart from one it does:
 *
 *   +0x04 status      2      +0x30 loop count 1
 *   +0x08 address     0      +0x34 format     0
 *   +0x10 length      0      +0x38 flags      0
 *   +0x3c rate    0x2b11     +0x40 volume     the library's master volume
 *
 * So a loop count of 1 and a rate of 11025 are AIL's own defaults and prove
 * nothing; every case below picks values that are not them.  The volume is the
 * one default with no fixed number, and it is compared against a second handle
 * that only AIL_init_sample has touched rather than against a literal.
 *
 * The block is a plain byte array because that is exactly what the function
 * takes -- it parses no header and does no arithmetic on the pointer, which is
 * what separates it from fdps_sfx_play above.
 */
#define AIL_DEFAULT_RATE 0x2b11 /* 11025 */
#define AIL_DEFAULT_LOOP_COUNT 1
#define START_TEST_RATE 22050
#define PCM_BLOCK_SIZE 0x40
#define PCM_TEST_LENGTH 0x2d
#define PCM_TEST_LOOPS 3

static unsigned char pcm_block[PCM_BLOCK_SIZE];

/* A handle that has been through AIL_init_sample and nothing else, so that a
   field this function leaves alone can be checked against what the library
   itself put there. */
static unsigned int init_only_sample[SAMPLE_WORDS];

static void stage_init_only_sample(void)
{
    int i;

    for (i = 0; i < SAMPLE_WORDS; i++) {
        init_only_sample[i] = 0;
    }
    init_only_sample[SAMPLE_DRIVER / 4] = (unsigned int) fake_driver;
    AIL_init_sample(init_only_sample);
}

/* Both flags open, one free slot, a rate in the global that is not AIL's
   default, and a block whose bytes are distinguishable from zero. */
static void stage_start_ready(int free_slot)
{
    int i;

    stage_slots(free_slot);
    data_fdps_audio_sfx_driver_available_flag = 1;
    data_fdps_audio_sfx_enabled_flag = 1;
    data_fdps_audio_sample_playback_rate = START_TEST_RATE;
    for (i = 0; i < PCM_BLOCK_SIZE; i++) {
        pcm_block[i] = (unsigned char) (i + 1);
    }
}

/* The premise the cases below rest on, checked against the linked library
   rather than assumed: the values AIL_init_sample leaves behind are the ones
   every assertion has to be distinguishable from, so a loop count of 3 or 0 and
   a rate of 22050, 8000 or 0 say something and a loop count of 1 or a rate of
   11025 would not. */
static void ail_init_sample_leaves_its_own_defaults(void)
{
    stage_start_ready(0);
    stage_init_only_sample();
    CHECK_EQ(sample_field(init_only_sample, SAMPLE_STATUS), STATUS_DONE);
    CHECK_EQ(sample_field(init_only_sample, SAMPLE_RATE), AIL_DEFAULT_RATE);
    CHECK_EQ(sample_field(init_only_sample, SAMPLE_LOOP_COUNT),
             AIL_DEFAULT_LOOP_COUNT);
    CHECK_EQ(sample_field(init_only_sample, SAMPLE_FORMAT), 0);
    CHECK_EQ(sample_field(init_only_sample, SAMPLE_FLAGS), 0);
    CHECK_EQ(sample_field(init_only_sample, SAMPLE_ADDRESS), 0);
}

/* The whole started-a-voice path: the scan skips the two playing slots, the
   answer is the slot it stopped on, and all four arguments AIL was given are
   read back out of that handle.  Status 4 is AIL_start_sample's own write at
   00047423, so it is what says the last of the five calls happened. */
static void start_sample_claims_the_first_free_slot(void)
{
    stage_start_ready(2);
    CHECK_EQ(fdps_audio_start_sample(pcm_block, PCM_TEST_LENGTH,
                                     PCM_TEST_LOOPS), 2);
    CHECK_EQ(sample_field(free_sample, SAMPLE_ADDRESS),
             (unsigned int) pcm_block);
    CHECK_EQ(sample_field(free_sample, SAMPLE_LENGTH), PCM_TEST_LENGTH);
    CHECK_EQ(sample_field(free_sample, SAMPLE_LOOP_COUNT), PCM_TEST_LOOPS);
    CHECK_EQ(sample_field(free_sample, SAMPLE_RATE), START_TEST_RATE);
    CHECK_EQ(sample_field(free_sample, SAMPLE_STATUS), STATUS_PLAYING);
    CHECK_EQ(sample_field(busy_sample, SAMPLE_ADDRESS), 0);
}

/* The scan runs 0..7, so the answer can be the last slot as readily as the
   first.  Both ends are asserted because the bound and the base are separate
   mistakes. */
static void start_sample_reaches_both_ends_of_the_table(void)
{
    stage_start_ready(0);
    CHECK_EQ(fdps_audio_start_sample(pcm_block, PCM_TEST_LENGTH,
                                     PCM_TEST_LOOPS), 0);
    CHECK_EQ(sample_field(free_sample, SAMPLE_LENGTH), PCM_TEST_LENGTH);

    stage_start_ready(SFX_SAMPLE_SLOT_COUNT - 1);
    CHECK_EQ(fdps_audio_start_sample(pcm_block, PCM_TEST_LENGTH,
                                     PCM_TEST_LOOPS),
             SFX_SAMPLE_SLOT_COUNT - 1);
    CHECK_EQ(sample_field(free_sample, SAMPLE_LENGTH), PCM_TEST_LENGTH);
}

/* All eight playing: -1, and nothing handed to AIL.  A scan whose compare went
   the other way would claim slot 0 and the busy handle would come back holding
   the block and AIL's default rate. */
static void start_sample_with_every_voice_busy_answers_minus_one(void)
{
    stage_start_ready(-1);
    CHECK_EQ(fdps_audio_start_sample(pcm_block, PCM_TEST_LENGTH,
                                     PCM_TEST_LOOPS), SFX_NO_SAMPLE_SLOT);
    CHECK_EQ(sample_field(busy_sample, SAMPLE_ADDRESS), 0);
    CHECK_EQ(sample_field(busy_sample, SAMPLE_LENGTH), 0);
    CHECK_EQ(sample_field(busy_sample, SAMPLE_RATE), 0);
    CHECK_EQ(sample_field(busy_sample, SAMPLE_STATUS), STATUS_PLAYING);
}

/* Either flag clear is the same -1 as a full table, and it is returned before
   anything is touched: a free handle that had been through AIL_init_sample
   would read 0x2b11 at +0x3c, so a rate still at zero is what says the function
   left without calling anything at all. */
static void start_sample_needs_both_audio_flags(void)
{
    stage_start_ready(0);
    data_fdps_audio_sfx_enabled_flag = 0;
    CHECK_EQ(fdps_audio_start_sample(pcm_block, PCM_TEST_LENGTH,
                                     PCM_TEST_LOOPS), SFX_NO_SAMPLE_SLOT);
    CHECK_EQ(sample_field(free_sample, SAMPLE_ADDRESS), 0);
    CHECK_EQ(sample_field(free_sample, SAMPLE_RATE), 0);

    stage_start_ready(0);
    data_fdps_audio_sfx_driver_available_flag = 0;
    CHECK_EQ(fdps_audio_start_sample(pcm_block, PCM_TEST_LENGTH,
                                     PCM_TEST_LOOPS), SFX_NO_SAMPLE_SLOT);
    CHECK_EQ(sample_field(free_sample, SAMPLE_ADDRESS), 0);
    CHECK_EQ(sample_field(free_sample, SAMPLE_RATE), 0);

    stage_start_ready(0);
    data_fdps_audio_sfx_enabled_flag = 0;
    data_fdps_audio_sfx_driver_available_flag = 0;
    CHECK_EQ(fdps_audio_start_sample(pcm_block, PCM_TEST_LENGTH,
                                     PCM_TEST_LOOPS), SFX_NO_SAMPLE_SLOT);
    CHECK_EQ(sample_field(free_sample, SAMPLE_ADDRESS), 0);
}

/* PUSH dword ptr [EBP+0x14] with no arithmetic on it: the pointer reaches AIL
   as it was passed, and the length is the caller's number and not the block's
   size.  A pointer eight bytes into the block is what tells this apart from
   fdps_sfx_play, which adds 8 to step over a .SAF item header. */
static void start_sample_forwards_the_block_unchanged(void)
{
    stage_start_ready(0);
    CHECK_EQ(fdps_audio_start_sample(pcm_block + 8, 1, PCM_TEST_LOOPS), 0);
    CHECK_EQ(sample_field(free_sample, SAMPLE_ADDRESS),
             (unsigned int) (pcm_block + 8));
    CHECK_EQ(sample_field(free_sample, SAMPLE_LENGTH), 1);
}

/* The loop count is the caller's, forwarded unchanged.  Three is not AIL's
   default of 1, and zero -- Miles' "loop for ever" -- is the value an
   implementation that quietly substituted the default would lose. */
static void start_sample_forwards_the_loop_count(void)
{
    stage_start_ready(0);
    CHECK_EQ(fdps_audio_start_sample(pcm_block, PCM_TEST_LENGTH, 3), 0);
    CHECK_EQ(sample_field(free_sample, SAMPLE_LOOP_COUNT), 3);

    stage_start_ready(0);
    CHECK_EQ(fdps_audio_start_sample(pcm_block, PCM_TEST_LENGTH, 0), 0);
    CHECK_EQ(sample_field(free_sample, SAMPLE_LOOP_COUNT), 0);
}

/* The rate is the global at 00069d5c and nothing else: it is read on every
   call, so a later value replaces an earlier one, and it is pushed
   unconditionally -- the worker at 00047310 writes +0x3c without testing it --
   so a zero global is a zero rate rather than AIL's 0x2b11 default.  That case
   is not hypothetical: nothing in the shipped image ever writes the global, so
   zero is what a real session would hand over. */
static void start_sample_rate_comes_from_the_global(void)
{
    stage_start_ready(0);
    data_fdps_audio_sample_playback_rate = 8000;
    CHECK_EQ(fdps_audio_start_sample(pcm_block, PCM_TEST_LENGTH,
                                     PCM_TEST_LOOPS), 0);
    CHECK_EQ(sample_field(free_sample, SAMPLE_RATE), 8000);

    stage_start_ready(0);
    data_fdps_audio_sample_playback_rate = 0;
    CHECK_EQ(fdps_audio_start_sample(pcm_block, PCM_TEST_LENGTH,
                                     PCM_TEST_LOOPS), 0);
    CHECK_EQ(sample_field(free_sample, SAMPLE_RATE), 0);
}

/* Neither AIL_set_sample_type nor AIL_set_sample_volume is called, which is the
   whole difference between this routine and fdps_sfx_play.  Format and flags
   are checked against the zeroes AIL_init_sample writes; the volume has no
   fixed default, so it is checked against a handle that only AIL_init_sample
   has been through.  fdps_sfx_play would leave 0x28 in it and 2 in the flags
   word. */
static void start_sample_sets_neither_type_nor_volume(void)
{
    stage_start_ready(0);
    stage_init_only_sample();
    CHECK_EQ(fdps_audio_start_sample(pcm_block, PCM_TEST_LENGTH,
                                     PCM_TEST_LOOPS), 0);
    CHECK_EQ(sample_field(free_sample, SAMPLE_FORMAT), 0);
    CHECK_EQ(sample_field(free_sample, SAMPLE_FLAGS), 0);
    CHECK_EQ(sample_field(free_sample, SAMPLE_VOLUME),
             sample_field(init_only_sample, SAMPLE_VOLUME));
}

/* ---- fdps_audio_sample_is_playing @ 00030740 -----------------------------
 *
 * Expected values come from 00030740 -- LEA EAX,[EAX*0x4+0x0] / PUSH dword ptr
 * [EAX+0x69d30] for the indexing, CALL AIL_sample_status, CMP EAX,0x4 / JNZ for
 * the test, and the literal 1 and 0 the two arms write into [EBP-0x4] -- and
 * from AIL_sample_status's worker at 00047290, which is MOV EAX,dword ptr
 * [ESP+0x4] / TEST EAX,EAX / JZ RET / MOV EAX,dword ptr [EAX+0x4]: a null
 * handle answers 0 and any other handle answers the word at +0x04.
 *
 * The out-of-range slot number both callers can pass is not exercised with a
 * literal -1 here.  In FDPS.LE that index reads 0x00069d2c, and no instruction
 * in the image writes 0x00069d28 or 0x00069d2c, so what the original fetches is
 * a null handle; the dword below the table in this executable is some other
 * symbol chosen by this build's link, so a -1 here would measure the test
 * build's layout rather than the game's.  What the path turns on -- that a null
 * handle answers 0 -- is asserted directly instead.
 *
 * The status word is written into the fixture by hand rather than reached
 * through AIL_init_sample, because these cases need states the library has no
 * call that produces, 4 among them.
 */
#define IS_PLAYING_TEST_SLOT 3

static unsigned int status_sample[SAMPLE_WORDS];

/* Every slot null except `slot`, which gets the fixture in state `status`.  A
   null everywhere else is what makes an answer of 1 attributable to that one
   slot: AIL reports 0 for all the others whatever the indexing did. */
static void stage_status_slot(int slot, unsigned int status)
{
    int i;

    for (i = 0; i < SAMPLE_WORDS; i++) {
        status_sample[i] = 0;
    }
    status_sample[SAMPLE_STATUS / 4] = status;
    for (i = 0; i < SFX_SAMPLE_SLOT_COUNT; i++) {
        data_fdps_audio_sample_handle_table[i] = 0;
    }
    data_fdps_audio_sample_handle_table[slot] = status_sample;
}

/* The one state that answers 1, and the answer is the literal the JNZ's taken
   arm writes, not merely a non-zero value. */
static void is_playing_reports_the_playing_state(void)
{
    stage_status_slot(IS_PLAYING_TEST_SLOT, STATUS_PLAYING);
    CHECK_EQ(fdps_audio_sample_is_playing(IS_PLAYING_TEST_SLOT), 1);
}

/* CMP EAX,0x4 / JNZ is an equality against 4 and nothing else, so a status
   above it is as much a 0 as a status below it.  0 is the state a handle that
   AIL has never been given holds, 2 is the state AIL_init_sample leaves a
   finished one in, and 5..7 are there to rule out a >= 4 reading. */
static void is_playing_reports_every_other_state_as_zero(void)
{
    static unsigned int other_states[7] = {0, 1, 2, 3, 5, 6, 7};
    int i;

    for (i = 0; i < 7; i++) {
        stage_status_slot(IS_PLAYING_TEST_SLOT, other_states[i]);
        CHECK_EQ(fdps_audio_sample_is_playing(IS_PLAYING_TEST_SLOT), 0);
    }
}

/* LEA EAX,[EAX*0x4+0x0] scales by the entry size and the base is the table
   itself, so both ends of the eight entries answer for themselves and no
   neighbour answers for them. */
static void is_playing_addresses_both_ends_of_the_table(void)
{
    stage_status_slot(0, STATUS_PLAYING);
    CHECK_EQ(fdps_audio_sample_is_playing(0), 1);
    CHECK_EQ(fdps_audio_sample_is_playing(1), 0);
    CHECK_EQ(fdps_audio_sample_is_playing(SFX_SAMPLE_SLOT_COUNT - 1), 0);

    stage_status_slot(SFX_SAMPLE_SLOT_COUNT - 1, STATUS_PLAYING);
    CHECK_EQ(fdps_audio_sample_is_playing(SFX_SAMPLE_SLOT_COUNT - 1), 1);
    CHECK_EQ(fdps_audio_sample_is_playing(0), 0);
    CHECK_EQ(fdps_audio_sample_is_playing(SFX_SAMPLE_SLOT_COUNT - 2), 0);
}

/* A slot still holding its BSS zero -- what the whole table looks like when the
   DIG driver never installed -- answers 0 rather than crashing or reporting a
   voice.  This is also the answer the game's out-of-range index depends on, for
   the reason in the block comment above. */
static void is_playing_answers_zero_for_a_null_handle(void)
{
    int slot;

    stage_status_slot(IS_PLAYING_TEST_SLOT, STATUS_PLAYING);
    data_fdps_audio_sample_handle_table[IS_PLAYING_TEST_SLOT] = 0;
    for (slot = 0; slot < SFX_SAMPLE_SLOT_COUNT; slot++) {
        CHECK_EQ(fdps_audio_sample_is_playing(slot), 0);
    }
}

/* Neither 0x00069d71 nor 0x00069d70 is compared anywhere in the function, so
   the answer is about the voice and not about whether audio is switched on.
   fdps_sfx_play and fdps_audio_start_sample both open with those two guards;
   this one does not have them. */
static void is_playing_consults_neither_audio_flag(void)
{
    stage_status_slot(IS_PLAYING_TEST_SLOT, STATUS_PLAYING);
    data_fdps_audio_sfx_driver_available_flag = 0;
    data_fdps_audio_sfx_enabled_flag = 0;
    CHECK_EQ(fdps_audio_sample_is_playing(IS_PLAYING_TEST_SLOT), 1);

    stage_status_slot(IS_PLAYING_TEST_SLOT, STATUS_DONE);
    data_fdps_audio_sfx_driver_available_flag = 1;
    data_fdps_audio_sfx_enabled_flag = 1;
    CHECK_EQ(fdps_audio_sample_is_playing(IS_PLAYING_TEST_SLOT), 0);
}

/* The function stores nothing: the only memory operands are the PUSH of the
   handle and the frame slot the answer goes through.  Asking twice is what a
   spinning caller does, and it gets the same answer both times with the handle
   left as it stood. */
static void is_playing_only_reads_the_handle(void)
{
    stage_status_slot(IS_PLAYING_TEST_SLOT, STATUS_PLAYING);
    CHECK_EQ(fdps_audio_sample_is_playing(IS_PLAYING_TEST_SLOT), 1);
    CHECK_EQ(fdps_audio_sample_is_playing(IS_PLAYING_TEST_SLOT), 1);
    CHECK_EQ(sample_field(status_sample, SAMPLE_STATUS), STATUS_PLAYING);
    CHECK_EQ((unsigned int) data_fdps_audio_sample_handle_table
                 [IS_PLAYING_TEST_SLOT],
             (unsigned int) status_sample);
}

/* ------------------------------------------------------------------ *
 * fdps_timer_tick_handler @ 00030790
 *
 * The whole body is INC dword ptr [0x00069d64] and CALL rand, so there are
 * exactly two things to observe and both are global: the counter's value, and
 * how far the CRT generator has been wound on.  The second is observed by
 * reseeding and comparing against the sequence the same seed gives with no
 * tick in the middle -- the real rand is linked in, so the numbers come out of
 * the library rather than out of this file.
 *
 * There is no return value to check: EAX holds rand()'s answer at the RET and
 * nothing reads it, because AIL's timer interrupt is what calls this through
 * the pointer fdps_audio_timer_install registered.
 * ------------------------------------------------------------------ */

/* Any seed does; 1 is the value the CRT starts at, so a run that never seeds
   sees the same stream the first assertion below pins. */
#define TICK_RAND_SEED 1

/* Arbitrary starting counts, chosen only to be far from zero and from each
   other so that an increment cannot be confused with a store. */
#define TICK_START_COUNT 0x1234
#define TICK_MARKER_RATE 0x5a5a

/* INC on a 32-bit destination, so the counter advances by exactly one and
   nothing in the body can reset it or skip a tick. */
static void a_tick_advances_the_clock_by_one(void)
{
    data_fdps_timer_tick_counter = 0;
    fdps_timer_tick_handler();
    CHECK_EQ(data_fdps_timer_tick_counter, 1);
    fdps_timer_tick_handler();
    CHECK_EQ(data_fdps_timer_tick_counter, 2);

    data_fdps_timer_tick_counter = TICK_START_COUNT;
    fdps_timer_tick_handler();
    CHECK_EQ(data_fdps_timer_tick_counter, TICK_START_COUNT + 1);
}

/* The counter is a full unsigned dword and the increment is a plain INC, so
   the tick after 0xffffffff is 0 -- there is no saturation and no guard.  The
   readers that latch a copy and test it for equality are unaffected by that;
   a reader that ordered two samples would not be, which is why the wrap is
   pinned here rather than assumed away. */
static void the_counter_wraps_at_32_bits(void)
{
    data_fdps_timer_tick_counter = 0xffffffffu;
    CHECK_EQ(data_fdps_timer_tick_counter == 0xffffffffu, 1);
    fdps_timer_tick_handler();
    CHECK_EQ(data_fdps_timer_tick_counter, 0);
}

/* One CALL rand per tick, in straight-line code with no branch, so a tick
   consumes exactly one value from the generator: after reseeding, a tick
   followed by rand() gives what the second rand() of that seed gives, and two
   ticks give the third. */
static void a_tick_draws_exactly_one_rand(void)
{
    int first;
    int second;
    int third;

    srand(TICK_RAND_SEED);
    first = rand();
    second = rand();
    third = rand();

    /* The premise the two checks below rest on: this seed's first three
       values are not all the same number. */
    CHECK_EQ(first == second, 0);
    CHECK_EQ(second == third, 0);

    srand(TICK_RAND_SEED);
    fdps_timer_tick_handler();
    CHECK_EQ(rand(), second);

    srand(TICK_RAND_SEED);
    fdps_timer_tick_handler();
    fdps_timer_tick_handler();
    CHECK_EQ(rand(), third);
}

/* The counter is the only memory operand in the body, so a tick leaves every
   other piece of audio state exactly as it stood: neither flag is read or
   written, the playback rate is untouched, and no sample handle is disturbed.
   Both flags are staged clear here on purpose -- a tick has to run whether or
   not the DIG driver ever installed, because the clock is what the whole game
   paces on and not just the sound. */
static void a_tick_touches_nothing_but_the_counter(void)
{
    int slot;

    stage_status_slot(IS_PLAYING_TEST_SLOT, STATUS_PLAYING);
    for (slot = 0; slot < SFX_SAMPLE_SLOT_COUNT; slot++) {
        data_fdps_audio_sample_handle_table[slot] = (void *) status_sample;
    }
    data_fdps_audio_sfx_driver_available_flag = 0;
    data_fdps_audio_sfx_enabled_flag = 0;
    data_fdps_audio_sample_playback_rate = TICK_MARKER_RATE;
    data_fdps_timer_tick_counter = TICK_START_COUNT;

    fdps_timer_tick_handler();

    CHECK_EQ(data_fdps_timer_tick_counter, TICK_START_COUNT + 1);
    CHECK_EQ(data_fdps_audio_sfx_driver_available_flag, 0);
    CHECK_EQ(data_fdps_audio_sfx_enabled_flag, 0);
    CHECK_EQ(data_fdps_audio_sample_playback_rate, TICK_MARKER_RATE);
    for (slot = 0; slot < SFX_SAMPLE_SLOT_COUNT; slot++) {
        CHECK_EQ((unsigned int) data_fdps_audio_sample_handle_table[slot],
                 (unsigned int) status_sample);
    }
    CHECK_EQ(sample_field(status_sample, SAMPLE_STATUS), STATUS_PLAYING);
}

void run_audio_tests(void)
{
    RUN_TEST(the_fixture_looks_like_a_handle_to_ail);
    RUN_TEST(plays_on_the_first_slot_that_is_not_playing);
    RUN_TEST(the_last_slot_is_reachable);
    RUN_TEST(all_slots_playing_drops_the_request);
    RUN_TEST(format_comes_from_the_channel_and_bit_depth_bytes);
    RUN_TEST(an_unknown_channel_bit_pair_falls_through_to_three);
    RUN_TEST(rate_is_a_16_bit_zero_extended_read);
    RUN_TEST(length_is_a_full_32_bit_read);
    RUN_TEST(a_negative_index_plays_nothing);
    RUN_TEST(the_count_is_an_exclusive_16_bit_bound);
    RUN_TEST(a_zero_count_plays_nothing);
    RUN_TEST(either_audio_flag_clear_plays_nothing);
    RUN_TEST(the_stored_offset_is_rebased_on_the_image_base);
    RUN_TEST(the_section_start_is_a_full_32_bit_field);
    RUN_TEST(shutdown_releases_the_ail_timer_handles);
    RUN_TEST(shutdown_leaves_the_game_audio_state_alone);
    RUN_TEST(the_sentinel_stops_all_eight_slots);
    RUN_TEST(an_index_stops_only_that_slot);
    RUN_TEST(the_first_and_last_slots_are_both_addressable);
    RUN_TEST(neither_audio_flag_is_consulted);
    RUN_TEST(a_null_slot_does_not_end_the_walk);
    RUN_TEST(a_slot_that_is_not_playing_is_left_as_it_stands);
    RUN_TEST(ail_init_sample_leaves_its_own_defaults);
    RUN_TEST(start_sample_claims_the_first_free_slot);
    RUN_TEST(start_sample_reaches_both_ends_of_the_table);
    RUN_TEST(start_sample_with_every_voice_busy_answers_minus_one);
    RUN_TEST(start_sample_needs_both_audio_flags);
    RUN_TEST(start_sample_forwards_the_block_unchanged);
    RUN_TEST(start_sample_forwards_the_loop_count);
    RUN_TEST(start_sample_rate_comes_from_the_global);
    RUN_TEST(start_sample_sets_neither_type_nor_volume);
    RUN_TEST(is_playing_reports_the_playing_state);
    RUN_TEST(is_playing_reports_every_other_state_as_zero);
    RUN_TEST(is_playing_addresses_both_ends_of_the_table);
    RUN_TEST(is_playing_answers_zero_for_a_null_handle);
    RUN_TEST(is_playing_consults_neither_audio_flag);
    RUN_TEST(is_playing_only_reads_the_handle);
    RUN_TEST(a_tick_advances_the_clock_by_one);
    RUN_TEST(the_counter_wraps_at_32_bits);
    RUN_TEST(a_tick_draws_exactly_one_rand);
    RUN_TEST(a_tick_touches_nothing_but_the_counter);
}
