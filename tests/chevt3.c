/* tests/chevt3.c -- cover for src/chevt3.c.
 *
 * The chapter 15 handler at 00037af0 is the chapter 13 handler's shape with the
 * range 0x1d..0x25 in place of 9..0x2c, and is covered the same way: its two
 * bounds, the signed inclusive compare between them and the 0xf0 mask are all
 * literals in its instruction stream -- MOV dword ptr [EBP-0x20],0x1d at
 * 00037b03, MOV dword ptr [EBP-0x1c],0x25 at 00037b0a, CMP EAX,[EBP-0x10] / JLE
 * at 00037b33, and AND DL,0xf0 at 00037b53 -- and the absence of a guard in
 * front of its loop is asserted by putting the shared one-shot latch slot up and
 * watching it run anyway.
 *
 * The unit array is staged here rather than read from a game file, because the
 * handler takes its whole effect through data_fdps_map_unit_array_ptr --
 * pointing that global at a local block is the only way to see the stores.
 * What the global itself holds is ticket 23's and is not asserted, so every
 * case writes the state it wants to see changed.
 *
 * The chapter 16 handler at 00038020 is the same expansion twice over behind an
 * equality on data_fdps_battle_turn_counter, so it is covered the same way with
 * one thing added: which branch a turn number reaches.  That test is what pins
 * the fall-through down as a fall-through -- the map schedules turns 5 and 15
 * and only 5 is compared for -- so several other turns are put through it and
 * all of them have to land on the lower range.  The counter is set by the test
 * because it is a global ticket 23 has not written yet; what it holds outside a
 * battle is not asserted.
 *
 * Which indices the range means comes from map14.dat: 9 player records are laid
 * down first at 0..8 and the file's wave-0 deployment records 1..43 follow in
 * file order, so unit index = deployment record index + 8 and 0x1d..0x25 are
 * records 21..29 -- the eight-unit bridgehead block at the top right of the map
 * plus record 27, the beam turret.  Both ends of the range are pinned hardest,
 * because the inclusive top bound is what every obvious rewrite of the loop
 * gets wrong at exactly one record.
 *
 * The chapter 16 wandering-smith cases in the middle of the file stage
 * differently again and say why in their own banner: that handler plays a
 * scripted scene of message windows and two-option prompts, so its cases run
 * the whole scene in mode 13h with a timer interrupt playing the player's keys
 * and read back the state it left -- the one-shot byte, Randis's inventory, the
 * purse and the stats the rebuild rewrites.
 *
 * The chapter 17 cases in the last third of the file stage differently and say
 * why in their own note: that handler's whole body is a call into
 * fdps_deploy_wave, which opens ICON.CEL and FIELD.VFS for itself, so the
 * cases need those files and skip themselves without them.
 *
 * The chapter 19 cases at the end stand on that same fixture with one thing
 * added -- a chapter text block, because that handler speaks after it deploys
 * -- and say the rest in their own banner.
 */
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dos.h>
#include <i86.h>
#include "testharn.h"
#include "fdpstype.h"
#include "gamedata.h"
#include "keybd.h"
#include "mapdraw.h"
#include "unit.h"
#include "chevt3.h"

/* The single inclusive range the one inline loop covers, read off the
   constants at 00037b03 (0x1d) and 00037b0a (0x25) with the signed JLE at
   00037b36. */
#define CH15_FIRST_INDEX 0x1d
#define CH15_LAST_INDEX  0x25

/* How many records that range covers: 0x25 - 0x1d + 1, the nine units the
   handler is named for. */
#define CH15_RANGE_UNITS 9

/* The last unit index chapter 15's map deploys in wave 0: map14.dat's records
   1..43 land at 9..0x33, so the range stops well short of the end of the array
   and everything above 0x25 has to keep the behaviour the map gave it. */
#define CH15_LAST_DEPLOYED_INDEX 0x33

/* Enough records to hold that whole deployment and a few past it, so an
   off-by-one at either end of the range has somewhere visible to land. */
#define CH15_STAGE_UNITS 0x38

/* The unit whose death script names this handler: map14.dat's deployment
   record 8, the level 19 ice mage inside the stockade, which becomes unit
   index 8 + 8. */
#define CH15_DEATH_SCRIPT_UNIT_INDEX 0x10

/* The slot of data_fdps_map_cell_event_triggered_flags the one-shot handlers
   of this family latch -- element 0x10, the first the map's own event codes
   cannot reach.  This handler does not use it, and that is what is asserted. */
#define CH15_LATCH_SLOT 0x10

static struct fdps_unit_record ch15_units[CH15_STAGE_UNITS];

/* Give every record the same AI byte and point the array global at the block.
   The staged value carries a high nibble as well as a behaviour code, because
   the whole point of the merge is that only one of the two moves. */
static void stage_ch15_units(int ai_behavior)
{
    unsigned char *bytes;
    int i;

    bytes = (unsigned char *) ch15_units;
    for (i = 0; i < (int) sizeof(ch15_units); i++) {
        bytes[i] = 0;
    }
    for (i = 0; i < CH15_STAGE_UNITS; i++) {
        ch15_units[i].ai_behavior = (unsigned char) ai_behavior;
    }
    data_fdps_map_unit_array_ptr = (unsigned char *) ch15_units;
}

/* The record has to be 0x50 bytes with its AI byte at +0x34 for the emitted C
   to address the byte the MOV byte ptr [EAX+0x34] store at 00037b5e addresses;
   the stride is the IMUL 0x50 inside fdps_get_unit_record. */
static void ch15_record_shape_matches_the_offsets(void)
{
    CHECK_EQ((int) sizeof(struct fdps_unit_record), 0x50);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, ai_behavior), 0x34);
}

/* Exactly indices 0x1d..0x25 are rewritten and every record either side of the
   range is left as it was.  The staged 0x52 is behaviour code 2 -- hold
   position, which is what byte 0x11 of map14.dat's deployment records gives its
   units -- under a high nibble of 0x50; the range comes out 0x50 because the
   mode ORed in is 0, and the rest keep 0x52.  Indices 0..0x1c cover the nine
   player records and the first twelve deployed enemies, which the range starts
   above, and 0x26..0x37 run past the last deployed unit at 0x33: both ends have
   to be untouched. */
static void ch15_activate_clears_exactly_the_range(void)
{
    int i;

    stage_ch15_units(0x52);
    fdps_chapter_15_event_activate_enemy_group(0);

    for (i = 0; i < CH15_STAGE_UNITS; i++) {
        if (i >= CH15_FIRST_INDEX && i <= CH15_LAST_INDEX) {
            CHECK_EQ(ch15_units[i].ai_behavior, 0x50);
        } else {
            CHECK_EQ(ch15_units[i].ai_behavior, 0x52);
        }
    }
}

/* The range covers nine records, not eight: 0x25 - 0x1d + 1, and the top bound
   is inclusive because the compare at 00037b33 is JLE.  The count is asserted
   by counting the records that moved, and both boundary records are named on
   their own -- 0x25 must move and 0x26 must not -- because a rewrite of the
   loop as i < 0x25 is exactly the mistake that leaves the last of the nine
   units holding position and changes nothing else. */
static void ch15_activate_includes_the_last_index(void)
{
    int i;
    int moved;

    stage_ch15_units(0x52);
    fdps_chapter_15_event_activate_enemy_group(0);

    moved = 0;
    for (i = 0; i < CH15_STAGE_UNITS; i++) {
        if (ch15_units[i].ai_behavior == 0x50) {
            moved++;
        }
    }
    CHECK_EQ(moved, CH15_RANGE_UNITS);
    CHECK_EQ(ch15_units[CH15_FIRST_INDEX - 1].ai_behavior, 0x52);
    CHECK_EQ(ch15_units[CH15_FIRST_INDEX].ai_behavior, 0x50);
    CHECK_EQ(ch15_units[CH15_LAST_INDEX].ai_behavior, 0x50);
    CHECK_EQ(ch15_units[CH15_LAST_INDEX + 1].ai_behavior, 0x52);
    CHECK_EQ(ch15_units[CH15_LAST_DEPLOYED_INDEX].ai_behavior, 0x52);
}

/* The high nibble is carried across untouched by the AND 0xf0 at 00037b53 and
   the low nibble ends at 0 whatever it held.  0x40 and 0x80 are the two AI
   flags read elsewhere -- fdps_map_actor_take_best_action at 00012c72 and
   fdps_score_targets_for_item at 00013380 -- so a merge that assigned the mode
   whole, or that masked with anything wider, would drop them.  Expected values
   are the staged byte ANDed with 0xf0. */
static void ch15_activate_keeps_the_high_nibble(void)
{
    stage_ch15_units(0);
    ch15_units[0x1d].ai_behavior = 0xc2;
    ch15_units[0x1e].ai_behavior = 0x02;
    ch15_units[0x1f].ai_behavior = 0xff;
    ch15_units[0x21].ai_behavior = 0x40;
    ch15_units[0x25].ai_behavior = 0x8b;

    fdps_chapter_15_event_activate_enemy_group(0);

    CHECK_EQ(ch15_units[0x1d].ai_behavior, 0xc0);
    CHECK_EQ(ch15_units[0x1e].ai_behavior, 0x00);
    CHECK_EQ(ch15_units[0x1f].ai_behavior, 0xf0);
    CHECK_EQ(ch15_units[0x21].ai_behavior, 0x40);
    CHECK_EQ(ch15_units[0x25].ai_behavior, 0x80);
}

/* Nothing guards the loop: the instruction after the argument-slot store at
   00037afc is the first of the three constant stores, with no compare between
   them, so this handler has no one-shot latch and runs its loop every time it
   is called.  The latch slot is put up before the call and the range still
   moves; the slot is also asserted unchanged, because a handler that had grown
   a latch would have written it. */
static void ch15_activate_has_no_one_shot_latch(void)
{
    stage_ch15_units(0x52);
    data_fdps_map_cell_event_triggered_flags[CH15_LATCH_SLOT] = 1;

    fdps_chapter_15_event_activate_enemy_group(0);

    CHECK_EQ(ch15_units[CH15_FIRST_INDEX].ai_behavior, 0x50);
    CHECK_EQ(ch15_units[CH15_LAST_INDEX].ai_behavior, 0x50);
    CHECK_EQ(data_fdps_map_cell_event_triggered_flags[CH15_LATCH_SLOT], 1);

    stage_ch15_units(0x52);
    fdps_chapter_15_event_activate_enemy_group(0);
    CHECK_EQ(ch15_units[CH15_FIRST_INDEX].ai_behavior, 0x50);
    CHECK_EQ(ch15_units[CH15_LAST_INDEX].ai_behavior, 0x50);
}

/* One byte of one record moves and nothing either side of it does.  Every byte
   of the block is stamped 0x55 first, so a store that landed at +0x33 or +0x35
   -- the death-script operand's high byte and ai_dest_x -- is visible; 0x55 is
   also a value whose low nibble is not already 0, so the write that should
   happen is visible too.  Both ends of the range are checked, because the
   stride the store is indexed by is the IMUL 0x50 inside fdps_get_unit_record
   and an error in it shows up furthest from the base. */
static void ch15_activate_touches_no_neighbouring_byte(void)
{
    unsigned char *bytes;
    int i;

    bytes = (unsigned char *) ch15_units;
    for (i = 0; i < (int) sizeof(ch15_units); i++) {
        bytes[i] = 0x55;
    }
    data_fdps_map_unit_array_ptr = bytes;

    fdps_chapter_15_event_activate_enemy_group(0);

    CHECK_EQ(bytes[0x1d * 0x50 + 0x34], 0x50);
    CHECK_EQ(bytes[0x1d * 0x50 + 0x33], 0x55);
    CHECK_EQ(bytes[0x1d * 0x50 + 0x35], 0x55);
    CHECK_EQ(bytes[0x25 * 0x50 + 0x34], 0x50);
    CHECK_EQ(bytes[0x25 * 0x50 + 0x33], 0x55);
    CHECK_EQ(bytes[0x25 * 0x50 + 0x35], 0x55);
    CHECK_EQ(bytes[0x1c * 0x50 + 0x34], 0x55);
    CHECK_EQ(bytes[0x26 * 0x50 + 0x34], 0x55);
}

/* The incoming argument slot is overwritten with 0 at 00037afc and never read,
   and no loop bound comes from it, so the index the dispatcher passes cannot
   reach the result.  The death-script runner is the only path this slot is
   reached by in the shipped data and it pushes its own actor index at 0001dcad
   -- the unit that landed the killing blow, so one of the nine player records
   at 0..8 -- while the unit whose script it is sits at 0x10; both are passed
   here, along with an index inside the range, and 0x26, -1 and 30000, which are
   the ones an argument-driven handler would betray itself on. */
static void ch15_activate_ignores_the_unit_index_argument(void)
{
    stage_ch15_units(0x52);
    fdps_chapter_15_event_activate_enemy_group(CH15_DEATH_SCRIPT_UNIT_INDEX);
    CHECK_EQ(ch15_units[CH15_DEATH_SCRIPT_UNIT_INDEX].ai_behavior, 0x52);
    CHECK_EQ(ch15_units[CH15_FIRST_INDEX].ai_behavior, 0x50);
    CHECK_EQ(ch15_units[CH15_LAST_INDEX].ai_behavior, 0x50);

    stage_ch15_units(0x52);
    fdps_chapter_15_event_activate_enemy_group(0);
    CHECK_EQ(ch15_units[0].ai_behavior, 0x52);
    CHECK_EQ(ch15_units[CH15_LAST_INDEX].ai_behavior, 0x50);

    stage_ch15_units(0x52);
    fdps_chapter_15_event_activate_enemy_group(CH15_FIRST_INDEX);
    CHECK_EQ(ch15_units[CH15_FIRST_INDEX].ai_behavior, 0x50);
    CHECK_EQ(ch15_units[CH15_LAST_INDEX + 1].ai_behavior, 0x52);

    stage_ch15_units(0x52);
    fdps_chapter_15_event_activate_enemy_group(CH15_LAST_INDEX + 1);
    CHECK_EQ(ch15_units[CH15_LAST_INDEX + 1].ai_behavior, 0x52);
    CHECK_EQ(ch15_units[CH15_LAST_INDEX].ai_behavior, 0x50);

    stage_ch15_units(0x52);
    fdps_chapter_15_event_activate_enemy_group(-1);
    CHECK_EQ(ch15_units[CH15_FIRST_INDEX].ai_behavior, 0x50);
    CHECK_EQ(ch15_units[0].ai_behavior, 0x52);

    stage_ch15_units(0x52);
    fdps_chapter_15_event_activate_enemy_group(30000);
    CHECK_EQ(ch15_units[CH15_LAST_INDEX].ai_behavior, 0x50);
    CHECK_EQ(ch15_units[0].ai_behavior, 0x52);
}

/* ---- fdps_chapter_16_event_wandering_smith_forge, 00037cd0 ---------------
 *
 * Expected values come from the assembly and from assets/items.md, never from
 * the emitted C: CMP dword ptr [EBP+0x14],0x0 / JNZ at 00037ce3, CMP byte ptr
 * [0x000640e8],0x0 / JZ at 00037ce9 and CMP dword ptr [0x00069ce8],0x14 / JLE
 * at 00037cf4 for the three gates; MOV byte ptr [0x000640e8],0x1 at 00037d37
 * for where the latch is raised; PUSH 0x58 at 00037d3e and PUSH 0x59 at
 * 00037d5c for the two swords looked for and the order they are looked for in;
 * the two CMP dword ptr [EBP+0x14],0x0 / JNZ at 00037df6 and 00037f43 for the
 * answers that accept; ADD dword ptr [0x000643a4],0x1388 at 00037efc for the
 * payment; and PUSH 0xa0, PUSH 0xa9 and PUSH 0xa3 at 00037eb3, 00037f7e and
 * 00037f8f for what is handed over.  Item ids 0x58 修佩魯, 0x59 雷德,
 * 0xa0 灼烈之劍, 0xa9 神的聖印 and 0xa3 金屬礦 are assets/items.md's.
 *
 * WHAT THE HANDLER IS OBSERVED THROUGH.  It returns nothing and paints its
 * whole scene, so what a case reads back afterwards is the state it left: the
 * one-shot byte, Randis's eight inventory entries, the party purse and the four
 * derived combat stats fdps_unit_recompute_combat_stats rewrites.  Those stats
 * are the witness that the rebuild ran at all -- each case stamps ap, dp, hit
 * and ev with a sentinel first, and a path that reached the rebuild replaces
 * them with the record's own bases while a path that did not leaves the
 * sentinel standing.
 *
 * THE SCENE IS PLAYED FOR REAL, the way tests/icon.c plays
 * fdps_icon_script_prompt_three_way_choice: the run is made in mode 13h with a
 * timer interrupt in place, because both the panel slides and
 * fdps_prompt_two_choice pace themselves on data_fdps_timer_tick_counter, which
 * nothing advances in a test image.  Message.cel and Shadow.cel are stood in
 * for with generated sheets, the chapter text block is one whose every entry is
 * a lone terminator, and the scene is left empty so the close's recomposition
 * draws nothing.
 *
 * HOW THE KEYS ARE PLAYED, and why this feeder is tests/icon.c's rather than
 * tests/msgwin.c's: a run here contains up to two prompts, every prompt opens
 * with fdps_flush_keyboard_queue, and the window between the two is many timer
 * ticks long, so a feeder that appended one code per tick would lose the second
 * prompt's key and hang.  This one appends only while the ring is EMPTY and
 * steps through the case's list only when the read index has moved, which only
 * fdps_read_keyboard_queue moves.  Past the end of the list the last code is
 * held, so a run that asks for one more prompt than the case staged answers it
 * and fails an assertion instead of spinning forever.  How many codes the run
 * consumed is therefore how many prompts it ran, which is what separates the
 * one-question path from the two-question one.
 *
 * WHICH TEXT ENTRY EACH DRAW ASKS FOR IS NOT ASSERTED, for the reason the
 * chapter 8 section of tests/chevt2.c gives and one more of this handler's own:
 * fdps_draw_text takes its whole effect through pixels and returns a cursor
 * this handler discards, and every one of the ten draws here is followed by
 * fdps_message_window_close, which repaints the whole visible page -- so not
 * one of them is still on screen when the handler returns.  The ten ids are
 * literals in the instruction stream and the reviewer's reading of them is what
 * stands behind the emitted C.  What the cases do pin about the draws is that
 * they do not stop the trade: every case runs to the end of the function.
 *
 * FACE.CEL HAS TO BE THERE.  Every panel reveal passes face 0x81, and
 * fdps_message_window_open sends a non-negative face straight to
 * fdps_load_and_draw_portrait, which ends the process at exit(1) on a sheet it
 * cannot open rather than failing an assertion.  Every case below skips itself
 * when the sheet is not next to the executable.
 * ------------------------------------------------------------------ */

