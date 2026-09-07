/* tests/church.c -- cover for src/church.c.
 *
 * Every expected value below is read off the assembly of
 * fdps_church_promote_unit at 00034c10 -- MOV AL,byte ptr [EDX+0x8] at
 * 00034cd4 for the identity the opening clip is named from, LEA EDX,[EDX+EDX*2]
 * / ADD EAX,EDX at 00034cf1 for the promotion route's stride, MOV AL,byte ptr
 * [EDX] and [EDX+0x1] for the route's two bytes and MOV byte ptr [EDX+0x7],AL /
 * MOV byte ptr [EDX+0x20],AL at 00034d14 and 00034d1d for where they go, the
 * PUSH 0x4800 / PUSH 0x1000 pairs at 00034c63 and 00034c9f and again at
 * 00035251 and 0003528d for the four blend-table reads, PUSH dword ptr
 * [0x000643e4] at 00034e0b and PUSH dword ptr [0x000643bc] at 00035228 for the
 * two palette uploads, PUSH 0xfa00 / PUSH 0x0 / PUSH 0xa0000 at 00034dea for
 * the screen clear, and PUSH 0xc8 / PUSH 0x140 / PUSH 0x140 / PUSH 0xa0000 /
 * PUSH 0x170 / <page + 0x2298> at 00034eed for what each frame presents.  None
 * of them is read off the emitted C.
 *
 * WHAT IS STAGED AND WHAT IS NOT.  The unit array and the RankUp.dat table are
 * pointed at blocks built here, and so are the two palette images; the three
 * .SAF clips and the four blend tables are the shipped files
 * (tests/gamefile.lst), because none of them can be stood in for.  The two
 * .tmp pairs are opened by bare name with the result untested, so a missing one
 * faults inside fread rather than failing an assertion, and the clips are
 * loaded through fdps_vfs_load_entry, which ends the process on a member it
 * cannot find.  That is also why the run below is evidence in itself: the three
 * member names the function composes -- Stand%03d.saf of the character id,
 * Stand%03d.saf of the new form id and one of the two magic0??.saf -- all
 * resolved inside the shipped containers, or nothing after the call would have
 * run at all.
 *
 * WHAT THE TIMER INTERRUPT IS FOR BESIDES THE TICK.  Three of this function's
 * effects only exist while it is running -- the fight blend tables, the fight
 * palette, and the unit record as it stands before the animation has finished
 * -- so the handler takes one snapshot of all three the first tick after a
 * frame has reached the screen.  That moment is past the record writes, past
 * the three clip loads and past the fight palette upload, and it is found by
 * reading video memory for a non-zero byte: the aperture is zeroed before the
 * call and the function's own memset leaves it zero, so the first non-zero byte
 * anywhere in it is a composed frame.
 *
 * WHY ONE RUN.  A run plays 108 or more frames paced by the timer, so every
 * case below shares one and reads what it needs out of the copies taken the
 * moment it returned.
 *
 * TWO THINGS THE RUN CANNOT SEPARATE, both settled by the assembly instead.
 * Which of magic0gg.saf and magic0bb.saf the class code picked is not visible
 * from outside: both members exist, both animate to completion, and the frame
 * sounds that would have told them apart are suppressed because the test has no
 * audio driver -- fdps_sfx_play returns on the first of its two gates
 * (src/audio.c).  That choice is the byte table at 0003107f indexed at 00034da9
 * and the 20-byte stride at 00034db5.  Nor can the present's source rectangle
 * be pinned: the composing page is freed before the call returns, so there is
 * nothing left to compare the finished screen against, and the 0x2298 offset
 * and the 0x170 pitch are the ADD EAX,0x2298 at 00034f0c and the PUSH 0x170 at
 * 00034f01.
 */
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <conio.h>
#include <dos.h>
#include <i86.h>
#include "testharn.h"
#include "fdpstype.h"
#include "gamedata.h"
#include "audio.h"
#include "unit.h"
#include "table.h"
#include "church.h"

/* The strides the two accessors in src/unit.c and src/table.c multiply by. */
#define UNIT_RECORD_STRIDE 0x50
#define PROMOTION_RECORD_STRIDE 0x0c

