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
#include "keybd.h"
#include "palcycle.h"
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

/* ------------------------------------------------------------------
 * fdps_church_select_promote_candidate @ 000352c0
 * ------------------------------------------------------------------
 *
 * Expected values come from the assembly -- the cancel arm's CMP ...,0x1 at
 * 000352f3; the confirm arm's 0x1c and 0x39 at 00035305 and 0003530b; the four
 * guarded arrow arms at 0003531d (0x4d, DEC EAX / CMP / JG against the count),
 * 00035356 (0x4b, CMP ...,0x0 / JG), 00035389 (0x48, CMP ...,0x2 / JG) and
 * 000353b7 (0x50, SUB EAX,0x3 / CMP / JG); the window steps ADD [EBP-0x24],0x3
 * behind a compare of base + 3 against the cursor and ADD [EBP-0x24],-0x3
 * behind a compare of the cursor against the base; the slot geometry IDIV 3 /
 * IMUL 0x65 / ADD 0xb and IMUL 0x1b / ADD 0x9 at 000354ad and 000354c6; the
 * icon's IMUL EAX,[EBP-0x2c],0xc at 00035584 over the ROSTER index; the level's
 * MOV AL,byte ptr [EDX+0x21] / PUSH 0x2 at 0003567e; and the epilogue CMP
 * dword ptr [EBP-0x10],0x0 / JLE at 0003577d that turns anything but a
 * confirmation into -1.  None of it is read off the emitted C.
 *
 * HOW A MODAL LOOP IS RUN FROM OUTSIDE.  The list draws its own frames and
 * answers one number, so every case stages the sheets and tables a frame is
 * drawn through, installs a timer interrupt that both advances the game's
 * clock and plays the case's keys, calls, and asserts on the number that came
 * back.  It is the scaffolding tests/vilmenu.c uses on the village grid, for
 * the same reasons and with the same two rules: the keys go into the SCANCODE
 * LATCH and not the ring, because fdps_read_scancode_auto_repeat answers from
 * the latch alone and throws the ring away on every poll (keybd.h); and two
 * equal keys in a row need the filter's own 0xff no-key value between them,
 * because the filter reports a code because it CHANGED and an unchanged latch
 * is a held key.  Past the end of a script the interrupt plays escape forever,
 * so a run that got away from its script cancels instead of hanging.
 *
 * WHY THIS ONE READS THE SCREEN BACK AND tests/vilmenu.c DOES NOT.  Which
 * candidates the three slots are showing is the whole of the window base, and
 * the window base is a LOCAL here -- there is no global left behind to look at
 * and the answer only carries the cursor.  So the two panel cases below make
 * the walking icons paint: every one of a roster member's twelve cache streams
 * is 24 rows of one fill run in that member's own colour, so the pixel under a
 * slot names the roster index that slot resolved, whatever walk frame the tick
 * happened to land on.  The level figure is read the same way, out of the
 * synthetic Number.cel whose glyph g paints colour 0x30 + g.
 *
 * EVERYTHING ELSE STAYS A SKIP-ONLY STREAM.  The window frame, the highlight,
 * the two stand halves, the level plate and the two scroll arrows are one skip
 * run per row and write no pixel, which is deliberate twice over: they are not
 * what these cases are about, and the down arrow's fixed 25x22 block at page
 * offset 0x4e96 runs off the end of the 0x5ca0 page the pass allocated
 * (church.c), so a sprite that really painted would corrupt this test image's
 * heap rather than draw a wrong picture.
 *
 * WHERE A PIXEL IS.  The present copies 304 by 67 from page offset 0x3ad, page
 * (5, 3), to screen 0xa9c48, screen (8, 125), so page (row, col) reaches
 * screen (row + 122, col + 3).  Slot i's icon is 24 by 24 at page (9,
 * 0x0b + i * 0x65) and its level figure's first digit cell is 6 by 8 at page
 * (0x2b, 0x1f + i * 0x65), which is where the two sample points below come
 * from.
 *
 * HOW THE TARGET CLASS IS READ BACK.  Which message entry a slot asked for is
 * the only witness to the route byte the promotion record gave up, so the text
 * table is built as a witness: every entry names a lone -1 terminator and
 * draws nothing, except the handful a case marks, which name one solid glyph.
 * Command.cel sprite 0x2b, the right half of the stand, is the one sheet
 * sprite that really paints, and its 25 by 22 block covers the whole glyph
 * cell, so "this slot asked for the entry the case expected" and "it asked for
 * some other entry" come back as two different colours rather than as one
 * colour and the allocator's leftovers.
 *
 * WHAT IS NOT ASSERTED.  The member's name is message entry char_id + 1 and no
 * case marks those entries, so the name draw walks an empty stream: which
 * entry it asked for is not pinned here.  Nor is the blink phase of the two
 * scroll arrows, which are skip-only like every other sheet sprite.
 */

