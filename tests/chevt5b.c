/* tests/chevt5b.c -- cover for src/chevt5b.c.
 *
 * Each handler has its own section with its own banner and its own fixture.
 * The battle-map staging the chapter 26 ambush needs sits at the top of the
 * file: it is the same fixture tests/chevt5.c builds its chapter 24 cases on,
 * copied here rather than shared because a file-local fixture cannot cross a
 * translation unit.  What it stages is a walkable map, a deployment table, the
 * character and item tables a deployed unit is built out of, a chapter text
 * block every entry of which is empty, and a one-unit array.
 *
 * The deployments the chapter 26 ambush makes are real: fdps_deploy_wave opens
 * ICON.CEL and FIELD.VFS for itself, staged through tests/gamefile.lst, and the
 * coordinates read back are the placement records inside this file's own
 * MAP00.COD.  A run without those two files would not fail a check, it would
 * hang in fdps_wait_any_key, so every case that deploys skips itself when they
 * are not there.
 *
 * The lines the handlers speak are drawn for real as well: each fixture aims
 * the chapter text pointer at a block whose every entry names one lone
 * terminator, so fdps_draw_text walks the entry, paints nothing and returns
 * without needing a font, a message panel or mode 13h.  A draw that paints
 * nothing leaves nothing behind, which is why no case asserts a text id; the
 * ids are pinned in src/chevt5b.c against the instruction addresses instead.
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
#include "chevt5b.h"

/* ---------------------------------------------------------------------------
   The shared battle-map fixture.

   Copied from tests/chevt5.c, where the chapter 24 cases own it, because the
   chapter 26 ambush below needs a staged map, a deployment table and the
   tables a deployed unit is built out of, and a file-local fixture cannot be
   shared between two translation units.  Only the ambush uses it; the other
   two sections stage arrays of their own.
   --------------------------------------------------------------------------- */

/* A map big enough for MAP00.COD's records, which reach (22, 12).  The tile
   search fdps_deploy_unit runs on a place_exact of 0 walks the whole grid, and
   the marking pass writes at a unit's own tile with no bound, so the grid has
   to cover the coordinates the real placement file names. */
#define CH26F_GRID_W 32
#define CH26F_GRID_H 16

/* Where the deployment records start in the MAP%02d.DAT block and where its
   header keeps their count: ADD EAX,0x83 at 00023463 and MOV AL,byte ptr
   [EAX+0x2] at 000238bb. */
#define CH26F_SPAWN_TABLE_RECORD_BASE 0x83
#define CH26F_SPAWN_TABLE_COUNT_OFFSET 2
#define CH26F_SPAWN_TABLE_RECORDS 8

/* The scene-layer offsets fdps_map_load_tile_info reads through: the tile
   map's width word at +7 with its 16-bit ids from +0xb, the attribute table's
   4-byte rows from +0x11, and the event layer's width at +7 with its cells
   from +0x10 (src/maptile.c). */
#define CH26F_TILE_MAP_WIDTH_OFFSET 7
#define CH26F_TILE_MAP_IDS_OFFSET 0xb
#define CH26F_TILE_ATTR_ROWS_OFFSET 0x11
#define CH26F_EVENT_LAYER_WIDTH_OFFSET 7
#define CH26F_EVENT_LAYER_CELLS_OFFSET 0x10

/* A terrain code the deployment search accepts and one it rejects, either side
   of the CMP EAX,0x5 / JGE at 00023404. */
#define CH26F_TERRAIN_WALKABLE 1
#define CH26F_TERRAIN_BLOCKED 5

#define CH26F_TILE_ATTR_ROWS 16
#define CH26F_CHAR_TABLE_ROWS 8
#define CH26F_ENEMY_TABLE_ROWS 8

/* Every item id is a valid index into the staged item table, 0xff included:
   fdps_unit_recompute_combat_stats follows an equipped flag into
   fdps_get_item_record without a bounds check. */
#define CH26F_ITEM_TABLE_ROWS 256
#define CH26F_ITEM_ID_NONE 0xff

/* The unit array's stride, as the deployment path allocates it: PUSH 0x50 at
   000232cc, and the stride fdps_get_unit_record multiplies the index by. */
#define CH26F_UNIT_STRIDE 0x50

/* The chapter text block the lines are spoken from: 0x15 entries, comfortably
   past the 0xc the ambush's last draw asks for at 0003932c, each pointing at
   the same lone terminator so that a draw walks it, paints nothing and
   returns. */
#define CH26F_TEXT_IDS 0x15
#define CH26F_TEXT_EMPTY_AT 0x40
#define CH26F_TEXT_BLOCK_BYTES (CH26F_TEXT_EMPTY_AT + 2)
#define CH26F_TEXT_END (-1)

static unsigned char ch26f_grid[4 + CH26F_GRID_W * CH26F_GRID_H * 2];
static unsigned char ch26f_spawn_table[CH26F_SPAWN_TABLE_RECORD_BASE +
                                       CH26F_SPAWN_TABLE_RECORDS * 0x1a];
static unsigned char ch26f_tile_map[CH26F_TILE_MAP_IDS_OFFSET +
                                    CH26F_GRID_W * CH26F_GRID_H * 2];
static unsigned char ch26f_tile_attr[CH26F_TILE_ATTR_ROWS_OFFSET +
                                     CH26F_TILE_ATTR_ROWS * 4];
static unsigned char ch26f_event_layer[CH26F_EVENT_LAYER_CELLS_OFFSET +
                                       CH26F_GRID_W * CH26F_GRID_H];
static struct fdps_character_base_record ch26f_char_base[CH26F_CHAR_TABLE_ROWS];
static struct fdps_character_growth ch26f_growth[CH26F_CHAR_TABLE_ROWS];
static struct fdps_enemy_data ch26f_enemy[CH26F_ENEMY_TABLE_ROWS];
static struct fdps_item_effect ch26f_items[CH26F_ITEM_TABLE_ROWS];
static unsigned char ch26f_text_block[CH26F_TEXT_BLOCK_BYTES];

static int ch26f_files_checked = 0;
static int ch26f_files_ready = 0;

/* ICON.CEL is read by fdps_cache_cel_sprite_group, which takes a fixed
   0x2970-byte bite out of the sheet's offset table, so a shorter file is one
   that reader runs off the end of rather than a smaller fixture.  FIELD.VFS
   cannot be stood in for at all: a missing container sends fdps_deploy_wave
   into fdps_wait_any_key and a missing member into exit(1). */
static void ch26f_ensure_game_files(void)
{
    FILE *fp;
    long size;

    if (ch26f_files_checked) {
        return;
    }
    ch26f_files_checked = 1;

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
    ch26f_files_ready = 1;
}

static struct fdps_char_spawn_record *ch26f_spawn_at(int record_index)
{
    return (struct fdps_char_spawn_record *)
           (ch26f_spawn_table + CH26F_SPAWN_TABLE_RECORD_BASE) + record_index;
}

/* One deployment record with nothing equipped and nothing carried, tagged with
   the wave it belongs to. */
static void ch26f_set_spawn(int record_index, int char_id, int wave_no)
{
    ch26f_spawn_at(record_index)->char_id = (unsigned char) char_id;
    ch26f_spawn_at(record_index)->level = 1;
    ch26f_spawn_at(record_index)->side = 0;
    ch26f_spawn_at(record_index)->equipped_item_0 = CH26F_ITEM_ID_NONE;
    ch26f_spawn_at(record_index)->equipped_item_1 = CH26F_ITEM_ID_NONE;
    ch26f_spawn_at(record_index)->carried_items[0] = CH26F_ITEM_ID_NONE;
    ch26f_spawn_at(record_index)->carried_items[1] = CH26F_ITEM_ID_NONE;
    ch26f_spawn_at(record_index)->carried_items[2] = CH26F_ITEM_ID_NONE;
    ch26f_spawn_at(record_index)->carried_items[3] = CH26F_ITEM_ID_NONE;
    ch26f_spawn_at(record_index)->carried_items[4] = CH26F_ITEM_ID_NONE;
    ch26f_spawn_at(record_index)->carried_items[5] = CH26F_ITEM_ID_NONE;
    ch26f_spawn_at(record_index)->wave_no = (unsigned char) wave_no;
}