/* Four unit records are staged so that a write which strayed into a neighbour
   would be visible, and the member promoted is not the first of them. */
#define STAGE_UNITS 4
#define PROMOTE_UNIT 2

/* Sixteen RankUp.dat records, so that the character id below cannot be the only
   one the multiply could have landed on. */
#define STAGE_CHARACTERS 16

/* Every byte of the unit block starts as this, so that the two the function
   writes are the only two that differ afterwards.  It is not a valid anything;
   the one field that has to be real is the character id. */
#define RECORD_FILL 0xa5

/* The identity at record +0x08.  STAND000.SAF is in the shipped Fight.vfs, and
   it is not the id any of the four routes hands back, so the opening clip and
   the closing clip are different members. */
#define PROMOTE_CHARACTER_ID 0

/* Which of the four routes the call asks for, and the twelve bytes the staged
   record holds.  All four (form, class) pairs are distinct, so a run that
   scaled the route number by anything but three would write a pair no
   assertion below accepts.  Every form id names a member Fight.vfs really
   holds and every class code is inside the 25-entry effect table. */
#define PROMOTE_ROUTE 2
#define ROUTE0_FORM 3
#define ROUTE0_CLASS 6
#define ROUTE1_FORM 4
#define ROUTE1_CLASS 7
#define ROUTE2_FORM 1
#define ROUTE2_CLASS 2
#define ROUTE3_FORM 5
#define ROUTE3_CLASS 8
/* Byte 2 of each route is the movement bonus, which this function never reads.
   All four carry the same value, and no assertion below expects to find it
   anywhere in the record. */
#define ROUTE_MOVE_BONUS 0x33

/* Where the two writes land, MOV byte ptr [EDX+0x7],AL and MOV byte ptr
   [EDX+0x20],AL. */
#define RECORD_FORM_AT 0x07
#define RECORD_CHARACTER_AT 0x08
#define RECORD_CLASS_AT 0x20

#define CHURCH_TIMER_VECTOR 8
#define CHURCH_MODE_TEXT 0x03
#define CHURCH_MODE_320X200X256 0x13
#define CHURCH_VGA_BASE 0x000a0000
#define CHURCH_SCREEN_BYTES (0x140 * 0xc8)

#define CHURCH_DAC_READ_INDEX 0x3c7
#define CHURCH_DAC_WRITE_INDEX 0x3c8
#define CHURCH_DAC_DATA 0x3c9
#define CHURCH_DAC_ENTRIES 256
/* Outside the 0..60 span both staged palettes are built in, so a DAC entry
   still holding it is one neither upload wrote. */
#define CHURCH_DAC_SENTINEL 63
#define CHURCH_PALETTE_SPAN 61
/* Three channels of entry 0 and three of entry 255. */
#define CHURCH_READING_SLOTS 6

/* PUSH 0x4800 at 00034c63 and PUSH 0x1000 at 00034c9f.  The window compared is
   128 bytes at an offset where the fight file and the map file differ:
   FMER1.TMP and MER1.TMP first disagree at 1028, FMER2.TMP and MER2.TMP at 2. */
#define CHURCH_RAMP_WINDOW_AT 1024
#define CHURCH_CUBE_WINDOW_AT 0
#define CHURCH_WINDOW_BYTES 128

static unsigned char unit_block[STAGE_UNITS * UNIT_RECORD_STRIDE];
static unsigned char unit_expected[STAGE_UNITS * UNIT_RECORD_STRIDE];
static unsigned char promo_block[STAGE_CHARACTERS * PROMOTION_RECORD_STRIDE];

static struct fdps_palette_entry fight_palette[CHURCH_DAC_ENTRIES];
static struct fdps_palette_entry field_palette[CHURCH_DAC_ENTRIES];

static unsigned char ramp_snapshot[CHURCH_WINDOW_BYTES];
static unsigned char cube_snapshot[CHURCH_WINDOW_BYTES];
static unsigned char ramp_at_return[CHURCH_WINDOW_BYTES];
static unsigned char cube_at_return[CHURCH_WINDOW_BYTES];
static unsigned char file_window[CHURCH_WINDOW_BYTES];