#define SMITH_VGA_BASE 0x000a0000
#define SMITH_MODE_TEXT 0x03
#define SMITH_MODE_320X200X256 0x13
#define SMITH_FACE_SHEET "FACE.CEL"
#define SMITH_TIMER_VECTOR 8

/* The make codes the prompt answers to: Enter confirms the highlighted cell,
   which is the left one on entry, the right arrow moves to the other cell, and
   Esc cancels with -1 (msgwin.h). */
#define SMITH_KEY_ESC 0x01
#define SMITH_KEY_ENTER 0x1c
#define SMITH_KEY_RIGHT 0x4d
#define SMITH_KEYS_MAX 4

/* The element of data_fdps_map_cell_event_triggered_flags the handler latches,
   byte ptr [0x000640e8] -- element 0x10 of the array based at 0x000640d8. */
#define SMITH_LATCH_SLOT 0x10

/* The last turn the event still fires on, CMP 0x14 / JLE at 00037cf4, and the
   first turn it does not. */
#define SMITH_LAST_TURN 0x14
#define SMITH_FIRST_LATE_TURN 0x15

/* The five item ids the scene moves, all of them literals in the instruction
   stream and all named in assets/items.md. */
#define SMITH_REFORGEABLE_SWORD 0x58
#define SMITH_BREAKING_SWORD 0x59
#define SMITH_REFORGED_SWORD 0xa0
#define SMITH_SEAL 0xa9
#define SMITH_ORE 0xa3

/* What the broken sword is paid for, ADD dword ptr [0x000643a4],0x1388. */
#define SMITH_COMPENSATION_GOLD 5000

/* The purse each case starts from.  Any value does; a round one just makes the
   5000 obvious in a failure line. */
#define SMITH_START_GOLD 1000

/* An inventory entry nobody is carrying: flag bit 0x80 is what
   fdps_unit_item_count and fdps_unit_add_item read as empty, and the stale id
   beside it is the 0xff a deployment leaves (unititem.h). */
#define SMITH_EMPTY_FLAG 0x80
#define SMITH_EMPTY_ID 0xff

/* An entry that is carried but not equipped: bit 0x40 clear, so
   fdps_unit_recompute_combat_stats adds no item modifier and the four stats it
   writes are the record's own bases. */
#define SMITH_CARRIED_FLAG 0x00

/* Randis's base stats and the sentinel the four derived stats are stamped with
   before each run.  The bases are arbitrary and only have to differ from each
   other and from the sentinel; with no equipped entry and no status timer the
   rebuild writes ap = ap_base, dp = dp_base and both hit and ev = dx_base
   (unit.h). */
#define SMITH_AP_BASE 40
#define SMITH_DP_BASE 30
#define SMITH_DX_BASE 20
#define SMITH_STAT_SENTINEL 0x7777

/* How many inventory entries a record has, and the array's stride. */
#define SMITH_INVENTORY_ENTRIES 8
#define SMITH_STAGE_UNITS 2

/* The chapter text block: 23 entries, which is what FDETXT16.TXT carries, every
   one of them pointing at the same lone terminator so that a draw walks it,
   paints nothing and returns at once. */
#define SMITH_TEXT_IDS 0x17
#define SMITH_TEXT_EMPTY_AT 0x40
#define SMITH_TEXT_BLOCK_BYTES (SMITH_TEXT_EMPTY_AT + 2)
#define SMITH_TEXT_END (-1)

/* The Message.cel stand-in: one 302 x 73 sprite encoded as five fill runs per
   row, because a fill run cannot be longer than 64 pixels. */
#define SMITH_PANEL_W 302
#define SMITH_PANEL_H 73
#define SMITH_PANEL_FILL_MAX 64
#define SMITH_PANEL_SEGMENTS 5
#define SMITH_PANEL_LAST_SEGMENT_W 46
#define SMITH_PANEL_ROW_BYTES (SMITH_PANEL_SEGMENTS * 2)
#define SMITH_PANEL_STREAM_AT 0x40
#define SMITH_PANEL_SHEET_BYTES (SMITH_PANEL_STREAM_AT \
                                 + SMITH_PANEL_H * SMITH_PANEL_ROW_BYTES)
#define SMITH_PANEL_COLOR 0x40

/* The Shadow.cel stand-in the prompt draws its two option cells out of:
   fourteen 24 x 24 sprites, one fill run per row, sprite i filled with i. */
#define SMITH_SHADOW_SPRITES 14
#define SMITH_SHADOW_SPRITE_W 24
#define SMITH_SHADOW_SPRITE_H 24
#define SMITH_SHADOW_STREAM_BYTES (SMITH_SHADOW_SPRITE_H * 2)
#define SMITH_SHADOW_STREAM_AT 0x50
#define SMITH_SHADOW_SHEET_BYTES (SMITH_SHADOW_STREAM_AT \
                                  + SMITH_SHADOW_SPRITES \
                                    * SMITH_SHADOW_STREAM_BYTES)

/* The .CEL header fields both fixtures carry, and a table position neither
   reader may consult: both hardwire the table at 0x0f. */
#define SMITH_CEL_TABLE_AT 0x0f
#define SMITH_CEL_DECOY_TABLE_AT 0x100
#define SMITH_CEL_VERSION 1
#define SMITH_CEL_PIXEL_FORMAT 2

/* Every item id is a valid index into the staged item table:
   fdps_unit_recompute_combat_stats follows an equipped flag into
   fdps_get_item_record without a bounds check. */
#define SMITH_ITEM_TABLE_ROWS 256

static unsigned char smith_panel_sheet[SMITH_PANEL_SHEET_BYTES];
static unsigned char smith_shadow_sheet[SMITH_SHADOW_SHEET_BYTES];
static unsigned char smith_text_block[SMITH_TEXT_BLOCK_BYTES];
static struct fdps_unit_record smith_units[SMITH_STAGE_UNITS];
static struct fdps_item_effect smith_items[SMITH_ITEM_TABLE_ROWS];
static unsigned char smith_keys[SMITH_KEYS_MAX];
static volatile int smith_key_count;
static volatile int smith_keys_read;
static volatile int smith_last_head;
static int smith_fixtures_staged = 0;
static void (__interrupt __far *smith_saved_timer)();

static void smith_u16(unsigned char *image, int at, unsigned int value)
{
    image[at] = (unsigned char) (value & 0xff);
    image[at + 1] = (unsigned char) ((value >> 8) & 0xff);
}

static void smith_u32(unsigned char *image, int at, unsigned long value)
{
    image[at] = (unsigned char) (value & 0xff);
    image[at + 1] = (unsigned char) ((value >> 8) & 0xff);
    image[at + 2] = (unsigned char) ((value >> 16) & 0xff);
    image[at + 3] = (unsigned char) ((value >> 24) & 0xff);
}

static int smith_sheet_present(void)
{
    FILE *probe;

    probe = fopen(SMITH_FACE_SHEET, "rb");
    if (probe == NULL) {
        return 0;
    }
    fclose(probe);
    return 1;
}

static void smith_cel_header(unsigned char *sheet, int width, int height,
                             int sprites)
{
    sheet[0] = 'C';
    sheet[1] = 'E';
    sheet[2] = 'L';
    smith_u16(sheet, 0x03, SMITH_CEL_VERSION);
    smith_u16(sheet, 0x05, SMITH_CEL_DECOY_TABLE_AT);
    smith_u16(sheet, 0x07, (unsigned int) width);
    smith_u16(sheet, 0x09, (unsigned int) height);
    smith_u16(sheet, 0x0b, (unsigned int) sprites);
    smith_u16(sheet, 0x0d, SMITH_CEL_PIXEL_FORMAT);
}

/* The two sheets and the text block.  None of them changes between cases, so
   they are built once. */
static void smith_stage_fixtures(void)
{
    int row;
    int segment;
    int cursor;
    int run;
    int sprite;
    int stream_at;
    int text_id;

    if (smith_fixtures_staged) {
        return;
    }
    smith_fixtures_staged = 1;

    memset(smith_panel_sheet, 0, (size_t) SMITH_PANEL_SHEET_BYTES);
    smith_cel_header(smith_panel_sheet, SMITH_PANEL_W, SMITH_PANEL_H, 1);
    smith_u32(smith_panel_sheet, SMITH_CEL_TABLE_AT,
              (unsigned long) SMITH_PANEL_STREAM_AT);
    smith_u32(smith_panel_sheet, SMITH_CEL_TABLE_AT + 4,
              (unsigned long) SMITH_PANEL_SHEET_BYTES);
    for (row = 0; row < SMITH_PANEL_H; row++) {
        cursor = SMITH_PANEL_STREAM_AT + row * SMITH_PANEL_ROW_BYTES;
        for (segment = 0; segment < SMITH_PANEL_SEGMENTS; segment++) {
            if (segment == SMITH_PANEL_SEGMENTS - 1) {
                run = SMITH_PANEL_LAST_SEGMENT_W;
            } else {
                run = SMITH_PANEL_FILL_MAX;
            }
            smith_panel_sheet[cursor] = (unsigned char) (run - 1);
            smith_panel_sheet[cursor + 1] = SMITH_PANEL_COLOR;
            cursor += 2;
        }
    }

    memset(smith_shadow_sheet, 0, (size_t) SMITH_SHADOW_SHEET_BYTES);
    smith_cel_header(smith_shadow_sheet, SMITH_SHADOW_SPRITE_W,
                     SMITH_SHADOW_SPRITE_H, SMITH_SHADOW_SPRITES);
    for (sprite = 0; sprite < SMITH_SHADOW_SPRITES; sprite++) {
        stream_at = SMITH_SHADOW_STREAM_AT
                    + sprite * SMITH_SHADOW_STREAM_BYTES;
        smith_u32(smith_shadow_sheet, SMITH_CEL_TABLE_AT + sprite * 4,
                  (unsigned long) stream_at);
        for (row = 0; row < SMITH_SHADOW_SPRITE_H; row++) {
            smith_shadow_sheet[stream_at + row * 2] =
                (unsigned char) (SMITH_SHADOW_SPRITE_W - 1);
            smith_shadow_sheet[stream_at + row * 2 + 1] =
                (unsigned char) sprite;
        }
    }
    smith_u32(smith_shadow_sheet,
              SMITH_CEL_TABLE_AT + SMITH_SHADOW_SPRITES * 4,
              (unsigned long) SMITH_SHADOW_SHEET_BYTES);

    memset(smith_text_block, 0, (size_t) SMITH_TEXT_BLOCK_BYTES);
    *(short *) (smith_text_block + SMITH_TEXT_EMPTY_AT) = (short) SMITH_TEXT_END;
    for (text_id = 0; text_id < SMITH_TEXT_IDS; text_id++) {
        *(short *) (smith_text_block + text_id * 2) =
            (short) SMITH_TEXT_EMPTY_AT;
    }
}

/* Everything the callees read that this section has to make definite: the text
   block, the two sheets, an item table for the stat rebuild, an empty scene so
   the close's recomposition draws nothing, and village mode so the prompt lifts
   its backdrop off the visible page instead of composing one. */
static void smith_stage_globals(void)
{
    smith_stage_fixtures();

    data_fdps_current_chapter_text_ptr = smith_text_block;
    data_fdps_message_window_sheet_ptr = smith_panel_sheet;
    data_fdps_shadow_sprite_sheet_ptr = smith_shadow_sheet;
    data_fdps_item_effect_table_ptr = (unsigned char *) smith_items;

    data_fdps_village_mode_flag = 1;
    data_fdps_scene_layer_count = 0;
    data_fdps_map_unit_count = 0;
    data_fdps_map_cursor_draw_mode = 0;
    data_fdps_timer_tick_counter = 0;
}

/* Randis's record: an empty eight-entry inventory carrying whichever items the
   case names, the three stat bases the rebuild reads and the sentinel in the
   four stats it writes.  Item ids are given as -1 for "carry nothing here". */
static void smith_stage_unit(int first_item, int second_item)
{
    int entry;

    memset(smith_units, 0, sizeof(smith_units));
    memset(smith_items, 0, sizeof(smith_items));

    for (entry = 0; entry < SMITH_INVENTORY_ENTRIES; entry++) {
        smith_units[0].inventory_slots[entry * 2] = SMITH_EMPTY_FLAG;
        smith_units[0].inventory_slots[entry * 2 + 1] = SMITH_EMPTY_ID;
    }
    if (first_item >= 0) {
        smith_units[0].inventory_slots[0] = SMITH_CARRIED_FLAG;
        smith_units[0].inventory_slots[1] = (unsigned char) first_item;
    }
    if (second_item >= 0) {
        smith_units[0].inventory_slots[2] = SMITH_CARRIED_FLAG;
        smith_units[0].inventory_slots[3] = (unsigned char) second_item;
    }

    smith_units[0].ap_base = SMITH_AP_BASE;
    smith_units[0].dp_base = SMITH_DP_BASE;
    smith_units[0].dx_base = SMITH_DX_BASE;
    smith_units[0].ap = SMITH_STAT_SENTINEL;
    smith_units[0].dp = SMITH_STAT_SENTINEL;
    smith_units[0].hit = SMITH_STAT_SENTINEL;
    smith_units[0].ev = SMITH_STAT_SENTINEL;

    data_fdps_map_unit_array_ptr = (unsigned char *) smith_units;
}

/* The three globals the entry gates and the payment read. */
static void smith_stage_state(int battle_turn, int latch)
{
    data_fdps_battle_turn_counter = battle_turn;
    data_fdps_map_cell_event_triggered_flags[SMITH_LATCH_SLOT] =
        (unsigned char) latch;
    data_fdps_shared_party_total_gold = SMITH_START_GOLD;
}

/* Loads the codes this case plays, in the order the prompts read them. */
static void smith_stage_keys(int count, int first, int second, int third)
{
    smith_keys[0] = (unsigned char) first;
    smith_keys[1] = (unsigned char) second;
    smith_keys[2] = (unsigned char) third;
    smith_keys[3] = (unsigned char) third;
    smith_key_count = count;
    smith_keys_read = 0;
}

static void smith_set_mode(int mode)
{
    union REGS regs;

    memset(&regs, 0, sizeof(regs));
    regs.x.eax = (unsigned) mode;
    int386(0x10, &regs, &regs);
}

/* The player.  It advances the game's clock like the real timer handler and
   appends the case's next make code the way fdps_keyboard_isr does, but only
   while the ring is empty, and it steps through the list on the read index
   moving rather than on ticks -- see the note at the top of this section. */
static void __interrupt __far smith_timer_isr(void)
{
    int slot;
    int next_key;

    ++data_fdps_timer_tick_counter;

    if (data_fdps_input_scancode_queue_head != smith_last_head) {
        smith_last_head = data_fdps_input_scancode_queue_head;
        smith_keys_read++;
    }

    if (data_fdps_input_scancode_queue_head
            == data_fdps_input_scancode_queue_write_index) {
        next_key = smith_keys_read;
        if (next_key >= smith_key_count) {
            next_key = smith_key_count - 1;
        }
        slot = data_fdps_input_scancode_queue_write_index;
        data_fdps_input_scancode_queue[slot] = smith_keys[next_key];
        slot++;
        if (slot == SCANCODE_QUEUE_LEN) {
            slot = 0;
        }
        data_fdps_input_scancode_queue_write_index = slot;
    }

    _chain_intr(smith_saved_timer);
}

/* One whole firing: the adapter in the mode the game runs it in, the feeder
   installed, and text mode back before anything is asserted so that a failure
   prints on a readable screen. */
static void smith_run(int unit_index)
{
    smith_stage_globals();

    smith_last_head = 0;
    smith_keys_read = 0;
    data_fdps_input_scancode_queue_head = 0;
    data_fdps_input_scancode_queue_write_index = 0;

    smith_set_mode(SMITH_MODE_320X200X256);
    smith_saved_timer = _dos_getvect(SMITH_TIMER_VECTOR);
    _dos_setvect(SMITH_TIMER_VECTOR, smith_timer_isr);
    fdps_chapter_16_event_wandering_smith_forge(unit_index);
    _dos_setvect(SMITH_TIMER_VECTOR, smith_saved_timer);
    smith_set_mode(SMITH_MODE_TEXT);

    if (data_fdps_portrait_sprite_buf_ptr != NULL) {
        free(data_fdps_portrait_sprite_buf_ptr);
        data_fdps_portrait_sprite_buf_ptr = NULL;
    }
    data_fdps_village_mode_flag = 0;
}