static void ch26f_set_terrain(int tile_id, int terrain)
{
    struct fdps_tile_attr_entry *rows;

    rows = (struct fdps_tile_attr_entry *)
           (ch26f_tile_attr + CH26F_TILE_ATTR_ROWS_OFFSET);
    rows[tile_id].terrain_type = (unsigned char) terrain;
}

static struct fdps_unit_record *ch26f_unit(int unit_index)
{
    return (struct fdps_unit_record *)
           (data_fdps_map_unit_array_ptr + unit_index * CH26F_UNIT_STRIDE);
}

/* A blank walkable map with one unit already on it, the chapter global on map
   0, the text block in place and the turn counter on the turn the case is
   about.  The staged unit stands at (0, 0), clear of every placement record
   these cases read back. */
static void ch26f_stage(int turn)
{
    int i;

    memset(ch26f_grid, 0, sizeof(ch26f_grid));
    memset(ch26f_spawn_table, 0, sizeof(ch26f_spawn_table));
    memset(ch26f_tile_map, 0, sizeof(ch26f_tile_map));
    memset(ch26f_tile_attr, 0, sizeof(ch26f_tile_attr));
    memset(ch26f_event_layer, 0, sizeof(ch26f_event_layer));
    memset(ch26f_char_base, 0, sizeof(ch26f_char_base));
    memset(ch26f_growth, 0, sizeof(ch26f_growth));
    memset(ch26f_enemy, 0, sizeof(ch26f_enemy));
    memset(ch26f_items, 0, sizeof(ch26f_items));

    memset(ch26f_text_block, 0, (size_t) CH26F_TEXT_BLOCK_BYTES);
    *(short *) (ch26f_text_block + CH26F_TEXT_EMPTY_AT) = (short) CH26F_TEXT_END;
    for (i = 0; i < CH26F_TEXT_IDS; i++) {
        *(short *) (ch26f_text_block + i * 2) = (short) CH26F_TEXT_EMPTY_AT;
    }

    *(short *) ch26f_grid = (short) CH26F_GRID_W;
    *(short *) (ch26f_grid + 2) = (short) CH26F_GRID_H;

    *(short *) (ch26f_tile_map + CH26F_TILE_MAP_WIDTH_OFFSET) =
        (short) CH26F_GRID_W;
    for (i = 0; i < CH26F_TILE_ATTR_ROWS; i++) {
        ch26f_set_terrain(i, CH26F_TERRAIN_WALKABLE);
    }

    *(short *) (ch26f_event_layer + CH26F_EVENT_LAYER_WIDTH_OFFSET) =
        (short) CH26F_GRID_W;

    data_fdps_battle_move_grid_ptr = ch26f_grid;
    data_fdps_tile_event_data_table_ptr = ch26f_spawn_table;
    data_fdps_scene_layer_tile_map_ptrs[0] = ch26f_tile_map;
    data_fdps_scene_layer_tile_attr_ptr[0] = ch26f_tile_attr;
    data_fdps_map_cell_event_code_layer_ptr = ch26f_event_layer;
    data_fdps_battle_character_base_table_ptr =
        (unsigned char *) ch26f_char_base;
    data_fdps_battle_character_growth_table_ptr =
        (unsigned char *) ch26f_growth;
    data_fdps_battle_enemy_data_table_ptr = (unsigned char *) ch26f_enemy;
    data_fdps_item_effect_table_ptr = (unsigned char *) ch26f_items;
    data_fdps_current_chapter_text_ptr = ch26f_text_block;

    data_fdps_map_unit_array_ptr =
        (unsigned char *) malloc((size_t) CH26F_UNIT_STRIDE);
    memset(data_fdps_map_unit_array_ptr, 0, (size_t) CH26F_UNIT_STRIDE);
    data_fdps_map_unit_count = 1;
    ch26f_unit(0)->pos_x = 0;
    ch26f_unit(0)->pos_y = 0;

    data_fdps_chapter_current_chapter_id = 0;
    data_fdps_battle_turn_counter = turn;
}

/* ---------------------------------------------------------------------------
   Chapter 26's turn-4 advance order, 00039190.

   The handler has no gate, no latch and no global to branch on: it speaks one
   line and then merges a constant behaviour mode into a fixed range of unit
   records.  So the fixture is the smallest one in this file -- a chapter text
   block and a unit array, nothing else -- and every case here is about the
   three constants the range is built out of, 0x1a at 000391c6, 0x2d at
   000391cd and 0 at 000391d4, and about the half of the AI byte the merge is
   required to leave alone, AND DL,0xf0 at 00039216.

   The line is drawn for real, the same way tests/chevt5.c's chapter 24 cases
   draw theirs: the chapter text pointer is aimed at a block whose every entry
   names one lone terminator, so fdps_draw_text walks the entry, paints nothing
   and returns without needing a font, a message panel or mode 13h.  The block
   runs to 0x17 entries because PUSH 0x16 at 000391b6 asks for the twenty-third
   of them.  Nothing is left behind by a draw that paints nothing, which is why no
   case here asserts a text id; that is pinned in src/chevt5b.c against the
   instruction address instead.

   The unit array is static rather than malloc'd, unlike the shared map fixture
   at the top of the file: this handler deploys nothing, so nothing reallocs
   the block under it.  It is longer than the range on both sides so that a
   case can watch the records the sweep must not touch.
   --------------------------------------------------------------------------- */

/* The array's stride and the record count the fixture stages.  0x32 records
   put one below the range and four above it. */
#define CH26A_UNIT_STRIDE 0x50
#define CH26A_STAGED_UNITS 0x32

/* The range the sweep covers and the two records immediately outside it.  The
   bound is inclusive -- MOV EAX,[EBP-0x8] / CMP EAX,[EBP-0x10] / JLE at
   000391f3 -- so 0x2d is written and 0x2e is not. */
#define CH26A_FIRST_ADVANCING 0x1a
#define CH26A_LAST_ADVANCING 0x2d
#define CH26A_BELOW_RANGE (CH26A_FIRST_ADVANCING - 1)
#define CH26A_ABOVE_RANGE (CH26A_LAST_ADVANCING + 1)

/* What every staged record's AI byte holds before a run and what the merge is
   required to leave behind.  The low nibble is 2, the holding mode MAP25.DAT
   deploys these twenty units in, and the high nibble carries both of the flag
   bits the target scorers read on their own -- 0x40 in
   fdps_map_actor_take_best_action and 0x80 in fdps_score_targets_for_item.  A
   rebuild that wrote the mode as a whole byte would leave 0 here instead of
   0xc0. */
#define CH26A_STAGED_AI_BYTE 0xc2
#define CH26A_ADVANCING_AI_BYTE 0xc0

/* The chapter text block the order is spoken from: 0x17 entries, because PUSH
   0x16 at 000391b6 asks for the last of them, each pointing at the same lone
   terminator so that a draw walks it, paints nothing and returns. */
#define CH26A_TEXT_IDS 0x17
#define CH26A_TEXT_EMPTY_AT 0x40
#define CH26A_TEXT_BLOCK_BYTES (CH26A_TEXT_EMPTY_AT + 2)
#define CH26A_TEXT_END (-1)

/* An argument that is a real unit index and is not the 0 the dispatcher pushes:
   it names the record one below the range, so a handler that used it -- as the
   ambush below does -- would show up as a write outside the range. */
#define CH26A_ARGUMENT_BELOW_RANGE CH26A_BELOW_RANGE

/* A unit count far short of the range, parked in the global so a case can show
   the bounds are literals and not derived from the array length. */
#define CH26A_SHORT_UNIT_COUNT 4