/* The party the list resolves its indices against.  Twelve members, each with
   its own level so that a slot showing the wrong record shows the wrong
   figure, and each with its own icon colour so that a slot showing the wrong
   member shows the wrong pixel. */
#define SEL_MEMBERS 12
#define SEL_LEVEL_BASE 10
#define SEL_ICON_COLOR_BASE 0x20

/* The longest candidate list a case builds. */
#define SEL_CANDIDATES_MAX 8

/* One promotion route is three bytes and byte 1 is the target class code:
   LEA EAX,[EAX+EAX*2] / MOV AL,byte ptr [EDX+0x1] at 0003548b. */
#define SEL_ROUTE_BYTES 3
#define SEL_ROUTE_FORM_AT 0
#define SEL_ROUTE_CLASS_AT 1
#define SEL_ROUTE_MOVE_AT 2
#define SEL_ROUTES 4
/* Values for the two bytes this screen never reads, chosen outside the class
   codes the table hands out and low enough that entry value + 0xa1 is still
   inside the text table. */
#define SEL_ROUTE_FORM_BASE 0x30
#define SEL_ROUTE_MOVE_BASE 0x38

/* The message entry a target class is spelled out as, ADD EAX,0xa1 at
   00035639. */
#define SEL_CLASS_TEXT_BIAS 0xa1

/* The make codes the list's chain knows, and the filter's no-key answer. */
#define SEL_KEY_ESC 0x01
#define SEL_KEY_ENTER 0x1c
#define SEL_KEY_SPACE 0x39
#define SEL_KEY_UP 0x48
#define SEL_KEY_LEFT 0x4b
#define SEL_KEY_RIGHT 0x4d
#define SEL_KEY_DOWN 0x50
#define SEL_KEY_NONE 0xff

/* How many codes one script can hold. */
#define SEL_SCRIPT_MAX 8

/* The shared .CEL.  One sheet satisfies the panel frame, the highlight and
   every Command.cel sprite: fdps_cel_blit_sprite takes the size out of the
   header's i16 pair at +0x07 and +0x09 (sprite.h) while
   fdps_blit_command_sprite passes its own fixed 25 by 22 and reads only the
   offset table.  Every stream is one skip run per row -- the op is the top two
   bits and the low six carry len-1, so 0xc0 | 24 skips exactly the 25 pixels
   the row must account for (rle.h) -- so no sprite writes anything.

   0x48 entries because the highest index asked for is 0x47, the lit frame of
   the down arrow. */
#define SEL_CEL_SPRITES 0x48
#define SEL_CEL_TABLE_AT 0x0f
#define SEL_CEL_W 25
#define SEL_CEL_H 22
#define SEL_CEL_SKIP_CMD (0xc0 | (SEL_CEL_W - 1))
#define SEL_CEL_STREAMS_AT (SEL_CEL_TABLE_AT + SEL_CEL_SPRITES * 4)
/* A fill run is two bytes to the row and a skip run one, so every sprite gets
   room for the wider of the two. */
#define SEL_CEL_STREAM_BYTES (SEL_CEL_H * 2)
#define SEL_CEL_BYTES (SEL_CEL_STREAMS_AT \
                       + SEL_CEL_SPRITES * SEL_CEL_STREAM_BYTES)

/* The one sheet sprite that really paints: Command.cel 0x2b, the right half of
   the stand, whose 25 by 22 block at page (slot_y + 0x10, slot_x + 0x17)
   covers the whole 8 by 8 cell the target-class glyph is drawn into.  It is
   the known background that makes "this slot drew its class entry" and "this
   slot drew nothing" different colours instead of one colour and the
   allocator's leftovers. */
#define SEL_STAND_RIGHT_SPRITE 0x2b
#define SEL_CEL_FILL_CMD (SEL_CEL_W - 1)
#define SEL_STAND_COLOR 0x11

/* The walking-icon cache.  Its offset table starts at the base with no header
   of any kind, it holds twelve streams per member, and the list indexes it by
   ROSTER slot.  Each stream is 24 rows of one fill run of 24 pixels -- the op
   is 00 and the low six bits carry len-1, so 0x17 followed by a colour byte
   fills the row -- in the colour that names the member. */
#define SEL_ICON_PER_SLOT 0x0c
#define SEL_ICON_SPRITES (SEL_MEMBERS * SEL_ICON_PER_SLOT)
#define SEL_ICON_SIDE 24
#define SEL_ICON_FILL_CMD (SEL_ICON_SIDE - 1)
#define SEL_ICON_STREAM_BYTES (SEL_ICON_SIDE * 2)
#define SEL_ICON_STREAMS_AT (SEL_ICON_SPRITES * 4)
#define SEL_ICON_BYTES (SEL_ICON_STREAMS_AT \
                        + SEL_ICON_SPRITES * SEL_ICON_STREAM_BYTES)

