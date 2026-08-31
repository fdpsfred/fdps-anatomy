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
}