/* Any line height at all; the draw never reaches a glyph. */
#define CH26A_FONT_LINE_HEIGHT 16

static unsigned char ch26a_units[CH26A_STAGED_UNITS * CH26A_UNIT_STRIDE];
static unsigned char ch26a_text_block[CH26A_TEXT_BLOCK_BYTES];

static struct fdps_unit_record *ch26a_unit(int unit_index)
{
    return (struct fdps_unit_record *)
           (data_fdps_map_unit_array_ptr + unit_index * CH26A_UNIT_STRIDE);
}

/* Fifty records all wearing the holding mode with both flag bits set, and a
   text block every entry of which is empty. */
static void ch26a_stage(void)
{
    int i;

    memset(ch26a_text_block, 0, (size_t) CH26A_TEXT_BLOCK_BYTES);
    *(short *) (ch26a_text_block + CH26A_TEXT_EMPTY_AT) = (short) CH26A_TEXT_END;
    for (i = 0; i < CH26A_TEXT_IDS; i++) {
        *(short *) (ch26a_text_block + i * 2) = (short) CH26A_TEXT_EMPTY_AT;
    }
    data_fdps_current_chapter_text_ptr = ch26a_text_block;
    data_fdps_font_line_height = CH26A_FONT_LINE_HEIGHT;

    memset(ch26a_units, 0, sizeof(ch26a_units));
    data_fdps_map_unit_array_ptr = ch26a_units;
    data_fdps_map_unit_count = CH26A_STAGED_UNITS;
    for (i = 0; i < CH26A_STAGED_UNITS; i++) {
        ch26a_unit(i)->ai_behavior = (unsigned char) CH26A_STAGED_AI_BYTE;
    }
}

/* The one field the handler reaches through and the stride it is indexed by.
   The merge is applied to byte [EAX+0x34] at 00039213 and 00039221, and
   fdps_get_unit_record multiplies the index by 0x50, so those are the offset
   and the stride the emitted names have to sit at. */
static void ch26a_record_shape_matches_the_offsets(void)
{
    CHECK_EQ((int) sizeof(struct fdps_unit_record), CH26A_UNIT_STRIDE);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, ai_behavior), 0x34);
}

/* Every record of the range comes out of the holding mode, and every one of
   them keeps its flag bits: 0xc2 becomes 0xc0 and not 0. */
static void ch26a_clears_the_mode_over_the_whole_range(void)
{
    int i;
    int wrong_value;

    ch26a_stage();

    fdps_chapter_26_event_enemies_advance(0);

    wrong_value = 0;
    for (i = CH26A_FIRST_ADVANCING; i <= CH26A_LAST_ADVANCING; i++) {
        if ((int) ch26a_unit(i)->ai_behavior != CH26A_ADVANCING_AI_BYTE) {
            wrong_value++;
        }
    }
    CHECK_EQ(wrong_value, 0);
    CHECK_EQ(CH26A_LAST_ADVANCING - CH26A_FIRST_ADVANCING + 1, 20);
}

/* The bound is inclusive at the top and the range starts where it starts: the
   record at 0x2d is written and the ones at 0x19 and 0x2e are not. */
static void ch26a_the_range_is_inclusive_at_both_ends(void)
{
    ch26a_stage();

    fdps_chapter_26_event_enemies_advance(0);

    CHECK_EQ((int) ch26a_unit(CH26A_FIRST_ADVANCING)->ai_behavior,
             CH26A_ADVANCING_AI_BYTE);
    CHECK_EQ((int) ch26a_unit(CH26A_LAST_ADVANCING)->ai_behavior,
             CH26A_ADVANCING_AI_BYTE);
    CHECK_EQ((int) ch26a_unit(CH26A_BELOW_RANGE)->ai_behavior,
             CH26A_STAGED_AI_BYTE);
    CHECK_EQ((int) ch26a_unit(CH26A_ABOVE_RANGE)->ai_behavior,
             CH26A_STAGED_AI_BYTE);
}

/* Nothing outside the twenty records is touched at all, from index 0 up to the
   end of the staged array. */
static void ch26a_writes_nothing_outside_the_range(void)
{
    int i;
    int disturbed;

    ch26a_stage();

    fdps_chapter_26_event_enemies_advance(0);

    disturbed = 0;
    for (i = 0; i < CH26A_STAGED_UNITS; i++) {
        if (i >= CH26A_FIRST_ADVANCING && i <= CH26A_LAST_ADVANCING) {
            continue;
        }
        if ((int) ch26a_unit(i)->ai_behavior != CH26A_STAGED_AI_BYTE) {
            disturbed++;
        }
    }
    CHECK_EQ(disturbed, 0);
}

/* The whole high nibble survives, not just the two bits the fixture's usual
   byte carries: each record of the range is staged with a different one and
   every one of them comes back unchanged with a zero low nibble. */
static void ch26a_keeps_the_high_nibble_of_every_record(void)
{
    int i;
    int wrong_value;
    int expected;

    ch26a_stage();
    for (i = CH26A_FIRST_ADVANCING; i <= CH26A_LAST_ADVANCING; i++) {
        ch26a_unit(i)->ai_behavior =
            (unsigned char) (((i & 0xf) << 4) | 0x0f);
    }

    fdps_chapter_26_event_enemies_advance(0);

    wrong_value = 0;
    for (i = CH26A_FIRST_ADVANCING; i <= CH26A_LAST_ADVANCING; i++) {
        expected = (i & 0xf) << 4;
        if ((int) ch26a_unit(i)->ai_behavior != expected) {
            wrong_value++;
        }
    }
    CHECK_EQ(wrong_value, 0);
    CHECK_EQ((int) ch26a_unit(CH26A_FIRST_ADVANCING)->ai_behavior,
             (CH26A_FIRST_ADVANCING & 0xf) << 4);
}

/* The incoming argument is stored over and never read: a run handed the index
   of the record just below the range does exactly what a run handed 0 does,
   and that record is still holding afterwards. */
static void ch26a_ignores_the_incoming_argument(void)
{
    ch26a_stage();

    fdps_chapter_26_event_enemies_advance(CH26A_ARGUMENT_BELOW_RANGE);

    CHECK_EQ((int) ch26a_unit(CH26A_ARGUMENT_BELOW_RANGE)->ai_behavior,
             CH26A_STAGED_AI_BYTE);
    CHECK_EQ((int) ch26a_unit(CH26A_FIRST_ADVANCING)->ai_behavior,
             CH26A_ADVANCING_AI_BYTE);
    CHECK_EQ((int) ch26a_unit(CH26A_LAST_ADVANCING)->ai_behavior,
             CH26A_ADVANCING_AI_BYTE);
}

/* The bounds are literals and not the unit count: a count of four leaves the
   same twenty records written.  Nothing in the body reads
   data_fdps_map_unit_count, unlike the chapter 25 sweep in src/chevt5.c, which
   takes its top bound from it. */
static void ch26a_range_does_not_follow_the_unit_count(void)
{
    ch26a_stage();
    data_fdps_map_unit_count = CH26A_SHORT_UNIT_COUNT;

    fdps_chapter_26_event_enemies_advance(0);

    CHECK_EQ((int) ch26a_unit(CH26A_FIRST_ADVANCING)->ai_behavior,
             CH26A_ADVANCING_AI_BYTE);
    CHECK_EQ((int) ch26a_unit(CH26A_LAST_ADVANCING)->ai_behavior,
             CH26A_ADVANCING_AI_BYTE);
    CHECK_EQ(data_fdps_map_unit_count, CH26A_SHORT_UNIT_COUNT);
}

/* Running it twice changes nothing the first run did not: there is no latch and
   no counter, so the order is simply re-issued. */