static int smith_entry_flag(int entry)
{
    return (int) smith_units[0].inventory_slots[entry * 2];
}

static int smith_entry_id(int entry)
{
    return (int) smith_units[0].inventory_slots[entry * 2 + 1];
}

/* Whether any of the eight entries holds that id, flag byte disregarded, which
   is how a case says an item was or was not handed over. */
static int smith_carries(int item_id)
{
    int entry;

    for (entry = 0; entry < SMITH_INVENTORY_ENTRIES; entry++) {
        if (smith_entry_flag(entry) != SMITH_EMPTY_FLAG
                && smith_entry_id(entry) == item_id) {
            return 1;
        }
    }
    return 0;
}

/* ---------------------------------------------------------------------- */

/* The record fields these cases read back and the stride they are indexed by.
   Every one of them would agree with itself while addressing another byte if
   the layout were wrong. */
static void smith_record_shape_matches_the_offsets(void)
{
    CHECK_EQ((int) sizeof(struct fdps_unit_record), 0x50);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, inventory_slots), 0x0a);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, ap), 0x48);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, ev), 0x4e);
}

/* 修佩魯 accepted: the sword leaves the bag and 灼烈之劍 takes its place, the
   purse is not touched because that branch pays nothing, the latch is up and
   the stat rebuild has run.  The new entry is at index 0 because
   fdps_unit_remove_item packs the entries down before fdps_unit_add_item looks
   for the first empty one.  Exactly one prompt runs, which is what says the
   0x59 arm was not entered. */
static void smith_reforges_the_first_sword(void)
{
    if (!smith_sheet_present()) {
        return;
    }
    smith_stage_unit(SMITH_REFORGEABLE_SWORD, -1);
    smith_stage_state(1, 0);
    smith_stage_keys(1, SMITH_KEY_ENTER, 0, 0);

    smith_run(0);

    CHECK_EQ(smith_keys_read, 1);
    CHECK_EQ(smith_carries(SMITH_REFORGEABLE_SWORD), 0);
    CHECK_EQ(smith_carries(SMITH_REFORGED_SWORD), 1);
    CHECK_EQ(smith_entry_id(0), SMITH_REFORGED_SWORD);
    CHECK_EQ(smith_entry_flag(0), SMITH_CARRIED_FLAG);
    CHECK_EQ(data_fdps_shared_party_total_gold, SMITH_START_GOLD);
    CHECK_EQ(data_fdps_map_cell_event_triggered_flags[SMITH_LATCH_SLOT], 1);
    CHECK_EQ(smith_units[0].ap, SMITH_AP_BASE);
    CHECK_EQ(smith_units[0].dp, SMITH_DP_BASE);
    CHECK_EQ(smith_units[0].hit, SMITH_DX_BASE);
    CHECK_EQ(smith_units[0].ev, SMITH_DX_BASE);
}

/* 雷德 accepted, and the second question accepted too: the sword is gone, the
   purse has gained exactly 5000 and 神的聖印 is what came back.  Two prompts
   run, which is the difference between this branch and the one above; the
   metal ore must NOT be there, because the two gifts are the two arms of the
   same test. */
static void smith_breaks_the_second_sword_and_pays(void)
{
    if (!smith_sheet_present()) {
        return;
    }
    smith_stage_unit(SMITH_BREAKING_SWORD, -1);
    smith_stage_state(1, 0);
    smith_stage_keys(2, SMITH_KEY_ENTER, SMITH_KEY_ENTER, 0);

    smith_run(0);

    CHECK_EQ(smith_keys_read, 2);
    CHECK_EQ(smith_carries(SMITH_BREAKING_SWORD), 0);
    CHECK_EQ(smith_carries(SMITH_SEAL), 1);
    CHECK_EQ(smith_carries(SMITH_ORE), 0);
    CHECK_EQ(smith_carries(SMITH_REFORGED_SWORD), 0);
    CHECK_EQ(data_fdps_shared_party_total_gold,
             SMITH_START_GOLD + SMITH_COMPENSATION_GOLD);
    CHECK_EQ(data_fdps_map_cell_event_triggered_flags[SMITH_LATCH_SLOT], 1);
    CHECK_EQ(smith_units[0].ap, SMITH_AP_BASE);
}

/* The second question declined gives the ore instead, and the 5000 is paid
   either way: the payment is above the question, not inside its yes arm.  The
   run takes three codes -- Enter for the offer, then right and Enter for the
   second question -- which is one more code and the same two prompts as the
   case above. */
static void smith_second_question_declined_gives_the_ore(void)
{
    if (!smith_sheet_present()) {
        return;
    }
    smith_stage_unit(SMITH_BREAKING_SWORD, -1);
    smith_stage_state(1, 0);
    smith_stage_keys(3, SMITH_KEY_ENTER, SMITH_KEY_RIGHT, SMITH_KEY_ENTER);

    smith_run(0);

    CHECK_EQ(smith_keys_read, 3);
    CHECK_EQ(smith_carries(SMITH_BREAKING_SWORD), 0);
    CHECK_EQ(smith_carries(SMITH_ORE), 1);
    CHECK_EQ(smith_carries(SMITH_SEAL), 0);
    CHECK_EQ(data_fdps_shared_party_total_gold,
             SMITH_START_GOLD + SMITH_COMPENSATION_GOLD);
    CHECK_EQ(smith_units[0].ap, SMITH_AP_BASE);
}

/* The offer declined leaves everything alone but the latch: the sword is still
   in entry 0, nothing was handed over, the purse is untouched and the four
   stats still carry the sentinel, because the rebuild sits inside the accepted
   arm.  The latch is up all the same -- it was raised before the inventory was
   even searched. */
static void smith_offer_declined_keeps_the_sword(void)
{
    if (!smith_sheet_present()) {
        return;
    }
    smith_stage_unit(SMITH_REFORGEABLE_SWORD, -1);
    smith_stage_state(1, 0);
    smith_stage_keys(2, SMITH_KEY_RIGHT, SMITH_KEY_ENTER, 0);

    smith_run(0);

    CHECK_EQ(smith_keys_read, 2);
    CHECK_EQ(smith_entry_id(0), SMITH_REFORGEABLE_SWORD);
    CHECK_EQ(smith_entry_flag(0), SMITH_CARRIED_FLAG);
    CHECK_EQ(smith_carries(SMITH_REFORGED_SWORD), 0);
    CHECK_EQ(data_fdps_shared_party_total_gold, SMITH_START_GOLD);
    CHECK_EQ(data_fdps_map_cell_event_triggered_flags[SMITH_LATCH_SLOT], 1);
    CHECK_EQ(smith_units[0].ap, SMITH_STAT_SENTINEL);
    CHECK_EQ(smith_units[0].dp, SMITH_STAT_SENTINEL);
}

/* A cancel is not the right-hand option but it declines exactly as one: Esc
   answers -1 and the test the handler makes is against 0, so the sword stays.
   This is the case a rewrite as "answer == 1 means no" would fail, by treating
   the cancel as an acceptance and forging the sword the player backed out
   of. */
static void smith_cancel_declines_like_the_right_option(void)
{
    if (!smith_sheet_present()) {
        return;
    }
    smith_stage_unit(SMITH_REFORGEABLE_SWORD, -1);
    smith_stage_state(1, 0);
    smith_stage_keys(1, SMITH_KEY_ESC, 0, 0);

    smith_run(0);

    CHECK_EQ(smith_keys_read, 1);
    CHECK_EQ(smith_entry_id(0), SMITH_REFORGEABLE_SWORD);
    CHECK_EQ(smith_carries(SMITH_REFORGED_SWORD), 0);
    CHECK_EQ(data_fdps_shared_party_total_gold, SMITH_START_GOLD);
    CHECK_EQ(smith_units[0].ap, SMITH_STAT_SENTINEL);
}

/* The two lookups are an else-chain and 修佩魯 is asked for first: a Randis
   holding both swords has 修佩魯 taken and 灼烈之劍 given, and 雷德 is still
   in the bag afterwards.  One prompt and an untouched purse are the other half
   of the same statement -- the 雷德 arm asks a second question and pays 5000,
   and neither happened. */
static void smith_prefers_the_first_sword_when_both_are_carried(void)
{
    if (!smith_sheet_present()) {
        return;
    }
    smith_stage_unit(SMITH_REFORGEABLE_SWORD, SMITH_BREAKING_SWORD);
    smith_stage_state(1, 0);
    smith_stage_keys(1, SMITH_KEY_ENTER, 0, 0);

    smith_run(0);

    CHECK_EQ(smith_keys_read, 1);
    CHECK_EQ(smith_carries(SMITH_REFORGEABLE_SWORD), 0);
    CHECK_EQ(smith_carries(SMITH_BREAKING_SWORD), 1);
    CHECK_EQ(smith_carries(SMITH_REFORGED_SWORD), 1);
    CHECK_EQ(smith_entry_id(0), SMITH_BREAKING_SWORD);
    CHECK_EQ(smith_entry_id(1), SMITH_REFORGED_SWORD);
    CHECK_EQ(data_fdps_shared_party_total_gold, SMITH_START_GOLD);
}

/* THE LATCH IS SPENT BY A RANDIS CARRYING NEITHER SWORD.  It is raised
   immediately after the greeting and before the inventory is searched, so the
   encounter is over for the rest of the chapter and the second visit below
   proves it: the same Randis, now holding 修佩魯, is refused.  A handler that
   latched where the trade happens -- which is what the sibling events do and
   what the obvious C would write -- would forge the sword on that second
   visit.

   No prompt runs on either visit, which is what the consumed-code count says,
   and the stat sentinel stands because the rebuild was never reached. */
static void smith_with_no_sword_spends_the_latch(void)
{
    if (!smith_sheet_present()) {
        return;
    }
    smith_stage_unit(-1, -1);
    smith_stage_state(1, 0);
    smith_stage_keys(1, SMITH_KEY_ENTER, 0, 0);

    smith_run(0);

    CHECK_EQ(smith_keys_read, 0);
    CHECK_EQ(data_fdps_map_cell_event_triggered_flags[SMITH_LATCH_SLOT], 1);
    CHECK_EQ(data_fdps_shared_party_total_gold, SMITH_START_GOLD);
    CHECK_EQ(smith_units[0].ap, SMITH_STAT_SENTINEL);

    smith_stage_unit(SMITH_REFORGEABLE_SWORD, -1);
    smith_stage_keys(1, SMITH_KEY_ENTER, 0, 0);

    smith_run(0);

    CHECK_EQ(smith_keys_read, 0);
    CHECK_EQ(smith_entry_id(0), SMITH_REFORGEABLE_SWORD);
    CHECK_EQ(smith_carries(SMITH_REFORGED_SWORD), 0);
    CHECK_EQ(smith_units[0].ap, SMITH_STAT_SENTINEL);
}

/* The turn bound is inclusive, because the compare at 00037cf4 is JLE: turn 20
   still fires and turn 21 does not.  Both sides are asserted through the latch,
   which the body raises before it looks at anything else, and the sword is left
   untouched on the firing side because no prompt is answered there -- the
   twentieth-turn visit is made with an empty bag, so it stops at the greeting.
   A rewrite as `< 0x14` would refuse the twentieth turn and this is the case
   that would catch it. */
static void smith_fires_on_the_last_turn_and_not_after(void)
{
    static int late_turns[4] = {SMITH_FIRST_LATE_TURN, 0x16, 100, 30000};
    int i;

    if (!smith_sheet_present()) {
        return;
    }
    smith_stage_unit(-1, -1);
    smith_stage_state(SMITH_LAST_TURN, 0);
    smith_stage_keys(1, SMITH_KEY_ENTER, 0, 0);

    smith_run(0);

    CHECK_EQ(data_fdps_map_cell_event_triggered_flags[SMITH_LATCH_SLOT], 1);
    CHECK_EQ(smith_keys_read, 0);

    for (i = 0; i < 4; i++) {
        smith_stage_unit(SMITH_REFORGEABLE_SWORD, -1);
        smith_stage_state(late_turns[i], 0);
        smith_stage_keys(1, SMITH_KEY_ENTER, 0, 0);

        smith_run(0);

        CHECK_EQ(data_fdps_map_cell_event_triggered_flags[SMITH_LATCH_SLOT], 0);
        CHECK_EQ(smith_keys_read, 0);
        CHECK_EQ(smith_entry_id(0), SMITH_REFORGEABLE_SWORD);
        CHECK_EQ(smith_carries(SMITH_REFORGED_SWORD), 0);
        CHECK_EQ(data_fdps_shared_party_total_gold, SMITH_START_GOLD);
        CHECK_EQ(smith_units[0].ap, SMITH_STAT_SENTINEL);
    }
}

/* Only battle unit 0 springs it.  The gate is an equality against 0 -- unit 0
   is Randis, always the first unit deployed -- so every other index, in range
   or not, leaves the handler doing nothing at all: the latch stays down, which
   means the encounter is still there for Randis himself. */
static void smith_fires_for_no_unit_but_randis(void)
{
    static int other_units[5] = {1, 2, 9, -1, 30000};
    int i;

    if (!smith_sheet_present()) {
        return;
    }
    for (i = 0; i < 5; i++) {
        smith_stage_unit(SMITH_REFORGEABLE_SWORD, -1);
        smith_stage_state(1, 0);
        smith_stage_keys(1, SMITH_KEY_ENTER, 0, 0);

        smith_run(other_units[i]);

        CHECK_EQ(data_fdps_map_cell_event_triggered_flags[SMITH_LATCH_SLOT], 0);
        CHECK_EQ(smith_keys_read, 0);
        CHECK_EQ(smith_entry_id(0), SMITH_REFORGEABLE_SWORD);
        CHECK_EQ(smith_carries(SMITH_REFORGED_SWORD), 0);
        CHECK_EQ(data_fdps_shared_party_total_gold, SMITH_START_GOLD);
        CHECK_EQ(smith_units[0].ap, SMITH_STAT_SENTINEL);
    }
}

/* A latch that is already up refuses the whole scene, greeting included: the
   byte the gate reads is element 0x10 of the array and the handler leaves it
   as it found it.  The two neighbouring elements are put up as well and then
   asserted untouched, because a gate reading one byte to either side would
   pass here and fail nothing. */
static void smith_does_not_fire_twice(void)
{
    if (!smith_sheet_present()) {
        return;
    }
    smith_stage_unit(SMITH_REFORGEABLE_SWORD, -1);
    smith_stage_state(1, 1);
    data_fdps_map_cell_event_triggered_flags[SMITH_LATCH_SLOT - 1] = 1;
    data_fdps_map_cell_event_triggered_flags[SMITH_LATCH_SLOT + 1] = 1;
    smith_stage_keys(1, SMITH_KEY_ENTER, 0, 0);

    smith_run(0);

    CHECK_EQ(smith_keys_read, 0);
    CHECK_EQ(data_fdps_map_cell_event_triggered_flags[SMITH_LATCH_SLOT], 1);
    CHECK_EQ(data_fdps_map_cell_event_triggered_flags[SMITH_LATCH_SLOT - 1], 1);
    CHECK_EQ(data_fdps_map_cell_event_triggered_flags[SMITH_LATCH_SLOT + 1], 1);
    CHECK_EQ(smith_entry_id(0), SMITH_REFORGEABLE_SWORD);
    CHECK_EQ(smith_carries(SMITH_REFORGED_SWORD), 0);
    CHECK_EQ(data_fdps_shared_party_total_gold, SMITH_START_GOLD);
    CHECK_EQ(smith_units[0].ap, SMITH_STAT_SENTINEL);
}

/* The two inclusive ranges the chapter 16 handler's two inline loops cover,
   read off the constants at 0003803c (0x19) and 00038043 (0x22) for the turn-5
   branch and 0003809e (0x0a) and 000380a5 (0x19) for the fall-through, each
   with a signed JLE. */
#define CH16_TURN5_FIRST_INDEX 0x19
#define CH16_TURN5_LAST_INDEX  0x22
#define CH16_OTHER_FIRST_INDEX 0x0a
#define CH16_OTHER_LAST_INDEX  0x19

/* How many records each range covers: 0x22 - 0x19 + 1 and 0x19 - 0x0a + 1. */
#define CH16_TURN5_UNITS 10
#define CH16_OTHER_UNITS 16

/* The one turn number the equality at 00038033 singles out.  Every other value
   falls through to the second loop, which is why the second is exercised with
   several turns and not just the 15 the map schedules. */
#define CH16_RELEASE_TURN 5

/* The other turn map15.dat's turn-event table names for this slot.  It reaches
   the same fall-through branch as any other non-5 turn; it is used here
   because it is what the shipped data actually fires. */
#define CH16_SHIPPED_SECOND_TURN 15

/* Enough records to hold chapter 16's whole deployment -- 10 player records at
   0..9 and 25 enemy records at 0x0a..0x22 -- and five past the top, so an
   off-by-one at either end of either range has somewhere visible to land. */
#define CH16_STAGE_UNITS 0x28

static struct fdps_unit_record ch16_units[CH16_STAGE_UNITS];

/* Stage the array the same way the chapter 15 cases do, and set the turn the
   handler is to read.  The staged AI byte carries a high nibble as well as a
   behaviour code, because the whole point of the merge is that only one of the
   two moves. */