/* The synthetic Number.cel: thirteen 6 by 8 entries in the order
   fdps_draw_number maps a character onto, each eight rows of one 6-pixel fill
   run, glyph g painted in colour 0x30 + g. */
#define SEL_DIGIT_ENTRIES 13
#define SEL_DIGIT_W 6
#define SEL_DIGIT_H 8
#define SEL_DIGIT_FILL_CMD (SEL_DIGIT_W - 1)
#define SEL_DIGIT_COLOR_BASE 0x30
#define SEL_DIGIT_STREAM_BYTES (SEL_DIGIT_H * 2)
#define SEL_DIGIT_STREAMS_AT (SEL_CEL_TABLE_AT + SEL_DIGIT_ENTRIES * 4)
#define SEL_DIGIT_BYTES (SEL_DIGIT_STREAMS_AT \
                         + SEL_DIGIT_ENTRIES * SEL_DIGIT_STREAM_BYTES)

/* The text block.  fdps_draw_text takes a table of signed 16-bit byte offsets
   measured from the block's own base and walks the stream it points at until
   the token -1 (text.h), so a table whose every entry names one lone
   terminator draws nothing.  The highest entry asked for is the target class
   plus 0xa1.

   The two entries past the table are the two streams every entry points at:
   a lone terminator that draws nothing, and one glyph followed by a
   terminator.  A case marks the entries it expects the panel to ask for, and
   only those are pointed at the painting stream, so a slot that resolved a
   different text id leaves the stand colour showing where its glyph would
   have been. */
#define SEL_TEXT_ENTRIES 0x100
#define SEL_TEXT_TERMINATOR (-1)
#define SEL_TEXT_EMPTY_AT (SEL_TEXT_ENTRIES * 2)
#define SEL_TEXT_GLYPH_AT ((SEL_TEXT_ENTRIES + 1) * 2)
#define SEL_TEXT_SLOTS (SEL_TEXT_ENTRIES + 3)
#define SEL_PAINTED_MAX 4

/* The synthetic font: one 8 by 8 cell per glyph, one byte to the row and the
   most significant bit leftmost, with glyph 1 solid and glyph 0 blank.  The
   panel asks for colours 0xd0 over no background with outline 0x6d, and with
   the outline flag clear that draws the shadow first and the body second, so a
   painted cell ends up 0xd0. */
#define SEL_FONT_GLYPHS 2
#define SEL_FONT_W 8
#define SEL_FONT_H 8
#define SEL_FONT_STRIDE 8
#define SEL_FONT_SOLID_GLYPH 1
#define SEL_TEXT_FG_COLOR 0xd0

/* An empty container: the lookup walks no entries and answers NULL, so
   fdps_play_sfx finds no clip and starts no sample (audio.c). */
#define SEL_WAV_BANK_BYTES 16

/* Where the present puts the page, and the two sample points inside a slot.
   The icon pixel is taken ten pixels into the 24 by 24 block so that no stand
   half or highlight edge could reach it even if one of them painted; the
   figure pixel is taken two into a 6 by 8 digit cell. */
#define SEL_PANEL_SCREEN_PITCH 0x140
#define SEL_SLOT_X_STRIDE 0x65
#define SEL_ICON_PIXEL_AT (141 * SEL_PANEL_SCREEN_PITCH + 24)
#define SEL_LEVEL_PIXEL_AT (167 * SEL_PANEL_SCREEN_PITCH + 36)
/* The target-class glyph cell is 8 by 8 at page (0x27, 0x2f + i * 0x65),
   sampled two in on both axes. */
#define SEL_CLASS_PIXEL_AT (163 * SEL_PANEL_SCREEN_PITCH + 52)
#define SEL_LEVEL_DIGIT_W 6
#define SEL_LEVEL_DIGITS 2
#define SEL_SLOTS 3

static struct fdps_unit_record sel_roster[SEL_MEMBERS];
static unsigned char sel_promo[SEL_MEMBERS * PROMOTION_RECORD_STRIDE];
static unsigned char sel_cel[SEL_CEL_BYTES];
static unsigned char sel_icons[SEL_ICON_BYTES];
static unsigned char sel_digits[SEL_DIGIT_BYTES];
static short sel_text[SEL_TEXT_SLOTS];
static unsigned char sel_font[SEL_FONT_GLYPHS * SEL_FONT_STRIDE];
static unsigned char sel_wav_bank[SEL_WAV_BANK_BYTES];

static int sel_roster_indices[SEL_CANDIDATES_MAX];
static int sel_choices[SEL_CANDIDATES_MAX];

/* The text ids the case expects the panel to ask for; sel_stage points these
   and no others at the painting stream. */
static int sel_painted_ids[SEL_PAINTED_MAX];
static int sel_painted_count;