static void ch26a_has_no_latch_and_repeats_cleanly(void)
{
    ch26a_stage();

    fdps_chapter_26_event_enemies_advance(0);
    ch26a_unit(CH26A_FIRST_ADVANCING)->ai_behavior =
        (unsigned char) CH26A_STAGED_AI_BYTE;
    fdps_chapter_26_event_enemies_advance(0);

    CHECK_EQ((int) ch26a_unit(CH26A_FIRST_ADVANCING)->ai_behavior,
             CH26A_ADVANCING_AI_BYTE);
    CHECK_EQ((int) ch26a_unit(CH26A_BELOW_RANGE)->ai_behavior,
             CH26A_STAGED_AI_BYTE);
}

/* ---------------------------------------------------------------------------
   fdps_chapter_26_event_deploy_waves_2_and_3 @ 00039230

   The shared map fixture above carries this section too, given a unit array
   long enough for the sweep: the release range is two literals, 0x0c and 0x4f,
   so the array has to reach index 0x4f before the handler is called at all or
   the loop writes past the allocation.  Eighty records put twelve party slots
   below the range, sixty-eight inside it and the two arrivals immediately
   above it.

   Both deployments are real, the same way tests/chevt5.c's chapter 24 and 25
   cases are: fdps_deploy_wave opens ICON.CEL and FIELD.VFS for itself, staged
   through tests/gamefile.lst, and every case that fires skips itself when they
   are not there -- a run without them would not fail a check, it would hang in
   fdps_wait_any_key.  The three lines are drawn for real as well, into the
   shared fixture's text block, whose every entry names one lone terminator
   so a draw walks it, paints nothing and returns.  That block holds 0x15
   entries and the largest id asked for here is 0xc, so all three are live.

   What the cases are really for is the pair of waves and the order they land
   in.  Four deployment records are staged, tagged waves 1, 2, 3 and 4 and
   carrying four different character ids, so a rebuild that drifted by one wave
   in either direction deploys a different character and a rebuild that swapped
   the two deployments appends them the other way round.  The two records the
   handler must deploy also carry AI classes of their own, which is what says
   the sweep stopped at 0x4f instead of running to the end of the array.

   Everything the handler does is asserted out of ONE firing.  A firing costs
   two real deployments and every frame it composes waits a timer tick, and the
   whole test image has a wall-clock budget, so the fixture parks the cursor
   one tile above the pan's target: the walk takes a single step, which is
   enough to say where it was aimed, and the twelve frames of the hold are
   still the bulk of what the firing composes.
   --------------------------------------------------------------------------- */

/* The one-shot latch element, shared with chapter 25's ambush: byte
   [0x000640e8], element 0x10 of the block at 0x000640d8. */
#define CH26B_LATCH_SLOT 0x10

/* The side values the gate is tested with.  2 is the only one that fires. */
#define CH26B_SIDE_PLAYER 2
#define CH26B_SIDE_ENEMY 0
#define CH26B_SIDE_GUEST 1
#define CH26B_SIDE_ABOVE_PLAYER 3

/* Units already on the map when the handler runs, and the indices the two
   arrivals therefore land on.  Eighty is one more than the sweep's last index,
   so the range is covered and the arrivals sit above it. */
#define CH26B_STAGED_UNITS 0x50
#define CH26B_ENEMY_ARRIVAL_INDEX CH26B_STAGED_UNITS
#define CH26B_ALLY_ARRIVAL_INDEX (CH26B_STAGED_UNITS + 1)

/* The unit the fixture puts on the side that springs the ambush.  It is
   neither the first record nor the last, so a handler that read unit 0, or the
   last unit, or ignored the argument would fire on the wrong call. */
#define CH26B_TRIGGERING_UNIT_INDEX 7

/* The four deployment records and what each is tagged with: the two waves the
   handler asks for, PUSH 0x2 at 0003926c and PUSH 0x3 at 00039309, with one
   record either side of them so a drift by one wave deploys a different
   character. */
#define CH26B_SPAWN_RECORD_COUNT 4
#define CH26B_BELOW_RECORD 0
#define CH26B_ENEMY_RECORD 1
#define CH26B_ALLY_RECORD 2
#define CH26B_ABOVE_RECORD 3
#define CH26B_WAVE_BELOW 1
#define CH26B_ENEMY_WAVE 2
#define CH26B_ALLY_WAVE 3
#define CH26B_WAVE_ABOVE 4

/* A different character id per record, all four inside the eight rows the
   shared fixture gives the character tables, so the id a deployed unit comes
   back carrying says which record was deployed. */
#define CH26B_BELOW_CHAR_ID 4
#define CH26B_ENEMY_CHAR_ID 5
#define CH26B_ALLY_CHAR_ID 6
#define CH26B_ABOVE_CHAR_ID 7

/* The AI classes the two deployed records carry.  fdps_deploy_unit copies the
   class straight into the arrival's AI byte, so a sweep that ran past 0x4f
   would overwrite these with 0xc0. */
#define CH26B_ENEMY_ARRIVAL_AI_BYTE 0x37
#define CH26B_ALLY_ARRIVAL_AI_BYTE 0x38

/* The range the sweep covers, MOV dword ptr [EBP-0x24],0xc at 0003933c and MOV
   dword ptr [EBP-0x20],0x4f at 00039343, and the last party slot below it. */
#define CH26B_FIRST_RELEASED_INDEX 0xc
#define CH26B_LAST_RELEASED_INDEX 0x4f
#define CH26B_LAST_HELD_INDEX (CH26B_FIRST_RELEASED_INDEX - 1)
#define CH26B_RELEASED_UNITS \
    (CH26B_LAST_RELEASED_INDEX - CH26B_FIRST_RELEASED_INDEX + 1)

/* What every staged record's AI byte holds before a firing and what the merge
   is required to leave behind: the low nibble goes to 0 and the high nibble --
   the two flag bits the target scorers read -- is carried across untouched,
   AND DL,0xf0 / OR DH,DL at 0003938c..00039395.  A rebuild that wrote the mode
   as a whole byte would leave 0 here instead of 0xc0. */
#define CH26B_STAGED_AI_BYTE 0xc2
#define CH26B_RELEASED_AI_BYTE 0xc0

/* The cursor mode the fixture parks in data_fdps_map_cursor_draw_mode before
   every run: neither of the two values the handler writes, so a run that left
   it alone, a run that hid the cursor and never put it back, and a run that
   restored what it found are all told apart from the mode the handler is
   supposed to leave behind. */
#define CH26B_STAGED_CURSOR_MODE 4
#define CH26B_CURSOR_MODE_NORMAL 1

/* Where the pan ends, PUSH 0x378 at 000392a9 and PUSH 0xf0 at 000392ae, and
   where the cursor is parked before the firing: the same column and one
   24-pixel tile above it, so the dominant axis is y, the walk is a single
   whole-tile step and it lands on the target exactly rather than a few pixels
   short of it.  A rebuild that swapped the two pushes walks a different axis
   and finishes nowhere near either coordinate. */
#define CH26B_PAN_END_X 0xf0
#define CH26B_PAN_END_Y 0x378
#define CH26B_PAN_TILE 0x18
#define CH26B_PAN_START_Y (CH26B_PAN_END_Y - CH26B_PAN_TILE)

/* How many frames the firing composes and the least the tick counter can move
   across them: the twelve of the hold, CMP dword ptr [EBP+0x14],0xc / JL at
   000392c2, plus the one the single-step walk draws.  Each composed frame
   after the first waits for the counter to differ from the one the frame
   before it recorded, so thirteen frames cannot pass in fewer than twelve
   ticks.  The bound is one-sided on purpose -- a slow machine spends more,
   never fewer -- so it cannot fail spuriously; what it cannot do on its own is
   prove a firing that skipped the hold did not spend its ticks elsewhere,
   which is why the frame latch is checked too. */
#define CH26B_HOLD_FRAMES 0xc
#define CH26B_PAN_FRAMES 1
#define CH26B_LEAST_HOLD_TICKS (CH26B_HOLD_FRAMES + CH26B_PAN_FRAMES - 1)