static int snapshot_dac[CHURCH_READING_SLOTS];
static int return_dac[CHURCH_READING_SLOTS];
static int snapshot_taken;
static int snapshot_form;
static int snapshot_class;
static int screen_drawn_bytes;

static int inputs_checked;
static int inputs_ready;
static int run_done;

static void (__interrupt __far *saved_timer)();

static struct fdps_unit_record *unit(int unit_index)
{
    return (struct fdps_unit_record *)
        (unit_block + unit_index * UNIT_RECORD_STRIDE);
}

static void read_dac(int *into)
{
    outp(CHURCH_DAC_READ_INDEX, 0);
    into[0] = (int) inp(CHURCH_DAC_DATA);
    into[1] = (int) inp(CHURCH_DAC_DATA);
    into[2] = (int) inp(CHURCH_DAC_DATA);
    outp(CHURCH_DAC_READ_INDEX, CHURCH_DAC_ENTRIES - 1);
    into[3] = (int) inp(CHURCH_DAC_DATA);
    into[4] = (int) inp(CHURCH_DAC_DATA);
    into[5] = (int) inp(CHURCH_DAC_DATA);
}

/* The three things that only exist while the animation is running. */
static void take_snapshot(void)
{
    snapshot_form = (int) unit(PROMOTE_UNIT)->portrait_id;
    snapshot_class = (int) unit(PROMOTE_UNIT)->clazz;
    memmove(ramp_snapshot,
            (unsigned char *) data_fdps_palette_shade_ramp_table
            + CHURCH_RAMP_WINDOW_AT, (size_t) CHURCH_WINDOW_BYTES);
    memmove(cube_snapshot,
            data_fdps_inverse_palette_cube + CHURCH_CUBE_WINDOW_AT,
            (size_t) CHURCH_WINDOW_BYTES);
    read_dac(snapshot_dac);
}

static void __interrupt __far church_timer_isr(void)
{
    unsigned char *aperture;
    int probe;

    ++data_fdps_timer_tick_counter;

    if (snapshot_taken == 0) {
        aperture = (unsigned char *) CHURCH_VGA_BASE;
        for (probe = 0; probe < CHURCH_SCREEN_BYTES; probe++) {
            if (aperture[probe] != 0) {
                take_snapshot();
                snapshot_taken = 1;
                break;
            }
        }
    }

    _chain_intr(saved_timer);
}

static void set_mode(int mode)
{
    union REGS regs;

    memset(&regs, 0, sizeof(regs));
    regs.x.eax = (unsigned) mode;
    int386(0x10, &regs, &regs);
}

static int file_present(char *name)
{
    FILE *fp;

    fp = fopen(name, "rb");
    if (fp == NULL) {
        return 0;
    }
    fclose(fp);
    return 1;
}

/* The four blend tables and the two containers, all of which have to be next
   to the executable for the call to come back at all. */
static void ensure_inputs(void)
{
    if (inputs_checked) {
        return;
    }
    inputs_checked = 1;
    if (file_present("FMER1.TMP") && file_present("FMER2.TMP")
        && file_present("MER1.TMP") && file_present("MER2.TMP")
        && file_present("FIGHT.VFS") && file_present("MISC.VFS")) {
        inputs_ready = 1;
    }
}

static void read_file_window(char *name, long at, unsigned char *into)
{
    FILE *fp;

    memset(into, 0, (size_t) CHURCH_WINDOW_BYTES);
    fp = fopen(name, "rb");
    if (fp != NULL) {
        fseek(fp, at, SEEK_SET);
        fread(into, 1, (size_t) CHURCH_WINDOW_BYTES, fp);
        fclose(fp);
    }
}

/* Publishes every global the function and its callees read, and remembers what
   the unit block is supposed to look like when the call comes back. */