static unsigned char sel_script[SEL_SCRIPT_MAX];
static int sel_script_len;
static int sel_script_next;
static void (__interrupt __far *sel_saved_timer)();

static unsigned char sel_icon_pixel[SEL_SLOTS];
static unsigned char sel_level_pixel[SEL_LEVEL_DIGITS];
static unsigned char sel_class_pixel[SEL_SLOTS];

/* Advances the game's clock the way fdps_timer_tick_handler does, and presents
   the next code of the script to the auto-repeat filter -- but only once the
   filter has taken the one already there, which is what an equal previous
   scancode and latch mean (keybd.h). */
static void __interrupt __far sel_timer_isr(void)
{
    unsigned char code;

    ++data_fdps_timer_tick_counter;

    if (data_fdps_input_key_repeat_prev_scancode
            == (unsigned int) data_fdps_input_last_scancode) {
        if (sel_script_next < sel_script_len) {
            code = sel_script[sel_script_next];
            sel_script_next++;
        } else if (data_fdps_input_last_scancode == SEL_KEY_ESC) {
            code = SEL_KEY_NONE;
        } else {
            code = SEL_KEY_ESC;
        }
        data_fdps_input_last_scancode = code;
    }

    _chain_intr(sel_saved_timer);
}

static void sel_u16(unsigned char *image, int at, unsigned int value)
{
    image[at] = (unsigned char) (value & 0xff);
    image[at + 1] = (unsigned char) ((value >> 8) & 0xff);
}

static void sel_u32(unsigned char *image, int at, unsigned long value)
{
    image[at] = (unsigned char) (value & 0xff);
    image[at + 1] = (unsigned char) ((value >> 8) & 0xff);
    image[at + 2] = (unsigned char) ((value >> 16) & 0xff);
    image[at + 3] = (unsigned char) ((value >> 24) & 0xff);
}

static void sel_build_cel(void)
{
    int sprite_id;
    int row;
    int stream_at;

    memset(sel_cel, 0, sizeof(sel_cel));
    sel_cel[0] = 'C';
    sel_cel[1] = 'E';
    sel_cel[2] = 'L';
    sel_u16(sel_cel, 0x07, (unsigned int) SEL_CEL_W);
    sel_u16(sel_cel, 0x09, (unsigned int) SEL_CEL_H);
    sel_u16(sel_cel, 0x0b, (unsigned int) SEL_CEL_SPRITES);

    for (sprite_id = 0; sprite_id < SEL_CEL_SPRITES; sprite_id++) {
        stream_at = SEL_CEL_STREAMS_AT + sprite_id * SEL_CEL_STREAM_BYTES;
        sel_u32(sel_cel, SEL_CEL_TABLE_AT + sprite_id * 4,
                (unsigned long) stream_at);
        if (sprite_id == SEL_STAND_RIGHT_SPRITE) {
            for (row = 0; row < SEL_CEL_H; row++) {
                sel_cel[stream_at + row * 2] =
                    (unsigned char) SEL_CEL_FILL_CMD;
                sel_cel[stream_at + row * 2 + 1] =
                    (unsigned char) SEL_STAND_COLOR;
            }
        } else {
            for (row = 0; row < SEL_CEL_H; row++) {
                sel_cel[stream_at + row] = (unsigned char) SEL_CEL_SKIP_CMD;
            }
        }
    }
}

static void sel_build_icons(void)
{
    int sprite_id;
    int row;
    int stream_at;
    unsigned char color;

    memset(sel_icons, 0, sizeof(sel_icons));
    for (sprite_id = 0; sprite_id < SEL_ICON_SPRITES; sprite_id++) {
        stream_at = SEL_ICON_STREAMS_AT + sprite_id * SEL_ICON_STREAM_BYTES;
        sel_u32(sel_icons, sprite_id * 4, (unsigned long) stream_at);
        color = (unsigned char) (SEL_ICON_COLOR_BASE
                                 + sprite_id / SEL_ICON_PER_SLOT);
        for (row = 0; row < SEL_ICON_SIDE; row++) {
            sel_icons[stream_at + row * 2] =
                (unsigned char) SEL_ICON_FILL_CMD;
            sel_icons[stream_at + row * 2 + 1] = color;
        }
    }
}

static void sel_build_digits(void)
{
    int glyph;
    int row;
    int stream_at;

    memset(sel_digits, 0, sizeof(sel_digits));
    sel_digits[0] = 'C';
    sel_digits[1] = 'E';
    sel_digits[2] = 'L';
    sel_u16(sel_digits, 0x07, (unsigned int) SEL_DIGIT_W);
    sel_u16(sel_digits, 0x09, (unsigned int) SEL_DIGIT_H);
    sel_u16(sel_digits, 0x0b, (unsigned int) SEL_DIGIT_ENTRIES);

    for (glyph = 0; glyph < SEL_DIGIT_ENTRIES; glyph++) {
        stream_at = SEL_DIGIT_STREAMS_AT + glyph * SEL_DIGIT_STREAM_BYTES;
        sel_u32(sel_digits, SEL_CEL_TABLE_AT + glyph * 4,
                (unsigned long) stream_at);
        for (row = 0; row < SEL_DIGIT_H; row++) {
            sel_digits[stream_at + row * 2] =
                (unsigned char) SEL_DIGIT_FILL_CMD;
            sel_digits[stream_at + row * 2 + 1] =
                (unsigned char) (SEL_DIGIT_COLOR_BASE + glyph);
        }
    }
}