/* A value the frame latch cannot legitimately hold, parked in it so a run that
   composed nothing is distinguishable from one that did. */
#define CH26B_FRAME_SENTINEL 0x5a5a5a5aU

/* Any turn at all: nothing in this handler reads the turn counter, and the
   fixture sets one because the shared map fixture it is built on wants one. */
#define CH26B_ANY_TURN 6

#define CH26B_VGA_MODE_TEXT 0x03
#define CH26B_VGA_MODE_320X200X256 0x13
#define CH26B_TIMER_VECTOR 8

static unsigned int ch26b_ticks_before;
static unsigned int ch26b_ticks_after;
static void (__interrupt __far *ch26b_saved_timer)();

static void __interrupt __far ch26b_timer_isr(void)
{
    ++data_fdps_timer_tick_counter;
    _chain_intr(ch26b_saved_timer);
}

static void ch26b_set_mode(int mode)
{
    union REGS regs;

    memset(&regs, 0, sizeof(regs));
    regs.x.eax = (unsigned) mode;
    int386(0x10, &regs, &regs);
}

/* The shared map fixture given an eighty-record unit array every one of whose
   units is on the enemy's side and wears the holding AI byte, four deployment
   records at the waves the cases ask about, and the compositor's own globals.
   Only the record at CH26B_TRIGGERING_UNIT_INDEX can spring the ambush.  The
   array is malloc'd because both deployments realloc it. */
static void ch26b_stage(void)
{
    int i;

    ch26f_stage(CH26B_ANY_TURN);

    data_fdps_map_unit_array_ptr = (unsigned char *)
        malloc((size_t) (CH26B_STAGED_UNITS * CH26F_UNIT_STRIDE));
    memset(data_fdps_map_unit_array_ptr, 0,
           (size_t) (CH26B_STAGED_UNITS * CH26F_UNIT_STRIDE));
    data_fdps_map_unit_count = CH26B_STAGED_UNITS;
    for (i = 0; i < CH26B_STAGED_UNITS; i++) {
        ch26f_unit(i)->side = (unsigned char) CH26B_SIDE_ENEMY;
        ch26f_unit(i)->ai_behavior = (unsigned char) CH26B_STAGED_AI_BYTE;
        ch26f_unit(i)->pos_x = 0;
        ch26f_unit(i)->pos_y = 0;
    }
    ch26f_unit(CH26B_TRIGGERING_UNIT_INDEX)->side =
        (unsigned char) CH26B_SIDE_PLAYER;

    ch26f_spawn_table[CH26F_SPAWN_TABLE_COUNT_OFFSET] =
        (unsigned char) CH26B_SPAWN_RECORD_COUNT;
    ch26f_set_spawn(CH26B_BELOW_RECORD, CH26B_BELOW_CHAR_ID,
                    CH26B_WAVE_BELOW);
    ch26f_set_spawn(CH26B_ENEMY_RECORD, CH26B_ENEMY_CHAR_ID,
                    CH26B_ENEMY_WAVE);
    ch26f_set_spawn(CH26B_ALLY_RECORD, CH26B_ALLY_CHAR_ID, CH26B_ALLY_WAVE);
    ch26f_set_spawn(CH26B_ABOVE_RECORD, CH26B_ABOVE_CHAR_ID,
                    CH26B_WAVE_ABOVE);
    ch26f_spawn_at(CH26B_ENEMY_RECORD)->ai_class =
        (unsigned char) CH26B_ENEMY_ARRIVAL_AI_BYTE;
    ch26f_spawn_at(CH26B_ALLY_RECORD)->ai_class =
        (unsigned char) CH26B_ALLY_ARRIVAL_AI_BYTE;

    data_fdps_scene_layer_count = 0;
    data_fdps_ui_terrain_hud_user_enabled = 0;
    data_fdps_ui_play_active_flag = 0;
    data_fdps_battle_view_window_origin_x = 0;
    data_fdps_battle_view_window_origin_y = 0;
    data_fdps_map_cursor_world_x = CH26B_PAN_END_X;
    data_fdps_map_cursor_world_y = CH26B_PAN_START_Y;
    data_fdps_view_frame_last_tick = CH26B_FRAME_SENTINEL;
    data_fdps_map_cursor_draw_mode = CH26B_STAGED_CURSOR_MODE;

    data_fdps_map_cell_event_triggered_flags[CH26B_LATCH_SLOT] = 0;
}

/* One firing, with the timer running and the adapter in the mode the frames
   present through, and the tick counter sampled either side so the length of
   the hold can be read back.  Text mode is back before anything is asserted,
   so a failure prints on a readable screen. */
static void ch26b_run(int unit_index)
{
    ch26b_set_mode(CH26B_VGA_MODE_320X200X256);
    ch26b_saved_timer = _dos_getvect(CH26B_TIMER_VECTOR);
    _dos_setvect(CH26B_TIMER_VECTOR, ch26b_timer_isr);
    ch26b_ticks_before = data_fdps_timer_tick_counter;
    fdps_chapter_26_event_deploy_waves_2_and_3(unit_index);
    ch26b_ticks_after = data_fdps_timer_tick_counter;
    _dos_setvect(CH26B_TIMER_VECTOR, ch26b_saved_timer);
    ch26b_set_mode(CH26B_VGA_MODE_TEXT);
}

/* How many records inside the release range are not holding the value the
   merge must leave, and how many outside it have moved at all. */
static int ch26b_wrong_in_range(int want)
{
    int i;
    int wrong;

    wrong = 0;
    for (i = CH26B_FIRST_RELEASED_INDEX; i <= CH26B_LAST_RELEASED_INDEX; i++) {
        if ((int) ch26f_unit(i)->ai_behavior != want) {
            wrong++;
        }
    }
    return wrong;
}

static int ch26b_wrong_below_range(int want)
{
    int i;
    int wrong;

    wrong = 0;
    for (i = 0; i <= CH26B_LAST_HELD_INDEX; i++) {
        if ((int) ch26f_unit(i)->ai_behavior != want) {
            wrong++;
        }
    }
    return wrong;
}

/* The three fields the handler reaches through and the stride they are indexed
   by.  CMP EAX,0x2 is applied to byte [EAX+0x6] at 00039257 and the merge to
   byte [EAX+0x34] at 00039389, so those are the offsets the emitted field
   names have to sit at; the wave byte is what each deployment walk matches its
   wave number against. */
static void ch26b_record_shape_matches_the_offsets(void)
{
    CHECK_EQ((int) sizeof(struct fdps_unit_record), CH26F_UNIT_STRIDE);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, side), 6);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, ai_behavior), 0x34);
    CHECK_EQ((int) offsetof(struct fdps_char_spawn_record, wave_no), 0x15);
}

/* A latch that is already up refuses the whole body, and it is tested against
   0 rather than against 1 -- CMP byte ptr [0x000640e8],0x0 / JNZ at 0003924b
   -- so any non-zero value in the slot blocks it.  Nothing is deployed, no
   frame is composed, no AI byte moves and the cursor mode is left exactly as
   it was found, which is what says the store of 0 at 0003929f is inside the
   gate and not ahead of it. */
static void ch26b_latch_blocks_the_whole_body(void)
{
    static int latch_values[2] = {1, 0x7f};
    int i;

    for (i = 0; i < 2; i++) {
        ch26b_stage();
        data_fdps_map_cell_event_triggered_flags[CH26B_LATCH_SLOT] =
            (unsigned char) latch_values[i];

        fdps_chapter_26_event_deploy_waves_2_and_3(
            CH26B_TRIGGERING_UNIT_INDEX);

        CHECK_EQ(data_fdps_map_unit_count, CH26B_STAGED_UNITS);
        CHECK_EQ(data_fdps_map_cell_event_triggered_flags[CH26B_LATCH_SLOT],
                 latch_values[i]);
        CHECK_EQ(data_fdps_map_cursor_draw_mode, CH26B_STAGED_CURSOR_MODE);
        CHECK_EQ(data_fdps_view_frame_last_tick == CH26B_FRAME_SENTINEL, 1);
        CHECK_EQ(ch26b_wrong_in_range(CH26B_STAGED_AI_BYTE), 0);
    }
}