static void stage_ch16_units(int battle_turn, int ai_behavior)
{
    unsigned char *bytes;
    int i;

    bytes = (unsigned char *) ch16_units;
    for (i = 0; i < (int) sizeof(ch16_units); i++) {
        bytes[i] = 0;
    }
    for (i = 0; i < CH16_STAGE_UNITS; i++) {
        ch16_units[i].ai_behavior = (unsigned char) ai_behavior;
    }
    data_fdps_map_unit_array_ptr = (unsigned char *) ch16_units;
    data_fdps_battle_turn_counter = battle_turn;
}

/* On turn 5 exactly indices 0x19..0x22 are rewritten and everything either
   side of them keeps what the map gave it.  The staged 0x52 is behaviour code
   2 -- hold position, which is what byte 0x11 of map15.dat's deployment
   records gives these units -- under a high nibble of 0x50; the range comes out
   0x50 because the mode ORed in is 0.  Indices 0..0x18 are the ten player
   records and waves 0 and 1, which this branch must not touch, and 0x23..0x27
   run past the last deployed unit at 0x22. */
static void ch16_turn5_clears_the_second_wave_block(void)
{
    int i;

    stage_ch16_units(CH16_RELEASE_TURN, 0x52);
    fdps_chapter_16_event_enemies_advance_for_turn(0);

    for (i = 0; i < CH16_STAGE_UNITS; i++) {
        if (i >= CH16_TURN5_FIRST_INDEX && i <= CH16_TURN5_LAST_INDEX) {
            CHECK_EQ(ch16_units[i].ai_behavior, 0x50);
        } else {
            CHECK_EQ(ch16_units[i].ai_behavior, 0x52);
        }
    }
}

/* On the turn the map schedules second, 15, the fall-through loop runs and
   exactly indices 0x0a..0x19 are rewritten: waves 0 and 1 plus the first
   wave-2 flyer.  The ten player records at 0..9 sit below the range and
   0x1a..0x27 above it, and both have to be untouched. */
static void ch16_other_turn_clears_the_opening_block(void)
{
    int i;

    stage_ch16_units(CH16_SHIPPED_SECOND_TURN, 0x52);
    fdps_chapter_16_event_enemies_advance_for_turn(0);

    for (i = 0; i < CH16_STAGE_UNITS; i++) {
        if (i >= CH16_OTHER_FIRST_INDEX && i <= CH16_OTHER_LAST_INDEX) {
            CHECK_EQ(ch16_units[i].ai_behavior, 0x50);
        } else {
            CHECK_EQ(ch16_units[i].ai_behavior, 0x52);
        }
    }
}

/* Both top bounds are inclusive, because both compares are JLE -- 0003806c for
   the turn-5 loop and 000380ce for the other -- so each range is one record
   longer than a rewrite with < would make it.  The counts are asserted by
   counting the records that moved and both boundary records of each range are
   named on their own: for turn 5 that is 0x22 moving and 0x23 not, and for the
   fall-through 0x19 moving and 0x1a not.  Index 0x19 is checked in both, since
   it is the top of one range and the bottom of the other. */
static void ch16_both_ranges_include_their_last_index(void)
{
    int i;
    int moved;

    stage_ch16_units(CH16_RELEASE_TURN, 0x52);
    fdps_chapter_16_event_enemies_advance_for_turn(0);

    moved = 0;
    for (i = 0; i < CH16_STAGE_UNITS; i++) {
        if (ch16_units[i].ai_behavior == 0x50) {
            moved++;
        }
    }
    CHECK_EQ(moved, CH16_TURN5_UNITS);
    CHECK_EQ(ch16_units[CH16_TURN5_FIRST_INDEX - 1].ai_behavior, 0x52);
    CHECK_EQ(ch16_units[CH16_TURN5_FIRST_INDEX].ai_behavior, 0x50);
    CHECK_EQ(ch16_units[CH16_TURN5_LAST_INDEX].ai_behavior, 0x50);
    CHECK_EQ(ch16_units[CH16_TURN5_LAST_INDEX + 1].ai_behavior, 0x52);

    stage_ch16_units(CH16_SHIPPED_SECOND_TURN, 0x52);
    fdps_chapter_16_event_enemies_advance_for_turn(0);

    moved = 0;
    for (i = 0; i < CH16_STAGE_UNITS; i++) {
        if (ch16_units[i].ai_behavior == 0x50) {
            moved++;
        }
    }
    CHECK_EQ(moved, CH16_OTHER_UNITS);
    CHECK_EQ(ch16_units[CH16_OTHER_FIRST_INDEX - 1].ai_behavior, 0x52);
    CHECK_EQ(ch16_units[CH16_OTHER_FIRST_INDEX].ai_behavior, 0x50);
    CHECK_EQ(ch16_units[CH16_OTHER_LAST_INDEX].ai_behavior, 0x50);
    CHECK_EQ(ch16_units[CH16_OTHER_LAST_INDEX + 1].ai_behavior, 0x52);
}

/* The branch is an equality on the turn counter, not a range test and not a
   test for turn 15: CMP dword ptr [0x00069ce8],0x5 / JNZ 0003809e at 00038033.
   So 5 is the only value that reaches the upper range and every other value --
   below it, just above it, the 15 the map schedules, and values no battle can
   reach -- lands on the lower one.  Each turn is checked at both ranges' first
   index, which is the pair that separates the branches. */
static void ch16_only_turn_five_takes_the_upper_range(void)
{
    static int other_turns[6] = {0, 1, 4, 6, 15, 30000};
    int i;

    stage_ch16_units(CH16_RELEASE_TURN, 0x52);
    fdps_chapter_16_event_enemies_advance_for_turn(0);
    CHECK_EQ(ch16_units[CH16_TURN5_LAST_INDEX].ai_behavior, 0x50);
    CHECK_EQ(ch16_units[CH16_OTHER_FIRST_INDEX].ai_behavior, 0x52);

    for (i = 0; i < 6; i++) {
        stage_ch16_units(other_turns[i], 0x52);
        fdps_chapter_16_event_enemies_advance_for_turn(0);
        CHECK_EQ(ch16_units[CH16_OTHER_FIRST_INDEX].ai_behavior, 0x50);
        CHECK_EQ(ch16_units[CH16_TURN5_LAST_INDEX].ai_behavior, 0x52);
    }

    stage_ch16_units(-1, 0x52);
    fdps_chapter_16_event_enemies_advance_for_turn(0);
    CHECK_EQ(ch16_units[CH16_OTHER_FIRST_INDEX].ai_behavior, 0x50);
    CHECK_EQ(ch16_units[CH16_TURN5_LAST_INDEX].ai_behavior, 0x52);
}

/* The high nibble is carried across untouched by the AND 0xf0 at 0003808c and
   000380ee, and the low nibble ends at 0 whatever it held.  0x40 and 0x80 are
   the two AI flags read elsewhere, so a merge that assigned the mode whole, or
   masked with anything wider, would drop them.  Expected values are the staged
   byte ANDed with 0xf0, and both loops are checked because they are two
   separate copies of the merge in the instruction stream. */
static void ch16_keeps_the_high_nibble(void)
{
    stage_ch16_units(CH16_RELEASE_TURN, 0);
    ch16_units[0x19].ai_behavior = 0xc2;
    ch16_units[0x1a].ai_behavior = 0x02;
    ch16_units[0x1f].ai_behavior = 0xff;
    ch16_units[0x21].ai_behavior = 0x40;
    ch16_units[0x22].ai_behavior = 0x8b;
    fdps_chapter_16_event_enemies_advance_for_turn(0);
    CHECK_EQ(ch16_units[0x19].ai_behavior, 0xc0);
    CHECK_EQ(ch16_units[0x1a].ai_behavior, 0x00);
    CHECK_EQ(ch16_units[0x1f].ai_behavior, 0xf0);
    CHECK_EQ(ch16_units[0x21].ai_behavior, 0x40);
    CHECK_EQ(ch16_units[0x22].ai_behavior, 0x80);

    stage_ch16_units(CH16_SHIPPED_SECOND_TURN, 0);
    ch16_units[0x0a].ai_behavior = 0xc2;
    ch16_units[0x11].ai_behavior = 0xff;
    ch16_units[0x18].ai_behavior = 0x40;
    ch16_units[0x19].ai_behavior = 0x8b;
    fdps_chapter_16_event_enemies_advance_for_turn(0);
    CHECK_EQ(ch16_units[0x0a].ai_behavior, 0xc0);
    CHECK_EQ(ch16_units[0x11].ai_behavior, 0xf0);
    CHECK_EQ(ch16_units[0x18].ai_behavior, 0x40);
    CHECK_EQ(ch16_units[0x19].ai_behavior, 0x80);
}

/* One byte of one record moves and nothing either side of it does.  Every byte
   of the block is stamped 0x55 first, so a store that landed at +0x33 or +0x35
   is visible, and 0x55's low nibble is not already 0, so the write that should
   happen is visible too.  Both ends of the running range are checked, because
   the stride the store is indexed by is the IMUL 0x50 inside
   fdps_get_unit_record and an error in it shows up furthest from the base. */
static void ch16_touches_no_neighbouring_byte(void)
{
    unsigned char *bytes;
    int i;

    bytes = (unsigned char *) ch16_units;
    for (i = 0; i < (int) sizeof(ch16_units); i++) {
        bytes[i] = 0x55;
    }
    data_fdps_map_unit_array_ptr = bytes;
    data_fdps_battle_turn_counter = CH16_RELEASE_TURN;

    fdps_chapter_16_event_enemies_advance_for_turn(0);

    CHECK_EQ(bytes[0x19 * 0x50 + 0x34], 0x50);
    CHECK_EQ(bytes[0x19 * 0x50 + 0x33], 0x55);
    CHECK_EQ(bytes[0x19 * 0x50 + 0x35], 0x55);
    CHECK_EQ(bytes[0x22 * 0x50 + 0x34], 0x50);
    CHECK_EQ(bytes[0x22 * 0x50 + 0x33], 0x55);
    CHECK_EQ(bytes[0x22 * 0x50 + 0x35], 0x55);
    CHECK_EQ(bytes[0x18 * 0x50 + 0x34], 0x55);
    CHECK_EQ(bytes[0x23 * 0x50 + 0x34], 0x55);

    for (i = 0; i < (int) sizeof(ch16_units); i++) {
        bytes[i] = 0x55;
    }
    data_fdps_battle_turn_counter = CH16_SHIPPED_SECOND_TURN;

    fdps_chapter_16_event_enemies_advance_for_turn(0);

    CHECK_EQ(bytes[0x0a * 0x50 + 0x34], 0x50);
    CHECK_EQ(bytes[0x0a * 0x50 + 0x33], 0x55);
    CHECK_EQ(bytes[0x0a * 0x50 + 0x35], 0x55);
    CHECK_EQ(bytes[0x19 * 0x50 + 0x34], 0x50);
    CHECK_EQ(bytes[0x19 * 0x50 + 0x33], 0x55);
    CHECK_EQ(bytes[0x19 * 0x50 + 0x35], 0x55);
    CHECK_EQ(bytes[0x09 * 0x50 + 0x34], 0x55);
    CHECK_EQ(bytes[0x1a * 0x50 + 0x34], 0x55);
}

/* Nothing guards either loop -- the only compare in the body is the one on the
   turn counter -- so the handler has no one-shot latch and runs its loop every
   time it is called.  The latch slot the one-shot handlers of this family use
   is put up before the call and the range still moves, and the slot is
   asserted unchanged because a handler that had grown a latch would have
   written it.  A second call on the same turn is made too: the merge is
   idempotent, so it must leave the same values. */
static void ch16_has_no_one_shot_latch(void)
{
    stage_ch16_units(CH16_RELEASE_TURN, 0x52);
    data_fdps_map_cell_event_triggered_flags[CH15_LATCH_SLOT] = 1;

    fdps_chapter_16_event_enemies_advance_for_turn(0);
    CHECK_EQ(ch16_units[CH16_TURN5_FIRST_INDEX].ai_behavior, 0x50);
    CHECK_EQ(ch16_units[CH16_TURN5_LAST_INDEX].ai_behavior, 0x50);
    CHECK_EQ(data_fdps_map_cell_event_triggered_flags[CH15_LATCH_SLOT], 1);

    stage_ch16_units(CH16_RELEASE_TURN, 0x52);
    fdps_chapter_16_event_enemies_advance_for_turn(0);
    CHECK_EQ(ch16_units[CH16_TURN5_FIRST_INDEX].ai_behavior, 0x50);
    CHECK_EQ(ch16_units[CH16_TURN5_LAST_INDEX].ai_behavior, 0x50);

    fdps_chapter_16_event_enemies_advance_for_turn(0);
    CHECK_EQ(ch16_units[CH16_TURN5_FIRST_INDEX].ai_behavior, 0x50);
    CHECK_EQ(ch16_units[CH16_TURN5_LAST_INDEX].ai_behavior, 0x50);
    CHECK_EQ(ch16_units[CH16_TURN5_LAST_INDEX + 1].ai_behavior, 0x52);
}

/* The incoming argument slot is overwritten with 0 at 0003802c before the turn
   counter is even read, and never read back, so the index the dispatcher
   passes cannot reach the result and cannot pick a branch either.  The
   turn-event runner is the only path this slot is reached by in the shipped
   data and it pushes a literal 0 at 0002e13e; the values passed here are that
   0, an index inside each range, one just past the upper range, and -1 and
   30000, which are the ones an argument-driven handler would betray itself
   on. */
static void ch16_ignores_the_unit_index_argument(void)
{
    static int arguments[6] = {0, 0x0a, 0x19, 0x23, -1, 30000};
    int i;

    for (i = 0; i < 6; i++) {
        stage_ch16_units(CH16_RELEASE_TURN, 0x52);
        fdps_chapter_16_event_enemies_advance_for_turn(arguments[i]);
        CHECK_EQ(ch16_units[CH16_TURN5_FIRST_INDEX].ai_behavior, 0x50);
        CHECK_EQ(ch16_units[CH16_TURN5_LAST_INDEX].ai_behavior, 0x50);
        CHECK_EQ(ch16_units[CH16_OTHER_FIRST_INDEX].ai_behavior, 0x52);

        stage_ch16_units(CH16_SHIPPED_SECOND_TURN, 0x52);
        fdps_chapter_16_event_enemies_advance_for_turn(arguments[i]);
        CHECK_EQ(ch16_units[CH16_OTHER_FIRST_INDEX].ai_behavior, 0x50);
        CHECK_EQ(ch16_units[CH16_OTHER_LAST_INDEX].ai_behavior, 0x50);
        CHECK_EQ(ch16_units[CH16_TURN5_LAST_INDEX].ai_behavior, 0x52);
    }
}

/* Chapter 17's turn-scheduled reinforcement at 00038110, from here down.
 *
 * The handler's whole body is one call, and none of the three values it hands
 * fdps_deploy_wave -- PUSH dword ptr [0x00069cf4], the counter less seven, and
 * a zeroed EAX, at 00038123..0003812f -- is left anywhere afterwards.  So the
 * only way to see any of them is to let the deployment happen, and these cases
 * run fdps_deploy_wave for real, then read back what landed where:
 *
 *   MAP00.COD  record 0 (18, 0)  record 1 (22, 12)  record 2 (8, 10)
 *   MAP01.COD  record 0 (9, 4)
 *
 * -- the same records tests/deploy.c's own wave cases and the chapter 10 cases
 * in tests/chevt2.c expect, staged through tests/gamefile.lst.  That function
 * opens ICON.CEL and FIELD.VFS for itself and a run without them would not
 * fail a check, it would hang in fdps_wait_any_key, so every case here skips
 * itself when they are not there.
 *
 * Which wave the spawn table's records carry is staged rather than read from
 * map16.dat, because what is under test is the arithmetic that picks a wave
 * and not what chapter 17 happens to have tagged: a record is planted at each
 * of the waves either side of the one the turn should ask for, so asking for
 * the wrong one is visible as a different character id on different tiles.
 *
 * The unit array is malloc'd rather than staged into a static block, because a
 * deployment reallocs it: a static block handed to realloc is not a smaller
 * fixture, it is undefined behaviour.  It is never freed, for the same reason
 * tests/deploy.c does not free it -- the block moves under the global on every
 * deployment.
 */

/* What the handler subtracts from the turn counter: SUB EAX,0x7 at 0003812b.
   Turns are written here as turn numbers and the wave each should reach is
   derived from them, so a test that agreed with a wrong offset would have to
   disagree with the turn the map schedules. */
#define CH17_WAVE_TURN_OFFSET 7

/* The two turns map16.dat's turn-event table names for this slot, {turn 8,
   slot 24, phase 0} and {turn 9, slot 24, phase 0}, which are the turns that
   reach waves 1 and 2. */
#define CH17_FIRST_SCHEDULED_TURN 8
#define CH17_SECOND_SCHEDULED_TURN 9

/* The turn whose key is wave 0, the group the map opens with.  Nothing
   schedules it; it is here because it is the value a clamped subtraction would
   turn every earlier turn into. */
#define CH17_WAVE_ZERO_TURN 7

/* A map big enough for MAP00.COD's records, which reach (22, 12).  The tile
   search fdps_deploy_unit runs on a place_exact of 0 walks the whole grid, and
   the marking pass writes at a unit's own tile with no bound, so the grid has
   to cover the coordinates the real placement file names. */
#define CH17_GRID_W 32
#define CH17_GRID_H 16

/* Where the deployment records start in the MAP%02d.DAT block and where its
   header keeps their count: ADD EAX,0x83 at 00023463 and MOV AL,byte ptr
   [EAX+0x2] at 000238bb. */
#define CH17_SPAWN_TABLE_RECORD_BASE 0x83
#define CH17_SPAWN_TABLE_COUNT_OFFSET 2