static void sel_stage(void)
{
    int slot;
    int route;
    int entry;

    sel_build_cel();
    sel_build_icons();
    sel_build_digits();

    memset(sel_roster, 0, sizeof(sel_roster));
    for (slot = 0; slot < SEL_MEMBERS; slot++) {
        sel_roster[slot].char_id = (unsigned char) slot;
        sel_roster[slot].level = (unsigned char) (SEL_LEVEL_BASE + slot);
    }

    /* Every one of a record's twelve bytes carries a distinct value, so a read
       that took the wrong route or the wrong byte of the right one names a
       text entry that is not the one the case marked as painting. */
    memset(sel_promo, 0, sizeof(sel_promo));
    for (slot = 0; slot < SEL_MEMBERS; slot++) {
        for (route = 0; route < SEL_ROUTES; route++) {
            sel_promo[slot * PROMOTION_RECORD_STRIDE
                      + route * SEL_ROUTE_BYTES + SEL_ROUTE_FORM_AT] =
                (unsigned char) (SEL_ROUTE_FORM_BASE + route);
            sel_promo[slot * PROMOTION_RECORD_STRIDE
                      + route * SEL_ROUTE_BYTES + SEL_ROUTE_CLASS_AT] =
                (unsigned char) (slot * SEL_ROUTES + route);
            sel_promo[slot * PROMOTION_RECORD_STRIDE
                      + route * SEL_ROUTE_BYTES + SEL_ROUTE_MOVE_AT] =
                (unsigned char) (SEL_ROUTE_MOVE_BASE + route);
        }
    }

    for (entry = 0; entry < SEL_TEXT_ENTRIES; entry++) {
        sel_text[entry] = (short) SEL_TEXT_EMPTY_AT;
    }
    sel_text[SEL_TEXT_ENTRIES] = SEL_TEXT_TERMINATOR;
    sel_text[SEL_TEXT_ENTRIES + 1] = (short) SEL_FONT_SOLID_GLYPH;
    sel_text[SEL_TEXT_ENTRIES + 2] = SEL_TEXT_TERMINATOR;
    for (entry = 0; entry < sel_painted_count; entry++) {
        sel_text[sel_painted_ids[entry]] = (short) SEL_TEXT_GLYPH_AT;
    }

    memset(sel_font, 0, sizeof(sel_font));
    for (entry = 0; entry < SEL_FONT_H; entry++) {
        sel_font[SEL_FONT_SOLID_GLYPH * SEL_FONT_STRIDE + entry] = 0xff;
    }

    memset(sel_wav_bank, 0, sizeof(sel_wav_bank));

    data_fdps_roster_array_ptr = (unsigned char *) sel_roster;
    data_fdps_promotion_table_ptr = sel_promo;
    data_fdps_village_window_sheet_ptr = sel_cel;
    data_fdps_selection_bar_sheet_ptr = sel_cel;
    data_fdps_command_sprite_sheet_ptr = sel_cel;
    data_fdps_number_glyph_sheet_ptr = sel_digits;
    data_fdps_number_glyph_color_row = 0;
    data_fdps_cel_sprite_cache_ptr = sel_icons;
    data_fdps_all_game_text_ptr = (unsigned char *) sel_text;
    data_fdps_font_sheet_ptr = sel_font;
    data_fdps_font_glyph_width = SEL_FONT_W;
    data_fdps_glyph_cell_height = SEL_FONT_H;
    data_fdps_font_glyph_stride_bytes = SEL_FONT_STRIDE;
    data_fdps_font_outline_enabled_flag = 0;
    data_fdps_font_shadow_offset_x = 0;
    data_fdps_glyph_shadow_row_offset = 0;
    data_fdps_glyph_advance_x = SEL_FONT_W;
    data_fdps_font_line_height = SEL_FONT_H;
    data_fdps_audio_basewav_sfx_bank_buf_ptr = sel_wav_bank;
}

/* Back to the state a freshly started program has these in.  It is not
   tidiness: the chapter loaders and the shutdown path free most of these
   pointers unguarded, so a case that walked away leaving one of them naming a
   static in this file would hand a later test a free() of storage that never
   came from the heap. */