/* The side gate is an equality against 2 and not a non-zero test and not a
   ">= 2" test: sides 0, 1 and 3 are all refused and all leave the ambush
   armed, so an enemy or a guest crossing the tile does not consume the event
   and a side above the player's does not spring it either. */
static void ch26b_only_the_player_side_fires(void)
{
    static int refused_sides[3] = {
        CH26B_SIDE_ENEMY, CH26B_SIDE_GUEST, CH26B_SIDE_ABOVE_PLAYER
    };
    int i;

    for (i = 0; i < 3; i++) {
        ch26b_stage();
        ch26f_unit(CH26B_TRIGGERING_UNIT_INDEX)->side =
            (unsigned char) refused_sides[i];

        fdps_chapter_26_event_deploy_waves_2_and_3(
            CH26B_TRIGGERING_UNIT_INDEX);

        CHECK_EQ(data_fdps_map_unit_count, CH26B_STAGED_UNITS);
        CHECK_EQ(data_fdps_map_cell_event_triggered_flags[CH26B_LATCH_SLOT], 0);
        CHECK_EQ(data_fdps_map_cursor_draw_mode, CH26B_STAGED_CURSOR_MODE);
        CHECK_EQ(data_fdps_view_frame_last_tick == CH26B_FRAME_SENTINEL, 1);
        CHECK_EQ(ch26b_wrong_in_range(CH26B_STAGED_AI_BYTE), 0);
    }
}

/* Everything the body does, out of one firing, because a firing is expensive.
   The fixture puts seventy-nine of its eighty units on side 0 and leaves one
   able to spring the ambush, so the indices that name a side-0 unit are
   refused -- they leave the latch down, which is what lets them run without
   restaging -- and only the index that names the eighth fires.

   THE SIDE BYTE READ BELONGS TO THE RECORD THE ARGUMENT NAMES.  A handler that
   read unit 0, or the last unit, or ignored the argument would fire on one of
   the three refused calls.

   TWO WAVES ARRIVE, 2 THEN 3.  Exactly two units are appended; the first
   carries the character id of the record tagged wave 2 and the second the id
   of the record tagged wave 3, so a rebuild that drifted by one wave in either
   direction appends a different character and a rebuild that swapped the two
   deployments appends them the other way round.  The records tagged 1 and 4
   stay where they are.

   THE PAN ENDS ON (0xf0, 0x378).  The cursor is parked one whole tile above
   that, so the walk is a single step down the same column and lands on the
   target exactly; a rebuild that swapped the two pushes walks the other axis
   and finishes nowhere near either coordinate.

   THE HOLD RAN.  The walk contributes one frame and the hold twelve, and every
   frame after the first waits for a timer tick: the frame latch comes back off
   its sentinel and the tick counter moves by at least twelve.

   THE CURSOR MODE IS 1 AFTERWARDS AND IS NOT THE MODE THE RUN FOUND.  The
   fixture parks 4 in the global, so a rebuild that saved and restored it, or
   that left the blank 0 behind, is caught.

   THE SWEEP RUNS 0x0c..0x4f INCLUSIVE AND KEEPS THE HIGH NIBBLE.  Every party
   slot from 0 to 11 still holds the staged 0xc2; every index from 12 to 0x4f
   holds 0xc0 -- mode 0 merged under the preserved 0xc0 -- and both arrivals,
   at 0x50 and 0x51, still carry the AI class their deployment records gave
   them, which is what says the bound is the literal 0x4f and not the unit
   count.

   THE LATCH IS SPENT.  It is 1 afterwards, and a second call with the same
   index deploys nothing more. */
static void ch26b_one_firing_does_everything(void)
{
    static int enemy_indices[3] = {0, 1, CH26B_STAGED_UNITS - 1};
    int i;

    ch26b_stage();
    for (i = 0; i < 3; i++) {
        fdps_chapter_26_event_deploy_waves_2_and_3(enemy_indices[i]);
        CHECK_EQ(data_fdps_map_cell_event_triggered_flags[CH26B_LATCH_SLOT], 0);
        CHECK_EQ(data_fdps_map_unit_count, CH26B_STAGED_UNITS);
    }

    ch26f_ensure_game_files();
    if (!ch26f_files_ready) {
        return;
    }

    ch26b_run(CH26B_TRIGGERING_UNIT_INDEX);

    CHECK_EQ(data_fdps_map_unit_count, CH26B_STAGED_UNITS + 2);
    CHECK_EQ((int) ch26f_unit(CH26B_ENEMY_ARRIVAL_INDEX)->char_id,
             CH26B_ENEMY_CHAR_ID);
    CHECK_EQ((int) ch26f_unit(CH26B_ALLY_ARRIVAL_INDEX)->char_id,
             CH26B_ALLY_CHAR_ID);

    CHECK_EQ(data_fdps_view_frame_last_tick == CH26B_FRAME_SENTINEL, 0);
    CHECK_EQ(ch26b_ticks_after - ch26b_ticks_before
                 >= (unsigned int) CH26B_LEAST_HOLD_TICKS,
             1);
    CHECK_EQ(data_fdps_map_cursor_draw_mode, CH26B_CURSOR_MODE_NORMAL);
    CHECK_EQ(data_fdps_map_cursor_world_x, CH26B_PAN_END_X);
    CHECK_EQ(data_fdps_map_cursor_world_y, CH26B_PAN_END_Y);

    CHECK_EQ(ch26b_wrong_below_range(CH26B_STAGED_AI_BYTE), 0);
    CHECK_EQ(ch26b_wrong_in_range(CH26B_RELEASED_AI_BYTE), 0);
    CHECK_EQ(CH26B_RELEASED_UNITS, 68);
    CHECK_EQ((int) ch26f_unit(CH26B_ENEMY_ARRIVAL_INDEX)->ai_behavior,
             CH26B_ENEMY_ARRIVAL_AI_BYTE);
    CHECK_EQ((int) ch26f_unit(CH26B_ALLY_ARRIVAL_INDEX)->ai_behavior,
             CH26B_ALLY_ARRIVAL_AI_BYTE);

    CHECK_EQ(data_fdps_map_cell_event_triggered_flags[CH26B_LATCH_SLOT], 1);
    fdps_chapter_26_event_deploy_waves_2_and_3(CH26B_TRIGGERING_UNIT_INDEX);
    CHECK_EQ(data_fdps_map_unit_count, CH26B_STAGED_UNITS + 2);
}

/* ---------------------------------------------------------------------------
   fdps_chapter_26_event_wave_2_defeated_line @ 000393b0

   The handler has no argument to work on, deploys nothing and moves nothing:
   it polls seven fixed unit records through fdps_unit_is_retired and, when all
   seven answer non-zero, speaks one line and raises a one-shot latch.  So the
   fixture is a unit array and a chapter text block, and the whole of what a
   case can watch is the latch byte -- the draw itself leaves nothing behind.

   The line is drawn for real, the same way the two chapter 26 cases above
   draw theirs: the chapter text pointer is aimed at a block whose every entry
   names one lone terminator, so fdps_draw_text walks the entry, paints nothing
   and returns without needing a font, a message panel or mode 13h.  The block
   runs to 0x18 entries because PUSH 0x17 at 00039418 asks for the last of them.

   The latch is a fair proxy for the draw: MOV byte ptr [0x000640ea],0x1 at
   00039428 sits inside the same guarded block as the CALL at 00039420 and
   nothing can reach one without the other, so a case that says the latch was
   raised says the line was spoken.

   What the cases are really for is the poll: which seven records are asked, in
   what sense their answers are read, and which of the 0x20 latch elements the
   result is written to.  The seven are indices 0x50..0x56 -- ADD EAX,0x50 at
   000393e6 over a counter run from 0 to 6 by the CMP dword ptr [EBP+0x14],0x7
   at 000393d3 -- so the fixture stages records either side of both ends and
   fires with the block shifted one record each way.

   The unit array is static rather than malloc'd, like the chapter 26 advance
   fixture above: this handler deploys nothing, so nothing reallocs the block
   under it.  It reaches index 0x5f so that the records above the polled block
   can be watched too.
   --------------------------------------------------------------------------- */