/* The scene-layer offsets fdps_map_load_tile_info reads through: the tile
   map's width word at +7 with its 16-bit ids from +0xb, the attribute table's
   4-byte rows from +0x11, and the event layer's width at +7 with its cells
   from +0x10 (src/maptile.c). */
#define CH17_TILE_MAP_WIDTH_OFFSET 7
#define CH17_TILE_MAP_IDS_OFFSET 0xb
#define CH17_TILE_ATTR_ROWS_OFFSET 0x11
#define CH17_EVENT_LAYER_WIDTH_OFFSET 7
#define CH17_EVENT_LAYER_CELLS_OFFSET 0x10

/* A terrain code the deployment search accepts and one it rejects, either side
   of the CMP EAX,0x5 / JGE at 00023404. */
#define CH17_TERRAIN_WALKABLE 1
#define CH17_TERRAIN_BLOCKED 5

#define CH17_TILE_ATTR_ROWS 16
#define CH17_CHAR_TABLE_ROWS 8
#define CH17_ENEMY_TABLE_ROWS 8

/* Every item id is a valid index into the staged item table, 0xff included:
   fdps_unit_recompute_combat_stats follows an equipped flag into
   fdps_get_item_record without a bounds check. */
#define CH17_ITEM_TABLE_ROWS 256
#define CH17_ITEM_ID_NONE 0xff

/* The unit array's stride, as the deployment path allocates it: PUSH 0x50 at
   000232cc. */
#define CH17_UNIT_STRIDE 0x50

/* The three staged deployment records and the wave each is tagged with, one at
   each of the waves the two scheduled turns reach and one below them.  The
   character ids are arbitrary and only have to differ, so that which record
   was deployed is readable off the unit. */
#define CH17_WAVE0_RECORD 0
#define CH17_WAVE1_RECORD 1
#define CH17_WAVE2_RECORD 2
#define CH17_WAVE0_CHAR_ID 5
#define CH17_WAVE1_CHAR_ID 6
#define CH17_WAVE2_CHAR_ID 7

static unsigned char ch17_grid[4 + CH17_GRID_W * CH17_GRID_H * 2];
static unsigned char ch17_spawn_table[CH17_SPAWN_TABLE_RECORD_BASE + 8 * 0x1a];
static unsigned char ch17_tile_map[CH17_TILE_MAP_IDS_OFFSET +
                                   CH17_GRID_W * CH17_GRID_H * 2];
static unsigned char ch17_tile_attr[CH17_TILE_ATTR_ROWS_OFFSET +
                                    CH17_TILE_ATTR_ROWS * 4];
static unsigned char ch17_event_layer[CH17_EVENT_LAYER_CELLS_OFFSET +
                                      CH17_GRID_W * CH17_GRID_H];
static struct fdps_character_base_record ch17_char_base[CH17_CHAR_TABLE_ROWS];
static struct fdps_character_growth ch17_growth[CH17_CHAR_TABLE_ROWS];
static struct fdps_enemy_data ch17_enemy[CH17_ENEMY_TABLE_ROWS];
static struct fdps_item_effect ch17_items[CH17_ITEM_TABLE_ROWS];

static int ch17_files_checked = 0;
static int ch17_files_ready = 0;

/* ICON.CEL is read by fdps_cache_cel_sprite_group, which takes a fixed
   0x2970-byte bite out of the sheet's offset table, so a shorter file is one
   that reader runs off the end of rather than a smaller fixture.  FIELD.VFS
   cannot be stood in for at all: a missing container sends fdps_deploy_wave
   into fdps_wait_any_key and a missing member into exit(1). */
static void ch17_ensure_game_files(void)
{
    FILE *fp;
    long size;

    if (ch17_files_checked) {
        return;
    }
    ch17_files_checked = 1;

    fp = fopen("ICON.CEL", "rb");
    if (fp == NULL) {
        return;
    }
    fseek(fp, 0, SEEK_END);
    size = ftell(fp);
    fclose(fp);
    if (size < (long) (15 + 0x2970)) {
        return;
    }

    fp = fopen("FIELD.VFS", "rb");
    if (fp == NULL) {
        return;
    }
    fclose(fp);
    ch17_files_ready = 1;
}

static void ch17_zero_bytes(void *block, int count)
{
    unsigned char *bytes;
    int i;

    bytes = (unsigned char *) block;
    for (i = 0; i < count; i++) {
        bytes[i] = 0;
    }
}

static struct fdps_char_spawn_record *ch17_spawn_at(int index)
{
    return (struct fdps_char_spawn_record *)
           (ch17_spawn_table + CH17_SPAWN_TABLE_RECORD_BASE) + index;
}

/* One deployment record with nothing equipped and nothing carried, tagged with
   the wave it belongs to. */
static void ch17_set_spawn(int index, int char_id, int wave_no)
{
    ch17_spawn_at(index)->char_id = (unsigned char) char_id;
    ch17_spawn_at(index)->level = 1;
    ch17_spawn_at(index)->side = 2;
    ch17_spawn_at(index)->equipped_item_0 = CH17_ITEM_ID_NONE;
    ch17_spawn_at(index)->equipped_item_1 = CH17_ITEM_ID_NONE;
    ch17_spawn_at(index)->carried_items[0] = CH17_ITEM_ID_NONE;
    ch17_spawn_at(index)->carried_items[1] = CH17_ITEM_ID_NONE;
    ch17_spawn_at(index)->carried_items[2] = CH17_ITEM_ID_NONE;
    ch17_spawn_at(index)->carried_items[3] = CH17_ITEM_ID_NONE;
    ch17_spawn_at(index)->carried_items[4] = CH17_ITEM_ID_NONE;
    ch17_spawn_at(index)->carried_items[5] = CH17_ITEM_ID_NONE;
    ch17_spawn_at(index)->wave_no = (unsigned char) wave_no;
}

static void ch17_set_terrain(int tile_id, int terrain)
{
    struct fdps_tile_attr_entry *rows;

    rows = (struct fdps_tile_attr_entry *)
           (ch17_tile_attr + CH17_TILE_ATTR_ROWS_OFFSET);
    rows[tile_id].terrain_type = (unsigned char) terrain;
}

/* The cell of the tile map at (tile_x, tile_y), so one tile can be given an id
   whose attribute row says the deployment search must not use it. */
static void ch17_set_tile_id(int tile_x, int tile_y, int tile_id)
{
    short *tile_ids;

    tile_ids = (short *) (ch17_tile_map + CH17_TILE_MAP_IDS_OFFSET);
    tile_ids[tile_y * CH17_GRID_W + tile_x] = (short) tile_id;
}

static struct fdps_unit_record *ch17_unit(int unit_index)
{
    return (struct fdps_unit_record *)
           (data_fdps_map_unit_array_ptr + unit_index * CH17_UNIT_STRIDE);
}

/* A blank walkable map with one unit already on it, the chapter global on map
   0 and the turn counter on the given turn, plus three deployment records
   tagged waves 0, 1 and 2 at table indices 0, 1 and 2.  Placement record and
   table index are the same number, so which record was deployed is readable
   twice over: off the character id and off the tile it landed on.

   The unit already on the map stands at (0, 0), clear of every placement
   record these cases read back. */
static void ch17_stage(int battle_turn)
{
    int i;

    ch17_zero_bytes(ch17_grid, (int) sizeof(ch17_grid));
    ch17_zero_bytes(ch17_spawn_table, (int) sizeof(ch17_spawn_table));
    ch17_zero_bytes(ch17_tile_map, (int) sizeof(ch17_tile_map));
    ch17_zero_bytes(ch17_tile_attr, (int) sizeof(ch17_tile_attr));
    ch17_zero_bytes(ch17_event_layer, (int) sizeof(ch17_event_layer));
    ch17_zero_bytes(ch17_char_base, (int) sizeof(ch17_char_base));
    ch17_zero_bytes(ch17_growth, (int) sizeof(ch17_growth));
    ch17_zero_bytes(ch17_enemy, (int) sizeof(ch17_enemy));
    ch17_zero_bytes(ch17_items, (int) sizeof(ch17_items));

    *(short *) ch17_grid = (short) CH17_GRID_W;
    *(short *) (ch17_grid + 2) = (short) CH17_GRID_H;

    *(short *) (ch17_tile_map + CH17_TILE_MAP_WIDTH_OFFSET) =
        (short) CH17_GRID_W;
    for (i = 0; i < CH17_TILE_ATTR_ROWS; i++) {
        ch17_set_terrain(i, CH17_TERRAIN_WALKABLE);
    }

    *(short *) (ch17_event_layer + CH17_EVENT_LAYER_WIDTH_OFFSET) =
        (short) CH17_GRID_W;

    data_fdps_battle_move_grid_ptr = ch17_grid;
    data_fdps_tile_event_data_table_ptr = ch17_spawn_table;
    data_fdps_scene_layer_tile_map_ptrs[0] = ch17_tile_map;
    data_fdps_scene_layer_tile_attr_ptr[0] = ch17_tile_attr;
    data_fdps_map_cell_event_code_layer_ptr = ch17_event_layer;
    data_fdps_battle_character_base_table_ptr =
        (unsigned char *) ch17_char_base;
    data_fdps_battle_character_growth_table_ptr = (unsigned char *) ch17_growth;
    data_fdps_battle_enemy_data_table_ptr = (unsigned char *) ch17_enemy;
    data_fdps_item_effect_table_ptr = (unsigned char *) ch17_items;

    data_fdps_map_unit_array_ptr = (unsigned char *) malloc(CH17_UNIT_STRIDE);
    ch17_zero_bytes(data_fdps_map_unit_array_ptr, CH17_UNIT_STRIDE);
    data_fdps_map_unit_count = 1;

    ch17_spawn_table[CH17_SPAWN_TABLE_COUNT_OFFSET] = 3;
    ch17_set_spawn(CH17_WAVE0_RECORD, CH17_WAVE0_CHAR_ID, 0);
    ch17_set_spawn(CH17_WAVE1_RECORD, CH17_WAVE1_CHAR_ID, 1);
    ch17_set_spawn(CH17_WAVE2_RECORD, CH17_WAVE2_CHAR_ID, 2);

    data_fdps_chapter_current_chapter_id = 0;
    data_fdps_battle_turn_counter = battle_turn;
}

/* The fields the cases below read back and the stride they are indexed by.
   Every one of them would agree with itself while addressing another byte if
   the layout were wrong. */
static void ch17_record_shape_matches_the_offsets(void)
{
    CHECK_EQ((int) sizeof(struct fdps_unit_record), CH17_UNIT_STRIDE);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, pos_x), 0);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, pos_y), 1);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, char_id), 8);
    CHECK_EQ((int) offsetof(struct fdps_char_spawn_record, wave_no), 0x15);
}

/* The wave asked for is the turn counter less seven and the turns that reach
   waves 1 and 2 are the 8 and 9 map16.dat schedules.  Three records are laid
   down at waves 0, 1 and 2 and exactly one of them deploys each time: turn 8
   brings on the wave-1 record, which is table index 1 and lands on MAP00.COD
   record 1 at (22, 12), and turn 9 the wave-2 record, table index 2 at
   (8, 10).  An offset of 8 or 6 would deploy one of the neighbours instead,
   and both the character id and the tile would say so. */
static void ch17_deploys_the_wave_the_turn_is_due(void)
{
    ch17_ensure_game_files();
    if (!ch17_files_ready) {
        return;
    }

    ch17_stage(CH17_FIRST_SCHEDULED_TURN);
    fdps_chapter_17_event_deploy_wave_for_turn(0);
    CHECK_EQ(data_fdps_map_unit_count, 2);
    CHECK_EQ((int) ch17_unit(1)->char_id, CH17_WAVE1_CHAR_ID);
    CHECK_EQ((int) ch17_unit(1)->pos_x, 22);
    CHECK_EQ((int) ch17_unit(1)->pos_y, 12);

    ch17_stage(CH17_SECOND_SCHEDULED_TURN);
    fdps_chapter_17_event_deploy_wave_for_turn(0);
    CHECK_EQ(data_fdps_map_unit_count, 2);
    CHECK_EQ((int) ch17_unit(1)->char_id, CH17_WAVE2_CHAR_ID);
    CHECK_EQ((int) ch17_unit(1)->pos_x, 8);
    CHECK_EQ((int) ch17_unit(1)->pos_y, 10);
}

/* The subtraction really is a subtraction and not a lookup keyed on the two
   turns the map happens to schedule: turn 7 is not in map16.dat's turn table
   at all, and it asks for wave 0 -- the wave-0 record, table index 0, on
   MAP00.COD record 0 at (18, 0).  A turn above them is put through as well:
   14 asks for wave 7, which no staged record carries, and the walk matches
   nothing while the file open and load still happen, so the key really is
   unbounded at the top too and not a choice between the map's two turns. */
static void ch17_wave_key_follows_the_counter(void)
{
    ch17_ensure_game_files();
    if (!ch17_files_ready) {
        return;
    }

    ch17_stage(CH17_WAVE_ZERO_TURN);
    fdps_chapter_17_event_deploy_wave_for_turn(0);
    CHECK_EQ(data_fdps_map_unit_count, 2);
    CHECK_EQ((int) ch17_unit(1)->char_id, CH17_WAVE0_CHAR_ID);
    CHECK_EQ((int) ch17_unit(1)->pos_x, 18);
    CHECK_EQ((int) ch17_unit(1)->pos_y, 0);

    ch17_stage(CH17_WAVE_ZERO_TURN + CH17_WAVE_TURN_OFFSET);
    fdps_chapter_17_event_deploy_wave_for_turn(0);
    CHECK_EQ(data_fdps_map_unit_count, 1);
}

/* The key is not clamped at the bottom, and this is the one that would pass
   just as happily if it were, were it not asserted: every turn below 7 gives a
   negative wave, which matches no record because a record's wave byte is
   unsigned, so nothing at all is deployed.  A clamp to 0 would instead match
   the wave-0 record and put the map's opening army down a second time.  Turns
   1, 6 and the 0 a counter never legitimately holds are all put through it. */
static void ch17_early_turns_deploy_nothing(void)
{
    static int early_turns[3] = {0, 1, 6};
    int i;

    ch17_ensure_game_files();
    if (!ch17_files_ready) {
        return;
    }

    for (i = 0; i < 3; i++) {
        ch17_stage(early_turns[i]);
        fdps_chapter_17_event_deploy_wave_for_turn(0);
        CHECK_EQ(data_fdps_map_unit_count, 1);
        CHECK_EQ((int) ch17_unit(0)->char_id, 0);
    }
}

/* The map the wave is deployed under is read from
   data_fdps_chapter_current_chapter_id at the call site and is not a literal:
   the same wave-0 record placed while that global says 1 lands on MAP01.COD's
   record 0 at (9, 4) instead of MAP00.COD's (18, 0). */
static void ch17_map_number_comes_from_the_chapter_global(void)
{
    ch17_ensure_game_files();
    if (!ch17_files_ready) {
        return;
    }

    ch17_stage(CH17_WAVE_ZERO_TURN);
    data_fdps_chapter_current_chapter_id = 1;

    fdps_chapter_17_event_deploy_wave_for_turn(0);

    CHECK_EQ(data_fdps_map_unit_count, 2);
    CHECK_EQ((int) ch17_unit(1)->char_id, CH17_WAVE0_CHAR_ID);
    CHECK_EQ((int) ch17_unit(1)->pos_x, 9);
    CHECK_EQ((int) ch17_unit(1)->pos_y, 4);
}

/* The placement flag is 0 -- XOR EAX,EAX / PUSH EAX at 00038123 -- so the
   reinforcements are put on the nearest free walkable tile to their placement
   record rather than on the record's own tile.  MAP00.COD record 1 names
   (22, 12); giving that one cell a tile id whose attribute row is terrain 5
   takes it out of the search, and the unit lands one tile away.  A flag of 1
   would drop it on (22, 12) regardless of the terrain there.

   (22, 13) is which of the four tiles at distance 1 it lands on, because the
   scan is row-major over the whole grid and a tie is accepted (CMP EAX,
   [EBP-0x14] / JLE at 000233e6), so the last candidate at the best distance
   wins. */
static void ch17_places_on_the_nearest_free_tile(void)
{
    ch17_ensure_game_files();
    if (!ch17_files_ready) {
        return;
    }

    ch17_stage(CH17_FIRST_SCHEDULED_TURN);
    ch17_set_tile_id(22, 12, 1);
    ch17_set_terrain(1, CH17_TERRAIN_BLOCKED);

    fdps_chapter_17_event_deploy_wave_for_turn(0);

    CHECK_EQ(data_fdps_map_unit_count, 2);
    CHECK_EQ((int) ch17_unit(1)->char_id, CH17_WAVE1_CHAR_ID);
    CHECK_EQ((int) ch17_unit(1)->pos_x, 22);
    CHECK_EQ((int) ch17_unit(1)->pos_y, 13);
}

/* Nothing guards the call: there is no compare anywhere in the body and no
   latch is written, so a second firing on the same turn deploys the same wave
   again rather than being refused.  The slot the one-shot handlers of this
   family latch is also put up beforehand and the wave still arrives, and the
   slot is asserted unchanged because a handler that had grown a latch would
   have written it. */