static void stage(void)
{
    int index;
    unsigned char *route;

    memset(unit_block, RECORD_FILL, (size_t) sizeof(unit_block));
    unit(PROMOTE_UNIT)->char_id = (unsigned char) PROMOTE_CHARACTER_ID;

    memcpy(unit_expected, unit_block, sizeof(unit_block));
    unit_expected[PROMOTE_UNIT * UNIT_RECORD_STRIDE + RECORD_FORM_AT] =
        (unsigned char) ROUTE2_FORM;
    unit_expected[PROMOTE_UNIT * UNIT_RECORD_STRIDE + RECORD_CLASS_AT] =
        (unsigned char) ROUTE2_CLASS;

    memset(promo_block, 0, (size_t) sizeof(promo_block));
    route = promo_block + PROMOTE_CHARACTER_ID * PROMOTION_RECORD_STRIDE;
    route[0] = ROUTE0_FORM;
    route[1] = ROUTE0_CLASS;
    route[2] = ROUTE_MOVE_BONUS;
    route[3] = ROUTE1_FORM;
    route[4] = ROUTE1_CLASS;
    route[5] = ROUTE_MOVE_BONUS;
    route[6] = ROUTE2_FORM;
    route[7] = ROUTE2_CLASS;
    route[8] = ROUTE_MOVE_BONUS;
    route[9] = ROUTE3_FORM;
    route[10] = ROUTE3_CLASS;
    route[11] = ROUTE_MOVE_BONUS;

    for (index = 0; index < CHURCH_DAC_ENTRIES; index++) {
        fight_palette[index].red =
            (unsigned char) (index % CHURCH_PALETTE_SPAN);
        fight_palette[index].green =
            (unsigned char) ((index + 7) % CHURCH_PALETTE_SPAN);
        fight_palette[index].blue =
            (unsigned char) ((index + 14) % CHURCH_PALETTE_SPAN);
        field_palette[index].red =
            (unsigned char) ((index + 21) % CHURCH_PALETTE_SPAN);
        field_palette[index].green =
            (unsigned char) ((index + 28) % CHURCH_PALETTE_SPAN);
        field_palette[index].blue =
            (unsigned char) ((index + 35) % CHURCH_PALETTE_SPAN);
    }

    data_fdps_map_unit_array_ptr = unit_block;
    data_fdps_promotion_table_ptr = promo_block;
    data_fdps_vga_fight_palette_ptr = (unsigned char *) fight_palette;
    data_fdps_vga_main_palette_ptr = (unsigned char *) field_palette;

    /* Both audio gates closed, so the frame sounds the effect clip carries
       play nothing. */
    data_fdps_audio_sfx_enabled_flag = 0;
    data_fdps_audio_sfx_driver_available_flag = 0;
    for (index = 0; index < SFX_SAMPLE_SLOT_COUNT; index++) {
        data_fdps_audio_sample_handle_table[index] = NULL;
    }

    data_fdps_timer_tick_counter = 0;
}

/* One whole promotion, with everything a case can read afterwards copied out
   before anything else can move it. */
static void run_once(void)
{
    int index;

    ensure_inputs();
    if (run_done || inputs_ready == 0) {
        return;
    }
    run_done = 1;

    stage();

    snapshot_taken = 0;
    snapshot_form = -1;
    snapshot_class = -1;
    for (index = 0; index < CHURCH_READING_SLOTS; index++) {
        snapshot_dac[index] = -1;
        return_dac[index] = -1;
    }

    set_mode(CHURCH_MODE_320X200X256);
    memset((void *) CHURCH_VGA_BASE, 0, (size_t) CHURCH_SCREEN_BYTES);
    for (index = 0; index < CHURCH_DAC_ENTRIES; index++) {
        outp(CHURCH_DAC_WRITE_INDEX, index);
        outp(CHURCH_DAC_DATA, CHURCH_DAC_SENTINEL);
        outp(CHURCH_DAC_DATA, CHURCH_DAC_SENTINEL);
        outp(CHURCH_DAC_DATA, CHURCH_DAC_SENTINEL);
    }

    saved_timer = _dos_getvect(CHURCH_TIMER_VECTOR);
    _dos_setvect(CHURCH_TIMER_VECTOR, church_timer_isr);
    fdps_church_promote_unit(PROMOTE_UNIT, PROMOTE_ROUTE);
    _dos_setvect(CHURCH_TIMER_VECTOR, saved_timer);

    read_dac(return_dac);
    screen_drawn_bytes = 0;
    for (index = 0; index < CHURCH_SCREEN_BYTES; index++) {
        if (((unsigned char *) CHURCH_VGA_BASE)[index] != 0) {
            screen_drawn_bytes++;
        }
    }
    set_mode(CHURCH_MODE_TEXT);

    memmove(ramp_at_return,
            (unsigned char *) data_fdps_palette_shade_ramp_table
            + CHURCH_RAMP_WINDOW_AT, (size_t) CHURCH_WINDOW_BYTES);
    memmove(cube_at_return,
            data_fdps_inverse_palette_cube + CHURCH_CUBE_WINDOW_AT,
            (size_t) CHURCH_WINDOW_BYTES);
}