/* The array's stride and the record count the fixture stages.  0x60 records
   put 0x50 below the polled block and nine above it. */
#define CH26W_UNIT_STRIDE 0x50
#define CH26W_STAGED_UNITS 0x60

/* The seven records the poll covers and the two immediately outside it. */
#define CH26W_FIRST_POLLED 0x50
#define CH26W_POLLED_COUNT 7
#define CH26W_LAST_POLLED (CH26W_FIRST_POLLED + CH26W_POLLED_COUNT - 1)
#define CH26W_BELOW_POLLED (CH26W_FIRST_POLLED - 1)
#define CH26W_ABOVE_POLLED (CH26W_LAST_POLLED + 1)

/* The latch element this handler owns, byte [0x000640ea] at 000393c3 and
   00039428 -- element 0x12 of the block at 0x000640d8 -- and the two elements
   the ambush above and chapter 25's handlers in tests/chevt5.c own, which it
   must leave alone. */
#define CH26W_LATCH_SLOT 0x12
#define CH26W_AMBUSH_LATCH_SLOT 0x10
#define CH26W_BOW_LATCH_SLOT 0x11
#define CH26W_LATCH_SLOT_ABOVE 0x13

/* A latch value that is neither 0 nor the 1 the handler writes: the test at
   000393c3 is against 0, so this has to block the body, and a body that ran
   anyway would leave 1 behind in its place. */
#define CH26W_LATCH_ALREADY_UP 2

/* The bit fdps_unit_is_retired reads, AND AL,0x1 at 000109d1 on the flags byte
   at record offset 5, and a flags byte carrying every other bit but that one --
   0x80 is the per-turn redraw flag, which a unit standing on the map does carry
   and which must not read as retirement. */
#define CH26W_RETIRED_BIT 0x01
#define CH26W_EVERY_BIT_BUT_RETIRED 0xfe
#define CH26W_EVERY_BIT 0xff

/* The chapter text block the line is spoken from: 0x18 entries, because PUSH
   0x17 at 00039418 asks for the last of them, each pointing at the same lone
   terminator so that a draw walks it, paints nothing and returns. */
#define CH26W_TEXT_IDS 0x18
#define CH26W_TEXT_EMPTY_AT 0x40
#define CH26W_TEXT_BLOCK_BYTES (CH26W_TEXT_EMPTY_AT + 2)
#define CH26W_TEXT_END (-1)

/* Any line height at all; the draw never reaches a glyph. */
#define CH26W_FONT_LINE_HEIGHT 16

/* Arguments a case fires with.  The dispatcher pushes the index of the unit
   that made the killing action, so both of these are indices a real firing
   could carry: one inside the polled block and one well below it. */
#define CH26W_ARGUMENT_INSIDE_BLOCK (CH26W_FIRST_POLLED + 1)
#define CH26W_ARGUMENT_KILLER 3

/* A unit count far short of the polled block, parked in the global so a case
   can show the block is two literals and not derived from the array length. */
#define CH26W_SHORT_UNIT_COUNT 4

static unsigned char ch26w_units[CH26W_STAGED_UNITS * CH26W_UNIT_STRIDE];
static unsigned char ch26w_text_block[CH26W_TEXT_BLOCK_BYTES];

static struct fdps_unit_record *ch26w_unit(int unit_index)
{
    return (struct fdps_unit_record *)
           (data_fdps_map_unit_array_ptr + unit_index * CH26W_UNIT_STRIDE);
}

/* Ninety-six records with every flags byte clear -- so every one of them is
   standing -- a text block whose every entry is empty, and a latch block with
   nothing raised in it. */
static void ch26w_stage(void)
{
    int i;

    memset(ch26w_text_block, 0, (size_t) CH26W_TEXT_BLOCK_BYTES);
    *(short *) (ch26w_text_block + CH26W_TEXT_EMPTY_AT) = (short) CH26W_TEXT_END;
    for (i = 0; i < CH26W_TEXT_IDS; i++) {
        *(short *) (ch26w_text_block + i * 2) = (short) CH26W_TEXT_EMPTY_AT;
    }
    data_fdps_current_chapter_text_ptr = ch26w_text_block;
    data_fdps_font_line_height = CH26W_FONT_LINE_HEIGHT;

    memset(ch26w_units, 0, sizeof(ch26w_units));
    data_fdps_map_unit_array_ptr = ch26w_units;
    data_fdps_map_unit_count = CH26W_STAGED_UNITS;

    memset(data_fdps_map_cell_event_triggered_flags, 0,
           sizeof(data_fdps_map_cell_event_triggered_flags));
}

/* Takes the seven records starting at first_index out of the battle, so a case
   can slide the retired block up and down against the block the handler polls. */
static void ch26w_retire_seven_from(int first_index)
{
    int i;

    for (i = 0; i < CH26W_POLLED_COUNT; i++) {
        ch26w_unit(first_index + i)->flags = (unsigned char) CH26W_RETIRED_BIT;
    }
}

/* The two record fields the poll reaches through and the stride it is indexed
   by.  fdps_unit_is_retired reads byte [EAX+0x5] at 000109ce and
   fdps_get_unit_record multiplies the index by 0x50 at 0002d21c, so those are
   the offset and the stride the fixture's own indexing has to agree with. */
static void ch26w_record_shape_matches_the_offsets(void)
{
    CHECK_EQ((int) sizeof(struct fdps_unit_record), CH26W_UNIT_STRIDE);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, flags), 5);
}

/* All seven retired and the latch clear: the line is spoken and the latch is
   raised. */
static void ch26w_the_last_death_speaks_the_line(void)
{
    ch26w_stage();
    ch26w_retire_seven_from(CH26W_FIRST_POLLED);

    CHECK_EQ(data_fdps_map_cell_event_triggered_flags[CH26W_LATCH_SLOT], 0);
    fdps_chapter_26_event_wave_2_defeated_line(CH26W_ARGUMENT_KILLER);
    CHECK_EQ(data_fdps_map_cell_event_triggered_flags[CH26W_LATCH_SLOT], 1);
}

/* One survivor anywhere in the seven holds the line back, wherever it stands:
   the block is retired in full and then one record of it is put back on its
   feet, once for each of the seven positions.  A rebuild that stopped polling
   after the first answer, or that read the flag the wrong way round, fires on
   at least one of these. */
static void ch26w_one_survivor_anywhere_holds_the_line(void)
{
    int survivor;
    int positions_tested;
    int fired_anyway;

    positions_tested = 0;
    fired_anyway = 0;
    for (survivor = CH26W_FIRST_POLLED; survivor <= CH26W_LAST_POLLED;
         survivor++) {
        ch26w_stage();
        ch26w_retire_seven_from(CH26W_FIRST_POLLED);
        ch26w_unit(survivor)->flags = 0;

        fdps_chapter_26_event_wave_2_defeated_line(CH26W_ARGUMENT_KILLER);

        if (data_fdps_map_cell_event_triggered_flags[CH26W_LATCH_SLOT] != 0) {
            fired_anyway++;
        }
        positions_tested++;
    }
    CHECK_EQ(fired_anyway, 0);
    CHECK_EQ(positions_tested, CH26W_POLLED_COUNT);
}

/* The polled block is 0x50..0x56 and nothing else.  Retiring exactly those
   seven fires while the records at 0x4f and 0x57 are still standing; sliding
   the retired block one record down leaves 0x56 standing and sliding it one
   record up leaves 0x50 standing, and neither fires. */