static void sel_unstage(void)
{
    data_fdps_roster_array_ptr = NULL;
    data_fdps_promotion_table_ptr = NULL;
    data_fdps_village_window_sheet_ptr = NULL;
    data_fdps_selection_bar_sheet_ptr = NULL;
    data_fdps_command_sprite_sheet_ptr = NULL;
    data_fdps_number_glyph_sheet_ptr = NULL;
    data_fdps_cel_sprite_cache_ptr = NULL;
    data_fdps_all_game_text_ptr = NULL;
    data_fdps_font_sheet_ptr = NULL;
    data_fdps_audio_basewav_sfx_bank_buf_ptr = NULL;
    sel_painted_count = 0;
    data_fdps_input_last_scancode = SEL_KEY_NONE;
    data_fdps_input_key_repeat_prev_scancode = SEL_KEY_NONE;
}

/* The three icon pixels and the two level-figure pixels of slot 0, read out of
   the aperture while the adapter is still in the mode the panel was presented
   in. */
static void sel_capture(void)
{
    unsigned char *screen;
    int slot;
    int digit;

    screen = (unsigned char *) CHURCH_VGA_BASE;
    for (slot = 0; slot < SEL_SLOTS; slot++) {
        sel_icon_pixel[slot] =
            screen[SEL_ICON_PIXEL_AT + slot * SEL_SLOT_X_STRIDE];
        sel_class_pixel[slot] =
            screen[SEL_CLASS_PIXEL_AT + slot * SEL_SLOT_X_STRIDE];
    }
    for (digit = 0; digit < SEL_LEVEL_DIGITS; digit++) {
        sel_level_pixel[digit] =
            screen[SEL_LEVEL_PIXEL_AT + digit * SEL_LEVEL_DIGIT_W];
    }
}

/* One whole run of the list, with the adapter in the mode the game draws it in
   and the timer interrupt both pacing the frames and playing the keys.  The
   candidate arrays are the caller's; every case builds its own. */
static int sel_run(int count, unsigned char *codes, int code_count)
{
    int answer;
    int index;

    sel_stage();

    for (index = 0; index < code_count; index++) {
        sel_script[index] = codes[index];
    }
    sel_script_len = code_count;
    sel_script_next = 0;

    data_fdps_input_last_scancode = SEL_KEY_NONE;
    data_fdps_input_key_repeat_prev_scancode = SEL_KEY_NONE;
    data_fdps_input_key_repeat_counter = 0;
    data_fdps_input_key_repeat_last_tick = 0;
    data_fdps_input_scancode_queue_head = 0;
    data_fdps_input_scancode_queue_write_index = 0;
    data_fdps_timer_tick_counter = 0;
    data_fdps_ui_palette_last_cycle_tick = 0;
    data_fdps_ui_palette_cycle_phase = 0;

    set_mode(CHURCH_MODE_320X200X256);
    sel_saved_timer = _dos_getvect(CHURCH_TIMER_VECTOR);
    _dos_setvect(CHURCH_TIMER_VECTOR, sel_timer_isr);
    answer = fdps_church_select_promote_candidate(count, sel_roster_indices,
                                                  sel_choices);
    _dos_setvect(CHURCH_TIMER_VECTOR, sel_saved_timer);
    sel_capture();
    set_mode(CHURCH_MODE_TEXT);

    return answer;
}

/* The five-candidate list every case but the two count-sensitive ones uses.
   The roster indices are deliberately not in order and deliberately not equal
   to the candidate position, so a build that used the candidate index where
   the assembly uses the roster index draws the wrong member. */
static void sel_five_candidates(void)
{
    static int indices[5] = { 7, 6, 5, 4, 3 };
    static int choices[5] = { 0, 1, 2, 3, 0 };
    int index;

    for (index = 0; index < 5; index++) {
        sel_roster_indices[index] = indices[index];
        sel_choices[index] = choices[index];
    }
}

/* Escape stores -1 in the loop result at 000352f9 and the epilogue's JLE hands
   that same -1 back. */
static void escape_cancels_the_list(void)
{
    unsigned char script[1];

    script[0] = SEL_KEY_ESC;
    sel_five_candidates();
    CHECK_EQ(sel_run(5, script, 1), -1);
    sel_unstage();
}

/* Enter stores 1 at 00035311 and the epilogue answers with the cursor, which
   this list always opens at 0 -- both seeds are MOV ...,0x0 at 000352cc and
   000352d3 and nothing else writes them. */
static void enter_answers_the_cursor(void)
{
    unsigned char script[1];

    script[0] = SEL_KEY_ENTER;
    sel_five_candidates();
    CHECK_EQ(sel_run(5, script, 1), 0);
    sel_unstage();
}

/* 0x39 is the second confirm code, tested at 0003530b and joining Enter's
   store. */
static void space_confirms_like_enter(void)
{
    unsigned char script[1];

    script[0] = SEL_KEY_SPACE;
    sel_five_candidates();
    CHECK_EQ(sel_run(5, script, 1), 0);
    sel_unstage();
}