static void ch17_has_no_one_shot_latch(void)
{
    ch17_ensure_game_files();
    if (!ch17_files_ready) {
        return;
    }

    ch17_stage(CH17_FIRST_SCHEDULED_TURN);
    data_fdps_map_cell_event_triggered_flags[CH15_LATCH_SLOT] = 1;

    fdps_chapter_17_event_deploy_wave_for_turn(0);
    CHECK_EQ(data_fdps_map_unit_count, 2);
    CHECK_EQ(data_fdps_map_cell_event_triggered_flags[CH15_LATCH_SLOT], 1);

    fdps_chapter_17_event_deploy_wave_for_turn(0);
    CHECK_EQ(data_fdps_map_unit_count, 3);
    CHECK_EQ((int) ch17_unit(2)->char_id, CH17_WAVE1_CHAR_ID);
    CHECK_EQ(data_fdps_map_cell_event_triggered_flags[CH15_LATCH_SLOT], 1);
}

/* The incoming argument slot is overwritten with 0 at 0003811c before the turn
   counter is read, and never read back, so the index the dispatcher passes
   cannot reach the wave asked for, the map asked for or the placement flag.
   The turn-event runner is the only path this slot is reached by in the
   shipped data and it pushes a literal 0; the values passed here are that 0,
   an index that names the unit already on the map, one past the array, and -1
   and 30000, which are the ones an argument-driven handler would betray itself
   on. */
static void ch17_ignores_the_unit_index_argument(void)
{
    static int arguments[5] = {0, 1, 2, -1, 30000};
    int i;

    ch17_ensure_game_files();
    if (!ch17_files_ready) {
        return;
    }

    for (i = 0; i < 5; i++) {
        ch17_stage(CH17_FIRST_SCHEDULED_TURN);
        fdps_chapter_17_event_deploy_wave_for_turn(arguments[i]);
        CHECK_EQ(data_fdps_map_unit_count, 2);
        CHECK_EQ((int) ch17_unit(1)->char_id, CH17_WAVE1_CHAR_ID);
        CHECK_EQ((int) ch17_unit(1)->pos_x, 22);
        CHECK_EQ((int) ch17_unit(1)->pos_y, 12);
    }
}

/* The chapter 18 handler is the chapter 17 one with the subtraction taken out,
   so the cases below stand on the same fixture: ch17_stage lays down three
   deployment records tagged waves 0, 1 and 2 at table indices 0, 1 and 2 on a
   blank walkable map with one unit already on it, and MAP00.COD's own records
   0, 1 and 2 name (18, 0), (22, 12) and (8, 10).  What changes is which turn
   reaches which record: chapter 17 subtracts seven and chapter 18 subtracts
   nothing, so here the turn IS the wave.  Sharing the fixture is what makes
   that the only difference these cases can be reading. */

/* The turns that name the fixture's three records, being the wave numbers
   themselves.  Chapter 18's own schedule runs 4..11 and 13; those waves are in
   map17.dat rather than in this fixture, and CH18_SCHEDULED_TURN_NO_RECORD is
   one of them, put through to show an unmatched key is carried rather than
   caught. */
#define CH18_WAVE0_TURN 0
#define CH18_WAVE1_TURN 1
#define CH18_WAVE2_TURN 2
#define CH18_UNMATCHED_TURN 3
#define CH18_SCHEDULED_TURN_NO_RECORD 13

/* The wave asked for is the turn counter itself with nothing taken off it.
   Turn 1 brings on the wave-1 record, table index 1, which lands on MAP00.COD
   record 1 at (22, 12), and turn 2 the wave-2 record, index 2 at (8, 10).
   This is the case chapter 17's offset would fail: subtracting seven from
   either turn gives a negative key that matches no record and deploys nothing,
   and any other offset would deploy the neighbouring record, which both the
   character id and the tile would say. */
static void ch18_deploys_the_wave_the_turn_names(void)
{
    ch17_ensure_game_files();
    if (!ch17_files_ready) {
        return;
    }

    ch17_stage(CH18_WAVE1_TURN);
    fdps_chapter_18_event_deploy_wave_for_turn(0);
    CHECK_EQ(data_fdps_map_unit_count, 2);
    CHECK_EQ((int) ch17_unit(1)->char_id, CH17_WAVE1_CHAR_ID);
    CHECK_EQ((int) ch17_unit(1)->pos_x, 22);
    CHECK_EQ((int) ch17_unit(1)->pos_y, 12);

    ch17_stage(CH18_WAVE2_TURN);
    fdps_chapter_18_event_deploy_wave_for_turn(0);
    CHECK_EQ(data_fdps_map_unit_count, 2);
    CHECK_EQ((int) ch17_unit(1)->char_id, CH17_WAVE2_CHAR_ID);
    CHECK_EQ((int) ch17_unit(1)->pos_x, 8);
    CHECK_EQ((int) ch17_unit(1)->pos_y, 10);
}

/* The counter reaches the call unadjusted at the bottom of its range too: turn
   0 asks for wave 0, the group a map opens with, and brings the wave-0 record
   on -- MAP00.COD record 0 at (18, 0).  An off-by-one either way would reach
   the wave-1 record or no record at all. */
static void ch18_turn_zero_asks_for_wave_zero(void)
{
    ch17_ensure_game_files();
    if (!ch17_files_ready) {
        return;
    }

    ch17_stage(CH18_WAVE0_TURN);
    fdps_chapter_18_event_deploy_wave_for_turn(0);
    CHECK_EQ(data_fdps_map_unit_count, 2);
    CHECK_EQ((int) ch17_unit(1)->char_id, CH17_WAVE0_CHAR_ID);
    CHECK_EQ((int) ch17_unit(1)->pos_x, 18);
    CHECK_EQ((int) ch17_unit(1)->pos_y, 0);
}

/* A key that matches nothing is carried through rather than caught or folded
   onto a wave that does exist: turn 3 and turn 13 both walk the table, match
   no record and deploy nobody.  A clamp or a fallback to wave 0 would put the
   map's opening group down a second time and the count would say so.  Turn 13
   is one of chapter 18's own nine scheduled turns, so the top of its real
   range is unbounded here as well. */
static void ch18_unmatched_turns_deploy_nothing(void)
{
    ch17_ensure_game_files();
    if (!ch17_files_ready) {
        return;
    }

    ch17_stage(CH18_UNMATCHED_TURN);
    fdps_chapter_18_event_deploy_wave_for_turn(0);
    CHECK_EQ(data_fdps_map_unit_count, 1);

    ch17_stage(CH18_SCHEDULED_TURN_NO_RECORD);
    fdps_chapter_18_event_deploy_wave_for_turn(0);
    CHECK_EQ(data_fdps_map_unit_count, 1);
}

/* The map the wave is deployed under is read from
   data_fdps_chapter_current_chapter_id at the call site and is not a literal:
   the same wave-0 record placed while that global says 1 lands on MAP01.COD's
   record 0 at (9, 4) instead of MAP00.COD's (18, 0). */
static void ch18_map_number_comes_from_the_chapter_global(void)
{
    ch17_ensure_game_files();
    if (!ch17_files_ready) {
        return;
    }

    ch17_stage(CH18_WAVE0_TURN);
    data_fdps_chapter_current_chapter_id = 1;

    fdps_chapter_18_event_deploy_wave_for_turn(0);

    CHECK_EQ(data_fdps_map_unit_count, 2);
    CHECK_EQ((int) ch17_unit(1)->char_id, CH17_WAVE0_CHAR_ID);
    CHECK_EQ((int) ch17_unit(1)->pos_x, 9);
    CHECK_EQ((int) ch17_unit(1)->pos_y, 4);
}

/* The placement flag is 0 -- XOR EAX,EAX / PUSH EAX at 00038163 -- so the
   reinforcements are put on the nearest free walkable tile to their placement
   record rather than on the record's own tile.  MAP00.COD record 1 names
   (22, 12); giving that one cell a tile id whose attribute row is terrain 5
   takes it out of the search and the unit lands one tile away.  A flag of 1
   would drop it on (22, 12) regardless of the terrain there.

   (22, 13) is which of the four tiles at distance 1 it lands on, because the
   scan is row-major over the whole grid and a tie is accepted, so the last
   candidate at the best distance wins. */
static void ch18_places_on_the_nearest_free_tile(void)
{
    ch17_ensure_game_files();
    if (!ch17_files_ready) {
        return;
    }

    ch17_stage(CH18_WAVE1_TURN);
    ch17_set_tile_id(22, 12, 1);
    ch17_set_terrain(1, CH17_TERRAIN_BLOCKED);

    fdps_chapter_18_event_deploy_wave_for_turn(0);

    CHECK_EQ(data_fdps_map_unit_count, 2);
    CHECK_EQ((int) ch17_unit(1)->char_id, CH17_WAVE1_CHAR_ID);
    CHECK_EQ((int) ch17_unit(1)->pos_x, 22);
    CHECK_EQ((int) ch17_unit(1)->pos_y, 13);
}

/* Nothing guards the call: there is no compare anywhere in the body and no
   latch is written, so a second firing on the same turn deploys the same wave
   again rather than being refused.  The slot the one-shot handlers of this
   family latch is also put up beforehand and the wave still arrives, and the
   slot is asserted unchanged because a handler that had grown a latch would
   have written it. */
static void ch18_has_no_one_shot_latch(void)
{
    ch17_ensure_game_files();
    if (!ch17_files_ready) {
        return;
    }

    ch17_stage(CH18_WAVE1_TURN);
    data_fdps_map_cell_event_triggered_flags[CH15_LATCH_SLOT] = 1;

    fdps_chapter_18_event_deploy_wave_for_turn(0);
    CHECK_EQ(data_fdps_map_unit_count, 2);
    CHECK_EQ(data_fdps_map_cell_event_triggered_flags[CH15_LATCH_SLOT], 1);

    fdps_chapter_18_event_deploy_wave_for_turn(0);
    CHECK_EQ(data_fdps_map_unit_count, 3);
    CHECK_EQ((int) ch17_unit(2)->char_id, CH17_WAVE1_CHAR_ID);
    CHECK_EQ(data_fdps_map_cell_event_triggered_flags[CH15_LATCH_SLOT], 1);
}

/* The incoming argument slot is overwritten with 0 at 0003815c before either
   global is read, and never read back, so the index the dispatcher passes
   cannot reach the wave asked for, the map asked for or the placement flag.
   The turn-event runner is the only path this slot is reached by in the
   shipped data and it pushes a literal 0; the values passed here are that 0,
   an index that names the unit already on the map, one past the array, and -1
   and 30000, which are the ones an argument-driven handler would betray itself
   on. */
static void ch18_ignores_the_unit_index_argument(void)
{
    static int arguments[5] = {0, 1, 2, -1, 30000};
    int i;

    ch17_ensure_game_files();
    if (!ch17_files_ready) {
        return;
    }

    for (i = 0; i < 5; i++) {
        ch17_stage(CH18_WAVE1_TURN);
        fdps_chapter_18_event_deploy_wave_for_turn(arguments[i]);
        CHECK_EQ(data_fdps_map_unit_count, 2);
        CHECK_EQ((int) ch17_unit(1)->char_id, CH17_WAVE1_CHAR_ID);
        CHECK_EQ((int) ch17_unit(1)->pos_x, 22);
        CHECK_EQ((int) ch17_unit(1)->pos_y, 12);
    }
}

/* ------------------------------------------------------------------
 * The chapter 19 arrival handler at 00038180.
 *
 * It is the chapter 17 and 18 handlers' shape with the wave key made a literal
 * and a spoken line added, so the cases below stand on the same fixture:
 * ch17_stage lays down three deployment records tagged waves 0, 1 and 2 at
 * table indices 0, 1 and 2 on a blank walkable map with one unit already on
 * it, and MAP00.COD's own records 0, 1 and 2 name (18, 0), (22, 12) and
 * (8, 10).  What the cases have to show is that the wave asked for is the
 * literal 1 and not a number taken off the turn counter, which is the only
 * arithmetic difference between this handler and its two neighbours, so the
 * counter is moved under it and the same record has to keep arriving.
 *
 * ONE THING IS ADDED TO THE FIXTURE: data_fdps_current_chapter_text_ptr, which
 * the chapter 17 and 18 cases never needed because those handlers do not
 * speak.  It is staged the way the smith cases above stage theirs, as a block
 * whose every entry is a lone -1 terminator: fdps_draw_text walks it, draws
 * nothing, touches no global and returns at once, which is what keeps a case
 * from painting the VGA aperture and standing a modal wait on a keyboard
 * nothing is typing at.
 *
 * WHICH TEXT ENTRY THE DRAW ASKS FOR IS NOT ASSERTED, for the reason the
 * chapter 3 section of tests/chevt1.c gives: the draw takes its whole effect
 * through pixels at the VGA aperture, keeps no state, and returns a cursor
 * this handler discards, so a unit test has nothing to read back.  The entry
 * id and the three colours are literals in the instruction stream (PUSH 0xa at
 * 000381b9 and PUSH 0xd0 / PUSH 0x0 / PUSH 0x6d at 000381aa, 000381a8 and
 * 000381a6) and the reviewer's reading of them is what stands behind the
 * emitted C.  What the cases do pin about the draw is that it does not stop
 * the deployment: every one of them runs the whole handler and reads the unit
 * back afterwards.
 *
 * The order of the two calls is not assertable here either, for the same
 * reason: what makes the deployment have to come first is that entry 10's
 * speaker code looks the arriving unit up on the map, and a fixture whose
 * entries are terminators carries no speaker code.  Reversing the calls in
 * src/chevt3.c would leave every case below green.
 * ------------------------------------------------------------------ */

/* The wave the handler asks for, PUSH 0x1 at 00038196, and a wave number no
   staged record carries, used to move the fixture's wave-1 tag onto another
   table index. */
#define CH19_ARRIVING_WAVE 1
#define CH19_UNUSED_WAVE 5

/* Turn counter values the cases sweep: the turn map18.dat schedules the slot
   for, the one chapter 18's shape would turn into wave 6, the one chapter
   17's would turn into a negative key, and a value no counter legitimately
   holds.  All four have to reach the same record. */
#define CH19_SCHEDULED_TURN 6
#define CH19_EARLY_TURN 1
#define CH19_LATE_TURN 20
#define CH19_ZERO_TURN 0

/* The chapter text block: 20 entries, which is what FDETXT19.TXT carries --
   its first offset is 40, and a block's offsets are 2 bytes each -- every one
   of them pointing at the same lone terminator so that a draw walks it, paints
   nothing and returns at once.  The real entry 10 opens with the speaker
   tokens -0x11 and 0x0b instead, which is exactly what a fixture must not
   carry: that code stands a modal wait on a keyboard nothing is typing at. */
#define CH19_TEXT_IDS 20
#define CH19_TEXT_EMPTY_AT 0x40
#define CH19_TEXT_BLOCK_BYTES (CH19_TEXT_EMPTY_AT + 2)
#define CH19_TEXT_END (-1)

static unsigned char ch19_text_block[CH19_TEXT_BLOCK_BYTES];

/* The chapter 17 fixture plus the text block the speech needs.  The turn
   counter ch17_stage sets is deliberately varied by the cases even though this
   handler must not read it. */
static void ch19_stage(int battle_turn)
{
    int text_id;

    ch17_stage(battle_turn);

    memset(ch19_text_block, 0, (size_t) CH19_TEXT_BLOCK_BYTES);
    *(short *) (ch19_text_block + CH19_TEXT_EMPTY_AT) = (short) CH19_TEXT_END;
    for (text_id = 0; text_id < CH19_TEXT_IDS; text_id++) {
        *(short *) (ch19_text_block + text_id * 2) = (short) CH19_TEXT_EMPTY_AT;
    }

    data_fdps_current_chapter_text_ptr = ch19_text_block;
}

/* The same fixture with the wave the handler asks for moved onto table index
   0, so the record it brings on is placed by MAP%02d.COD record 0 -- the one
   record both MAP00.COD and MAP01.COD are known to name, at (18, 0) and
   (9, 4).  The record that carried the tag is retagged to a wave nothing asks
   for, so exactly one record still matches. */
static void ch19_stage_arrival_at_record_zero(int battle_turn)
{
    ch19_stage(battle_turn);
    ch17_set_spawn(CH17_WAVE0_RECORD, CH17_WAVE0_CHAR_ID, CH19_ARRIVING_WAVE);
    ch17_set_spawn(CH17_WAVE1_RECORD, CH17_WAVE1_CHAR_ID, CH19_UNUSED_WAVE);
}

/* Wave 1 is what arrives, and it is the fixture's table index 1 -- MAP00.COD
   record 1 at (22, 12), character id 6.  Wave 0 would put the map's opening
   army down again at (18, 0) and wave 2 the record at (8, 10), so both the
   character id and the tile say which key was used. */
static void ch19_deploys_wave_one(void)
{
    ch17_ensure_game_files();
    if (!ch17_files_ready) {
        return;
    }

    ch19_stage(CH19_SCHEDULED_TURN);

    fdps_chapter_19_event_lancelot_joins(0);

    CHECK_EQ(data_fdps_map_unit_count, 2);
    CHECK_EQ((int) ch17_unit(1)->char_id, CH17_WAVE1_CHAR_ID);
    CHECK_EQ((int) ch17_unit(1)->pos_x, 22);
    CHECK_EQ((int) ch17_unit(1)->pos_y, 12);
}

/* The wave key is a literal and the battle turn counter is not read at all,
   which is the whole difference between this handler and the two above it.
   The counter is swept over four values and the same wave-1 record has to
   arrive every time: chapter 18's shape would ask for waves 6, 1, 20 and 0 and
   chapter 17's for -1, -6, 13 and -7, and only the turn 1 run of chapter 18's
   shape would agree with this fixture -- which is why more than one turn is
   put through. */