static void ch26w_the_block_is_the_seven_records_from_0x50(void)
{
    ch26w_stage();
    ch26w_retire_seven_from(CH26W_FIRST_POLLED);
    fdps_chapter_26_event_wave_2_defeated_line(CH26W_ARGUMENT_KILLER);
    CHECK_EQ(data_fdps_map_cell_event_triggered_flags[CH26W_LATCH_SLOT], 1);

    ch26w_stage();
    ch26w_retire_seven_from(CH26W_BELOW_POLLED);
    fdps_chapter_26_event_wave_2_defeated_line(CH26W_ARGUMENT_KILLER);
    CHECK_EQ(data_fdps_map_cell_event_triggered_flags[CH26W_LATCH_SLOT], 0);

    ch26w_stage();
    ch26w_retire_seven_from(CH26W_ABOVE_POLLED);
    fdps_chapter_26_event_wave_2_defeated_line(CH26W_ARGUMENT_KILLER);
    CHECK_EQ(data_fdps_map_cell_event_triggered_flags[CH26W_LATCH_SLOT], 0);
}

/* A latch that is already up refuses the whole body, and it is tested against
   zero rather than against one: an element staged with 2 comes back holding 2,
   where a body that ran would have written 1 over it.  That is also what says
   the line cannot be spoken twice -- the six other deaths of the same seven
   each run this handler again. */
static void ch26w_a_raised_latch_refuses_the_body(void)
{
    ch26w_stage();
    ch26w_retire_seven_from(CH26W_FIRST_POLLED);
    data_fdps_map_cell_event_triggered_flags[CH26W_LATCH_SLOT] =
        (unsigned char) CH26W_LATCH_ALREADY_UP;

    fdps_chapter_26_event_wave_2_defeated_line(CH26W_ARGUMENT_KILLER);

    CHECK_EQ(data_fdps_map_cell_event_triggered_flags[CH26W_LATCH_SLOT],
             CH26W_LATCH_ALREADY_UP);

    fdps_chapter_26_event_wave_2_defeated_line(CH26W_ARGUMENT_KILLER);

    CHECK_EQ(data_fdps_map_cell_event_triggered_flags[CH26W_LATCH_SLOT],
             CH26W_LATCH_ALREADY_UP);
}

/* Element 0x12 is written and its neighbours are not: 0x10 is the ambush latch
   the handler above shares with chapter 25's, and 0x11 is the bow offer's, and
   a rebuild that drifted by one element would spend one of those instead. */
static void ch26w_latches_its_own_element_and_not_a_neighbour(void)
{
    ch26w_stage();
    ch26w_retire_seven_from(CH26W_FIRST_POLLED);

    fdps_chapter_26_event_wave_2_defeated_line(CH26W_ARGUMENT_KILLER);

    CHECK_EQ(data_fdps_map_cell_event_triggered_flags[CH26W_LATCH_SLOT], 1);
    CHECK_EQ(data_fdps_map_cell_event_triggered_flags[CH26W_AMBUSH_LATCH_SLOT],
             0);
    CHECK_EQ(data_fdps_map_cell_event_triggered_flags[CH26W_BOW_LATCH_SLOT], 0);
    CHECK_EQ(data_fdps_map_cell_event_triggered_flags[CH26W_LATCH_SLOT_ABOVE],
             0);
}

/* The incoming index is stored over and never read: a firing handed a unit
   inside the polled block does what a firing handed the killer's index does,
   and a firing handed the index of the one survivor does not fire either. */
static void ch26w_ignores_the_incoming_argument(void)
{
    ch26w_stage();
    ch26w_retire_seven_from(CH26W_FIRST_POLLED);

    fdps_chapter_26_event_wave_2_defeated_line(CH26W_ARGUMENT_INSIDE_BLOCK);

    CHECK_EQ(data_fdps_map_cell_event_triggered_flags[CH26W_LATCH_SLOT], 1);

    ch26w_stage();
    ch26w_retire_seven_from(CH26W_FIRST_POLLED);
    ch26w_unit(CH26W_FIRST_POLLED)->flags = 0;

    fdps_chapter_26_event_wave_2_defeated_line(CH26W_FIRST_POLLED);

    CHECK_EQ(data_fdps_map_cell_event_triggered_flags[CH26W_LATCH_SLOT], 0);
}

/* The block is two literals and not the unit count: a count of four leaves the
   same seven records polled and the same latch raised.  Nothing in the body
   reads data_fdps_map_unit_count, unlike the chapter 25 ambush's sweep. */
static void ch26w_the_block_does_not_follow_the_unit_count(void)
{
    ch26w_stage();
    ch26w_retire_seven_from(CH26W_FIRST_POLLED);
    data_fdps_map_unit_count = CH26W_SHORT_UNIT_COUNT;

    fdps_chapter_26_event_wave_2_defeated_line(CH26W_ARGUMENT_KILLER);

    CHECK_EQ(data_fdps_map_cell_event_triggered_flags[CH26W_LATCH_SLOT], 1);
    CHECK_EQ(data_fdps_map_unit_count, CH26W_SHORT_UNIT_COUNT);
}

/* Only bit 0 of the flags byte retires a unit.  Seven records carrying every
   bit but that one are all still standing and the line is held back; the same
   seven carrying every bit are all retired and it is spoken. */
static void ch26w_only_bit_zero_of_the_flags_byte_retires_a_unit(void)
{
    int i;

    ch26w_stage();
    for (i = CH26W_FIRST_POLLED; i <= CH26W_LAST_POLLED; i++) {
        ch26w_unit(i)->flags = (unsigned char) CH26W_EVERY_BIT_BUT_RETIRED;
    }

    fdps_chapter_26_event_wave_2_defeated_line(CH26W_ARGUMENT_KILLER);

    CHECK_EQ(data_fdps_map_cell_event_triggered_flags[CH26W_LATCH_SLOT], 0);

    for (i = CH26W_FIRST_POLLED; i <= CH26W_LAST_POLLED; i++) {
        ch26w_unit(i)->flags = (unsigned char) CH26W_EVERY_BIT;
    }

    fdps_chapter_26_event_wave_2_defeated_line(CH26W_ARGUMENT_KILLER);

    CHECK_EQ(data_fdps_map_cell_event_triggered_flags[CH26W_LATCH_SLOT], 1);
}

void run_chevt5b_tests(void)
{
    RUN_TEST(ch26a_record_shape_matches_the_offsets);
    RUN_TEST(ch26a_clears_the_mode_over_the_whole_range);
    RUN_TEST(ch26a_the_range_is_inclusive_at_both_ends);
    RUN_TEST(ch26a_writes_nothing_outside_the_range);
    RUN_TEST(ch26a_keeps_the_high_nibble_of_every_record);
    RUN_TEST(ch26a_ignores_the_incoming_argument);
    RUN_TEST(ch26a_range_does_not_follow_the_unit_count);
    RUN_TEST(ch26a_has_no_latch_and_repeats_cleanly);

    RUN_TEST(ch26b_record_shape_matches_the_offsets);
    RUN_TEST(ch26b_latch_blocks_the_whole_body);
    RUN_TEST(ch26b_only_the_player_side_fires);
    RUN_TEST(ch26b_one_firing_does_everything);

    RUN_TEST(ch26w_record_shape_matches_the_offsets);
    RUN_TEST(ch26w_the_last_death_speaks_the_line);
    RUN_TEST(ch26w_one_survivor_anywhere_holds_the_line);
    RUN_TEST(ch26w_the_block_is_the_seven_records_from_0x50);
    RUN_TEST(ch26w_a_raised_latch_refuses_the_body);
    RUN_TEST(ch26w_latches_its_own_element_and_not_a_neighbour);
    RUN_TEST(ch26w_ignores_the_incoming_argument);
    RUN_TEST(ch26w_the_block_does_not_follow_the_unit_count);
    RUN_TEST(ch26w_only_bit_zero_of_the_flags_byte_retires_a_unit);
}