/* The offsets this body reads and writes, and the size of the record the route
   is stepped through.  If any of them moved, every case below would be reading
   other bytes. */
static void the_record_offsets_the_promotion_uses(void)
{
    CHECK_EQ((int) offsetof(struct fdps_unit_record, portrait_id),
             RECORD_FORM_AT);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, char_id),
             RECORD_CHARACTER_AT);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, clazz), RECORD_CLASS_AT);
    CHECK_EQ((int) sizeof(struct fdps_promotion_record),
             PROMOTION_RECORD_STRIDE);
    CHECK_EQ((int) offsetof(struct fdps_promotion_record,
                            dark_badge_portrait_id),
             PROMOTE_ROUTE * 3);
}

/* Route 2's two bytes reach the record, and neither of the other three routes'
   does.  The four pairs are distinct, so this is what pins the route stride at
   three bytes rather than at one, two or four. */
static void the_route_the_caller_named_is_the_one_applied(void)
{
    run_once();
    if (inputs_ready == 0) {
        return;
    }
    CHECK_EQ((int) unit(PROMOTE_UNIT)->portrait_id, ROUTE2_FORM);
    CHECK_EQ((int) unit(PROMOTE_UNIT)->clazz, ROUTE2_CLASS);
    CHECK_EQ((int) unit(PROMOTE_UNIT)->portrait_id != ROUTE0_FORM, 1);
    CHECK_EQ((int) unit(PROMOTE_UNIT)->portrait_id != ROUTE1_FORM, 1);
    CHECK_EQ((int) unit(PROMOTE_UNIT)->portrait_id != ROUTE3_FORM, 1);
    CHECK_EQ((int) unit(PROMOTE_UNIT)->clazz != ROUTE0_CLASS, 1);
    CHECK_EQ((int) unit(PROMOTE_UNIT)->clazz != ROUTE1_CLASS, 1);
    CHECK_EQ((int) unit(PROMOTE_UNIT)->clazz != ROUTE3_CLASS, 1);
}

/* Those two bytes are the whole of what the record loses.  Every other byte of
   all four staged records -- including the route's third byte, the movement
   bonus, which nothing here reads -- is still what the fixture wrote. */
static void nothing_but_those_two_bytes_of_any_record_moves(void)
{
    run_once();
    if (inputs_ready == 0) {
        return;
    }
    CHECK_EQ(memcmp(unit_block, unit_expected, sizeof(unit_block)), 0);
    CHECK_EQ((int) unit(PROMOTE_UNIT)->char_id, PROMOTE_CHARACTER_ID);
}

/* Both writes are done before the first frame is composed, so a promotion the
   player interrupts has already happened. */
static void the_record_is_written_before_the_first_frame(void)
{
    run_once();
    if (inputs_ready == 0) {
        return;
    }
    CHECK_EQ(snapshot_taken, 1);
    CHECK_EQ(snapshot_form, ROUTE2_FORM);
    CHECK_EQ(snapshot_class, ROUTE2_CLASS);
}

/* FMer1.tmp and FMer2.tmp are in the two globals for the length of the
   animation, and Mer1.tmp and Mer2.tmp are back in them when it returns.  The
   window compared is one the fight file and the map file disagree over, so
   neither half can pass by holding the other's bytes. */