/* Right steps the cursor one candidate on and Left steps it back, INC and DEC
   of [EBP-0x20] at 00035331 and 00035367.  Two rights and one left leave it on
   candidate 1. */
static void right_steps_on_and_left_steps_back(void)
{
    unsigned char script[5];

    script[0] = SEL_KEY_RIGHT;
    script[1] = SEL_KEY_NONE;
    script[2] = SEL_KEY_RIGHT;
    script[3] = SEL_KEY_LEFT;
    script[4] = SEL_KEY_ENTER;
    sel_five_candidates();
    CHECK_EQ(sel_run(5, script, 5), 1);
    sel_unstage();
}

/* Right's guard is count - 1 against the cursor with JG, so the last candidate
   is where it stops: in a two-candidate list the first right moves and the
   second does not. */
static void right_stops_at_the_last_candidate(void)
{
    unsigned char script[4];

    script[0] = SEL_KEY_RIGHT;
    script[1] = SEL_KEY_NONE;
    script[2] = SEL_KEY_RIGHT;
    script[3] = SEL_KEY_ENTER;
    sel_five_candidates();
    CHECK_EQ(sel_run(2, script, 4), 1);
    sel_unstage();
}

/* Left's guard is 0 against the cursor with JG, so a left on the first
   candidate moves nothing. */
static void left_stops_at_the_first_candidate(void)
{
    unsigned char script[2];

    script[0] = SEL_KEY_LEFT;
    script[1] = SEL_KEY_ENTER;
    sel_five_candidates();
    CHECK_EQ(sel_run(5, script, 2), 0);
    sel_unstage();
}

/* Down adds three, and its guard is count - 3 against the cursor with JG.  In
   a five-candidate list the first down moves 0 to 3 and the second is refused
   even though candidate 4 exists -- the move is by a whole row or not at
   all. */
static void down_steps_a_row_and_then_stops(void)
{
    unsigned char script[4];

    script[0] = SEL_KEY_DOWN;
    script[1] = SEL_KEY_NONE;
    script[2] = SEL_KEY_DOWN;
    script[3] = SEL_KEY_ENTER;
    sel_five_candidates();
    CHECK_EQ(sel_run(5, script, 4), 3);
    sel_unstage();
}

/* Up subtracts three behind CMP [EBP-0x20],0x2 / JG, so it needs a cursor of
   at least 3.  On candidate 0 it does nothing; after two downs in an
   eight-candidate list it takes 6 back to 3. */
static void up_needs_a_whole_row_above_it(void)
{
    unsigned char script[2];
    unsigned char down_up[5];
    int index;

    script[0] = SEL_KEY_UP;
    script[1] = SEL_KEY_ENTER;
    for (index = 0; index < SEL_CANDIDATES_MAX; index++) {
        sel_roster_indices[index] = index;
        sel_choices[index] = 0;
    }
    CHECK_EQ(sel_run(8, script, 2), 0);
    sel_unstage();

    down_up[0] = SEL_KEY_DOWN;
    down_up[1] = SEL_KEY_NONE;
    down_up[2] = SEL_KEY_DOWN;
    down_up[3] = SEL_KEY_UP;
    down_up[4] = SEL_KEY_ENTER;
    for (index = 0; index < SEL_CANDIDATES_MAX; index++) {
        sel_roster_indices[index] = index;
        sel_choices[index] = 0;
    }
    CHECK_EQ(sel_run(8, down_up, 5), 3);
    sel_unstage();
}

/* The panel the list opens on shows candidates 0, 1 and 2 in slots 0, 1 and 2,
   each resolved through the caller's roster-index array and not through its
   own position: the icons are roster 7, 6 and 5 in a list whose candidates are
   0 to 4.  The level under slot 0 is roster member 7's own, level 17, printed
   as a zero-padded pair of digits. */
static void the_panel_shows_the_members_the_caller_named(void)
{
    unsigned char script[1];

    script[0] = SEL_KEY_ENTER;
    sel_five_candidates();
    CHECK_EQ(sel_run(5, script, 1), 0);
    CHECK_EQ(sel_icon_pixel[0], SEL_ICON_COLOR_BASE + 7);
    CHECK_EQ(sel_icon_pixel[1], SEL_ICON_COLOR_BASE + 6);
    CHECK_EQ(sel_icon_pixel[2], SEL_ICON_COLOR_BASE + 5);
    CHECK_EQ(sel_level_pixel[0], SEL_DIGIT_COLOR_BASE + 1);
    CHECK_EQ(sel_level_pixel[1], SEL_DIGIT_COLOR_BASE + 7);
    sel_unstage();
}