static void ch19_wave_key_is_a_literal_not_the_turn_counter(void)
{
    static int turns[4] = {CH19_SCHEDULED_TURN, CH19_EARLY_TURN,
                           CH19_LATE_TURN, CH19_ZERO_TURN};
    int i;

    ch17_ensure_game_files();
    if (!ch17_files_ready) {
        return;
    }

    for (i = 0; i < 4; i++) {
        ch19_stage(turns[i]);

        fdps_chapter_19_event_lancelot_joins(0);

        CHECK_EQ(data_fdps_map_unit_count, 2);
        CHECK_EQ((int) ch17_unit(1)->char_id, CH17_WAVE1_CHAR_ID);
        CHECK_EQ((int) ch17_unit(1)->pos_x, 22);
        CHECK_EQ((int) ch17_unit(1)->pos_y, 12);
    }
}

/* The map the wave is deployed under is read from
   data_fdps_chapter_current_chapter_id at the call site and is not a literal:
   the same record placed while that global says 0 lands on MAP00.COD's record
   0 at (18, 0) and while it says 1 on MAP01.COD's record 0 at (9, 4). */
static void ch19_map_number_comes_from_the_chapter_global(void)
{
    ch17_ensure_game_files();
    if (!ch17_files_ready) {
        return;
    }

    ch19_stage_arrival_at_record_zero(CH19_SCHEDULED_TURN);
    data_fdps_chapter_current_chapter_id = 0;

    fdps_chapter_19_event_lancelot_joins(0);

    CHECK_EQ(data_fdps_map_unit_count, 2);
    CHECK_EQ((int) ch17_unit(1)->char_id, CH17_WAVE0_CHAR_ID);
    CHECK_EQ((int) ch17_unit(1)->pos_x, 18);
    CHECK_EQ((int) ch17_unit(1)->pos_y, 0);

    ch19_stage_arrival_at_record_zero(CH19_SCHEDULED_TURN);
    data_fdps_chapter_current_chapter_id = 1;

    fdps_chapter_19_event_lancelot_joins(0);

    CHECK_EQ(data_fdps_map_unit_count, 2);
    CHECK_EQ((int) ch17_unit(1)->char_id, CH17_WAVE0_CHAR_ID);
    CHECK_EQ((int) ch17_unit(1)->pos_x, 9);
    CHECK_EQ((int) ch17_unit(1)->pos_y, 4);
}

/* The placement flag is 0, so the arriving unit is put on the nearest free
   walkable tile to its placement record rather than on the record's own tile.
   MAP00.COD record 1 names (22, 12); giving that one cell a tile id whose
   attribute row is terrain 5 takes it out of the search and the unit lands one
   tile away.  A flag of 1 would drop it on (22, 12) regardless of the terrain
   there, which is what would put the paladin on top of whatever is standing on
   his arrival tile.

   (22, 13) is which of the four tiles at distance 1 it lands on, because the
   scan is row-major over the whole grid and a tie is accepted, so the last
   candidate at the best distance wins. */
static void ch19_places_on_the_nearest_free_tile(void)
{
    ch17_ensure_game_files();
    if (!ch17_files_ready) {
        return;
    }

    ch19_stage(CH19_SCHEDULED_TURN);
    ch17_set_tile_id(22, 12, 1);
    ch17_set_terrain(1, CH17_TERRAIN_BLOCKED);

    fdps_chapter_19_event_lancelot_joins(0);

    CHECK_EQ(data_fdps_map_unit_count, 2);
    CHECK_EQ((int) ch17_unit(1)->char_id, CH17_WAVE1_CHAR_ID);
    CHECK_EQ((int) ch17_unit(1)->pos_x, 22);
    CHECK_EQ((int) ch17_unit(1)->pos_y, 13);
}

/* Nothing guards the calls: there is no compare anywhere in the body and no
   latch is written, so a second firing brings the same unit on again rather
   than being refused.  The slot the one-shot handlers of this family latch is
   put up beforehand and the arrival still happens, and the slot is asserted
   unchanged because a handler that had grown a latch would have written it. */
static void ch19_has_no_one_shot_latch(void)
{
    ch17_ensure_game_files();
    if (!ch17_files_ready) {
        return;
    }

    ch19_stage(CH19_SCHEDULED_TURN);
    data_fdps_map_cell_event_triggered_flags[CH15_LATCH_SLOT] = 1;

    fdps_chapter_19_event_lancelot_joins(0);
    CHECK_EQ(data_fdps_map_unit_count, 2);
    CHECK_EQ(data_fdps_map_cell_event_triggered_flags[CH15_LATCH_SLOT], 1);

    fdps_chapter_19_event_lancelot_joins(0);
    CHECK_EQ(data_fdps_map_unit_count, 3);
    CHECK_EQ((int) ch17_unit(2)->char_id, CH17_WAVE1_CHAR_ID);
    CHECK_EQ(data_fdps_map_cell_event_triggered_flags[CH15_LATCH_SLOT], 1);
}

/* The incoming argument slot is overwritten with 0 at 0003818c before either
   call and never read back, so the index the dispatcher passes cannot reach
   the wave asked for, the map asked for, the placement flag or the text entry
   spoken.  The turn-event runner is the only path this slot is reached by in
   the shipped data and it pushes a literal 0; the values passed here are that
   0, an index that names the unit already on the map, one past the array, and
   -1 and 30000, which are the ones an argument-driven handler would betray
   itself on. */
static void ch19_ignores_the_unit_index_argument(void)
{
    static int arguments[5] = {0, 1, 2, -1, 30000};
    int i;

    ch17_ensure_game_files();
    if (!ch17_files_ready) {
        return;
    }

    for (i = 0; i < 5; i++) {
        ch19_stage(CH19_SCHEDULED_TURN);

        fdps_chapter_19_event_lancelot_joins(arguments[i]);

        CHECK_EQ(data_fdps_map_unit_count, 2);
        CHECK_EQ((int) ch17_unit(1)->char_id, CH17_WAVE1_CHAR_ID);
        CHECK_EQ((int) ch17_unit(1)->pos_x, 22);
        CHECK_EQ((int) ch17_unit(1)->pos_y, 12);
    }
}

/* ------------------------------------------------------------------
 * The chapter 19 flank ambush at 000381d0.
 *
 * It is the chapter 10 ambush of tests/chevt2.c with three pans and a spoken
 * line added, so the cases below stand on the chapter 17 fixture the rest of
 * this file uses -- a blank walkable 32 x 16 map, a deployment table whose
 * records are also their own MAP%02d.COD placement records, and the real
 * MAP00.COD and MAP01.COD read off disk -- with three things added:
 *
 *   a unit array of four records, so an index really names one record out of
 *   several and the side gate can be shown reading the one it was handed;
 *
 *   the globals the frame compositor reads, staged the way the pan-bearing
 *   cases of tests/chevt2.c stage them: no scene layers, both HUD flags down,
 *   the view and the cursor at the origin, and a sentinel in the frame latch
 *   that a run composing no frame at all would leave behind;
 *
 *   an enemy table long enough for character id 0x80, which is the id every
 *   staged deployment record carries because it is the one the compositor
 *   drops -- 0x80 is 68 rows past ENEMY_CHAR_ID_BASE, and the eight-row table
 *   the chapter 17 fixture points at is not that long.
 *
 * WHICH RECORD ARRIVED IS READ OFF ITS LEVEL AND NOT ITS CHARACTER ID, for
 * that same reason: every record has to carry the dropped id, so the level is
 * what tells the wave-6 record from the wave-5 and wave-7 records either side
 * of it.
 *
 * THE CASES THAT REACH THE BODY RUN WITH THE TIMER INSTALLED AND THE ADAPTER
 * IN MODE 13H, because the three twelve-frame holds spin on the tick counter
 * inside fdps_render_view_frame and nothing else advances it.  They also reach
 * fdps_deploy_wave, which opens ICON.CEL and FIELD.VFS for itself, so they
 * skip themselves when those are not staged.  The cases whose gate refuses the
 * body need none of that: nothing is opened and no frame is composed, so they
 * cost nothing and are free to sweep several arguments.
 *
 * EACH FIRING COSTS SEVERAL SECONDS OF REAL TIME and that is why so many
 * claims are asserted out of one of them.  A firing composes the thirty-six
 * held frames plus a frame for every pan step that dragged the view, and every
 * composed frame waits for a timer tick, so a case that fires cannot be made
 * cheaper -- only the number of firings can.  Grouping is therefore deliberate
 * here, not carelessness, and each case's comment says which claims it carries.
 *
 * WHICH TEXT ENTRY THE DRAW ASKS FOR IS NOT ASSERTED, for the reason the
 * chapter 19 arrival section above gives: the draw takes its whole effect
 * through pixels at the VGA aperture, keeps no state and returns a cursor this
 * handler discards.  The entry id and the three colours are literals in the
 * instruction stream (PUSH 0x13 at 000382c9 and PUSH 0xd0 / PUSH 0x0 /
 * PUSH 0x6d at 000382ba, 000382b8 and 000382b6).  The text block is staged as
 * entries that are a lone terminator so the draw walks it, paints nothing and
 * returns at once.
 * ------------------------------------------------------------------ */

/* The wave the ambush asks for, PUSH 0x6 at 00038212, and the two waves parked
   either side of it so that asking for the wrong one is visible. */
#define CH19W6_WAVE 6
#define CH19W6_WAVE_BELOW 5
#define CH19W6_WAVE_ABOVE 7

/* Which table index each of the three sits on.  The wave the handler asks for
   is on record 0, whose MAP00.COD and MAP01.COD coordinates the cases above
   already read back out of the real files. */
#define CH19W6_WAVE6_RECORD 0
#define CH19W6_WAVE5_RECORD 1
#define CH19W6_WAVE7_RECORD 2
#define CH19W6_SPAWN_RECORD_COUNT 3

/* The level each record carries, which is how the cases tell which one
   arrived.  The wave-6 record's level is deliberately not its wave number. */
#define CH19W6_WAVE6_LEVEL 17
#define CH19W6_WAVE5_LEVEL 5
#define CH19W6_WAVE7_LEVEL 7

/* The id the compositor drops, PORTRAIT_ID_NO_MAP_SPRITE at 00033c56, and how
   many enemy rows a table has to have for fdps_deploy_unit to resolve it
   inside itself: 0x80 - ENEMY_CHAR_ID_BASE + 1. */
#define CH19W6_ARRIVAL_CHAR_ID 0x80
#define CH19W6_ENEMY_TABLE_ROWS (0x80 - 0x3c + 1)

/* MAP00.COD's placement record 0 and MAP01.COD's record 0. */
#define CH19W6_MAP00_RECORD0_X 18
#define CH19W6_MAP00_RECORD0_Y 0
#define CH19W6_MAP01_RECORD0_X 9
#define CH19W6_MAP01_RECORD0_Y 4

/* Units already on the map when the handler runs, and the index the arrival
   therefore lands on.  Four is enough for an index to name one record out of
   several, and they stand along the top row clear of every placement record
   the cases read back. */
#define CH19W6_STAGED_UNITS 4
#define CH19W6_ARRIVAL_UNIT_INDEX CH19W6_STAGED_UNITS

/* The unit the fixture puts on the side that springs the ambush, and the
   sides themselves: 0 is the enemy's, 2 the player's own roster.  0x80 is the
   value that tells a plain non-zero test from a signed one. */
#define CH19W6_TRIGGERING_UNIT_INDEX 2
#define CH19W6_SIDE_ENEMY 0
#define CH19W6_SIDE_PLAYER 2
#define CH19W6_SIDE_HIGH_BIT 0x80

/* The cursor mode the fixture parks in data_fdps_map_cursor_draw_mode before
   every run: neither of the two values the handler writes, so a run that left
   it alone, a run that hid the cursor and never put it back, and a run that
   restored what it found are all told apart from the mode the handler is
   supposed to leave behind. */
#define CH19W6_STAGED_CURSOR_MODE 4

/* The two modes the handler itself writes: 0 at 00038205 and 1 at 000382ac. */
#define CH19W6_CURSOR_MODE_HIDDEN 0
#define CH19W6_CURSOR_MODE_BOX 1

/* The three world pixels the pans walk to, PUSH 0x0 / PUSH 0x108 at 00038222,
   PUSH 0x210 / PUSH 0x0 at 0003824f and PUSH 0x210 / PUSH 0x300 at
   0003827c. */
#define CH19W6_TOP_EDGE_WORLD_X 0x108
#define CH19W6_TOP_EDGE_WORLD_Y 0
#define CH19W6_LEFT_FLANK_WORLD_X 0
#define CH19W6_LEFT_FLANK_WORLD_Y 0x210
#define CH19W6_RIGHT_FLANK_WORLD_X 0x300
#define CH19W6_RIGHT_FLANK_WORLD_Y 0x210

/* How long each hold lasts, CMP dword ptr [EBP+0x14],0xc at 00038238, 00038265
   and 00038295, and the least the tick counter can move across all three of
   them.  The bound is one-sided on purpose -- a slow machine spends more ticks
   than this, never fewer, and the frames the pans themselves compose are on
   top of it -- so it cannot fail spuriously, while a rebuild that dropped one
   of the three loops cannot meet it at any sane speed. */
#define CH19W6_HOLD_FRAMES 0xc
#define CH19W6_LEAST_HOLD_TICKS (3 * CH19W6_HOLD_FRAMES - 1)

/* A value the tick counter cannot legitimately hold, parked in the frame latch
   so a run that composed nothing is distinguishable from one that did. */
#define CH19W6_FRAME_SENTINEL 0x5a5a5a5aU

/* Any turn at all: nothing in this handler reads the turn counter, and the
   fixture sets one because the chapter 17 stage it is built on wants one. */
#define CH19W6_ANY_TURN 4

static struct fdps_enemy_data ch19w6_enemy[CH19W6_ENEMY_TABLE_ROWS];
static unsigned int ch19w6_ticks_before;
static unsigned int ch19w6_ticks_after;
static void (__interrupt __far *ch19w6_saved_timer)();

static void __interrupt __far ch19w6_timer_isr(void)
{
    ++data_fdps_timer_tick_counter;
    _chain_intr(ch19w6_saved_timer);
}

/* The chapter 19 arrival fixture -- the chapter 17 map with a text block over
   it -- given a four-record unit array every one of whose units is on the
   player's side and wears the dropped portrait id, three deployment records at
   the waves the cases ask about, an enemy table long enough for the id they
   carry, and the compositor's own globals. */
static void ch19w6_stage(void)
{
    int i;

    ch19_stage(CH19W6_ANY_TURN);

    memset(ch19w6_enemy, 0, sizeof(ch19w6_enemy));
    data_fdps_battle_enemy_data_table_ptr = (unsigned char *) ch19w6_enemy;

    data_fdps_map_unit_array_ptr =
        (unsigned char *) malloc(CH19W6_STAGED_UNITS * CH17_UNIT_STRIDE);
    memset(data_fdps_map_unit_array_ptr, 0,
           (size_t) (CH19W6_STAGED_UNITS * CH17_UNIT_STRIDE));
    data_fdps_map_unit_count = CH19W6_STAGED_UNITS;
    for (i = 0; i < CH19W6_STAGED_UNITS; i++) {
        ch17_unit(i)->portrait_id = (unsigned char) CH19W6_ARRIVAL_CHAR_ID;
        ch17_unit(i)->side = (unsigned char) CH19W6_SIDE_PLAYER;
        ch17_unit(i)->pos_x = (unsigned char) i;
        ch17_unit(i)->pos_y = 0;
    }

    ch17_spawn_table[CH17_SPAWN_TABLE_COUNT_OFFSET] =
        (unsigned char) CH19W6_SPAWN_RECORD_COUNT;
    ch17_set_spawn(CH19W6_WAVE6_RECORD, CH19W6_ARRIVAL_CHAR_ID, CH19W6_WAVE);
    ch17_set_spawn(CH19W6_WAVE5_RECORD, CH19W6_ARRIVAL_CHAR_ID,
                   CH19W6_WAVE_BELOW);
    ch17_set_spawn(CH19W6_WAVE7_RECORD, CH19W6_ARRIVAL_CHAR_ID,
                   CH19W6_WAVE_ABOVE);
    ch17_spawn_at(CH19W6_WAVE6_RECORD)->level =
        (unsigned char) CH19W6_WAVE6_LEVEL;
    ch17_spawn_at(CH19W6_WAVE5_RECORD)->level =
        (unsigned char) CH19W6_WAVE5_LEVEL;
    ch17_spawn_at(CH19W6_WAVE7_RECORD)->level =
        (unsigned char) CH19W6_WAVE7_LEVEL;

    data_fdps_scene_layer_count = 0;
    data_fdps_ui_terrain_hud_user_enabled = 0;
    data_fdps_ui_play_active_flag = 0;
    data_fdps_battle_view_window_origin_x = 0;
    data_fdps_battle_view_window_origin_y = 0;
    data_fdps_map_cursor_world_x = 0;
    data_fdps_map_cursor_world_y = 0;
    data_fdps_view_frame_last_tick = CH19W6_FRAME_SENTINEL;
    data_fdps_map_cursor_draw_mode = CH19W6_STAGED_CURSOR_MODE;

    data_fdps_map_cell_event_triggered_flags[CH15_LATCH_SLOT] = 0;
}

/* One firing, with the timer running and the adapter in the mode the frames
   present through, and the tick counter sampled either side so the length of
   the three holds can be read back.  Text mode is back before anything is
   asserted, so a failure prints on a readable screen. */