static void the_fight_blend_tables_go_in_and_the_map_ones_come_back(void)
{
    run_once();
    if (inputs_ready == 0) {
        return;
    }
    CHECK_EQ(snapshot_taken, 1);

    read_file_window("FMER1.TMP", CHURCH_RAMP_WINDOW_AT, file_window);
    CHECK_EQ(memcmp(ramp_snapshot, file_window, CHURCH_WINDOW_BYTES), 0);
    read_file_window("FMER2.TMP", CHURCH_CUBE_WINDOW_AT, file_window);
    CHECK_EQ(memcmp(cube_snapshot, file_window, CHURCH_WINDOW_BYTES), 0);

    read_file_window("MER1.TMP", CHURCH_RAMP_WINDOW_AT, file_window);
    CHECK_EQ(memcmp(ramp_at_return, file_window, CHURCH_WINDOW_BYTES), 0);
    read_file_window("MER2.TMP", CHURCH_CUBE_WINDOW_AT, file_window);
    CHECK_EQ(memcmp(cube_at_return, file_window, CHURCH_WINDOW_BYTES), 0);
}

/* The fight palette is uploaded whole on the way in and the field palette
   whole on the way out, both with no bias, so entries 0 and 255 of the DAC
   carry the staged images' own numbers at each end.  The sentinel written into
   every entry beforehand survives neither. */
static void the_fight_palette_goes_up_and_the_field_one_comes_back(void)
{
    run_once();
    if (inputs_ready == 0) {
        return;
    }
    CHECK_EQ(snapshot_taken, 1);

    CHECK_EQ(snapshot_dac[0], (int) fight_palette[0].red);
    CHECK_EQ(snapshot_dac[1], (int) fight_palette[0].green);
    CHECK_EQ(snapshot_dac[2], (int) fight_palette[0].blue);
    CHECK_EQ(snapshot_dac[3],
             (int) fight_palette[CHURCH_DAC_ENTRIES - 1].red);
    CHECK_EQ(snapshot_dac[4],
             (int) fight_palette[CHURCH_DAC_ENTRIES - 1].green);
    CHECK_EQ(snapshot_dac[5],
             (int) fight_palette[CHURCH_DAC_ENTRIES - 1].blue);

    CHECK_EQ(return_dac[0], (int) field_palette[0].red);
    CHECK_EQ(return_dac[1], (int) field_palette[0].green);
    CHECK_EQ(return_dac[2], (int) field_palette[0].blue);
    CHECK_EQ(return_dac[3],
             (int) field_palette[CHURCH_DAC_ENTRIES - 1].red);
    CHECK_EQ(return_dac[4],
             (int) field_palette[CHURCH_DAC_ENTRIES - 1].green);
    CHECK_EQ(return_dac[5],
             (int) field_palette[CHURCH_DAC_ENTRIES - 1].blue);
}

/* Frames really reach the adapter at 0xa0000, and the screen the call leaves
   behind is a figure on a cleared page rather than a full one: every frame is
   composed over a page the loop memsets to zero first, so the last present
   puts down the closing figure and black everywhere else.  A body that never
   presented would have left the aperture at the zero this file wrote before the
   call. */
static void the_animation_reaches_the_adapter(void)
{
    run_once();
    if (inputs_ready == 0) {
        return;
    }
    CHECK_EQ(snapshot_taken, 1);
    CHECK_EQ(screen_drawn_bytes > 0, 1);
    CHECK_EQ(screen_drawn_bytes < CHURCH_SCREEN_BYTES, 1);
}

void run_church_tests(void)
{
    RUN_TEST(the_record_offsets_the_promotion_uses);
    RUN_TEST(the_route_the_caller_named_is_the_one_applied);
    RUN_TEST(nothing_but_those_two_bytes_of_any_record_moves);
    RUN_TEST(the_record_is_written_before_the_first_frame);
    RUN_TEST(the_fight_blend_tables_go_in_and_the_map_ones_come_back);
    RUN_TEST(the_fight_palette_goes_up_and_the_field_one_comes_back);
    RUN_TEST(the_animation_reaches_the_adapter);
}