/* The window base follows the cursor a row at a time: after the down that puts
   the cursor on candidate 3, base + 3 <= cursor holds and the base steps to 3,
   so slot 0 now shows candidate 3 -- roster 4, level 14 -- and slot 1 shows
   candidate 4.  Slot 2 is past the end of the five-candidate list and is not
   drawn, so its pixel is the allocator's leftovers and is not asserted. */
static void the_window_follows_the_cursor_down(void)
{
    unsigned char script[2];

    script[0] = SEL_KEY_DOWN;
    script[1] = SEL_KEY_ENTER;
    sel_five_candidates();
    CHECK_EQ(sel_run(5, script, 2), 3);
    CHECK_EQ(sel_icon_pixel[0], SEL_ICON_COLOR_BASE + 4);
    CHECK_EQ(sel_icon_pixel[1], SEL_ICON_COLOR_BASE + 3);
    CHECK_EQ(sel_level_pixel[0], SEL_DIGIT_COLOR_BASE + 1);
    CHECK_EQ(sel_level_pixel[1], SEL_DIGIT_COLOR_BASE + 4);
    sel_unstage();
}

/* Marks one text entry as the one that paints. */
static void sel_expect_text(int text_id)
{
    sel_painted_ids[sel_painted_count] = text_id;
    sel_painted_count++;
}

/* The class code each slot spells out is byte 1 of the route THAT candidate's
   promotion_choices entry names -- record + choice * 3, LEA EAX,[EAX+EAX*2] /
   MOV AL,byte ptr [EDX+0x1] at 0003548b -- and the message entry is that code
   plus 0xa1.  The three candidates carry choices 0, 1 and 2 over characters 7,
   6 and 5, so the three ids are 0xbd, 0xba and 0xb6 and no two of them are
   what any other route or any other byte of the same record would have named:
   a stride of one would read the move bonus, a byte offset of zero the form
   id, and a choice taken from the slot instead of the array would read route 0
   for slot 0 and disagree for the other two. */
static void the_slot_spells_out_the_class_the_route_names(void)
{
    unsigned char script[1];

    script[0] = SEL_KEY_ENTER;
    sel_five_candidates();
    sel_painted_count = 0;
    sel_expect_text(SEL_CLASS_TEXT_BIAS + 7 * SEL_ROUTES + 0);
    sel_expect_text(SEL_CLASS_TEXT_BIAS + 6 * SEL_ROUTES + 1);
    sel_expect_text(SEL_CLASS_TEXT_BIAS + 5 * SEL_ROUTES + 2);
    CHECK_EQ(sel_run(5, script, 1), 0);
    CHECK_EQ(sel_class_pixel[0], SEL_TEXT_FG_COLOR);
    CHECK_EQ(sel_class_pixel[1], SEL_TEXT_FG_COLOR);
    CHECK_EQ(sel_class_pixel[2], SEL_TEXT_FG_COLOR);
    sel_unstage();
}

/* The other half of the same claim: with every candidate carrying route 0 the
   two entries the case above painted for slots 1 and 2 are no longer asked
   for, so those two cells keep the stand colour while slot 0, whose choice was
   0 both times, still paints.  A build that ignored promotion_choices would
   paint all three here or none there. */
static void a_different_route_names_a_different_class(void)
{
    unsigned char script[1];
    int index;

    script[0] = SEL_KEY_ENTER;
    sel_five_candidates();
    for (index = 0; index < SEL_CANDIDATES_MAX; index++) {
        sel_choices[index] = 0;
    }
    sel_painted_count = 0;
    sel_expect_text(SEL_CLASS_TEXT_BIAS + 7 * SEL_ROUTES + 0);
    sel_expect_text(SEL_CLASS_TEXT_BIAS + 6 * SEL_ROUTES + 1);
    sel_expect_text(SEL_CLASS_TEXT_BIAS + 5 * SEL_ROUTES + 2);
    CHECK_EQ(sel_run(5, script, 1), 0);
    CHECK_EQ(sel_class_pixel[0], SEL_TEXT_FG_COLOR);
    CHECK_EQ(sel_class_pixel[1], SEL_STAND_COLOR);
    CHECK_EQ(sel_class_pixel[2], SEL_STAND_COLOR);
    sel_unstage();
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
    RUN_TEST(escape_cancels_the_list);
    RUN_TEST(enter_answers_the_cursor);
    RUN_TEST(space_confirms_like_enter);
    RUN_TEST(right_steps_on_and_left_steps_back);
    RUN_TEST(right_stops_at_the_last_candidate);
    RUN_TEST(left_stops_at_the_first_candidate);
    RUN_TEST(down_steps_a_row_and_then_stops);
    RUN_TEST(up_needs_a_whole_row_above_it);
    RUN_TEST(the_panel_shows_the_members_the_caller_named);
    RUN_TEST(the_window_follows_the_cursor_down);
    RUN_TEST(the_slot_spells_out_the_class_the_route_names);
    RUN_TEST(a_different_route_names_a_different_class);
}