static void ch19w6_run(int unit_index)
{
    smith_set_mode(SMITH_MODE_320X200X256);
    ch19w6_saved_timer = _dos_getvect(SMITH_TIMER_VECTOR);
    _dos_setvect(SMITH_TIMER_VECTOR, ch19w6_timer_isr);
    ch19w6_ticks_before = data_fdps_timer_tick_counter;
    fdps_chapter_19_event_deploy_wave_6(unit_index);
    ch19w6_ticks_after = data_fdps_timer_tick_counter;
    _dos_setvect(SMITH_TIMER_VECTOR, ch19w6_saved_timer);
    smith_set_mode(SMITH_MODE_TEXT);
}

/* The two fields the gate reads and the stride they are indexed by.  The side
   byte is the whole of the second gate, and CMP byte ptr [EAX+0x6],0x0 at
   000381fb is the offset it has to be at; the wave byte is what the deployment
   walk matches 6 against. */
static void ch19w6_record_shape_matches_the_offsets(void)
{
    CHECK_EQ((int) sizeof(struct fdps_unit_record), CH17_UNIT_STRIDE);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, side), 6);
    CHECK_EQ((int) offsetof(struct fdps_char_spawn_record, wave_no), 0x15);
}

/* A latch that is already up refuses the whole body, and it is tested against
   0 rather than against 1 -- CMP byte ptr [0x000640e8],0x0 / JNZ at 000381dc
   -- so any non-zero value in the slot blocks it.  Nothing is deployed, no
   frame is composed and the cursor mode is left exactly as it was found, which
   is what says the store of 0 at 00038205 is inside the gate and not ahead of
   it. */
static void ch19w6_latch_blocks_the_whole_body(void)
{
    static int latch_values[2] = {1, 0x7f};
    int i;

    for (i = 0; i < 2; i++) {
        ch19w6_stage();
        data_fdps_map_cell_event_triggered_flags[CH15_LATCH_SLOT] =
            (unsigned char) latch_values[i];

        ch19w6_run(CH19W6_TRIGGERING_UNIT_INDEX);

        CHECK_EQ(data_fdps_map_unit_count, CH19W6_STAGED_UNITS);
        CHECK_EQ(data_fdps_map_cell_event_triggered_flags[CH15_LATCH_SLOT],
                 latch_values[i]);
        CHECK_EQ(data_fdps_map_cursor_draw_mode, CH19W6_STAGED_CURSOR_MODE);
        CHECK_EQ(data_fdps_view_frame_last_tick == CH19W6_FRAME_SENTINEL, 1);
    }
}

/* A unit on side 0 cannot spring the ambush: the second gate is CMP byte ptr
   [EAX+0x6],0x0 / JZ at 000381fb, so a zero side byte jumps to the same exit
   the latch does.  Nothing is deployed, the latch is NOT spent, no frame is
   composed and the cursor mode is untouched -- an enemy walking over the tile
   leaves the ambush armed for the unit that comes next. */
static void ch19w6_side_zero_does_not_fire(void)
{
    ch19w6_stage();
    ch17_unit(CH19W6_TRIGGERING_UNIT_INDEX)->side =
        (unsigned char) CH19W6_SIDE_ENEMY;

    ch19w6_run(CH19W6_TRIGGERING_UNIT_INDEX);

    CHECK_EQ(data_fdps_map_unit_count, CH19W6_STAGED_UNITS);
    CHECK_EQ(data_fdps_map_cell_event_triggered_flags[CH15_LATCH_SLOT], 0);
    CHECK_EQ(data_fdps_map_cursor_draw_mode, CH19W6_STAGED_CURSOR_MODE);
    CHECK_EQ(data_fdps_view_frame_last_tick == CH19W6_FRAME_SENTINEL, 1);
}

/* Three claims out of one firing, because a firing is expensive.

   THE SIDE BYTE READ BELONGS TO THE RECORD THE ARGUMENT NAMES.  Three of the
   four staged units are put on side 0 and the fourth is left able to spring
   the ambush; the three indices that name a side-0 unit are refused one after
   another -- they leave the latch down, which is what lets them run without
   restaging -- and only the index that names the fourth fires.  A handler that
   read unit 0, or the last unit, or ignored the argument, would fire on the
   wrong one of these four calls.

   THE SIDE TEST IS A PLAIN NON-ZERO TEST AND NOT A SIGNED ONE.  The unit that
   springs it is put on side 0x80, which is negative read as a signed char, so
   a rebuild that had written the gate as "side > 0" would refuse this firing.
   Side 2, the ordinary player side, is what every other firing in this section
   uses.

   WAVE 6 IS WHAT ARRIVES.  The one record tagged 6 is deployed, carrying its
   own level and MAP00.COD record 0's coordinates, and the records tagged 5 and
   7 are left where they are: the unit count moves by exactly one, and the
   level is what says which record moved, every record carrying the same
   character id. */
static void ch19w6_deploys_wave_six_for_the_record_the_index_names(void)
{
    static int enemy_indices[3] = {0, 1, 3};
    int i;

    ch19w6_stage();
    for (i = 0; i < 3; i++) {
        ch17_unit(enemy_indices[i])->side = (unsigned char) CH19W6_SIDE_ENEMY;
    }
    ch17_unit(CH19W6_TRIGGERING_UNIT_INDEX)->side =
        (unsigned char) CH19W6_SIDE_HIGH_BIT;

    for (i = 0; i < 3; i++) {
        ch19w6_run(enemy_indices[i]);
        CHECK_EQ(data_fdps_map_cell_event_triggered_flags[CH15_LATCH_SLOT], 0);
        CHECK_EQ(data_fdps_map_unit_count, CH19W6_STAGED_UNITS);
    }

    ch17_ensure_game_files();
    if (!ch17_files_ready) {
        return;
    }

    ch19w6_run(CH19W6_TRIGGERING_UNIT_INDEX);

    CHECK_EQ(data_fdps_map_cell_event_triggered_flags[CH15_LATCH_SLOT], 1);
    CHECK_EQ(data_fdps_map_unit_count, CH19W6_STAGED_UNITS + 1);
    CHECK_EQ((int) ch17_unit(CH19W6_ARRIVAL_UNIT_INDEX)->level,
             CH19W6_WAVE6_LEVEL);
    CHECK_EQ((int) ch17_unit(CH19W6_ARRIVAL_UNIT_INDEX)->pos_x,
             CH19W6_MAP00_RECORD0_X);
    CHECK_EQ((int) ch17_unit(CH19W6_ARRIVAL_UNIT_INDEX)->pos_y,
             CH19W6_MAP00_RECORD0_Y);
}

/* The map the wave is placed under is read from
   data_fdps_chapter_current_chapter_id at the call site and is not a literal:
   the same record placed while that global says 1 lands on MAP01.COD's record
   0 at (9, 4), where the case above -- which fires with that global on 0 --
   has it landing on MAP00.COD's (18, 0).  The two halves of the claim are in
   two cases because each of them costs a firing. */
static void ch19w6_map_number_comes_from_the_chapter_global(void)
{
    ch17_ensure_game_files();
    if (!ch17_files_ready) {
        return;
    }

    ch19w6_stage();
    data_fdps_chapter_current_chapter_id = 1;

    ch19w6_run(CH19W6_TRIGGERING_UNIT_INDEX);

    CHECK_EQ(data_fdps_map_unit_count, CH19W6_STAGED_UNITS + 1);
    CHECK_EQ((int) ch17_unit(CH19W6_ARRIVAL_UNIT_INDEX)->level,
             CH19W6_WAVE6_LEVEL);
    CHECK_EQ((int) ch17_unit(CH19W6_ARRIVAL_UNIT_INDEX)->pos_x,
             CH19W6_MAP01_RECORD0_X);
    CHECK_EQ((int) ch17_unit(CH19W6_ARRIVAL_UNIT_INDEX)->pos_y,
             CH19W6_MAP01_RECORD0_Y);
}

/* The placement flag is 0 -- XOR EAX,EAX / PUSH EAX at 0003820f -- so each
   arrival is put on the nearest free walkable tile to its placement record
   rather than on the record's own tile.  MAP00.COD record 0 names (18, 0);
   giving that one cell a tile id whose attribute row is terrain 5 takes it out
   of the search and the unit lands one tile away.  A flag of 1 would drop it
   on (18, 0) regardless of the terrain there, which is what would put an
   arriving enemy on top of whatever is already standing on its spawn tile.

   (18, 1) is which of the three tiles at distance 1 it lands on: the scan is
   row-major over the whole grid and a tie is accepted, so the last candidate
   at the best distance wins, and (18, 1) is a row below (17, 0) and (19, 0). */
static void ch19w6_places_on_the_nearest_free_tile(void)
{
    ch17_ensure_game_files();
    if (!ch17_files_ready) {
        return;
    }

    ch19w6_stage();
    ch17_set_tile_id(CH19W6_MAP00_RECORD0_X, CH19W6_MAP00_RECORD0_Y, 1);
    ch17_set_terrain(1, CH17_TERRAIN_BLOCKED);

    ch19w6_run(CH19W6_TRIGGERING_UNIT_INDEX);

    CHECK_EQ(data_fdps_map_unit_count, CH19W6_STAGED_UNITS + 1);
    CHECK_EQ((int) ch17_unit(CH19W6_ARRIVAL_UNIT_INDEX)->level,
             CH19W6_WAVE6_LEVEL);
    CHECK_EQ((int) ch17_unit(CH19W6_ARRIVAL_UNIT_INDEX)->pos_x,
             CH19W6_MAP00_RECORD0_X);
    CHECK_EQ((int) ch17_unit(CH19W6_ARRIVAL_UNIT_INDEX)->pos_y,
             CH19W6_MAP00_RECORD0_Y + 1);
}

/* Everything about the pan sequence, out of the one firing it costs.

   THE THREE PANS RUN IN THE ORDER THE INSTRUCTION STREAM HAS THEM AND THE LAST
   IS THE RIGHT FLANK.  The cursor starts at the origin and is left on world
   pixel (768, 528) exactly, which is tile (32, 22).  A run that stopped after
   the first pan would leave it on (264, 0) and one that stopped after the
   second on (0, 528); both are asserted against.  The frame latch has lost its
   sentinel, so frames really were composed.

   ALL THREE HOLDS ARE TWELVE FRAMES LONG AND ALL THREE ARE THERE.  A composed
   frame costs at least one timer tick, so thirty-six of them cannot pass in
   fewer than thirty-five ticks, and the frames the pans themselves compose are
   on top of that.  A rebuild that held only two of the three, or that held each
   for fewer frames, spends fewer ticks than any machine can excuse.

   THE CURSOR MODE IS LEFT ON 1 AND NOT ON THE 4 THE FIXTURE PARKED THERE,
   which is the whole of the second store at 000382ac: the handler does not
   restore the mode it found, it writes the plain box over it.  A rebuild that
   saved and restored would leave 4 here and one that only ever hid the cursor
   would leave 0. */
static void ch19w6_pan_ends_on_the_right_flank_after_three_holds(void)
{
    ch17_ensure_game_files();
    if (!ch17_files_ready) {
        return;
    }

    ch19w6_stage();
    ch19w6_run(CH19W6_TRIGGERING_UNIT_INDEX);

    CHECK_EQ(data_fdps_map_cursor_world_x, CH19W6_RIGHT_FLANK_WORLD_X);
    CHECK_EQ(data_fdps_map_cursor_world_y, CH19W6_RIGHT_FLANK_WORLD_Y);
    CHECK_EQ(data_fdps_map_cursor_world_x == CH19W6_TOP_EDGE_WORLD_X, 0);
    CHECK_EQ(data_fdps_map_cursor_world_x == CH19W6_LEFT_FLANK_WORLD_X, 0);
    CHECK_EQ(data_fdps_view_frame_last_tick == CH19W6_FRAME_SENTINEL, 0);
    CHECK_EQ((int) (ch19w6_ticks_after - ch19w6_ticks_before)
                 >= CH19W6_LEAST_HOLD_TICKS, 1);
    CHECK_EQ(data_fdps_map_cursor_draw_mode, CH19W6_CURSOR_MODE_BOX);
    CHECK_EQ(data_fdps_map_cursor_draw_mode == CH19W6_STAGED_CURSOR_MODE, 0);
    CHECK_EQ(data_fdps_map_cursor_draw_mode == CH19W6_CURSOR_MODE_HIDDEN, 0);
}

/* The ambush fires once and the latch it leaves behind is what stops it: a
   second call on the same map deploys nothing, composes no frame and leaves
   the cursor mode alone.  The mode is parked at 4 again between the two calls,
   so a second firing would be visible in it even if the deployment somehow
   were not. */
static void ch19w6_fires_once_only(void)
{
    ch17_ensure_game_files();
    if (!ch17_files_ready) {
        return;
    }

    ch19w6_stage();
    ch19w6_run(CH19W6_TRIGGERING_UNIT_INDEX);
    CHECK_EQ(data_fdps_map_unit_count, CH19W6_STAGED_UNITS + 1);
    CHECK_EQ(data_fdps_map_cell_event_triggered_flags[CH15_LATCH_SLOT], 1);

    data_fdps_map_cursor_draw_mode = CH19W6_STAGED_CURSOR_MODE;
    data_fdps_view_frame_last_tick = CH19W6_FRAME_SENTINEL;

    ch19w6_run(CH19W6_TRIGGERING_UNIT_INDEX);
    CHECK_EQ(data_fdps_map_unit_count, CH19W6_STAGED_UNITS + 1);
    CHECK_EQ(data_fdps_map_cursor_draw_mode, CH19W6_STAGED_CURSOR_MODE);
    CHECK_EQ(data_fdps_view_frame_last_tick == CH19W6_FRAME_SENTINEL, 1);
}

void run_chevt3_tests(void)
{
    RUN_TEST(ch15_record_shape_matches_the_offsets);
    RUN_TEST(ch15_activate_clears_exactly_the_range);
    RUN_TEST(ch15_activate_includes_the_last_index);
    RUN_TEST(ch15_activate_keeps_the_high_nibble);
    RUN_TEST(ch15_activate_has_no_one_shot_latch);
    RUN_TEST(ch15_activate_touches_no_neighbouring_byte);
    RUN_TEST(ch15_activate_ignores_the_unit_index_argument);
    RUN_TEST(smith_record_shape_matches_the_offsets);
    RUN_TEST(smith_reforges_the_first_sword);
    RUN_TEST(smith_breaks_the_second_sword_and_pays);
    RUN_TEST(smith_second_question_declined_gives_the_ore);
    RUN_TEST(smith_offer_declined_keeps_the_sword);
    RUN_TEST(smith_cancel_declines_like_the_right_option);
    RUN_TEST(smith_prefers_the_first_sword_when_both_are_carried);
    RUN_TEST(smith_with_no_sword_spends_the_latch);
    RUN_TEST(smith_fires_on_the_last_turn_and_not_after);
    RUN_TEST(smith_fires_for_no_unit_but_randis);
    RUN_TEST(smith_does_not_fire_twice);
    RUN_TEST(ch16_turn5_clears_the_second_wave_block);
    RUN_TEST(ch16_other_turn_clears_the_opening_block);
    RUN_TEST(ch16_both_ranges_include_their_last_index);
    RUN_TEST(ch16_only_turn_five_takes_the_upper_range);
    RUN_TEST(ch16_keeps_the_high_nibble);
    RUN_TEST(ch16_touches_no_neighbouring_byte);
    RUN_TEST(ch16_has_no_one_shot_latch);
    RUN_TEST(ch16_ignores_the_unit_index_argument);
    RUN_TEST(ch17_record_shape_matches_the_offsets);
    RUN_TEST(ch17_deploys_the_wave_the_turn_is_due);
    RUN_TEST(ch17_wave_key_follows_the_counter);
    RUN_TEST(ch17_early_turns_deploy_nothing);
    RUN_TEST(ch17_map_number_comes_from_the_chapter_global);
    RUN_TEST(ch17_places_on_the_nearest_free_tile);
    RUN_TEST(ch17_has_no_one_shot_latch);
    RUN_TEST(ch17_ignores_the_unit_index_argument);
    RUN_TEST(ch18_deploys_the_wave_the_turn_names);
    RUN_TEST(ch18_turn_zero_asks_for_wave_zero);
    RUN_TEST(ch18_unmatched_turns_deploy_nothing);
    RUN_TEST(ch18_map_number_comes_from_the_chapter_global);
    RUN_TEST(ch18_places_on_the_nearest_free_tile);
    RUN_TEST(ch18_has_no_one_shot_latch);
    RUN_TEST(ch18_ignores_the_unit_index_argument);
    RUN_TEST(ch19_deploys_wave_one);
    RUN_TEST(ch19_wave_key_is_a_literal_not_the_turn_counter);
    RUN_TEST(ch19_map_number_comes_from_the_chapter_global);
    RUN_TEST(ch19_places_on_the_nearest_free_tile);
    RUN_TEST(ch19_has_no_one_shot_latch);
    RUN_TEST(ch19_ignores_the_unit_index_argument);
    RUN_TEST(ch19w6_record_shape_matches_the_offsets);
    RUN_TEST(ch19w6_latch_blocks_the_whole_body);
    RUN_TEST(ch19w6_side_zero_does_not_fire);
    RUN_TEST(ch19w6_deploys_wave_six_for_the_record_the_index_names);
    RUN_TEST(ch19w6_map_number_comes_from_the_chapter_global);
    RUN_TEST(ch19w6_places_on_the_nearest_free_tile);
    RUN_TEST(ch19w6_pan_ends_on_the_right_flank_after_three_holds);
    RUN_TEST(ch19w6_fires_once_only);
}
