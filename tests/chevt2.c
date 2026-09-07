/* tests/chevt2.c -- cover for src/chevt2.c.
 *
 * The chapter 13 handler at 000379d0 is the chapter 7 handler's shape with the
 * range 9..0x2c in place of 4..8, and is covered the same way: its two bounds,
 * the signed inclusive compare between them and the 0xf0 mask are all literals
 * in its instruction stream -- MOV dword ptr [EBP-0x20],0x9 at 000379e3, MOV
 * dword ptr [EBP-0x1c],0x2c at 000379ea, CMP EAX,[EBP-0x10] / JLE at 00037a13,
 * and AND DL,0xf0 at 00037a33 -- and the absence of a guard in front of its
 * loop is asserted by putting the shared one-shot latch slot up and watching it
 * run anyway.
 *
 * The unit array is staged here rather than read from a game file, because the
 * handler takes its whole effect through data_fdps_map_unit_array_ptr --
 * pointing that global at a local block is the only way to see the stores.
 * What the global itself holds is ticket 23's and is not asserted, so every
 * case writes the state it wants to see changed.
 *
 * Which indices the range means comes from map12.dat: its header byte 1 is 9,
 * the count of player records laid down first at 0..8, and its 37 deployment
 * records become unit indices 9..0x2d.  The loop's last index is 0x2c, so the
 * 37th of them is deliberately left in the behaviour the map gave it; that is
 * the boundary the cases below pin hardest, because every obvious rewrite of
 * the loop would sweep it in too.
 *
 * The chapter 10 cases in the middle of the file stage differently and say why
 * in their own note: that handler's payload is a call into fdps_deploy_wave,
 * which opens ICON.CEL and FIELD.VFS for itself, so the cases that let it fire
 * need those files and skip themselves without them.
 *
 * The chapter 8 cases at the end of the file stage like the chapter 13 ones,
 * plus a text-block fixture for the draw on the end of that handler; their own
 * note says what the draw can and cannot be asserted about.  There are two
 * chapter 8 sections: the guard-death handler's, and after the chapter 10
 * block the villager-escape handler's, which stages bag entries and the
 * chapter's escape counter as well.
 */
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <dos.h>
#include <i86.h>
#include "testharn.h"
#include "fdpstype.h"
#include "gamedata.h"
#include "unit.h"
#include "deploy.h"
#include "mapdraw.h"
#include "chevt2.h"

/* The single inclusive range the one inline loop covers, read off the
   constants at 000379e3 (9) and 000379ea (0x2c) with the signed JLE at
   00037a16. */
#define CH13_FIRST_INDEX 9
#define CH13_LAST_INDEX  0x2c

/* The last unit index chapter 13's map deploys, one past the loop's last:
   map12.dat's 37th record, which the event leaves holding position. */
#define CH13_LAST_DEPLOYED_INDEX 0x2d

/* Two records past the last deployed unit, so an off-by-one at the top end of
   the range has somewhere visible to land. */
#define CH13_STAGE_UNITS 0x30

/* The slot of data_fdps_map_cell_event_triggered_flags the one-shot handlers
   of this family latch -- element 0x10, the first the map's own event codes
   cannot reach.  This handler does not use it, and that is what is asserted. */
#define CH13_LATCH_SLOT 0x10

static struct fdps_unit_record ch13_units[CH13_STAGE_UNITS];

/* Give every record the same AI byte and point the array global at the block.
   The staged value carries a high nibble as well as a behaviour code, because
   the whole point of the merge is that only one of the two moves. */
static void stage_ch13_units(int ai_behavior)
{
    unsigned char *bytes;
    int i;

    bytes = (unsigned char *) ch13_units;
    for (i = 0; i < (int) sizeof(ch13_units); i++) {
        bytes[i] = 0;
    }
    for (i = 0; i < CH13_STAGE_UNITS; i++) {
        ch13_units[i].ai_behavior = (unsigned char) ai_behavior;
    }
    data_fdps_map_unit_array_ptr = (unsigned char *) ch13_units;
}

/* The record has to be 0x50 bytes with its AI byte at +0x34 for the emitted C
   to address the byte the MOV byte ptr [EAX+0x34] store at 00037a3e addresses;
   the stride is the IMUL 0x50 inside fdps_get_unit_record. */
static void ch13_record_shape_matches_the_offsets(void)
{
    CHECK_EQ((int) sizeof(struct fdps_unit_record), 0x50);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, ai_behavior), 0x34);
}

/* Exactly indices 9..0x2c are rewritten and every record either side of the
   range is left as it was.  The staged 0x52 is behaviour code 2 -- hold
   position, which is what byte 0x11 of all 37 of map12.dat's deployment
   records gives its units -- under a high nibble of 0x50; the range comes out
   0x50 because the mode ORed in is 0, and the rest keep 0x52.  Indices 0..8 are
   chapter 13's nine player records, which the range deliberately starts above,
   and 0x2d..0x2f are the last deployed unit and two past it: both ends have to
   be untouched.  Index 0x2c itself has to be written, which is what makes the
   JLE inclusive rather than a bound one short. */
static void ch13_advance_clears_exactly_the_range(void)
{
    int i;

    stage_ch13_units(0x52);
    fdps_chapter_13_event_enemies_advance(0);

    for (i = 0; i < CH13_STAGE_UNITS; i++) {
        if (i >= CH13_FIRST_INDEX && i <= CH13_LAST_INDEX) {
            CHECK_EQ(ch13_units[i].ai_behavior, 0x50);
        } else {
            CHECK_EQ(ch13_units[i].ai_behavior, 0x52);
        }
    }
}

/* The range covers 36 records, not 37: 0x2c - 9 + 1.  The count is asserted by
   counting the records that moved, because it is the one number every obvious
   rewrite of the loop -- over the map's deployment record count, up to the live
   unit count, or over the 36 enemies the strategy guide lists, which is a
   different set of 36 -- gets wrong at exactly one record. */
static void ch13_advance_leaves_the_last_deployed_unit(void)
{
    int i;
    int moved;

    stage_ch13_units(0x52);
    fdps_chapter_13_event_enemies_advance(0);

    moved = 0;
    for (i = 0; i < CH13_STAGE_UNITS; i++) {
        if (ch13_units[i].ai_behavior == 0x50) {
            moved++;
        }
    }
    CHECK_EQ(moved, 36);
    CHECK_EQ(ch13_units[CH13_LAST_INDEX].ai_behavior, 0x50);
    CHECK_EQ(ch13_units[CH13_LAST_DEPLOYED_INDEX].ai_behavior, 0x52);
}

/* The high nibble is carried across untouched by the AND 0xf0 at 00037a33 and
   the low nibble ends at 0 whatever it held.  0x40 and 0x80 are the two AI
   flags read elsewhere, so a merge that assigned the mode whole -- or that
   masked with anything wider -- would drop them.  Expected values are the
   staged byte ANDed with 0xf0. */
static void ch13_advance_keeps_the_high_nibble(void)
{
    stage_ch13_units(0);
    ch13_units[9].ai_behavior = 0xc2;
    ch13_units[10].ai_behavior = 0x02;
    ch13_units[11].ai_behavior = 0xff;
    ch13_units[0x25].ai_behavior = 0x40;
    ch13_units[0x2c].ai_behavior = 0x8b;

    fdps_chapter_13_event_enemies_advance(0);

    CHECK_EQ(ch13_units[9].ai_behavior, 0xc0);
    CHECK_EQ(ch13_units[10].ai_behavior, 0x00);
    CHECK_EQ(ch13_units[11].ai_behavior, 0xf0);
    CHECK_EQ(ch13_units[0x25].ai_behavior, 0x40);
    CHECK_EQ(ch13_units[0x2c].ai_behavior, 0x80);
}

/* Nothing guards the loop: the instruction after the argument-slot store at
   000379dc is the first of the three constant stores, with no compare between
   them, so unlike the chapter 5 handler this one has no one-shot latch and runs
   its loop every time it is called.  The latch slot is put up before the call
   and the range still moves; the slot is also asserted unchanged, because a
   handler that had grown a latch would have written it. */
static void ch13_advance_has_no_one_shot_latch(void)
{
    stage_ch13_units(0x52);
    data_fdps_map_cell_event_triggered_flags[CH13_LATCH_SLOT] = 1;

    fdps_chapter_13_event_enemies_advance(0);

    CHECK_EQ(ch13_units[CH13_FIRST_INDEX].ai_behavior, 0x50);
    CHECK_EQ(ch13_units[CH13_LAST_INDEX].ai_behavior, 0x50);
    CHECK_EQ(data_fdps_map_cell_event_triggered_flags[CH13_LATCH_SLOT], 1);

    stage_ch13_units(0x52);
    fdps_chapter_13_event_enemies_advance(0);
    CHECK_EQ(ch13_units[CH13_FIRST_INDEX].ai_behavior, 0x50);
    CHECK_EQ(ch13_units[CH13_LAST_INDEX].ai_behavior, 0x50);
}

/* One byte of one record moves and nothing either side of it does.  Every byte
   of the block is stamped 0x55 first, so a store that landed at +0x33 or +0x35
   -- the death-script operand's high byte and ai_dest_x -- is visible; 0x55 is
   also a value whose low nibble is not already 0, so the write that should
   happen is visible too.  Both ends of the range are checked, because the
   stride the store is indexed by is the IMUL 0x50 inside fdps_get_unit_record
   and an error in it shows up furthest from the base. */
static void ch13_advance_touches_no_neighbouring_byte(void)
{
    unsigned char *bytes;
    int i;

    bytes = (unsigned char *) ch13_units;
    for (i = 0; i < (int) sizeof(ch13_units); i++) {
        bytes[i] = 0x55;
    }
    data_fdps_map_unit_array_ptr = bytes;

    fdps_chapter_13_event_enemies_advance(0);

    CHECK_EQ(bytes[9 * 0x50 + 0x34], 0x50);
    CHECK_EQ(bytes[9 * 0x50 + 0x33], 0x55);
    CHECK_EQ(bytes[9 * 0x50 + 0x35], 0x55);
    CHECK_EQ(bytes[0x2c * 0x50 + 0x34], 0x50);
    CHECK_EQ(bytes[0x2c * 0x50 + 0x33], 0x55);
    CHECK_EQ(bytes[0x2c * 0x50 + 0x35], 0x55);
    CHECK_EQ(bytes[8 * 0x50 + 0x34], 0x55);
    CHECK_EQ(bytes[0x2d * 0x50 + 0x34], 0x55);
}

/* The incoming argument slot is overwritten with 0 at 000379dc and never read,
   and no loop bound comes from it, so the index the dispatcher passes cannot
   reach the result.  The death-script runner is the only path this slot is
   reached by in the shipped data and it pushes the index of the unit whose
   death is being resolved -- 0x25 for map12.dat's record 28 -- so that index is
   the realistic argument; 0x2d, 8, -1 and 30000 are the ones an
   argument-driven handler would betray itself on, and the two in-block ones are
   asserted unchanged. */
static void ch13_advance_ignores_the_unit_index_argument(void)
{
    stage_ch13_units(0x52);
    fdps_chapter_13_event_enemies_advance(0x25);
    CHECK_EQ(ch13_units[0x25].ai_behavior, 0x50);
    CHECK_EQ(ch13_units[CH13_LAST_DEPLOYED_INDEX].ai_behavior, 0x52);

    stage_ch13_units(0x52);
    fdps_chapter_13_event_enemies_advance(CH13_LAST_DEPLOYED_INDEX);
    CHECK_EQ(ch13_units[CH13_LAST_DEPLOYED_INDEX].ai_behavior, 0x52);
    CHECK_EQ(ch13_units[CH13_LAST_INDEX].ai_behavior, 0x50);

    stage_ch13_units(0x52);
    fdps_chapter_13_event_enemies_advance(8);
    CHECK_EQ(ch13_units[8].ai_behavior, 0x52);
    CHECK_EQ(ch13_units[CH13_FIRST_INDEX].ai_behavior, 0x50);

    stage_ch13_units(0x52);
    fdps_chapter_13_event_enemies_advance(-1);
    CHECK_EQ(ch13_units[CH13_FIRST_INDEX].ai_behavior, 0x50);
    CHECK_EQ(ch13_units[0].ai_behavior, 0x52);

    stage_ch13_units(0x52);
    fdps_chapter_13_event_enemies_advance(30000);
    CHECK_EQ(ch13_units[CH13_LAST_INDEX].ai_behavior, 0x50);
    CHECK_EQ(ch13_units[0].ai_behavior, 0x52);
}

/* Chapter 10's stairway ambush at 000378a0, from here down.
 *
 * Three of its four decisions are cheap to see -- the latch guard, the side
 * guard and which record the index names -- because refusing to fire leaves
 * the map alone and needs nothing staged but a unit array.  The fourth is the
 * argument triple it hands fdps_deploy_wave (PUSH dword ptr [0x00069cf4] /
 * PUSH 0xa / PUSH EAX with EAX zeroed, at 000378d6..000378e1), and the only
 * way to see any of those three values is to let the deployment happen: none
 * of them is left anywhere afterwards.
 *
 * So the cases that fire run fdps_deploy_wave for real, which opens ICON.CEL
 * and FIELD.VFS for itself, and they read back where the unit landed:
 *
 *   MAP00.COD  record 0 (18, 0)  record 1 (22, 12)  record 2 (8, 10)
 *   MAP01.COD  record 0 (9, 4)
 *
 * -- the same records tests/deploy.c's own wave cases expect, staged through
 * tests/gamefile.lst.  A run without those two files would not fail a check,
 * it would hang in fdps_wait_any_key, so every firing case skips itself when
 * they are not there.
 *
 * The unit array is malloc'd rather than staged into a static block, because a
 * deployment reallocs it: a static block handed to realloc is not a smaller
 * fixture, it is undefined behaviour.  It is never freed, for the same reason
 * tests/deploy.c does not free it -- the block moves under the global on every
 * deployment.
 */

/* The slot of data_fdps_map_cell_event_triggered_flags this handler latches --
   element 0x10, the first the map's own event codes cannot reach, shared by
   every one-shot chapter handler in the game.  Same slot the chapter 13 case
   above puts up to prove that handler ignores it. */
#define CH10_LATCH_SLOT 0x10

/* The wave the handler asks for and the one either side of it, so a record
   tagged 9 or 11 is present to be left behind. */
#define CH10_WAVE 10

/* A map big enough for MAP00.COD's records, which reach (22, 12).  The tile
   search fdps_deploy_unit runs on a place_exact of 0 walks the whole grid, and
   the marking pass writes at a unit's own tile with no bound, so the grid has
   to cover the coordinates the real placement file names. */
#define CH10_GRID_W 32
#define CH10_GRID_H 16

/* Where the deployment records start in the MAP%02d.DAT block and where its
   header keeps their count: ADD EAX,0x83 at 00023463 and MOV AL,byte ptr
   [EAX+0x2] at 000238bb. */
#define CH10_SPAWN_TABLE_RECORD_BASE 0x83
#define CH10_SPAWN_TABLE_COUNT_OFFSET 2

/* The scene-layer offsets fdps_map_load_tile_info reads through: the tile
   map's width word at +7 with its 16-bit ids from +0xb, the attribute table's
   4-byte rows from +0x11, and the event layer's width at +7 with its cells
   from +0x10 (src/maptile.c). */
#define CH10_TILE_MAP_WIDTH_OFFSET 7
#define CH10_TILE_MAP_IDS_OFFSET 0xb
#define CH10_TILE_ATTR_ROWS_OFFSET 0x11
#define CH10_EVENT_LAYER_WIDTH_OFFSET 7
#define CH10_EVENT_LAYER_CELLS_OFFSET 0x10

/* A terrain code the deployment search accepts and one it rejects, either side
   of the CMP EAX,0x5 / JGE at 00023404. */
#define CH10_TERRAIN_WALKABLE 1
#define CH10_TERRAIN_BLOCKED 5

#define CH10_TILE_ATTR_ROWS 16
#define CH10_CHAR_TABLE_ROWS 8
#define CH10_ENEMY_TABLE_ROWS 8

/* Every item id is a valid index into the staged item table, 0xff included:
   fdps_unit_recompute_combat_stats follows an equipped flag into
   fdps_get_item_record without a bounds check. */
#define CH10_ITEM_TABLE_ROWS 256
#define CH10_ITEM_ID_NONE 0xff

/* The unit array's stride, as the deployment path allocates it: PUSH 0x50 at
   000232cc. */
#define CH10_UNIT_STRIDE 0x50

static unsigned char ch10_grid[4 + CH10_GRID_W * CH10_GRID_H * 2];
static unsigned char ch10_spawn_table[CH10_SPAWN_TABLE_RECORD_BASE + 8 * 0x1a];
static unsigned char ch10_tile_map[CH10_TILE_MAP_IDS_OFFSET +
                                   CH10_GRID_W * CH10_GRID_H * 2];
static unsigned char ch10_tile_attr[CH10_TILE_ATTR_ROWS_OFFSET +
                                    CH10_TILE_ATTR_ROWS * 4];
static unsigned char ch10_event_layer[CH10_EVENT_LAYER_CELLS_OFFSET +
                                      CH10_GRID_W * CH10_GRID_H];
static struct fdps_character_base_record ch10_char_base[CH10_CHAR_TABLE_ROWS];
static struct fdps_character_growth ch10_growth[CH10_CHAR_TABLE_ROWS];
static struct fdps_enemy_data ch10_enemy[CH10_ENEMY_TABLE_ROWS];
static struct fdps_item_effect ch10_items[CH10_ITEM_TABLE_ROWS];

static int ch10_files_checked = 0;
static int ch10_files_ready = 0;

/* ICON.CEL is read by fdps_cache_cel_sprite_group, which takes a fixed
   0x2970-byte bite out of the sheet's offset table, so a shorter file is one
   that reader runs off the end of rather than a smaller fixture.  FIELD.VFS
   cannot be stood in for at all: a missing container sends fdps_deploy_wave
   into fdps_wait_any_key and a missing member into exit(1). */
static void ch10_ensure_game_files(void)
{
    FILE *fp;
    long size;

    if (ch10_files_checked) {
        return;
    }
    ch10_files_checked = 1;

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
    ch10_files_ready = 1;
}

static void ch10_zero_bytes(void *block, int count)
{
    unsigned char *bytes;
    int i;

    bytes = (unsigned char *) block;
    for (i = 0; i < count; i++) {
        bytes[i] = 0;
    }
}

static struct fdps_char_spawn_record *ch10_spawn_at(int index)
{
    return (struct fdps_char_spawn_record *)
           (ch10_spawn_table + CH10_SPAWN_TABLE_RECORD_BASE) + index;
}

/* One deployment record with nothing equipped and nothing carried, tagged with
   the wave it belongs to.  Side 2 is what the record gives the unit it
   deploys and has nothing to do with the side the handler tests. */
static void ch10_set_spawn(int index, int char_id, int wave_no)
{
    ch10_spawn_at(index)->char_id = (unsigned char) char_id;
    ch10_spawn_at(index)->level = 1;
    ch10_spawn_at(index)->side = 2;
    ch10_spawn_at(index)->equipped_item_0 = CH10_ITEM_ID_NONE;
    ch10_spawn_at(index)->equipped_item_1 = CH10_ITEM_ID_NONE;
    ch10_spawn_at(index)->carried_items[0] = CH10_ITEM_ID_NONE;
    ch10_spawn_at(index)->carried_items[1] = CH10_ITEM_ID_NONE;
    ch10_spawn_at(index)->carried_items[2] = CH10_ITEM_ID_NONE;
    ch10_spawn_at(index)->carried_items[3] = CH10_ITEM_ID_NONE;
    ch10_spawn_at(index)->carried_items[4] = CH10_ITEM_ID_NONE;
    ch10_spawn_at(index)->carried_items[5] = CH10_ITEM_ID_NONE;
    ch10_spawn_at(index)->wave_no = (unsigned char) wave_no;
}

static void ch10_set_terrain(int tile_id, int terrain)
{
    struct fdps_tile_attr_entry *rows;

    rows = (struct fdps_tile_attr_entry *)
           (ch10_tile_attr + CH10_TILE_ATTR_ROWS_OFFSET);
    rows[tile_id].terrain_type = (unsigned char) terrain;
}

/* The cell of the tile map at (tile_x, tile_y), so one tile can be given an id
   whose attribute row says the deployment search must not use it. */
static void ch10_set_tile_id(int tile_x, int tile_y, int tile_id)
{
    short *tile_ids;

    tile_ids = (short *) (ch10_tile_map + CH10_TILE_MAP_IDS_OFFSET);
    tile_ids[tile_y * CH10_GRID_W + tile_x] = (short) tile_id;
}

static struct fdps_unit_record *ch10_unit(int unit_index)
{
    return (struct fdps_unit_record *)
           (data_fdps_map_unit_array_ptr + unit_index * CH10_UNIT_STRIDE);
}

/* A blank walkable map with unit_count units already on it, the latch down and
   the chapter global on map 0.  Each staged unit stands on its own tile along
   the top row, clear of every placement record these cases read back.  The
   sides are left at 0 and each case sets the one it is about. */
static void ch10_stage(int unit_count)
{
    int i;

    ch10_zero_bytes(ch10_grid, (int) sizeof(ch10_grid));
    ch10_zero_bytes(ch10_spawn_table, (int) sizeof(ch10_spawn_table));
    ch10_zero_bytes(ch10_tile_map, (int) sizeof(ch10_tile_map));
    ch10_zero_bytes(ch10_tile_attr, (int) sizeof(ch10_tile_attr));
    ch10_zero_bytes(ch10_event_layer, (int) sizeof(ch10_event_layer));
    ch10_zero_bytes(ch10_char_base, (int) sizeof(ch10_char_base));
    ch10_zero_bytes(ch10_growth, (int) sizeof(ch10_growth));
    ch10_zero_bytes(ch10_enemy, (int) sizeof(ch10_enemy));
    ch10_zero_bytes(ch10_items, (int) sizeof(ch10_items));

    *(short *) ch10_grid = (short) CH10_GRID_W;
    *(short *) (ch10_grid + 2) = (short) CH10_GRID_H;

    *(short *) (ch10_tile_map + CH10_TILE_MAP_WIDTH_OFFSET) =
        (short) CH10_GRID_W;
    for (i = 0; i < CH10_TILE_ATTR_ROWS; i++) {
        ch10_set_terrain(i, CH10_TERRAIN_WALKABLE);
    }

    *(short *) (ch10_event_layer + CH10_EVENT_LAYER_WIDTH_OFFSET) =
        (short) CH10_GRID_W;

    data_fdps_battle_move_grid_ptr = ch10_grid;
    data_fdps_tile_event_data_table_ptr = ch10_spawn_table;
    data_fdps_scene_layer_tile_map_ptrs[0] = ch10_tile_map;
    data_fdps_scene_layer_tile_attr_ptr[0] = ch10_tile_attr;
    data_fdps_map_cell_event_code_layer_ptr = ch10_event_layer;
    data_fdps_battle_character_base_table_ptr =
        (unsigned char *) ch10_char_base;
    data_fdps_battle_character_growth_table_ptr = (unsigned char *) ch10_growth;
    data_fdps_battle_enemy_data_table_ptr = (unsigned char *) ch10_enemy;
    data_fdps_item_effect_table_ptr = (unsigned char *) ch10_items;

    data_fdps_map_unit_array_ptr =
        (unsigned char *) malloc(unit_count * CH10_UNIT_STRIDE);
    ch10_zero_bytes(data_fdps_map_unit_array_ptr,
                    unit_count * CH10_UNIT_STRIDE);
    data_fdps_map_unit_count = unit_count;
    for (i = 0; i < unit_count; i++) {
        ch10_unit(i)->pos_x = (unsigned char) i;
        ch10_unit(i)->pos_y = 0;
    }

    data_fdps_map_cell_event_triggered_flags[CH10_LATCH_SLOT] = 0;
    data_fdps_chapter_current_chapter_id = 0;
}

/* The side byte the JA at 000378cb tests is record +6, and the record is
   0x50 bytes, which is the stride fdps_get_unit_record multiplies by.  Every
   case below would agree with itself while addressing another byte if either
   were wrong. */
static void ch10_side_is_the_byte_at_record_six(void)
{
    CHECK_EQ((int) offsetof(struct fdps_unit_record, side), 6);
    CHECK_EQ((int) sizeof(struct fdps_unit_record), 0x50);
}

/* A side of 0 fires nothing and, just as importantly, does not put the latch
   up: the store at 000378cf is inside the branch both tests jump over, so a
   unit that fails the side test leaves the event available to the next one. */
static void ch10_side_zero_does_not_fire_or_latch(void)
{
    ch10_stage(2);
    ch10_unit(0)->side = 0;
    ch10_unit(1)->side = 2;

    fdps_chapter_10_event_deploy_wave_10(0);

    CHECK_EQ(data_fdps_map_cell_event_triggered_flags[CH10_LATCH_SLOT], 0);
    CHECK_EQ(data_fdps_map_unit_count, 2);
}

/* The record read is the one unit_index names and not a fixed one: with only
   index 2 on a firing side, the three indices either side of it all refuse,
   and each refusal is visible in the latch staying down. */
static void ch10_reads_the_record_the_index_names(void)
{
    ch10_stage(4);
    ch10_unit(0)->side = 0;
    ch10_unit(1)->side = 0;
    ch10_unit(2)->side = 2;
    ch10_unit(3)->side = 0;

    fdps_chapter_10_event_deploy_wave_10(0);
    CHECK_EQ(data_fdps_map_cell_event_triggered_flags[CH10_LATCH_SLOT], 0);
    fdps_chapter_10_event_deploy_wave_10(1);
    CHECK_EQ(data_fdps_map_cell_event_triggered_flags[CH10_LATCH_SLOT], 0);
    fdps_chapter_10_event_deploy_wave_10(3);
    CHECK_EQ(data_fdps_map_cell_event_triggered_flags[CH10_LATCH_SLOT], 0);
    CHECK_EQ(data_fdps_map_unit_count, 4);
}

/* The latch is tested against 0 and not against 1 -- CMP byte ptr
   [0x000640e8],0x0 / JNZ at 000378bb -- so any non-zero value in the slot
   blocks the body, and the slot keeps the value it had rather than being
   normalised to 1.  A unit on a firing side is staged, so the only thing
   holding the event back is the latch. */
static void ch10_any_non_zero_latch_blocks(void)
{
    ch10_stage(1);
    ch10_unit(0)->side = 2;
    data_fdps_map_cell_event_triggered_flags[CH10_LATCH_SLOT] = 1;

    fdps_chapter_10_event_deploy_wave_10(0);

    CHECK_EQ(data_fdps_map_cell_event_triggered_flags[CH10_LATCH_SLOT], 1);
    CHECK_EQ(data_fdps_map_unit_count, 1);

    ch10_stage(1);
    ch10_unit(0)->side = 2;
    data_fdps_map_cell_event_triggered_flags[CH10_LATCH_SLOT] = 0x7f;

    fdps_chapter_10_event_deploy_wave_10(0);

    CHECK_EQ(data_fdps_map_cell_event_triggered_flags[CH10_LATCH_SLOT], 0x7f);
    CHECK_EQ(data_fdps_map_unit_count, 1);
}

/* The wave asked for is 10 and nothing else: three records tagged 9, 10 and 11
   leave exactly the middle one deployed, carrying record 1's character id and
   record 1's placement coordinates out of MAP00.COD.  The latch goes up in the
   same call.

   Both halves matter -- a wave of 9 or 11 would deploy a different record onto
   different coordinates, and a wave the table does not carry would deploy
   nothing at all while still putting the latch up. */
static void ch10_deploys_the_records_tagged_wave_10(void)
{
    ch10_ensure_game_files();
    if (!ch10_files_ready) {
        return;
    }
    ch10_stage(2);
    ch10_unit(0)->side = 0;
    ch10_unit(1)->side = 2;
    ch10_spawn_table[CH10_SPAWN_TABLE_COUNT_OFFSET] = 3;
    ch10_set_spawn(0, 5, CH10_WAVE - 1);
    ch10_set_spawn(1, 6, CH10_WAVE);
    ch10_set_spawn(2, 7, CH10_WAVE + 1);

    fdps_chapter_10_event_deploy_wave_10(1);

    CHECK_EQ(data_fdps_map_cell_event_triggered_flags[CH10_LATCH_SLOT], 1);
    CHECK_EQ(data_fdps_map_unit_count, 3);
    CHECK_EQ((int) ch10_unit(2)->char_id, 6);
    CHECK_EQ((int) ch10_unit(2)->pos_x, 22);
    CHECK_EQ((int) ch10_unit(2)->pos_y, 12);
}

/* The map the wave is deployed under is read from
   data_fdps_chapter_current_chapter_id at the call site and is not a literal:
   the same deployment record placed while that global says 1 lands on
   MAP01.COD's record 0 at (9, 4) instead of MAP00.COD's (18, 0). */
static void ch10_map_number_comes_from_the_chapter_global(void)
{
    ch10_ensure_game_files();
    if (!ch10_files_ready) {
        return;
    }
    ch10_stage(1);
    ch10_unit(0)->side = 2;
    ch10_spawn_table[CH10_SPAWN_TABLE_COUNT_OFFSET] = 1;
    ch10_set_spawn(0, 5, CH10_WAVE);
    data_fdps_chapter_current_chapter_id = 1;

    fdps_chapter_10_event_deploy_wave_10(0);

    CHECK_EQ(data_fdps_map_unit_count, 2);
    CHECK_EQ((int) ch10_unit(1)->pos_x, 9);
    CHECK_EQ((int) ch10_unit(1)->pos_y, 4);
}

/* The placement flag is 0 -- XOR EAX,EAX / PUSH EAX at 000378d6 -- so the
   reinforcements are put on the nearest free walkable tile to their placement
   record rather than on the record's own tile.  MAP00.COD record 1 names
   (22, 12); giving that one cell a tile id whose attribute row is terrain 5
   takes it out of the search, and the unit lands one tile away.  A flag of 1
   would drop it on (22, 12) regardless of the terrain there.

   (22, 13) is which of the four tiles at distance 1 it lands on, because the
   scan is row-major over the whole grid and a tie is accepted (CMP EAX,
   [EBP-0x14] / JLE at 000233e6), so the last candidate at the best distance
   wins. */
static void ch10_places_on_the_nearest_free_tile(void)
{
    ch10_ensure_game_files();
    if (!ch10_files_ready) {
        return;
    }
    ch10_stage(1);
    ch10_unit(0)->side = 2;
    ch10_spawn_table[CH10_SPAWN_TABLE_COUNT_OFFSET] = 2;
    ch10_set_spawn(0, 5, CH10_WAVE - 1);
    ch10_set_spawn(1, 6, CH10_WAVE);
    ch10_set_tile_id(22, 12, 1);
    ch10_set_terrain(1, CH10_TERRAIN_BLOCKED);

    fdps_chapter_10_event_deploy_wave_10(0);

    CHECK_EQ(data_fdps_map_unit_count, 2);
    CHECK_EQ((int) ch10_unit(1)->char_id, 6);
    CHECK_EQ((int) ch10_unit(1)->pos_x, 22);
    CHECK_EQ((int) ch10_unit(1)->pos_y, 13);
}

/* Contract C on the side byte: CMP byte ptr [EAX+0x6],0x0 / JA is unsigned, so
   a side of 0xff is 255 and fires the event.  Read as a signed char with the
   signed branch it would be -1 and the ambush would never happen for it.  The
   sides the shipped data uses are 0, 1 and 2, so this is the direction the
   rebuild can get wrong without any map showing it. */
static void ch10_side_test_is_unsigned(void)
{
    ch10_ensure_game_files();
    if (!ch10_files_ready) {
        return;
    }
    ch10_stage(1);
    ch10_unit(0)->side = 0xff;
    ch10_spawn_table[CH10_SPAWN_TABLE_COUNT_OFFSET] = 1;
    ch10_set_spawn(0, 5, CH10_WAVE);

    fdps_chapter_10_event_deploy_wave_10(0);

    CHECK_EQ(data_fdps_map_cell_event_triggered_flags[CH10_LATCH_SLOT], 1);
    CHECK_EQ(data_fdps_map_unit_count, 2);
}

/* One shot, and the latch is what makes it one: the second call on the same
   unit adds nothing, because the first left the slot up.  Deploying twice is
   what the wave walk would otherwise do -- it appends and never checks whether
   the wave is already on the map. */
static void ch10_fires_once_only(void)
{
    ch10_ensure_game_files();
    if (!ch10_files_ready) {
        return;
    }
    ch10_stage(1);
    ch10_unit(0)->side = 2;
    ch10_spawn_table[CH10_SPAWN_TABLE_COUNT_OFFSET] = 2;
    ch10_set_spawn(0, 5, CH10_WAVE);
    ch10_set_spawn(1, 6, CH10_WAVE);

    fdps_chapter_10_event_deploy_wave_10(0);
    CHECK_EQ(data_fdps_map_unit_count, 3);
    CHECK_EQ(data_fdps_map_cell_event_triggered_flags[CH10_LATCH_SLOT], 1);

    fdps_chapter_10_event_deploy_wave_10(0);
    CHECK_EQ(data_fdps_map_unit_count, 3);
    CHECK_EQ(data_fdps_map_cell_event_triggered_flags[CH10_LATCH_SLOT], 1);
}

/* Chapter 8's guard-death handler at 00037440, from here down.
 *
 * It is the chapter 13 handler's shape with the range 0x13..0x13 in place of
 * 9..0x2c and a behaviour code of 4 in place of 0, plus one draw on the end,
 * so it is covered the same way: both bounds and the mode are literals in its
 * instruction stream -- MOV dword ptr [EBP-0x20],0x13 at 00037453, MOV dword
 * ptr [EBP-0x1c],0x13 at 0003745a, MOV dword ptr [EBP-0x18],0x4 at 00037461,
 * with the signed inclusive CMP EAX,[EBP-0x10] / JLE at 00037483 -- and the
 * absence of any guard in front of the walk is asserted by putting the shared
 * one-shot latch slot up, and by taking the live unit count down to zero, and
 * watching it write anyway.
 *
 * WHICH TEXT ENTRY THE DRAW ASKS FOR IS NOT ASSERTED HERE, for the reason the
 * chapter 3 section of tests/chevt1.c gives: fdps_draw_text takes its whole
 * effect through pixels at the VGA aperture, keeps no state and returns a
 * cursor this handler discards, so a unit test has nothing to read back.  The
 * entry id is a literal in the instruction stream (PUSH 0xf at 000374c6) and
 * the reviewer's reading of it is what stands behind the emitted C.  What the
 * cases below do pin about the draw is that it does not stop the re-aim: every
 * case runs to the end of the function.
 *
 * So the chapter's text block is staged as a fixture whose every entry is a
 * lone -1 terminator.  fdps_draw_text walks it, draws nothing, touches no
 * global and returns at once, which is what keeps a case from painting the
 * screen and standing a modal wait on a keyboard nothing is typing at.
 */

/* The one index the handler re-aims, read off the two bound constants at
   00037453 and 0003745a, and the behaviour code the merge ORs in, read off
   00037461. */
#define CH08_MAGE_INDEX 0x13
#define CH08_MODE_WALK_TO_DEST 4

/* Two records past the one that moves, so an off-by-one at either end of the
   one-element range has somewhere visible to land. */
#define CH08_STAGE_UNITS 0x16

/* The unit-record stride, the IMUL 0x50 inside fdps_get_unit_record. */
#define CH08_UNIT_STRIDE 0x50

/* The shared one-shot latch slot.  This handler does not use it, and that is
   what is asserted. */
#define CH08_LATCH_SLOT 0x10

/* Enough entries for the id the draw asks for, 0xf, plus the terminator every
   entry points at. */
#define CH08_TEXT_ENTRY_COUNT 0x10

static struct fdps_unit_record ch08_units[CH08_STAGE_UNITS];
static short ch08_text_block[CH08_TEXT_ENTRY_COUNT + 1];

/* Give every record the same AI byte, point the array global at the block and
   give the draw an entry table whose every id resolves to a lone terminator.
   The staged value carries a high nibble as well as a behaviour code, because
   the whole point of the merge is that only one of the two moves. */
static void stage_ch08_units(int ai_behavior)
{
    unsigned char *bytes;
    int i;

    bytes = (unsigned char *) ch08_units;
    for (i = 0; i < (int) sizeof(ch08_units); i++) {
        bytes[i] = 0;
    }
    for (i = 0; i < CH08_STAGE_UNITS; i++) {
        ch08_units[i].ai_behavior = (unsigned char) ai_behavior;
    }
    data_fdps_map_unit_array_ptr = (unsigned char *) ch08_units;

    for (i = 0; i < CH08_TEXT_ENTRY_COUNT; i++) {
        ch08_text_block[i] = (short) (CH08_TEXT_ENTRY_COUNT * 2);
    }
    ch08_text_block[CH08_TEXT_ENTRY_COUNT] = -1;
    data_fdps_current_chapter_text_ptr = (unsigned char *) ch08_text_block;
}

/* Exactly index 0x13 is rewritten and every record either side of it is left
   as it was.  The staged 0x52 is behaviour code 2 -- hold position -- under a
   high nibble of 0x50; index 0x13 comes out 0x54 because the mode ORed in is
   4, and the rest keep 0x52.  Index 0x12 is the last of the 19 records
   chapter 8's map opens with and 0x14 is one past the mage, so both ends of
   the one-element range have a witness. */
static void ch08_sends_exactly_the_guest_mage(void)
{
    int i;

    stage_ch08_units(0x52);
    fdps_chapter_08_event_send_guest_mage_to_cells(0);

    for (i = 0; i < CH08_STAGE_UNITS; i++) {
        if (i == CH08_MAGE_INDEX) {
            CHECK_EQ(ch08_units[i].ai_behavior, 0x54);
        } else {
            CHECK_EQ(ch08_units[i].ai_behavior, 0x52);
        }
    }
}

/* The high nibble is carried across untouched by the AND 0xf0 at 000374a3 and
   the low nibble ends at 4 whatever it held.  0x40 and 0x80 are the two AI
   flags read elsewhere, so a merge that assigned the mode whole -- or that
   masked with anything wider -- would drop them.  Expected values are the
   staged byte ANDed with 0xf0 and ORed with 4; the last pair is already in
   mode 4 and has to come back unchanged. */
static void ch08_keeps_the_high_nibble(void)
{
    stage_ch08_units(0);
    ch08_units[CH08_MAGE_INDEX].ai_behavior = 0xc2;
    fdps_chapter_08_event_send_guest_mage_to_cells(0);
    CHECK_EQ(ch08_units[CH08_MAGE_INDEX].ai_behavior, 0xc4);

    stage_ch08_units(0);
    ch08_units[CH08_MAGE_INDEX].ai_behavior = 0x00;
    fdps_chapter_08_event_send_guest_mage_to_cells(0);
    CHECK_EQ(ch08_units[CH08_MAGE_INDEX].ai_behavior, 0x04);

    stage_ch08_units(0);
    ch08_units[CH08_MAGE_INDEX].ai_behavior = 0xff;
    fdps_chapter_08_event_send_guest_mage_to_cells(0);
    CHECK_EQ(ch08_units[CH08_MAGE_INDEX].ai_behavior, 0xf4);

    stage_ch08_units(0);
    ch08_units[CH08_MAGE_INDEX].ai_behavior = 0x8b;
    fdps_chapter_08_event_send_guest_mage_to_cells(0);
    CHECK_EQ(ch08_units[CH08_MAGE_INDEX].ai_behavior, 0x84);

    stage_ch08_units(0);
    ch08_units[CH08_MAGE_INDEX].ai_behavior = 0x44;
    fdps_chapter_08_event_send_guest_mage_to_cells(0);
    CHECK_EQ(ch08_units[CH08_MAGE_INDEX].ai_behavior, 0x44);
}

/* One byte of one record moves and nothing either side of it does.  Every byte
   of the block is stamped 0x55 first, so a store that landed at +0x33 or +0x35
   -- the death-script operand's high byte and ai_dest_x -- is visible; 0x55 is
   also a value whose low nibble is not already 4, so the write that should
   happen is visible too.  The record the store lands in is the one the IMUL
   0x50 inside fdps_get_unit_record picks, and 0x13 is far enough from the base
   that a wrong stride misses it. */
static void ch08_touches_no_neighbouring_byte(void)
{
    unsigned char *bytes;
    int i;

    stage_ch08_units(0);
    bytes = (unsigned char *) ch08_units;
    for (i = 0; i < (int) sizeof(ch08_units); i++) {
        bytes[i] = 0x55;
    }
    data_fdps_map_unit_array_ptr = bytes;

    fdps_chapter_08_event_send_guest_mage_to_cells(0);

    CHECK_EQ(bytes[CH08_MAGE_INDEX * CH08_UNIT_STRIDE + 0x34], 0x54);
    CHECK_EQ(bytes[CH08_MAGE_INDEX * CH08_UNIT_STRIDE + 0x33], 0x55);
    CHECK_EQ(bytes[CH08_MAGE_INDEX * CH08_UNIT_STRIDE + 0x35], 0x55);
    CHECK_EQ(bytes[0x12 * CH08_UNIT_STRIDE + 0x34], 0x55);
    CHECK_EQ(bytes[0x14 * CH08_UNIT_STRIDE + 0x34], 0x55);
}

/* Nothing guards the walk: the instruction after the argument-slot store at
   0003744c is the first of the three constant stores, with no compare between
   them, so this handler has no one-shot latch and re-aims the mage every time
   it is called.  The latch slot is put up before the call and the mage still
   moves; the slot is also asserted unchanged, because a handler that had grown
   a latch would have written it. */
static void ch08_has_no_one_shot_latch(void)
{
    stage_ch08_units(0x52);
    data_fdps_map_cell_event_triggered_flags[CH08_LATCH_SLOT] = 1;

    fdps_chapter_08_event_send_guest_mage_to_cells(0);
    CHECK_EQ(ch08_units[CH08_MAGE_INDEX].ai_behavior, 0x54);
    CHECK_EQ(data_fdps_map_cell_event_triggered_flags[CH08_LATCH_SLOT], 1);

    stage_ch08_units(0x52);
    fdps_chapter_08_event_send_guest_mage_to_cells(0);
    CHECK_EQ(ch08_units[CH08_MAGE_INDEX].ai_behavior, 0x54);
    fdps_chapter_08_event_send_guest_mage_to_cells(0);
    CHECK_EQ(ch08_units[CH08_MAGE_INDEX].ai_behavior, 0x54);
}

/* data_fdps_map_unit_count is not consulted: nothing in the instruction stream
   reads 0x00060150, and the write goes ahead with the count at zero and with
   it at 0x13, the value it holds for the whole of chapter 8 before the turn-3
   cutscene deploys the mage.  That is the case the original writes one record
   past the end of the live array in, and adding the guard that would stop it
   is the divergence this case exists to catch.  The count is restored so the
   later chapter 10 cases stage from where they expect. */
static void ch08_writes_without_a_unit_count_check(void)
{
    int saved_count;

    saved_count = data_fdps_map_unit_count;

    stage_ch08_units(0x52);
    data_fdps_map_unit_count = 0;
    fdps_chapter_08_event_send_guest_mage_to_cells(0);
    CHECK_EQ(ch08_units[CH08_MAGE_INDEX].ai_behavior, 0x54);

    stage_ch08_units(0x52);
    data_fdps_map_unit_count = CH08_MAGE_INDEX;
    fdps_chapter_08_event_send_guest_mage_to_cells(0);
    CHECK_EQ(ch08_units[CH08_MAGE_INDEX].ai_behavior, 0x54);

    data_fdps_map_unit_count = saved_count;
}

/* The incoming argument slot is overwritten with 0 at 0003744c and never read,
   and neither bound of the walk comes from it, so the index the dispatcher
   passes cannot reach the result.  The death-script runner is the only path
   this slot is reached by in the shipped data and it pushes the index of the
   unit whose death is being resolved -- 14 for map07.dat's record 18, the
   soldier posted by the cells -- so that index is the realistic argument; 0,
   the mage's own index, -1 and 30000 are the ones an argument-driven handler
   would betray itself on, and index 0x12 is asserted unchanged throughout. */
static void ch08_ignores_the_unit_index_argument(void)
{
    stage_ch08_units(0x52);
    fdps_chapter_08_event_send_guest_mage_to_cells(14);
    CHECK_EQ(ch08_units[CH08_MAGE_INDEX].ai_behavior, 0x54);
    CHECK_EQ(ch08_units[14].ai_behavior, 0x52);

    stage_ch08_units(0x52);
    fdps_chapter_08_event_send_guest_mage_to_cells(CH08_MAGE_INDEX);
    CHECK_EQ(ch08_units[CH08_MAGE_INDEX].ai_behavior, 0x54);
    CHECK_EQ(ch08_units[0x12].ai_behavior, 0x52);

    stage_ch08_units(0x52);
    fdps_chapter_08_event_send_guest_mage_to_cells(0);
    CHECK_EQ(ch08_units[CH08_MAGE_INDEX].ai_behavior, 0x54);
    CHECK_EQ(ch08_units[0].ai_behavior, 0x52);

    stage_ch08_units(0x52);
    fdps_chapter_08_event_send_guest_mage_to_cells(-1);
    CHECK_EQ(ch08_units[CH08_MAGE_INDEX].ai_behavior, 0x54);
    CHECK_EQ(ch08_units[0x12].ai_behavior, 0x52);

    stage_ch08_units(0x52);
    fdps_chapter_08_event_send_guest_mage_to_cells(30000);
    CHECK_EQ(ch08_units[CH08_MAGE_INDEX].ai_behavior, 0x54);
    CHECK_EQ(ch08_units[0x12].ai_behavior, 0x52);
}

/* The behaviour code the merge leaves behind is 4 and not any of the codes the
   handler's siblings write: mode 0 advances on the nearest opposing unit, mode
   2 holds position, and only mode 4 makes
   fdps_map_actor_behavior_step walk the unit toward the destination tile in
   its own record, which is what sends the mage to the cage.  Asserted against
   the low nibble on its own so the case says which half of the byte carries
   it. */
static void ch08_mode_is_walk_to_destination(void)
{
    stage_ch08_units(0);
    fdps_chapter_08_event_send_guest_mage_to_cells(0);
    CHECK_EQ(ch08_units[CH08_MAGE_INDEX].ai_behavior & 0x0f,
             CH08_MODE_WALK_TO_DEST);
    CHECK_EQ(ch08_units[CH08_MAGE_INDEX].ai_behavior, 0x04);
}

/* Chapter 8's villager-escape handler at 00037600, from here down.
 *
 * It is staged like the two sections above -- a local block of unit records
 * under data_fdps_map_unit_array_ptr, and a chapter text block whose every
 * entry is a lone -1 terminator, so fdps_draw_text walks it, draws nothing and
 * returns at once instead of painting the VGA aperture and standing a modal
 * wait on a keyboard nothing is typing at.  WHICH text entry either draw asks
 * for is not asserted, for the reason the chapter 3 section of
 * tests/chevt1.c gives: the draw takes its whole effect through pixels, keeps
 * no state and returns a cursor this handler discards.  The entry ids are
 * literals in the instruction stream -- ADD EAX,0xd at 00037640 and the 0x20 /
 * 0x21 pair at 0003769e and 000376a7 -- and the reviewer's reading of them is
 * what stands behind the emitted C.
 *
 * What IS readable afterwards is all three of the handler's real effects: the
 * escape count in element 0x11 of data_fdps_map_cell_event_triggered_flags,
 * the retired flag written into the escaping villager's record, and the reward
 * item fdps_unit_add_item puts in the guest mage's first free bag entry.  The
 * cases below stage each of them and read them back.
 *
 * Every record is staged with a flags byte of 0x80 -- the acted-this-turn bit,
 * with the retired bit clear -- so a case can tell an assignment of 1 from an
 * OR of 1, and so fdps_unit_is_retired answers 0 for a villager the case did
 * not deliberately retire.
 */

/* The inclusive range the handler acts for, off the two bound compares at
   00037613 and 00037619, and the guest mage the reward goes to, off PUSH 0x13
   at 00037708. */
#define CH08E_FIRST_VILLAGER 0x0f
#define CH08E_LAST_VILLAGER 0x12
#define CH08E_MAGE_INDEX 0x13

/* Two records past the mage, so a write that ran off either end of the range
   has somewhere visible to land. */
#define CH08E_STAGE_UNITS 0x16

/* The element of data_fdps_map_cell_event_triggered_flags the escapes are
   counted in -- byte ptr [0x000640e9], element 0x11 -- and the two elements
   either side of it, which the cases watch for collateral damage.  Element
   0x10 is the one-shot latch the rest of this file's handlers use. */
#define CH08E_COUNT_SLOT 0x11
#define CH08E_SLOT_BELOW 0x10
#define CH08E_SLOT_ABOVE 0x12

/* Sentinels for those two neighbours; neither is a value the handler could
   write. */
#define CH08E_SENTINEL_BELOW 0x5a
#define CH08E_SENTINEL_ABOVE 0xa5

/* The flags byte a staged, still-fighting unit carries: bit 7 up, the retired
   bit 0 clear.  If the handler ORed its 1 in rather than assigning it, a
   retired villager would come out 0x81. */
#define CH08E_ACTED_THIS_TURN 0x80

/* What the handler writes over the whole flags byte, MOV byte ptr [EAX+0x5],
   0x1 at 00037724. */
#define CH08E_RETIRED 1

/* An inventory entry's empty bit, the 0x80 fdps_unit_add_item scans for, and
   the flag byte it leaves behind when it takes a slot -- a whole zero. */
#define CH08E_BAG_EMPTY 0x80
#define CH08E_BAG_TAKEN 0

/* The three rewards, off the constants at 000376df, 000376f4 and 000376fd:
   炎之寶石, 速度藥水 and 風精之羽 (assets/items.md). */
#define CH08E_REWARD_TWO 0xc8
#define CH08E_REWARD_THREE 0xda
#define CH08E_REWARD_FOUR 0xdd

/* Enough entries for the highest id either draw asks for, 0x21, plus the
   terminator every entry points at. */
#define CH08E_TEXT_ENTRY_COUNT 0x22

static struct fdps_unit_record ch08e_units[CH08E_STAGE_UNITS];
static short ch08e_text_block[CH08E_TEXT_ENTRY_COUNT + 1];

/* Zero the block, give every record a still-fighting flags byte and eight
   empty bag entries, retire the villagers named by retired_mask -- bit 0 is
   unit 0xf and bit 3 is unit 0x12 -- and park escaped_count in the count slot
   with a sentinel in each neighbour.  The text block is rebuilt every time so
   a case that ran before cannot leave it pointing anywhere else. */
static void stage_ch08e(int escaped_count, int retired_mask)
{
    unsigned char *bytes;
    int i;
    int slot;

    bytes = (unsigned char *) ch08e_units;
    for (i = 0; i < (int) sizeof(ch08e_units); i++) {
        bytes[i] = 0;
    }
    for (i = 0; i < CH08E_STAGE_UNITS; i++) {
        ch08e_units[i].flags = CH08E_ACTED_THIS_TURN;
        for (slot = 0; slot < 8; slot++) {
            ch08e_units[i].inventory_slots[slot * 2] = CH08E_BAG_EMPTY;
            ch08e_units[i].inventory_slots[slot * 2 + 1] = 0;
        }
    }
    for (i = CH08E_FIRST_VILLAGER; i <= CH08E_LAST_VILLAGER; i++) {
        if ((retired_mask & (1 << (i - CH08E_FIRST_VILLAGER))) != 0) {
            ch08e_units[i].flags = CH08E_RETIRED;
        }
    }
    data_fdps_map_unit_array_ptr = (unsigned char *) ch08e_units;

    for (i = 0; i < CH08E_TEXT_ENTRY_COUNT; i++) {
        ch08e_text_block[i] = (short) (CH08E_TEXT_ENTRY_COUNT * 2);
    }
    ch08e_text_block[CH08E_TEXT_ENTRY_COUNT] = -1;
    data_fdps_current_chapter_text_ptr = (unsigned char *) ch08e_text_block;

    data_fdps_map_cell_event_triggered_flags[CH08E_SLOT_BELOW] =
        CH08E_SENTINEL_BELOW;
    data_fdps_map_cell_event_triggered_flags[CH08E_COUNT_SLOT] =
        (unsigned char) escaped_count;
    data_fdps_map_cell_event_triggered_flags[CH08E_SLOT_ABOVE] =
        CH08E_SENTINEL_ABOVE;
}

/* The id byte of one bag entry, and its flag byte beside it. */
static int ch08e_bag_id(int unit_index, int slot)
{
    return ch08e_units[unit_index].inventory_slots[slot * 2 + 1];
}

static int ch08e_bag_flag(int unit_index, int slot)
{
    return ch08e_units[unit_index].inventory_slots[slot * 2];
}

/* Every index outside 0xf..0x12 falls straight through the range test and
   nothing at all happens.  The state staged here is the one an in-range call
   would pay the reward from -- three villagers already out and a count that
   would come up to 2 -- so what each of these indices proves is that the guard
   and not some later test is what stops it.  0x13 is the guest mage himself
   and 30000 is far outside the staged array: neither reaches
   fdps_get_unit_record, which is the only reason the second one is safe to
   pass. */
static void ch08e_ignores_every_index_outside_the_four(void)
{
    static int outside[6] = {-1, 0, 0x0e, 0x13, 0x14, 30000};
    int i;

    for (i = 0; i < 6; i++) {
        stage_ch08e(1, 0x7);
        fdps_chapter_08_event_villager_escapes(outside[i]);
        CHECK_EQ(data_fdps_map_cell_event_triggered_flags[CH08E_COUNT_SLOT], 1);
        CHECK_EQ(ch08e_bag_flag(CH08E_MAGE_INDEX, 0), CH08E_BAG_EMPTY);
        CHECK_EQ(ch08e_units[CH08E_MAGE_INDEX].flags, CH08E_ACTED_THIS_TURN);
        CHECK_EQ(ch08e_units[CH08E_LAST_VILLAGER].flags,
                 CH08E_ACTED_THIS_TURN);
    }
}

/* An in-range call bumps the escape count by exactly one and takes that one
   villager out of the battle, leaving the other three where they were. */
static void ch08e_counts_the_escape_and_retires_the_villager(void)
{
    stage_ch08e(0, 0);
    fdps_chapter_08_event_villager_escapes(CH08E_FIRST_VILLAGER);

    CHECK_EQ(data_fdps_map_cell_event_triggered_flags[CH08E_COUNT_SLOT], 1);
    CHECK_EQ(ch08e_units[0x0f].flags, CH08E_RETIRED);
    CHECK_EQ(ch08e_units[0x10].flags, CH08E_ACTED_THIS_TURN);
    CHECK_EQ(ch08e_units[0x11].flags, CH08E_ACTED_THIS_TURN);
    CHECK_EQ(ch08e_units[0x12].flags, CH08E_ACTED_THIS_TURN);
}

/* All four of 0xf..0x12 are inside the range, so both bounds are inclusive and
   neither end is off by one.  0xe and 0x13 either side of them are covered by
   the out-of-range case above. */
static void ch08e_all_four_villagers_are_in_range(void)
{
    int villager;

    for (villager = CH08E_FIRST_VILLAGER; villager <= CH08E_LAST_VILLAGER;
         villager++) {
        stage_ch08e(0, 0);
        fdps_chapter_08_event_villager_escapes(villager);
        CHECK_EQ(data_fdps_map_cell_event_triggered_flags[CH08E_COUNT_SLOT], 1);
        CHECK_EQ(ch08e_units[villager].flags, CH08E_RETIRED);
    }
}

/* The retired flag is written over the WHOLE byte and is not ORed in: a
   villager carrying the acted-this-turn bit comes out holding 1 and not 0x81,
   and one carrying every bit comes out holding 1 as well. */
static void ch08e_retire_is_a_whole_byte_assignment(void)
{
    stage_ch08e(0, 0);
    CHECK_EQ(ch08e_units[0x10].flags, CH08E_ACTED_THIS_TURN);
    fdps_chapter_08_event_villager_escapes(0x10);
    CHECK_EQ(ch08e_units[0x10].flags, CH08E_RETIRED);

    stage_ch08e(0, 0);
    ch08e_units[0x11].flags = 0xff;
    fdps_chapter_08_event_villager_escapes(0x11);
    CHECK_EQ(ch08e_units[0x11].flags, CH08E_RETIRED);
}

/* The reward is paid only when EXACTLY three of the four are already out, and
   the count is taken before this villager is marked, so three means "the one
   leaving now is the last one still in".  Two out is too few and four out --
   which happens when the handler is called a second time for a villager that
   has already gone -- is too many, and neither pays. */
static void ch08e_reward_needs_exactly_three_already_out(void)
{
    stage_ch08e(1, 0x7);
    fdps_chapter_08_event_villager_escapes(CH08E_LAST_VILLAGER);
    CHECK_EQ(data_fdps_map_cell_event_triggered_flags[CH08E_COUNT_SLOT], 2);
    CHECK_EQ(ch08e_bag_flag(CH08E_MAGE_INDEX, 0), CH08E_BAG_TAKEN);
    CHECK_EQ(ch08e_bag_id(CH08E_MAGE_INDEX, 0), CH08E_REWARD_TWO);

    stage_ch08e(2, 0x3);
    fdps_chapter_08_event_villager_escapes(0x11);
    CHECK_EQ(data_fdps_map_cell_event_triggered_flags[CH08E_COUNT_SLOT], 3);
    CHECK_EQ(ch08e_bag_flag(CH08E_MAGE_INDEX, 0), CH08E_BAG_EMPTY);
    CHECK_EQ(ch08e_bag_id(CH08E_MAGE_INDEX, 0), 0);

    stage_ch08e(3, 0xf);
    fdps_chapter_08_event_villager_escapes(CH08E_LAST_VILLAGER);
    CHECK_EQ(data_fdps_map_cell_event_triggered_flags[CH08E_COUNT_SLOT], 4);
    CHECK_EQ(ch08e_bag_flag(CH08E_MAGE_INDEX, 0), CH08E_BAG_EMPTY);
    CHECK_EQ(ch08e_bag_id(CH08E_MAGE_INDEX, 0), 0);
}

/* The escape count has to pass 1, not reach it: three villagers killed and the
   fourth walking out leaves the count at 1 and pays nothing, even though it is
   the last one out.  The villager still retires and the count still stands. */
static void ch08e_reward_needs_more_than_one_escape(void)
{
    stage_ch08e(0, 0x7);
    fdps_chapter_08_event_villager_escapes(CH08E_LAST_VILLAGER);

    CHECK_EQ(data_fdps_map_cell_event_triggered_flags[CH08E_COUNT_SLOT], 1);
    CHECK_EQ(ch08e_bag_flag(CH08E_MAGE_INDEX, 0), CH08E_BAG_EMPTY);
    CHECK_EQ(ch08e_units[CH08E_LAST_VILLAGER].flags, CH08E_RETIRED);
}

/* Which item is paid is decided on the escape count AFTER this escape has been
   added: two escapes give 炎之寶石, three 速度藥水 and all four 風精之羽, the
   last being the guide's 若四個村民全被救出，結束後會得到風精之羽（在費塔加
   身上）. */
static void ch08e_reward_scales_with_the_escape_count(void)
{
    stage_ch08e(1, 0x7);
    fdps_chapter_08_event_villager_escapes(CH08E_LAST_VILLAGER);
    CHECK_EQ(data_fdps_map_cell_event_triggered_flags[CH08E_COUNT_SLOT], 2);
    CHECK_EQ(ch08e_bag_id(CH08E_MAGE_INDEX, 0), CH08E_REWARD_TWO);

    stage_ch08e(2, 0x7);
    fdps_chapter_08_event_villager_escapes(CH08E_LAST_VILLAGER);
    CHECK_EQ(data_fdps_map_cell_event_triggered_flags[CH08E_COUNT_SLOT], 3);
    CHECK_EQ(ch08e_bag_id(CH08E_MAGE_INDEX, 0), CH08E_REWARD_THREE);

    stage_ch08e(3, 0x7);
    fdps_chapter_08_event_villager_escapes(CH08E_LAST_VILLAGER);
    CHECK_EQ(data_fdps_map_cell_event_triggered_flags[CH08E_COUNT_SLOT], 4);
    CHECK_EQ(ch08e_bag_id(CH08E_MAGE_INDEX, 0), CH08E_REWARD_FOUR);
}

/* The reward goes to unit 0x13, the guest mage, and to no one else: the
   villager that just walked out keeps an empty bag, and so do the other
   three. */
static void ch08e_reward_goes_to_the_guest_mage(void)
{
    int villager;

    stage_ch08e(3, 0x7);
    fdps_chapter_08_event_villager_escapes(CH08E_LAST_VILLAGER);

    CHECK_EQ(ch08e_bag_id(CH08E_MAGE_INDEX, 0), CH08E_REWARD_FOUR);
    CHECK_EQ(ch08e_bag_flag(CH08E_MAGE_INDEX, 1), CH08E_BAG_EMPTY);
    for (villager = CH08E_FIRST_VILLAGER; villager <= CH08E_LAST_VILLAGER;
         villager++) {
        CHECK_EQ(ch08e_bag_flag(villager, 0), CH08E_BAG_EMPTY);
        CHECK_EQ(ch08e_bag_id(villager, 0), 0);
    }
}

/* The count is kept in element 0x11 and the two elements either side of it are
   left alone -- element 0x10 in particular, which is the one-shot latch the
   other handlers in this file share. */
static void ch08e_touches_no_neighbouring_flag_slot(void)
{
    stage_ch08e(1, 0);
    fdps_chapter_08_event_villager_escapes(CH08E_FIRST_VILLAGER);

    CHECK_EQ(data_fdps_map_cell_event_triggered_flags[CH08E_SLOT_BELOW],
             CH08E_SENTINEL_BELOW);
    CHECK_EQ(data_fdps_map_cell_event_triggered_flags[CH08E_COUNT_SLOT], 2);
    CHECK_EQ(data_fdps_map_cell_event_triggered_flags[CH08E_SLOT_ABOVE],
             CH08E_SENTINEL_ABOVE);
}

/* Nothing latches the handler, so calling it again for a villager that has
   already gone counts another escape: the count is the number of in-range
   calls made and not the number of distinct villagers out.  That is what the
   original does, and a guard on the record's own retired flag would take
   escapes away rather than add any. */
static void ch08e_has_no_latch(void)
{
    stage_ch08e(0, 0);

    fdps_chapter_08_event_villager_escapes(CH08E_FIRST_VILLAGER);
    CHECK_EQ(data_fdps_map_cell_event_triggered_flags[CH08E_COUNT_SLOT], 1);
    CHECK_EQ(ch08e_units[CH08E_FIRST_VILLAGER].flags, CH08E_RETIRED);

    fdps_chapter_08_event_villager_escapes(CH08E_FIRST_VILLAGER);
    CHECK_EQ(data_fdps_map_cell_event_triggered_flags[CH08E_COUNT_SLOT], 2);

    fdps_chapter_08_event_villager_escapes(CH08E_FIRST_VILLAGER);
    CHECK_EQ(data_fdps_map_cell_event_triggered_flags[CH08E_COUNT_SLOT], 3);
    CHECK_EQ(ch08e_bag_flag(CH08E_MAGE_INDEX, 0), CH08E_BAG_EMPTY);
}

/* The count slot is read as an unsigned byte -- XOR EAX,EAX / MOV AL at
   00037689, 000376d5 and 000376ea -- so a value with bit 7 up is a large
   positive number and not a negative one.  Staged at 0xff it comes up to 0
   through the byte-wide INC, which fails the "more than one" test; staged at
   0xfe it comes up to 0xff, which passes it and falls past both named counts
   to the 風精之羽 branch.  Read as a signed char, 0xff would be -1 and both
   would take the other arm. */
static void ch08e_the_escape_count_is_an_unsigned_byte(void)
{
    stage_ch08e(0xff, 0x7);
    fdps_chapter_08_event_villager_escapes(CH08E_LAST_VILLAGER);
    CHECK_EQ(data_fdps_map_cell_event_triggered_flags[CH08E_COUNT_SLOT], 0);
    CHECK_EQ(ch08e_bag_flag(CH08E_MAGE_INDEX, 0), CH08E_BAG_EMPTY);

    stage_ch08e(0xfe, 0x7);
    fdps_chapter_08_event_villager_escapes(CH08E_LAST_VILLAGER);
    CHECK_EQ(data_fdps_map_cell_event_triggered_flags[CH08E_COUNT_SLOT], 0xff);
    CHECK_EQ(ch08e_bag_id(CH08E_MAGE_INDEX, 0), CH08E_REWARD_FOUR);
}

/* The retired count comes from fdps_unit_is_retired, which reads bit 0 of the
   flags byte on its own, so a villager carrying only the acted-this-turn bit
   0x80 does not count as out.  Three villagers staged at 0x80 and one at 1
   leaves one out, not four, and the reward stays unpaid. */
static void ch08e_only_bit_zero_counts_as_out(void)
{
    stage_ch08e(3, 0);
    ch08e_units[0x0f].flags = CH08E_RETIRED;
    ch08e_units[0x10].flags = 0x80;
    ch08e_units[0x11].flags = 0x80;
    fdps_chapter_08_event_villager_escapes(CH08E_LAST_VILLAGER);

    CHECK_EQ(data_fdps_map_cell_event_triggered_flags[CH08E_COUNT_SLOT], 4);
    CHECK_EQ(ch08e_bag_flag(CH08E_MAGE_INDEX, 0), CH08E_BAG_EMPTY);
    CHECK_EQ(ch08e_units[CH08E_LAST_VILLAGER].flags, CH08E_RETIRED);

    stage_ch08e(3, 0);
    ch08e_units[0x0f].flags = 0x81;
    ch08e_units[0x10].flags = 0x81;
    ch08e_units[0x11].flags = 0x81;
    fdps_chapter_08_event_villager_escapes(CH08E_LAST_VILLAGER);
    CHECK_EQ(ch08e_bag_id(CH08E_MAGE_INDEX, 0), CH08E_REWARD_FOUR);
}

/* Chapter 9's reinforcement handler at 00037730, from here down.
 *
 * Its body is two calls and a return with no branch in it, so everything worth
 * pinning is an argument: which wave the deployment asks for, where the map
 * number comes from, what the placement flag is, and that neither call is
 * guarded.  All four are read back through the deployment's own effect on
 * data_fdps_map_unit_array_ptr and data_fdps_map_unit_count.
 *
 * The map fixture is the chapter 10 section's, reused rather than copied: it
 * stages the resident MAP%02d.DAT deployment block, the scene layers and the
 * tables fdps_deploy_unit reads, none of which is about a particular chapter,
 * and both handlers reach the same fdps_deploy_wave through it.  So these
 * cases need the same real files -- ICON.CEL and FIELD.VFS, which cannot be
 * stood in for -- and skip themselves without them.  The coordinates they
 * expect are MAP00.COD's and MAP01.COD's own placement records: record 0 at
 * (18, 0), record 1 at (22, 12), and MAP01.COD's record 0 at (9, 4).
 *
 * WHICH TEXT ENTRY THE DRAW ASKS FOR IS NOT ASSERTED HERE, for the reason the
 * chapter 8 sections give: fdps_draw_text takes its whole effect through
 * pixels at the VGA aperture, keeps no state and returns a cursor this handler
 * discards.  The entry id is a literal in the instruction stream (PUSH 0x17 at
 * 00037769) and the reviewer's reading of it is what stands behind the emitted
 * C.  The chapter's text block is staged as a fixture whose every entry is a
 * lone -1 terminator, so the draw walks it, paints nothing and returns at once
 * instead of standing a modal wait on a keyboard nothing is typing at.  What
 * the cases do pin about the draw is that it does not stop the deployment:
 * every one of them runs to the end of the function.
 */

/* The wave the handler asks for, PUSH 0x1 at 00037746, and the turn chapter 9
   schedules the event on.  The two are staged apart on purpose: a handler that
   passed the turn counter instead of the literal would deploy a different set
   of records. */
#define CH09_ARRIVAL_WAVE 1
#define CH09_EVENT_TURN 15

/* Enough entries for the id the draw asks for, 0x17, plus the terminator every
   entry points at.  That is fdetxt09.txt's own entry count. */
#define CH09_TEXT_ENTRY_COUNT 0x18

/* The shared one-shot latch slot.  This handler does not use it, and that is
   what is asserted. */
#define CH09_LATCH_SLOT 0x10

static short ch09_text_block[CH09_TEXT_ENTRY_COUNT + 1];

/* The chapter 10 section's map fixture plus a text block whose every id
   resolves to a lone terminator. */
static void ch09_stage(int unit_count)
{
    int i;

    ch10_stage(unit_count);

    for (i = 0; i < CH09_TEXT_ENTRY_COUNT; i++) {
        ch09_text_block[i] = (short) (CH09_TEXT_ENTRY_COUNT * 2);
    }
    ch09_text_block[CH09_TEXT_ENTRY_COUNT] = -1;
    data_fdps_current_chapter_text_ptr = (unsigned char *) ch09_text_block;
}

/* The wave asked for is 1 and nothing else: three records tagged 0, 1 and 2
   leave exactly the middle one deployed, carrying record 1's character id and
   record 1's MAP00.COD coordinates.  A wave of 0 would bring the whole opening
   group on a second time and a wave of 2 would bring a different record. */
static void ch09_deploys_the_records_tagged_wave_1(void)
{
    ch10_ensure_game_files();
    if (!ch10_files_ready) {
        return;
    }
    ch09_stage(1);
    ch10_spawn_table[CH10_SPAWN_TABLE_COUNT_OFFSET] = 3;
    ch10_set_spawn(0, 5, CH09_ARRIVAL_WAVE - 1);
    ch10_set_spawn(1, 6, CH09_ARRIVAL_WAVE);
    ch10_set_spawn(2, 7, CH09_ARRIVAL_WAVE + 1);

    fdps_chapter_09_event_deploy_wave_1(0);

    CHECK_EQ(data_fdps_map_unit_count, 2);
    CHECK_EQ((int) ch10_unit(1)->char_id, 6);
    CHECK_EQ((int) ch10_unit(1)->pos_x, 22);
    CHECK_EQ((int) ch10_unit(1)->pos_y, 12);
}

/* The wave is the literal 1 and not the battle turn counter, which is what
   separates this handler from chapter 3's turn-scheduled one: with the counter
   parked on 15, the turn chapter 9 schedules this event for, the record tagged
   1 is still the only one that comes on, and the records tagged 15 and 14 --
   the counter and the counter less one, the two values a turn-driven handler
   would ask for -- are left where they are. */
static void ch09_wave_is_a_literal_and_not_the_turn_counter(void)
{
    int saved_turn;

    ch10_ensure_game_files();
    if (!ch10_files_ready) {
        return;
    }
    saved_turn = data_fdps_battle_turn_counter;

    ch09_stage(1);
    data_fdps_battle_turn_counter = CH09_EVENT_TURN;
    ch10_spawn_table[CH10_SPAWN_TABLE_COUNT_OFFSET] = 3;
    ch10_set_spawn(0, 5, CH09_ARRIVAL_WAVE);
    ch10_set_spawn(1, 6, CH09_EVENT_TURN);
    ch10_set_spawn(2, 7, CH09_EVENT_TURN - 1);

    fdps_chapter_09_event_deploy_wave_1(0);

    CHECK_EQ(data_fdps_map_unit_count, 2);
    CHECK_EQ((int) ch10_unit(1)->char_id, 5);
    CHECK_EQ((int) ch10_unit(1)->pos_x, 18);
    CHECK_EQ((int) ch10_unit(1)->pos_y, 0);

    data_fdps_battle_turn_counter = saved_turn;
}

/* The map the wave is deployed under is read from
   data_fdps_chapter_current_chapter_id at the call site and is not a literal:
   the same deployment record placed while that global says 1 lands on
   MAP01.COD's record 0 at (9, 4) instead of MAP00.COD's (18, 0). */
static void ch09_map_number_comes_from_the_chapter_global(void)
{
    ch10_ensure_game_files();
    if (!ch10_files_ready) {
        return;
    }
    ch09_stage(1);
    ch10_spawn_table[CH10_SPAWN_TABLE_COUNT_OFFSET] = 1;
    ch10_set_spawn(0, 5, CH09_ARRIVAL_WAVE);

    fdps_chapter_09_event_deploy_wave_1(0);
    CHECK_EQ((int) ch10_unit(1)->pos_x, 18);
    CHECK_EQ((int) ch10_unit(1)->pos_y, 0);

    ch09_stage(1);
    ch10_spawn_table[CH10_SPAWN_TABLE_COUNT_OFFSET] = 1;
    ch10_set_spawn(0, 5, CH09_ARRIVAL_WAVE);
    data_fdps_chapter_current_chapter_id = 1;

    fdps_chapter_09_event_deploy_wave_1(0);
    CHECK_EQ(data_fdps_map_unit_count, 2);
    CHECK_EQ((int) ch10_unit(1)->pos_x, 9);
    CHECK_EQ((int) ch10_unit(1)->pos_y, 4);
}

/* The placement flag is 0 -- XOR EAX,EAX / PUSH EAX at 00037743 -- so the
   arrivals are put on the nearest free walkable tile to their placement record
   rather than on the record's own tile.  MAP00.COD record 1 names (22, 12);
   giving that one cell a tile id whose attribute row is terrain 5 takes it out
   of the search and the unit lands one tile away, at (22, 13), the last of the
   four candidates at distance 1 the row-major scan accepts.  A flag of 1 would
   drop it on (22, 12) whatever the terrain there. */
static void ch09_places_on_the_nearest_free_tile(void)
{
    ch10_ensure_game_files();
    if (!ch10_files_ready) {
        return;
    }
    ch09_stage(1);
    ch10_spawn_table[CH10_SPAWN_TABLE_COUNT_OFFSET] = 2;
    ch10_set_spawn(0, 5, CH09_ARRIVAL_WAVE - 1);
    ch10_set_spawn(1, 6, CH09_ARRIVAL_WAVE);
    ch10_set_tile_id(22, 12, 1);
    ch10_set_terrain(1, CH10_TERRAIN_BLOCKED);

    fdps_chapter_09_event_deploy_wave_1(0);

    CHECK_EQ(data_fdps_map_unit_count, 2);
    CHECK_EQ((int) ch10_unit(1)->char_id, 6);
    CHECK_EQ((int) ch10_unit(1)->pos_x, 22);
    CHECK_EQ((int) ch10_unit(1)->pos_y, 13);
}

/* Nothing guards either call: the instruction after the argument-slot store at
   0003773c is the XOR that builds the deployment's third argument, with no
   compare between them.  The shared one-shot latch is put up before the call
   and the wave still arrives, and the slot comes back holding what it held --
   a handler that had grown a latch would have written it. */
static void ch09_has_no_one_shot_latch(void)
{
    ch10_ensure_game_files();
    if (!ch10_files_ready) {
        return;
    }
    ch09_stage(1);
    ch10_spawn_table[CH10_SPAWN_TABLE_COUNT_OFFSET] = 1;
    ch10_set_spawn(0, 5, CH09_ARRIVAL_WAVE);
    data_fdps_map_cell_event_triggered_flags[CH09_LATCH_SLOT] = 1;

    fdps_chapter_09_event_deploy_wave_1(0);

    CHECK_EQ(data_fdps_map_unit_count, 2);
    CHECK_EQ(data_fdps_map_cell_event_triggered_flags[CH09_LATCH_SLOT], 1);
}

/* Nothing records that the handler ran either, so a second call appends the
   same records a second time: the wave walk only ever appends and never asks
   whether the wave is already on the map.  That is the original's behaviour
   and what makes the data -- one turn of one chapter naming this slot -- the
   only thing keeping the event to one firing. */
static void ch09_fires_again_on_every_call(void)
{
    ch10_ensure_game_files();
    if (!ch10_files_ready) {
        return;
    }
    ch09_stage(1);
    ch10_spawn_table[CH10_SPAWN_TABLE_COUNT_OFFSET] = 1;
    ch10_set_spawn(0, 5, CH09_ARRIVAL_WAVE);

    fdps_chapter_09_event_deploy_wave_1(0);
    CHECK_EQ(data_fdps_map_unit_count, 2);

    fdps_chapter_09_event_deploy_wave_1(0);
    CHECK_EQ(data_fdps_map_unit_count, 3);

    fdps_chapter_09_event_deploy_wave_1(0);
    CHECK_EQ(data_fdps_map_unit_count, 4);
}

/* A wave the table does not carry is not an error: the walk matches nothing,
   the file open and release still happen and nothing is appended.  Staged with
   every record on wave 0, which is what would come on if the handler asked for
   the opening wave instead of 1. */
static void ch09_a_wave_no_record_carries_deploys_nothing(void)
{
    ch10_ensure_game_files();
    if (!ch10_files_ready) {
        return;
    }
    ch09_stage(1);
    ch10_spawn_table[CH10_SPAWN_TABLE_COUNT_OFFSET] = 3;
    ch10_set_spawn(0, 5, 0);
    ch10_set_spawn(1, 6, 0);
    ch10_set_spawn(2, 7, 0);

    fdps_chapter_09_event_deploy_wave_1(0);

    CHECK_EQ(data_fdps_map_unit_count, 1);
}

/* The incoming argument slot is overwritten with 0 at 0003773c and never read,
   and neither call takes anything from it, so the index the dispatcher passes
   cannot reach the result.  The turn-event dispatcher is the only path that
   reaches this slot and it pushes a literal 0; -1, 30000 and a real unit index
   are the arguments an argument-driven handler would betray itself on, and
   none of them reaches fdps_get_unit_record, which is why 30000 is safe to
   pass.  Each call deploys the same record onto the same tile. */
static void ch09_ignores_the_unit_index_argument(void)
{
    static int arguments[4] = {0, -1, 30000, 1};
    int i;

    ch10_ensure_game_files();
    if (!ch10_files_ready) {
        return;
    }
    for (i = 0; i < 4; i++) {
        ch09_stage(2);
        ch10_spawn_table[CH10_SPAWN_TABLE_COUNT_OFFSET] = 1;
        ch10_set_spawn(0, 6, CH09_ARRIVAL_WAVE);

        fdps_chapter_09_event_deploy_wave_1(arguments[i]);

        CHECK_EQ(data_fdps_map_unit_count, 3);
        CHECK_EQ((int) ch10_unit(2)->char_id, 6);
        CHECK_EQ((int) ch10_unit(2)->pos_x, 18);
        CHECK_EQ((int) ch10_unit(2)->pos_y, 0);
    }
}

/* Chapter 10's turn-scheduled handler at 00037780, from here down.
 *
 * It is three arms off one read of data_fdps_battle_turn_counter, so what the
 * cases pin is which arm a turn lands in and what that arm asks for: the wave
 * number, the map number it is placed under, and -- for the middle arm alone --
 * that the cursor crosses to the right-hand door and that frames are composed
 * at all.
 *
 * The map fixture is the chapter 10 ambush section's, reused rather than
 * copied, with three things added on top of it.
 *
 * A PORTRAIT ID EVERY UNIT ON THE MAP IS DROPPED ON.  The middle arm composes
 * frames, which runs the whole map compositor over every unit in the array, and
 * a unit fdps_draw_map_unit does not drop is blitted out of the sprite cache
 * and the shadow sheet, neither of which a test image has.  0x80 is the id it
 * returns on at 00033c56 (src/mapdraw.c), so every staged record carries it and
 * so does every deployment record, which is why the cases tell arrivals apart
 * by the level byte fdps_deploy_unit copies across and never by a character id.
 *
 * A TEXT BLOCK COVERING BOTH IDS THE HANDLER NAMES, 0x12 and 0x13, with every
 * entry holding the same offset onto the lone -1 that follows the table, so
 * whichever entry a draw resolves the stream ends before a glyph is drawn and
 * before any of the modal codes fdps_draw_text would stand a keyboard wait on.
 *
 * A REAL TIMER INTERRUPT for the duration of every call that reaches the middle
 * arm.  fdps_render_view_frame's pacing wait ends only when
 * data_fdps_timer_tick_counter moves (mapdraw.h) and nothing advances that
 * counter in a test image, so the second frame would never end.  Each run hooks
 * IRQ0 with a handler that increments the counter and chains to the one that
 * was there, and puts the adapter in mode 13h so the two retrace spins see a
 * real retrace -- exactly as tests/chevt1.c, tests/death.c and tests/icon.c do.
 *
 * HOW MANY FRAMES EACH HOLD COMPOSES IS NOT ASSERTED.  A frame consumes one
 * timer tick and a test cannot count them without racing the interrupt, so
 * twelve is a playtest contract, the position tests/chevt1.c leaves the same
 * question in.  What IS exactly observable is whether any frame ran, because
 * fdps_render_view_frame latches the tick counter into
 * data_fdps_view_frame_last_tick as the last thing it does, so a sentinel
 * staged into that latch survives an arm that composes nothing and is gone
 * after one that composes.
 *
 * WHERE THE FIRST CURSOR TARGET IS CANNOT BE SEEN FROM OUTSIDE.  The second
 * move overwrites the position the first one left, and the view origin the walk
 * drags along settles at the same place either way, so PUSH 0x48 / PUSH 0xd8 at
 * 000377f7 stands on the reviewer's reading of the instruction stream in the
 * way the text ids do.  What the cases do pin is the position the pan ends on,
 * which is the right-hand door and not the left one.
 *
 * WHICH TEXT ENTRY EACH DRAW ASKS FOR IS NOT ASSERTED EITHER, for the reason
 * the chapter 8 and chapter 9 sections give: fdps_draw_text takes its whole
 * effect through pixels at the VGA aperture, keeps no state and returns a
 * cursor this handler discards.  What the cases pin about the draws is that
 * neither stops what follows it and that neither arm that draws also pans.
 *
 * Every case reaches the deployment, which opens ICON.CEL and FIELD.VFS for
 * itself, so every case skips itself without them.
 */

/* The turn the announced arm tests for, CMP dword ptr [0x00069ce8],0x3 at
   0003778c, and the wave it asks for, PUSH 0x1 at 00037798. */
#define CH10T_ANNOUNCED_TURN 3
#define CH10T_ANNOUNCED_WAVE 1

/* The middle arm's upper bound, CMP dword ptr [0x00069ce8],0xd / JG at
   000377d0, with the first turn map09.dat schedules the arm for and the first
   turn past the bound.  13 and 14 are the pair that pin the boundary: 13 has to
   pan and 14 has to speak. */
#define CH10T_FIRST_DOOR_TURN 6
#define CH10T_LAST_DOOR_TURN 13
#define CH10T_FIRST_LAST_WAVE_TURN 14

/* The turn map09.dat schedules the last wave on, and the wave that arm asks
   for -- PUSH 0xb at 00037859. */
#define CH10T_LAST_WAVE_TURN 19
#define CH10T_LAST_WAVE 11

/* What the middle arm subtracts from the counter, SUB EAX,0x4 at 000377e5. */
#define CH10T_DOOR_WAVE_TURN_BIAS 4

/* Two turns below everything map09.dat schedules, which the arm has no lower
   bound against: turn 4 asks for wave 0, which the fixture carries, and turn 0
   asks for wave -4, which nothing can carry. */
#define CH10T_WAVE_ZERO_TURN 4
#define CH10T_NEGATIVE_WAVE_TURN 0

/* The two world pixels the pan ends between, PUSH 0x48 at 000377fc and PUSH
   0x168 at 00037829, with the shared Y at 000377f7 and 00037824.  At the
   24-pixel tile step they are tiles (3, 9) and (15, 9). */
#define CH10T_LEFT_DOOR_WORLD_X 0x48
#define CH10T_RIGHT_DOOR_WORLD_X 0x168
#define CH10T_DOOR_WORLD_Y 0xd8

/* The deployment records the fixture lays down, one per wave the cases ask
   about, at the table index that is also their MAP%02d.COD placement record.
   Record 0 and record 1 are the two whose coordinates the ambush cases above
   already read back out of the real files. */
#define CH10T_WAVE1_RECORD 0
#define CH10T_WAVE2_RECORD 1
#define CH10T_WAVE3_RECORD 2
#define CH10T_WAVE9_RECORD 3
#define CH10T_WAVE0_RECORD 4
#define CH10T_WAVE10_RECORD 5
#define CH10T_WAVE11_RECORD 6
#define CH10T_UNASKED_RECORD 7
#define CH10T_SPAWN_RECORD_COUNT 8

/* The level each record carries, which is how the cases tell which one arrived:
   the character id cannot do it, because every record has to carry the one id
   the compositor drops.  Each is its own wave number except the wave-0 record,
   which takes a level no wave number shares. */
#define CH10T_WAVE0_LEVEL 20
#define CH10T_WAVE1_LEVEL 1
#define CH10T_WAVE2_LEVEL 2
#define CH10T_WAVE3_LEVEL 3
#define CH10T_WAVE9_LEVEL 9
#define CH10T_WAVE10_LEVEL 10
#define CH10T_WAVE11_LEVEL 11

/* A wave nothing in the handler asks for, parked on the spare record so the
   table is never exhausted of records that must not arrive. */
#define CH10T_UNASKED_WAVE 0xff

/* The id the compositor drops, PORTRAIT_ID_NO_MAP_SPRITE at 00033c56. */
#define CH10T_ARRIVAL_CHAR_ID 0x80

/* MAP00.COD's placement record 0 and record 1, and MAP01.COD's record 0 -- the
   same coordinates the ambush cases above read back out of the real files. */
#define CH10T_MAP00_RECORD0_X 18
#define CH10T_MAP00_RECORD0_Y 0
#define CH10T_MAP00_RECORD1_X 22
#define CH10T_MAP00_RECORD1_Y 12
#define CH10T_MAP01_RECORD0_X 9
#define CH10T_MAP01_RECORD0_Y 4

/* Units already on the map when the handler runs.  Four is enough to make the
   compositor walk a real array, and the fixture parks them along the top row
   clear of every placement record these cases read back. */
#define CH10T_STAGED_UNITS 4

/* Both ids the handler names plus the terminator every entry points at. */
#define CH10T_TEXT_ENTRY_COUNT 0x14

/* A value the tick counter cannot legitimately hold on entry, parked in the
   frame latch so a run that composed nothing is distinguishable from one that
   did. */
#define CH10T_FRAME_SENTINEL 0x5a5a5a5aU

#define CH10T_TIMER_VECTOR 8
#define CH10T_MODE_13H 0x13
#define CH10T_MODE_TEXT 0x03

static short ch10t_text_block[CH10T_TEXT_ENTRY_COUNT + 1];
static void (__interrupt __far *ch10t_saved_timer)();

static void __interrupt __far ch10t_timer_isr(void)
{
    ++data_fdps_timer_tick_counter;
    _chain_intr(ch10t_saved_timer);
}

static void ch10t_set_mode(int mode)
{
    union REGS regs;

    ch10_zero_bytes(&regs, (int) sizeof(regs));
    regs.x.eax = (unsigned) mode;
    int386(0x10, &regs, &regs);
}

/* The ambush section's map fixture, with the compositor given nothing to draw,
   every staged unit wearing the dropped portrait id, one deployment record per
   wave the cases ask about, the two-entry text block and the frame latch
   sentinel.  The cursor starts at the origin so the pan has a real distance to
   cover on both axes. */
static void ch10t_stage(int battle_turn)
{
    int i;

    ch10_stage(CH10T_STAGED_UNITS);

    for (i = 0; i < CH10T_STAGED_UNITS; i++) {
        ch10_unit(i)->portrait_id = (unsigned char) CH10T_ARRIVAL_CHAR_ID;
    }

    ch10_spawn_table[CH10_SPAWN_TABLE_COUNT_OFFSET] =
        (unsigned char) CH10T_SPAWN_RECORD_COUNT;
    ch10_set_spawn(CH10T_WAVE1_RECORD, CH10T_ARRIVAL_CHAR_ID,
                   CH10T_ANNOUNCED_WAVE);
    ch10_set_spawn(CH10T_WAVE2_RECORD, CH10T_ARRIVAL_CHAR_ID, 2);
    ch10_set_spawn(CH10T_WAVE3_RECORD, CH10T_ARRIVAL_CHAR_ID, 3);
    ch10_set_spawn(CH10T_WAVE9_RECORD, CH10T_ARRIVAL_CHAR_ID, 9);
    ch10_set_spawn(CH10T_WAVE0_RECORD, CH10T_ARRIVAL_CHAR_ID, 0);
    ch10_set_spawn(CH10T_WAVE10_RECORD, CH10T_ARRIVAL_CHAR_ID, CH10_WAVE);
    ch10_set_spawn(CH10T_WAVE11_RECORD, CH10T_ARRIVAL_CHAR_ID,
                   CH10T_LAST_WAVE);
    ch10_set_spawn(CH10T_UNASKED_RECORD, CH10T_ARRIVAL_CHAR_ID,
                   CH10T_UNASKED_WAVE);
    ch10_spawn_at(CH10T_WAVE1_RECORD)->level = (unsigned char) CH10T_WAVE1_LEVEL;
    ch10_spawn_at(CH10T_WAVE2_RECORD)->level = (unsigned char) CH10T_WAVE2_LEVEL;
    ch10_spawn_at(CH10T_WAVE3_RECORD)->level = (unsigned char) CH10T_WAVE3_LEVEL;
    ch10_spawn_at(CH10T_WAVE9_RECORD)->level = (unsigned char) CH10T_WAVE9_LEVEL;
    ch10_spawn_at(CH10T_WAVE0_RECORD)->level = (unsigned char) CH10T_WAVE0_LEVEL;
    ch10_spawn_at(CH10T_WAVE10_RECORD)->level =
        (unsigned char) CH10T_WAVE10_LEVEL;
    ch10_spawn_at(CH10T_WAVE11_RECORD)->level =
        (unsigned char) CH10T_WAVE11_LEVEL;

    for (i = 0; i < CH10T_TEXT_ENTRY_COUNT; i++) {
        ch10t_text_block[i] = (short) (CH10T_TEXT_ENTRY_COUNT * 2);
    }
    ch10t_text_block[CH10T_TEXT_ENTRY_COUNT] = -1;
    data_fdps_current_chapter_text_ptr = (unsigned char *) ch10t_text_block;

    data_fdps_scene_layer_count = 0;
    data_fdps_map_cursor_draw_mode = 0;
    data_fdps_ui_terrain_hud_user_enabled = 0;
    data_fdps_ui_play_active_flag = 0;
    data_fdps_battle_view_window_origin_x = 0;
    data_fdps_battle_view_window_origin_y = 0;
    data_fdps_map_cursor_world_x = 0;
    data_fdps_map_cursor_world_y = 0;
    data_fdps_view_frame_last_tick = CH10T_FRAME_SENTINEL;

    data_fdps_battle_turn_counter = battle_turn;
}

/* One call with the timer running and the adapter in the mode the frames
   present through, so the pacing spin sees a moving counter and the two retrace
   spins see a real retrace. */
static void ch10t_run(int event_arg)
{
    ch10t_set_mode(CH10T_MODE_13H);
    ch10t_saved_timer = _dos_getvect(CH10T_TIMER_VECTOR);
    _dos_setvect(CH10T_TIMER_VECTOR, ch10t_timer_isr);
    fdps_chapter_10_event_deploy_wave_for_turn(event_arg);
    _dos_setvect(CH10T_TIMER_VECTOR, ch10t_saved_timer);
    ch10t_set_mode(CH10T_MODE_TEXT);
}

/* The fields the cases read back and the stride they are indexed by.  The level
   byte is what tells one arrival from another and the portrait byte is what
   keeps the compositor off them, so both offsets have to be the ones the
   emitted code and fdps_deploy_unit address. */
static void ch10t_record_shape_matches_the_offsets(void)
{
    CHECK_EQ((int) sizeof(struct fdps_unit_record), CH10_UNIT_STRIDE);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, level), 0x21);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, portrait_id), 7);
    CHECK_EQ((int) offsetof(struct fdps_char_spawn_record, wave_no), 0x15);
}

/* Turn 3 brings on wave 1 and stops at the draw: one unit arrives, carrying the
   wave-1 record's level and MAP00.COD's record 0 coordinates, the cursor is
   still at the origin and the frame latch still holds the sentinel, so no pan
   and no frame happened.  This is the case that would come out differently if
   the JNZ at 00037793 had been read the other way round. */
static void ch10t_announced_turn_deploys_wave_one_and_stops(void)
{
    ch10_ensure_game_files();
    if (!ch10_files_ready) {
        return;
    }

    ch10t_stage(CH10T_ANNOUNCED_TURN);
    ch10t_run(0);

    CHECK_EQ(data_fdps_map_unit_count, CH10T_STAGED_UNITS + 1);
    CHECK_EQ((int) ch10_unit(CH10T_STAGED_UNITS)->level, CH10T_WAVE1_LEVEL);
    CHECK_EQ((int) ch10_unit(CH10T_STAGED_UNITS)->pos_x,
             CH10T_MAP00_RECORD0_X);
    CHECK_EQ((int) ch10_unit(CH10T_STAGED_UNITS)->pos_y,
             CH10T_MAP00_RECORD0_Y);
    CHECK_EQ(data_fdps_map_cursor_world_x, 0);
    CHECK_EQ(data_fdps_map_cursor_world_y, 0);
    CHECK_EQ(data_fdps_view_frame_last_tick == CH10T_FRAME_SENTINEL, 1);
}

/* The middle arm asks for the turn less four and for nothing else.  Turn 6
   brings on the wave-2 record and turn 13 the wave-9 one, with the wave-1 and
   wave-3 records sitting either side of the first of them in the same table:
   a bias of 3 or 5, or a handler that passed the counter whole, lands on a
   different record every time.  Turn 13 is also the arm's last turn, so it
   pins the JG bound from below. */
static void ch10t_door_turn_asks_for_the_turn_less_four(void)
{
    ch10_ensure_game_files();
    if (!ch10_files_ready) {
        return;
    }

    ch10t_stage(CH10T_FIRST_DOOR_TURN);
    ch10t_run(0);
    CHECK_EQ(data_fdps_map_unit_count, CH10T_STAGED_UNITS + 1);
    CHECK_EQ((int) ch10_unit(CH10T_STAGED_UNITS)->level, CH10T_WAVE2_LEVEL);
    CHECK_EQ((int) ch10_unit(CH10T_STAGED_UNITS)->pos_x,
             CH10T_MAP00_RECORD1_X);
    CHECK_EQ((int) ch10_unit(CH10T_STAGED_UNITS)->pos_y,
             CH10T_MAP00_RECORD1_Y);

    ch10t_stage(CH10T_LAST_DOOR_TURN);
    ch10t_run(0);
    CHECK_EQ(data_fdps_map_unit_count, CH10T_STAGED_UNITS + 1);
    CHECK_EQ((int) ch10_unit(CH10T_STAGED_UNITS)->level, CH10T_WAVE9_LEVEL);
}

/* The middle arm ends the pan on the right-hand door: the cursor starts at the
   origin and is left on world pixel (360, 216) exactly, which is tile (15, 9),
   and the frame latch has lost its sentinel, so frames were composed.  An arm
   that only walked to the left-hand door would leave the cursor on (72, 216),
   and one that composed no frames at all would leave the sentinel. */
static void ch10t_door_turn_pans_to_the_right_hand_door(void)
{
    ch10_ensure_game_files();
    if (!ch10_files_ready) {
        return;
    }

    ch10t_stage(CH10T_FIRST_DOOR_TURN);
    ch10t_run(0);

    CHECK_EQ(data_fdps_map_cursor_world_x, CH10T_RIGHT_DOOR_WORLD_X);
    CHECK_EQ(data_fdps_map_cursor_world_y, CH10T_DOOR_WORLD_Y);
    CHECK_EQ(data_fdps_view_frame_last_tick == CH10T_FRAME_SENTINEL, 0);
    CHECK_EQ(data_fdps_map_cursor_world_x == CH10T_LEFT_DOOR_WORLD_X, 0);
}

/* The middle arm has no lower bound, which is the whole of contract C on the
   JG at 000377d7.  Turn 4 is below everything map09.dat schedules and still
   runs the arm, asking for wave 0 -- the fixture carries a wave-0 record and it
   arrives.  Turn 0 asks for wave -4, which no record's unsigned wave byte can
   equal, so nothing arrives at all; the pan runs anyway, which is what says the
   arm was entered rather than skipped.  Read as an unsigned compare, both turns
   would still take this arm, but a counter restored negative from a save would
   take the last-wave arm and bring wave 11 on early. */
static void ch10t_door_arm_has_no_lower_bound(void)
{
    ch10_ensure_game_files();
    if (!ch10_files_ready) {
        return;
    }

    ch10t_stage(CH10T_WAVE_ZERO_TURN);
    ch10t_run(0);
    CHECK_EQ(data_fdps_map_unit_count, CH10T_STAGED_UNITS + 1);
    CHECK_EQ((int) ch10_unit(CH10T_STAGED_UNITS)->level, CH10T_WAVE0_LEVEL);
    CHECK_EQ(data_fdps_map_cursor_world_x, CH10T_RIGHT_DOOR_WORLD_X);

    ch10t_stage(CH10T_NEGATIVE_WAVE_TURN);
    ch10t_run(0);
    CHECK_EQ(data_fdps_map_unit_count, CH10T_STAGED_UNITS);
    CHECK_EQ(data_fdps_map_cursor_world_x, CH10T_RIGHT_DOOR_WORLD_X);
    CHECK_EQ(data_fdps_map_cursor_world_y, CH10T_DOOR_WORLD_Y);
    CHECK_EQ(data_fdps_view_frame_last_tick == CH10T_FRAME_SENTINEL, 0);
}

/* Turn 14, the first turn past the bound, takes the last-wave arm: wave 11
   arrives, the cursor never moves and no frame is composed.  Turn 19, the turn
   map09.dat actually schedules, does the same.  Against the turn 13 half of the
   case above, this is the pair that pins JG on 0xd rather than JGE or a bound
   one either way -- and the wave-10 record left in the table underneath is what
   rules out this arm asking for the tile trigger's wave. */
static void ch10t_turn_past_the_bound_takes_the_last_wave(void)
{
    ch10_ensure_game_files();
    if (!ch10_files_ready) {
        return;
    }

    ch10t_stage(CH10T_FIRST_LAST_WAVE_TURN);
    ch10t_run(0);
    CHECK_EQ(data_fdps_map_unit_count, CH10T_STAGED_UNITS + 1);
    CHECK_EQ((int) ch10_unit(CH10T_STAGED_UNITS)->level, CH10T_WAVE11_LEVEL);
    CHECK_EQ(data_fdps_map_cursor_world_x, 0);
    CHECK_EQ(data_fdps_map_cursor_world_y, 0);
    CHECK_EQ(data_fdps_view_frame_last_tick == CH10T_FRAME_SENTINEL, 1);

    ch10t_stage(CH10T_LAST_WAVE_TURN);
    ch10t_run(0);
    CHECK_EQ(data_fdps_map_unit_count, CH10T_STAGED_UNITS + 1);
    CHECK_EQ((int) ch10_unit(CH10T_STAGED_UNITS)->level, CH10T_WAVE11_LEVEL);
    CHECK_EQ(data_fdps_view_frame_last_tick == CH10T_FRAME_SENTINEL, 1);
}

/* The map each deployment is placed under is read from
   data_fdps_chapter_current_chapter_id at the call site -- PUSH dword ptr
   [0x00069cf4] at 0003779a, 000377e9 and 0003785b -- and is not a literal.  The
   same wave-1 record lands on MAP00.COD's record 0 at (18, 0) with the global
   on 0 and on MAP01.COD's record 0 at (9, 4) with it on 1; a handler that
   pushed a literal would land on the same tile both times.  The announced arm
   is used because it reaches the deployment without composing a frame. */
static void ch10t_map_number_comes_from_the_chapter_global(void)
{
    ch10_ensure_game_files();
    if (!ch10_files_ready) {
        return;
    }

    ch10t_stage(CH10T_ANNOUNCED_TURN);
    data_fdps_chapter_current_chapter_id = 0;
    ch10t_run(0);
    CHECK_EQ((int) ch10_unit(CH10T_STAGED_UNITS)->pos_x,
             CH10T_MAP00_RECORD0_X);
    CHECK_EQ((int) ch10_unit(CH10T_STAGED_UNITS)->pos_y,
             CH10T_MAP00_RECORD0_Y);

    ch10t_stage(CH10T_ANNOUNCED_TURN);
    data_fdps_chapter_current_chapter_id = 1;
    ch10t_run(0);
    CHECK_EQ((int) ch10_unit(CH10T_STAGED_UNITS)->pos_x,
             CH10T_MAP01_RECORD0_X);
    CHECK_EQ((int) ch10_unit(CH10T_STAGED_UNITS)->pos_y,
             CH10T_MAP01_RECORD0_Y);
}

/* There is no guard of any kind: the family's shared one-shot slot raised does
   not block the handler, and a second call on the same turn deploys a second
   time rather than being refused.  Ten arrivals over ten scheduled turns is
   what the shipped data gets out of that, and it is also what makes the wave
   walk's own behaviour visible -- it appends and never checks whether the wave
   is already on the map.  The latch is raised rather than assumed clear because
   its starting value is ticket 23's. */
static void ch10t_has_no_latch_and_fires_every_call(void)
{
    ch10_ensure_game_files();
    if (!ch10_files_ready) {
        return;
    }

    ch10t_stage(CH10T_ANNOUNCED_TURN);
    data_fdps_map_cell_event_triggered_flags[CH10_LATCH_SLOT] = 1;

    ch10t_run(0);
    CHECK_EQ(data_fdps_map_unit_count, CH10T_STAGED_UNITS + 1);
    CHECK_EQ((int) ch10_unit(CH10T_STAGED_UNITS)->level, CH10T_WAVE1_LEVEL);

    ch10t_run(0);
    CHECK_EQ(data_fdps_map_unit_count, CH10T_STAGED_UNITS + 2);
    CHECK_EQ((int) ch10_unit(CH10T_STAGED_UNITS + 1)->level,
             CH10T_WAVE1_LEVEL);
    CHECK_EQ(data_fdps_map_cell_event_triggered_flags[CH10_LATCH_SLOT], 1);
}

/* The incoming argument cannot reach anything.  On the middle arm it is
   overwritten with 0 at 00037806 and used as the frame counter, and the other
   two arms never read it, so an argument of 7 and one of -1 deploy the same
   record and end the pan on the same pixel.  A handler that had kept the
   incoming value as its counter would hold the view for a different length of
   time for each of them, and -1 would run the first hold 13 times rather than
   12. */
static void ch10t_ignores_the_event_argument(void)
{
    static int arguments[2] = {7, -1};
    int i;

    ch10_ensure_game_files();
    if (!ch10_files_ready) {
        return;
    }

    for (i = 0; i < 2; i++) {
        ch10t_stage(CH10T_FIRST_DOOR_TURN);
        ch10t_run(arguments[i]);

        CHECK_EQ(data_fdps_map_unit_count, CH10T_STAGED_UNITS + 1);
        CHECK_EQ((int) ch10_unit(CH10T_STAGED_UNITS)->level,
                 CH10T_WAVE2_LEVEL);
        CHECK_EQ(data_fdps_map_cursor_world_x, CH10T_RIGHT_DOOR_WORLD_X);
        CHECK_EQ(data_fdps_map_cursor_world_y, CH10T_DOOR_WORLD_Y);
    }
}

/* Chapter 11's turn-scheduled handler at 000378f0, from here down.
 *
 * It is two arms off one equality test on data_fdps_battle_turn_counter, so
 * what the cases pin is which arm a turn lands in, which wave that arm asks
 * for, the map number it is placed under, and -- for the fall-through arm --
 * which unit the pan ends on and that frames were composed at all.
 *
 * THE TURN AND THE WAVE ARE CROSSED, which is the whole reason this section
 * exists: turn 4 asks for wave 5 and every other turn asks for wave 4.  The
 * fixture gives the two waves different levels AND different placement
 * records, so a rebuild that paired them the intuitive way round fails on both
 * the level and the coordinates.
 *
 * The map fixture is the chapter 10 ambush section's, reused rather than
 * copied, with four things on top of it.
 *
 * FIFTY-THREE UNITS ALREADY ON THE MAP.  The fall-through arm pans onto unit
 * indices 0x34 and 0x2c and neither is range checked, so an array shorter than
 * 0x35 records would send the pan to whatever coordinate bytes lie past the
 * block -- up to (255, 255), which is a walk of thousands of steps.  The
 * staged units are laid out seven to a row so that both indices are on the
 * grid and on different tiles from each other, and clear of every placement
 * record the cases read back.
 *
 * A PORTRAIT ID EVERY UNIT IS DROPPED ON, for the reason the chapter 10 turn
 * section gives: the fall-through arm composes frames, which runs the whole
 * map compositor over every unit in the array, and 0x80 is the id
 * fdps_draw_map_unit returns on.  So the cases tell arrivals apart by the
 * level byte and never by a character id.
 *
 * A TEXT BLOCK COVERING THE ONE ID THE HANDLER NAMES, 0x0f, with every entry
 * holding the same offset onto the lone -1 that follows the table, so the
 * stream ends before a glyph is drawn and before any of the modal codes
 * fdps_draw_text would stand a keyboard wait on.
 *
 * A REAL TIMER INTERRUPT for the duration of every call that reaches the
 * fall-through arm, hooked and unhooked the way the chapter 10 turn section
 * hooks its own -- fdps_render_view_frame's pacing wait ends only when
 * data_fdps_timer_tick_counter moves.
 *
 * HOW MANY FRAMES EACH HOLD COMPOSES IS NOT ASSERTED, for the reason that
 * section gives as well: a frame consumes one timer tick and a test cannot
 * count them without racing the interrupt, so twelve is a playtest contract.
 * What IS exactly observable is whether any frame ran, through the sentinel
 * parked in data_fdps_view_frame_last_tick.
 *
 * WHERE THE FIRST PAN GOES CANNOT BE SEEN FROM OUTSIDE.  The second move
 * overwrites the position the first one left, so PUSH 0x34 at 00037953 stands
 * on the reviewer's reading of the instruction stream; what the cases pin is
 * that the pan ends on unit 0x2c's tile and not on unit 0x34's.
 *
 * Every case reaches the deployment, which opens ICON.CEL and FIELD.VFS for
 * itself, so every case skips itself without them.
 */

/* The one turn the equality test names, CMP dword ptr [0x00069ce8],0x4 at
   000378fc, and the wave that arm asks for, PUSH 0x5 at 00037908. */
#define CH11T_FOUR_CORNER_TURN 4
#define CH11T_FOUR_CORNER_WAVE 5

/* The only other turn map10.dat schedules the handler for, and the wave the
   fall-through arm asks for, PUSH 0x4 at 00037943. */
#define CH11T_TOP_GROUPS_TURN 7
#define CH11T_TOP_GROUPS_WAVE 4

/* Two turns no map sends here, one either side of the scheduled pair, which
   the fall-through arm has no bound against. */
#define CH11T_EARLY_TURN 1
#define CH11T_LATE_TURN 100

/* The two units the pan visits, PUSH 0x34 at 00037953 and PUSH 0x2c at
   0003797b, and the tile step fdps_map_cursor_move_to_unit scales their
   coordinates by, IMUL EAX,EAX,0x18 at 0002da76. */
#define CH11T_TOP_LEFT_GROUP_UNIT_INDEX 0x34
#define CH11T_TOP_RIGHT_GROUP_UNIT_INDEX 0x2c
#define CH11T_TILE_STEP 24

/* Enough units for the higher of the two pan indices to be a live record, laid
   out seven to a row so both indices land on the grid and on different tiles:
   0x2c falls on (2, 6) and 0x34 on (3, 7). */
#define CH11T_STAGED_UNITS (CH11T_TOP_LEFT_GROUP_UNIT_INDEX + 1)
#define CH11T_STAGE_ROW_WIDTH 7

/* One deployment record per wave the cases ask about, at the table index that
   is also its MAP%02d.COD placement record.  The two waves the handler names
   sit on records 0 and 1, whose coordinates the ambush cases above already
   read back out of the real files, and the waves either side of them sit on
   records the cases only ever check are left behind. */
#define CH11T_WAVE5_RECORD 0
#define CH11T_WAVE4_RECORD 1
#define CH11T_WAVE3_RECORD 2
#define CH11T_WAVE6_RECORD 3
#define CH11T_SPAWN_RECORD_COUNT 4

/* The level each record carries, which is how the cases tell which one
   arrived: the character id cannot do it, because every record has to carry
   the one id the compositor drops. */
#define CH11T_WAVE5_LEVEL 5
#define CH11T_WAVE4_LEVEL 4
#define CH11T_WAVE3_LEVEL 3
#define CH11T_WAVE6_LEVEL 6

/* MAP00.COD's placement record 0 and record 1, and MAP01.COD's record 0 -- the
   same coordinates the ambush cases above read back out of the real files. */
#define CH11T_MAP00_RECORD0_X 18
#define CH11T_MAP00_RECORD0_Y 0
#define CH11T_MAP00_RECORD1_X 22
#define CH11T_MAP00_RECORD1_Y 12
#define CH11T_MAP01_RECORD0_X 9
#define CH11T_MAP01_RECORD0_Y 4

/* The id the compositor drops, PORTRAIT_ID_NO_MAP_SPRITE at 00033c56. */
#define CH11T_ARRIVAL_CHAR_ID 0x80

/* The one id the handler names plus the terminator every entry points at. */
#define CH11T_TEXT_ENTRY_COUNT 0x10

/* A value the tick counter cannot legitimately hold on entry, parked in the
   frame latch so a run that composed nothing is distinguishable from one that
   did. */
#define CH11T_FRAME_SENTINEL 0x5a5a5a5aU

static short ch11t_text_block[CH11T_TEXT_ENTRY_COUNT + 1];

/* The ambush section's map fixture with 0x35 units on it, every one of them
   wearing the dropped portrait id and standing seven to a row, one deployment
   record per wave the cases ask about, the text block and the frame latch
   sentinel.  The cursor starts at the origin so the pan has a real distance to
   cover on both axes. */
static void ch11t_stage(int battle_turn)
{
    int i;

    ch10_stage(CH11T_STAGED_UNITS);

    for (i = 0; i < CH11T_STAGED_UNITS; i++) {
        ch10_unit(i)->portrait_id = (unsigned char) CH11T_ARRIVAL_CHAR_ID;
        ch10_unit(i)->pos_x = (unsigned char) (i % CH11T_STAGE_ROW_WIDTH);
        ch10_unit(i)->pos_y = (unsigned char) (i / CH11T_STAGE_ROW_WIDTH);
    }

    ch10_spawn_table[CH10_SPAWN_TABLE_COUNT_OFFSET] =
        (unsigned char) CH11T_SPAWN_RECORD_COUNT;
    ch10_set_spawn(CH11T_WAVE5_RECORD, CH11T_ARRIVAL_CHAR_ID,
                   CH11T_FOUR_CORNER_WAVE);
    ch10_set_spawn(CH11T_WAVE4_RECORD, CH11T_ARRIVAL_CHAR_ID,
                   CH11T_TOP_GROUPS_WAVE);
    ch10_set_spawn(CH11T_WAVE3_RECORD, CH11T_ARRIVAL_CHAR_ID, 3);
    ch10_set_spawn(CH11T_WAVE6_RECORD, CH11T_ARRIVAL_CHAR_ID, 6);
    ch10_spawn_at(CH11T_WAVE5_RECORD)->level =
        (unsigned char) CH11T_WAVE5_LEVEL;
    ch10_spawn_at(CH11T_WAVE4_RECORD)->level =
        (unsigned char) CH11T_WAVE4_LEVEL;
    ch10_spawn_at(CH11T_WAVE3_RECORD)->level =
        (unsigned char) CH11T_WAVE3_LEVEL;
    ch10_spawn_at(CH11T_WAVE6_RECORD)->level =
        (unsigned char) CH11T_WAVE6_LEVEL;

    for (i = 0; i < CH11T_TEXT_ENTRY_COUNT; i++) {
        ch11t_text_block[i] = (short) (CH11T_TEXT_ENTRY_COUNT * 2);
    }
    ch11t_text_block[CH11T_TEXT_ENTRY_COUNT] = -1;
    data_fdps_current_chapter_text_ptr = (unsigned char *) ch11t_text_block;

    data_fdps_scene_layer_count = 0;
    data_fdps_map_cursor_draw_mode = 0;
    data_fdps_ui_terrain_hud_user_enabled = 0;
    data_fdps_ui_play_active_flag = 0;
    data_fdps_battle_view_window_origin_x = 0;
    data_fdps_battle_view_window_origin_y = 0;
    data_fdps_map_cursor_world_x = 0;
    data_fdps_map_cursor_world_y = 0;
    data_fdps_view_frame_last_tick = CH11T_FRAME_SENTINEL;

    data_fdps_battle_turn_counter = battle_turn;
}

/* One call with the timer running and the adapter in the mode the frames
   present through, exactly as the chapter 10 turn section runs its own. */
static void ch11t_run(int event_arg)
{
    ch10t_set_mode(CH10T_MODE_13H);
    ch10t_saved_timer = _dos_getvect(CH10T_TIMER_VECTOR);
    _dos_setvect(CH10T_TIMER_VECTOR, ch10t_timer_isr);
    fdps_chapter_11_event_deploy_wave_for_turn(event_arg);
    _dos_setvect(CH10T_TIMER_VECTOR, ch10t_saved_timer);
    ch10t_set_mode(CH10T_MODE_TEXT);
}

/* Turn 4 brings on wave 5 -- not wave 4 -- and stops at the draw: one unit
   arrives carrying the wave-5 record's level and MAP00.COD record 0's
   coordinates, the cursor is still at the origin and the frame latch still
   holds the sentinel, so no pan and no frame happened.  This is the case that
   would come out differently if the JNZ at 00037903 had been read the other
   way round, and the level and the coordinates together are what catch a
   rebuild that paired turn 4 with wave 4. */
static void ch11t_turn_four_deploys_wave_five_and_stops(void)
{
    ch10_ensure_game_files();
    if (!ch10_files_ready) {
        return;
    }

    ch11t_stage(CH11T_FOUR_CORNER_TURN);
    ch11t_run(0);

    CHECK_EQ(data_fdps_map_unit_count, CH11T_STAGED_UNITS + 1);
    CHECK_EQ((int) ch10_unit(CH11T_STAGED_UNITS)->level, CH11T_WAVE5_LEVEL);
    CHECK_EQ((int) ch10_unit(CH11T_STAGED_UNITS)->pos_x,
             CH11T_MAP00_RECORD0_X);
    CHECK_EQ((int) ch10_unit(CH11T_STAGED_UNITS)->pos_y,
             CH11T_MAP00_RECORD0_Y);
    CHECK_EQ(data_fdps_map_cursor_world_x, 0);
    CHECK_EQ(data_fdps_map_cursor_world_y, 0);
    CHECK_EQ(data_fdps_view_frame_last_tick == CH11T_FRAME_SENTINEL, 1);
}

/* Turn 7 brings on wave 4 -- not wave 5 -- landing on MAP00.COD record 1.  The
   wave-3 and wave-6 records sit either side of it in the same table, so a
   handler that passed the turn counter, or the turn less something, lands on a
   different record or on none. */
static void ch11t_turn_seven_deploys_wave_four(void)
{
    ch10_ensure_game_files();
    if (!ch10_files_ready) {
        return;
    }

    ch11t_stage(CH11T_TOP_GROUPS_TURN);
    ch11t_run(0);

    CHECK_EQ(data_fdps_map_unit_count, CH11T_STAGED_UNITS + 1);
    CHECK_EQ((int) ch10_unit(CH11T_STAGED_UNITS)->level, CH11T_WAVE4_LEVEL);
    CHECK_EQ((int) ch10_unit(CH11T_STAGED_UNITS)->pos_x,
             CH11T_MAP00_RECORD1_X);
    CHECK_EQ((int) ch10_unit(CH11T_STAGED_UNITS)->pos_y,
             CH11T_MAP00_RECORD1_Y);
}

/* The fall-through arm has no bound of any kind: the branch is one equality
   test, so turn 1 and turn 100 -- neither of which any map schedules this slot
   for -- run it in full and deploy wave 4, and the second of them also pans,
   which is what says the arm was entered rather than skipped. */
static void ch11t_fall_through_has_no_turn_bound(void)
{
    ch10_ensure_game_files();
    if (!ch10_files_ready) {
        return;
    }

    ch11t_stage(CH11T_EARLY_TURN);
    ch11t_run(0);
    CHECK_EQ(data_fdps_map_unit_count, CH11T_STAGED_UNITS + 1);
    CHECK_EQ((int) ch10_unit(CH11T_STAGED_UNITS)->level, CH11T_WAVE4_LEVEL);

    ch11t_stage(CH11T_LATE_TURN);
    ch11t_run(0);
    CHECK_EQ(data_fdps_map_unit_count, CH11T_STAGED_UNITS + 1);
    CHECK_EQ((int) ch10_unit(CH11T_STAGED_UNITS)->level, CH11T_WAVE4_LEVEL);
    CHECK_EQ(data_fdps_view_frame_last_tick == CH11T_FRAME_SENTINEL, 0);
}

/* The pan ends on unit 0x2c and not on unit 0x34: the staged layout puts 0x2c
   on tile (2, 6) and 0x34 on tile (3, 7), and the cursor is left on the world
   pixel the first of those scales to.  An arm that stopped after the first
   move, or that visited the two in the other order, would leave the cursor on
   0x34's tile instead, and one that composed no frames at all would leave the
   sentinel in the latch. */
static void ch11t_pan_ends_on_the_top_right_group(void)
{
    int left_tile_x;
    int left_tile_y;
    int right_tile_x;
    int right_tile_y;

    ch10_ensure_game_files();
    if (!ch10_files_ready) {
        return;
    }

    left_tile_x = CH11T_TOP_LEFT_GROUP_UNIT_INDEX % CH11T_STAGE_ROW_WIDTH;
    left_tile_y = CH11T_TOP_LEFT_GROUP_UNIT_INDEX / CH11T_STAGE_ROW_WIDTH;
    right_tile_x = CH11T_TOP_RIGHT_GROUP_UNIT_INDEX % CH11T_STAGE_ROW_WIDTH;
    right_tile_y = CH11T_TOP_RIGHT_GROUP_UNIT_INDEX / CH11T_STAGE_ROW_WIDTH;

    ch11t_stage(CH11T_TOP_GROUPS_TURN);
    ch11t_run(0);

    CHECK_EQ(data_fdps_map_cursor_world_x, right_tile_x * CH11T_TILE_STEP);
    CHECK_EQ(data_fdps_map_cursor_world_y, right_tile_y * CH11T_TILE_STEP);
    CHECK_EQ(data_fdps_map_cursor_world_x == left_tile_x * CH11T_TILE_STEP, 0);
    CHECK_EQ(data_fdps_map_cursor_world_y == left_tile_y * CH11T_TILE_STEP, 0);
    CHECK_EQ(data_fdps_view_frame_last_tick == CH11T_FRAME_SENTINEL, 0);
}

/* The map the wave is deployed under is read from
   data_fdps_chapter_current_chapter_id at the call site and is not a literal:
   the same wave-5 record placed while that global says 1 lands on MAP01.COD's
   record 0 at (9, 4) instead of MAP00.COD's (18, 0). */
static void ch11t_map_number_comes_from_the_chapter_global(void)
{
    ch10_ensure_game_files();
    if (!ch10_files_ready) {
        return;
    }

    ch11t_stage(CH11T_FOUR_CORNER_TURN);
    ch11t_run(0);
    CHECK_EQ((int) ch10_unit(CH11T_STAGED_UNITS)->pos_x,
             CH11T_MAP00_RECORD0_X);
    CHECK_EQ((int) ch10_unit(CH11T_STAGED_UNITS)->pos_y,
             CH11T_MAP00_RECORD0_Y);

    ch11t_stage(CH11T_FOUR_CORNER_TURN);
    data_fdps_chapter_current_chapter_id = 1;
    ch11t_run(0);
    CHECK_EQ((int) ch10_unit(CH11T_STAGED_UNITS)->pos_x,
             CH11T_MAP01_RECORD0_X);
    CHECK_EQ((int) ch10_unit(CH11T_STAGED_UNITS)->pos_y,
             CH11T_MAP01_RECORD0_Y);
}

/* The placement flag is 0 -- XOR EAX,EAX / PUSH EAX at 00037905 -- so the
   arrivals are put on the nearest free walkable tile to their placement record
   rather than on the record's own tile.  MAP00.COD record 0 names (18, 0);
   giving that one cell a tile id whose attribute row is terrain 5 takes it out
   of the search and the unit lands one tile away.  A flag of 1 would drop it
   on (18, 0) regardless of the terrain there.

   (18, 1) is which of the three tiles at distance 1 it lands on, for the
   reason the ambush section's own case gives: the scan is row-major over the
   whole grid and a tie is accepted, so the last candidate at the best distance
   wins, and (18, 1) is a row below (17, 0) and (19, 0). */
static void ch11t_places_on_the_nearest_free_tile(void)
{
    ch10_ensure_game_files();
    if (!ch10_files_ready) {
        return;
    }

    ch11t_stage(CH11T_FOUR_CORNER_TURN);
    ch10_set_tile_id(CH11T_MAP00_RECORD0_X, CH11T_MAP00_RECORD0_Y, 1);
    ch10_set_terrain(1, CH10_TERRAIN_BLOCKED);

    ch11t_run(0);

    CHECK_EQ(data_fdps_map_unit_count, CH11T_STAGED_UNITS + 1);
    CHECK_EQ((int) ch10_unit(CH11T_STAGED_UNITS)->level, CH11T_WAVE5_LEVEL);
    CHECK_EQ((int) ch10_unit(CH11T_STAGED_UNITS)->pos_x,
             CH11T_MAP00_RECORD0_X);
    CHECK_EQ((int) ch10_unit(CH11T_STAGED_UNITS)->pos_y,
             CH11T_MAP00_RECORD0_Y + 1);
}

/* No latch and no other guard: the handler fires in full on every call, so a
   second call on the same turn appends the same record again, and a one-shot
   slot standing up makes no difference to either.  The wave walk appends and
   never checks whether the wave is already on the map, so this is what the
   original does. */
static void ch11t_has_no_latch_and_fires_every_call(void)
{
    ch10_ensure_game_files();
    if (!ch10_files_ready) {
        return;
    }

    ch11t_stage(CH11T_FOUR_CORNER_TURN);
    data_fdps_map_cell_event_triggered_flags[CH10_LATCH_SLOT] = 1;

    ch11t_run(0);
    CHECK_EQ(data_fdps_map_unit_count, CH11T_STAGED_UNITS + 1);
    ch11t_run(0);

    CHECK_EQ(data_fdps_map_unit_count, CH11T_STAGED_UNITS + 2);
    CHECK_EQ((int) ch10_unit(CH11T_STAGED_UNITS + 1)->level,
             CH11T_WAVE5_LEVEL);
    CHECK_EQ(data_fdps_map_cell_event_triggered_flags[CH10_LATCH_SLOT], 1);
}

/* The incoming argument cannot reach anything.  On the fall-through arm it is
   overwritten with 0 at 0003795d and used as the frame counter, and the turn-4
   arm never reads it, so an argument of 7 and one of -1 deploy the same record
   and end the pan on the same pixel.  A handler that had kept the incoming
   value as its counter would hold the view for a different length of time for
   each of them, and -1 would run the first hold 13 times rather than 12. */
static void ch11t_ignores_the_event_argument(void)
{
    static int arguments[2] = {7, -1};
    int i;
    int right_tile_x;
    int right_tile_y;

    ch10_ensure_game_files();
    if (!ch10_files_ready) {
        return;
    }

    right_tile_x = CH11T_TOP_RIGHT_GROUP_UNIT_INDEX % CH11T_STAGE_ROW_WIDTH;
    right_tile_y = CH11T_TOP_RIGHT_GROUP_UNIT_INDEX / CH11T_STAGE_ROW_WIDTH;

    for (i = 0; i < 2; i++) {
        ch11t_stage(CH11T_TOP_GROUPS_TURN);
        ch11t_run(arguments[i]);

        CHECK_EQ(data_fdps_map_unit_count, CH11T_STAGED_UNITS + 1);
        CHECK_EQ((int) ch10_unit(CH11T_STAGED_UNITS)->level,
                 CH11T_WAVE4_LEVEL);
        CHECK_EQ(data_fdps_map_cursor_world_x,
                 right_tile_x * CH11T_TILE_STEP);
        CHECK_EQ(data_fdps_map_cursor_world_y,
                 right_tile_y * CH11T_TILE_STEP);
    }
}

/* Chapter 14's turn-6 handler at 00037a50 is the chapter 11 handler's
 * fall-through arm with no branch in front of it, and it is staged the same
 * way: the chapter 10 ambush fixture with a unit array, a deployment table and
 * a text block under it, MAP%02d.COD read from the real game files for the
 * placement coordinates, and the timer running so the frame holds can finish.
 *
 * THE FIXTURE PUTS THE ARRIVAL ON THE SECOND PAN'S OWN INDEX.  It stages 0x2a
 * units, indices 0 through 0x29, so the one record the wave brings on becomes
 * unit index 0x2a -- exactly the index the second pan names.  That is what
 * lets the cases see two things at once: which index the pan ends on, and that
 * the deployment ran before it, because index 0x2a is not a live record until
 * the wave has appended it.
 *
 * WHERE THE FIRST PAN GOES CANNOT BE SEEN FROM OUTSIDE, for the same reason as
 * in the chapter 11 section: the second move overwrites the position the first
 * one left, so PUSH 0x24 at 00037a76 stands on the reading of the instruction
 * stream.  What the cases pin is that the cursor ends on unit 0x2a's tile and
 * not on unit 0x24's.
 *
 * HOW LONG EACH HOLD LASTS IS PINNED THROUGH THE TIMER.  fdps_render_view_frame
 * blocks until the tick counter moves and then latches it, so 24 composed
 * frames cannot pass with fewer than 23 ticks between the first and the last.
 * The bound is one-sided on purpose -- a slow machine spends more ticks, never
 * fewer -- so it cannot fail spuriously, and a rebuild that dropped one of the
 * two loops cannot meet it at any sane speed.
 *
 * Every case reaches the deployment, which opens ICON.CEL and FIELD.VFS for
 * itself, so every case skips itself without them.
 */

/* The wave the handler asks for, PUSH 0x4 at 00037a66. */
#define CH14_WAVE 4

/* The two units the pans visit, PUSH 0x24 at 00037a76 and PUSH 0x2a at
   00037a9e, and the tile step fdps_map_cursor_move_to_unit scales their
   coordinates by, IMUL EAX,EAX,0x18 at 0002da76. */
#define CH14_LEFT_GROUP_UNIT_INDEX 0x24
#define CH14_RIGHT_GROUP_UNIT_INDEX 0x2a
#define CH14_TILE_STEP 24

/* How long each hold lasts, CMP dword ptr [EBP+0x14],0xc at 00037a87 and at
   00037aaf, and the least the tick counter can move across both of them. */
#define CH14_PAN_HOLD_FRAMES 0xc
#define CH14_LEAST_HOLD_TICKS (2 * CH14_PAN_HOLD_FRAMES - 1)

/* Units already on the map when the handler runs: one short of the second
   pan's index, so the record the wave brings on becomes that index.  They are
   laid out seven to a row, which puts 0x24 on tile (1, 5) and leaves every
   placement coordinate these cases read back clear of them. */
#define CH14_STAGED_UNITS CH14_RIGHT_GROUP_UNIT_INDEX
#define CH14_STAGE_ROW_WIDTH 7
#define CH14_ARRIVAL_UNIT_INDEX CH14_STAGED_UNITS

/* One deployment record per wave the cases ask about, at the table index that
   is also its MAP%02d.COD placement record.  The wave the handler names sits
   on record 0, whose coordinates the ambush cases above already read back out
   of the real files, and the waves either side of it sit on records the cases
   only ever check are left behind. */
#define CH14_WAVE4_RECORD 0
#define CH14_WAVE3_RECORD 1
#define CH14_WAVE5_RECORD 2
#define CH14_UNASKED_RECORD 3
#define CH14_SPAWN_RECORD_COUNT 4

/* A wave nothing in the handler asks for, parked on the spare record. */
#define CH14_UNASKED_WAVE 0xff

/* The level each record carries, which is how the cases tell which one
   arrived: the character id cannot do it, because every record has to carry
   the one id the compositor drops.  The wave-4 record's level is deliberately
   not its wave number, so a rebuild that deployed the wave it was handed and
   one that deployed something else are told apart by two fields and not one. */
#define CH14_WAVE4_LEVEL 7
#define CH14_WAVE3_LEVEL 3
#define CH14_WAVE5_LEVEL 5
#define CH14_UNASKED_LEVEL 6

/* MAP00.COD's placement record 0 and MAP01.COD's record 0 -- the same
   coordinates the ambush cases above read back out of the real files. */
#define CH14_MAP00_RECORD0_X 18
#define CH14_MAP00_RECORD0_Y 0
#define CH14_MAP01_RECORD0_X 9
#define CH14_MAP01_RECORD0_Y 4

/* The id the compositor drops, PORTRAIT_ID_NO_MAP_SPRITE at 00033c56. */
#define CH14_ARRIVAL_CHAR_ID 0x80

/* The one id the handler names plus the terminator every entry points at. */
#define CH14_TEXT_ENTRY_COUNT 0x12

/* A value the tick counter cannot legitimately hold on entry, parked in the
   frame latch so a run that composed nothing is distinguishable from one that
   did. */
#define CH14_FRAME_SENTINEL 0x5a5a5a5aU

/* The turn the map schedules this handler for.  Nothing in the body reads the
   counter; it is staged so a rebuild that did read it has a value to be caught
   deploying. */
#define CH14_SCHEDULED_TURN 6

/* Any other turn, staged to show the body has no turn test in it at all. */
#define CH14_OTHER_TURN 5

static short ch14_text_block[CH14_TEXT_ENTRY_COUNT + 1];
static unsigned int ch14_ticks_before;
static unsigned int ch14_ticks_after;

/* The ambush section's map fixture with 0x2a units on it, every one of them
   wearing the dropped portrait id and standing seven to a row, one deployment
   record per wave the cases ask about, the text block and the frame latch
   sentinel.  The cursor starts at the origin so both pans have a real distance
   to cover on both axes. */
static void ch14_stage(int battle_turn)
{
    int i;

    ch10_stage(CH14_STAGED_UNITS);

    for (i = 0; i < CH14_STAGED_UNITS; i++) {
        ch10_unit(i)->portrait_id = (unsigned char) CH14_ARRIVAL_CHAR_ID;
        ch10_unit(i)->pos_x = (unsigned char) (i % CH14_STAGE_ROW_WIDTH);
        ch10_unit(i)->pos_y = (unsigned char) (i / CH14_STAGE_ROW_WIDTH);
    }

    ch10_spawn_table[CH10_SPAWN_TABLE_COUNT_OFFSET] =
        (unsigned char) CH14_SPAWN_RECORD_COUNT;
    ch10_set_spawn(CH14_WAVE4_RECORD, CH14_ARRIVAL_CHAR_ID, CH14_WAVE);
    ch10_set_spawn(CH14_WAVE3_RECORD, CH14_ARRIVAL_CHAR_ID, 3);
    ch10_set_spawn(CH14_WAVE5_RECORD, CH14_ARRIVAL_CHAR_ID, 5);
    ch10_set_spawn(CH14_UNASKED_RECORD, CH14_ARRIVAL_CHAR_ID,
                   CH14_UNASKED_WAVE);
    ch10_spawn_at(CH14_WAVE4_RECORD)->level = (unsigned char) CH14_WAVE4_LEVEL;
    ch10_spawn_at(CH14_WAVE3_RECORD)->level = (unsigned char) CH14_WAVE3_LEVEL;
    ch10_spawn_at(CH14_WAVE5_RECORD)->level = (unsigned char) CH14_WAVE5_LEVEL;
    ch10_spawn_at(CH14_UNASKED_RECORD)->level =
        (unsigned char) CH14_UNASKED_LEVEL;

    for (i = 0; i < CH14_TEXT_ENTRY_COUNT; i++) {
        ch14_text_block[i] = (short) (CH14_TEXT_ENTRY_COUNT * 2);
    }
    ch14_text_block[CH14_TEXT_ENTRY_COUNT] = -1;
    data_fdps_current_chapter_text_ptr = (unsigned char *) ch14_text_block;

    data_fdps_scene_layer_count = 0;
    data_fdps_map_cursor_draw_mode = 0;
    data_fdps_ui_terrain_hud_user_enabled = 0;
    data_fdps_ui_play_active_flag = 0;
    data_fdps_battle_view_window_origin_x = 0;
    data_fdps_battle_view_window_origin_y = 0;
    data_fdps_map_cursor_world_x = 0;
    data_fdps_map_cursor_world_y = 0;
    data_fdps_view_frame_last_tick = CH14_FRAME_SENTINEL;

    data_fdps_battle_turn_counter = battle_turn;
}

/* One call with the timer running and the adapter in the mode the frames
   present through, with the tick counter sampled either side so the length of
   the two holds can be read back. */
static void ch14_run(int unit_index)
{
    ch10t_set_mode(CH10T_MODE_13H);
    ch10t_saved_timer = _dos_getvect(CH10T_TIMER_VECTOR);
    _dos_setvect(CH10T_TIMER_VECTOR, ch10t_timer_isr);
    ch14_ticks_before = data_fdps_timer_tick_counter;
    fdps_chapter_14_event_deploy_wave_4(unit_index);
    ch14_ticks_after = data_fdps_timer_tick_counter;
    _dos_setvect(CH10T_TIMER_VECTOR, ch10t_saved_timer);
    ch10t_set_mode(CH10T_MODE_TEXT);
}

/* The wave is 4: the one record tagged 4 arrives, carrying its own level and
   MAP00.COD record 0's coordinates, and the records tagged 3, 5 and 0xff are
   left where they are.  The unit count moves by exactly one, which is what
   says the neighbours in the table stayed put. */
static void ch14_deploys_the_records_tagged_wave_four(void)
{
    ch10_ensure_game_files();
    if (!ch10_files_ready) {
        return;
    }

    ch14_stage(CH14_SCHEDULED_TURN);
    ch14_run(0);

    CHECK_EQ(data_fdps_map_unit_count, CH14_STAGED_UNITS + 1);
    CHECK_EQ((int) ch10_unit(CH14_ARRIVAL_UNIT_INDEX)->level, CH14_WAVE4_LEVEL);
    CHECK_EQ((int) ch10_unit(CH14_ARRIVAL_UNIT_INDEX)->pos_x,
             CH14_MAP00_RECORD0_X);
    CHECK_EQ((int) ch10_unit(CH14_ARRIVAL_UNIT_INDEX)->pos_y,
             CH14_MAP00_RECORD0_Y);
}

/* The wave number is the literal 4 and not the turn counter, nor the turn less
   anything: the same record arrives whether the counter says 6 -- the turn
   MAP13.DAT schedules the handler for -- or 5, and no record tagged with
   either of those numbers exists for a rebuild that read the counter to find. */
static void ch14_wave_is_a_literal_and_not_the_turn_counter(void)
{
    ch10_ensure_game_files();
    if (!ch10_files_ready) {
        return;
    }

    ch14_stage(CH14_SCHEDULED_TURN);
    ch14_run(0);
    CHECK_EQ(data_fdps_map_unit_count, CH14_STAGED_UNITS + 1);
    CHECK_EQ((int) ch10_unit(CH14_ARRIVAL_UNIT_INDEX)->level, CH14_WAVE4_LEVEL);

    ch14_stage(CH14_OTHER_TURN);
    ch14_run(0);
    CHECK_EQ(data_fdps_map_unit_count, CH14_STAGED_UNITS + 1);
    CHECK_EQ((int) ch10_unit(CH14_ARRIVAL_UNIT_INDEX)->level, CH14_WAVE4_LEVEL);
}

/* The map the wave is placed under is read from
   data_fdps_chapter_current_chapter_id at the call site and is not a literal:
   the same record placed while that global says 1 lands on MAP01.COD's record
   0 at (9, 4) instead of MAP00.COD's (18, 0). */
static void ch14_map_number_comes_from_the_chapter_global(void)
{
    ch10_ensure_game_files();
    if (!ch10_files_ready) {
        return;
    }

    ch14_stage(CH14_SCHEDULED_TURN);
    ch14_run(0);
    CHECK_EQ((int) ch10_unit(CH14_ARRIVAL_UNIT_INDEX)->pos_x,
             CH14_MAP00_RECORD0_X);
    CHECK_EQ((int) ch10_unit(CH14_ARRIVAL_UNIT_INDEX)->pos_y,
             CH14_MAP00_RECORD0_Y);

    ch14_stage(CH14_SCHEDULED_TURN);
    data_fdps_chapter_current_chapter_id = 1;
    ch14_run(0);
    CHECK_EQ((int) ch10_unit(CH14_ARRIVAL_UNIT_INDEX)->pos_x,
             CH14_MAP01_RECORD0_X);
    CHECK_EQ((int) ch10_unit(CH14_ARRIVAL_UNIT_INDEX)->pos_y,
             CH14_MAP01_RECORD0_Y);
}

/* The placement flag is 0 -- XOR EAX,EAX / PUSH EAX at 00037a63 -- so the
   arrivals are put on the nearest free walkable tile to their placement record
   rather than on the record's own tile.  MAP00.COD record 0 names (18, 0);
   giving that one cell a tile id whose attribute row is terrain 5 takes it out
   of the search and the unit lands one tile away.  A flag of 1 would drop it on
   (18, 0) regardless of the terrain there.

   (18, 1) is which of the three tiles at distance 1 it lands on, for the reason
   the ambush section's own case gives: the scan is row-major over the whole
   grid and a tie is accepted, so the last candidate at the best distance wins,
   and (18, 1) is a row below (17, 0) and (19, 0). */
static void ch14_places_on_the_nearest_free_tile(void)
{
    ch10_ensure_game_files();
    if (!ch10_files_ready) {
        return;
    }

    ch14_stage(CH14_SCHEDULED_TURN);
    ch10_set_tile_id(CH14_MAP00_RECORD0_X, CH14_MAP00_RECORD0_Y, 1);
    ch10_set_terrain(1, CH10_TERRAIN_BLOCKED);

    ch14_run(0);

    CHECK_EQ(data_fdps_map_unit_count, CH14_STAGED_UNITS + 1);
    CHECK_EQ((int) ch10_unit(CH14_ARRIVAL_UNIT_INDEX)->level, CH14_WAVE4_LEVEL);
    CHECK_EQ((int) ch10_unit(CH14_ARRIVAL_UNIT_INDEX)->pos_x,
             CH14_MAP00_RECORD0_X);
    CHECK_EQ((int) ch10_unit(CH14_ARRIVAL_UNIT_INDEX)->pos_y,
             CH14_MAP00_RECORD0_Y + 1);
}

/* The pan ends on unit 0x2a and not on unit 0x24: the fixture leaves 0x24 on
   tile (1, 5) and the wave puts 0x2a on (18, 0), and the cursor is left on the
   world pixel the second of those scales to.  A body that stopped after the
   first move, or that visited the two in the other order, would leave the
   cursor on 0x24's tile instead, and one that composed no frames at all would
   leave the sentinel in the latch. */
static void ch14_pan_ends_on_the_right_hand_group(void)
{
    int left_tile_x;
    int left_tile_y;

    ch10_ensure_game_files();
    if (!ch10_files_ready) {
        return;
    }

    left_tile_x = CH14_LEFT_GROUP_UNIT_INDEX % CH14_STAGE_ROW_WIDTH;
    left_tile_y = CH14_LEFT_GROUP_UNIT_INDEX / CH14_STAGE_ROW_WIDTH;

    ch14_stage(CH14_SCHEDULED_TURN);
    ch14_run(0);

    CHECK_EQ(data_fdps_map_cursor_world_x,
             CH14_MAP00_RECORD0_X * CH14_TILE_STEP);
    CHECK_EQ(data_fdps_map_cursor_world_y,
             CH14_MAP00_RECORD0_Y * CH14_TILE_STEP);
    CHECK_EQ(data_fdps_map_cursor_world_x == left_tile_x * CH14_TILE_STEP, 0);
    CHECK_EQ(data_fdps_map_cursor_world_y == left_tile_y * CH14_TILE_STEP, 0);
    CHECK_EQ(data_fdps_view_frame_last_tick == CH14_FRAME_SENTINEL, 0);
}

/* The deployment runs before the pans and they read the array as it then
   stands.  Unit index 0x2a is not a live record on entry -- the fixture stages
   0x2a units, so the highest live index is 0x29 -- and the cursor still ends on
   the tile the wave put that record on, which can only happen if the arrival
   was appended first. */
static void ch14_pans_after_the_deployment(void)
{
    ch10_ensure_game_files();
    if (!ch10_files_ready) {
        return;
    }

    ch14_stage(CH14_SCHEDULED_TURN);
    CHECK_EQ(data_fdps_map_unit_count, CH14_RIGHT_GROUP_UNIT_INDEX);

    ch14_run(0);

    CHECK_EQ(data_fdps_map_unit_count, CH14_RIGHT_GROUP_UNIT_INDEX + 1);
    CHECK_EQ(data_fdps_map_cursor_world_x,
             CH14_MAP00_RECORD0_X * CH14_TILE_STEP);
    CHECK_EQ(data_fdps_map_cursor_world_y,
             CH14_MAP00_RECORD0_Y * CH14_TILE_STEP);
}

/* Both holds run, twelve frames each.  Every frame after the first blocks
   until the tick counter moves, so 24 of them cannot pass in fewer than 23
   ticks; a body carrying one hold instead of two spends about half that. */
static void ch14_holds_both_pans_for_twelve_frames(void)
{
    ch10_ensure_game_files();
    if (!ch10_files_ready) {
        return;
    }

    ch14_stage(CH14_SCHEDULED_TURN);
    ch14_run(0);

    CHECK_EQ(ch14_ticks_after - ch14_ticks_before >=
             (unsigned int) CH14_LEAST_HOLD_TICKS, 1);
    CHECK_EQ(data_fdps_view_frame_last_tick == CH14_FRAME_SENTINEL, 0);
}

/* No latch and no other guard: the handler fires in full on every call, so a
   second call appends the same record again, and a one-shot slot standing up
   makes no difference.  The wave walk appends and never checks whether the
   wave is already on the map, so this is what the original does. */
static void ch14_has_no_latch_and_fires_every_call(void)
{
    ch10_ensure_game_files();
    if (!ch10_files_ready) {
        return;
    }

    ch14_stage(CH14_SCHEDULED_TURN);
    data_fdps_map_cell_event_triggered_flags[CH10_LATCH_SLOT] = 1;

    ch14_run(0);
    CHECK_EQ(data_fdps_map_unit_count, CH14_STAGED_UNITS + 1);
    ch14_run(0);

    CHECK_EQ(data_fdps_map_unit_count, CH14_STAGED_UNITS + 2);
    CHECK_EQ((int) ch10_unit(CH14_STAGED_UNITS + 1)->level, CH14_WAVE4_LEVEL);
    CHECK_EQ(data_fdps_map_cell_event_triggered_flags[CH10_LATCH_SLOT], 1);
}

/* The incoming argument cannot reach anything: it is overwritten with 0 at
   00037a5c before the deployment and then used as the frame counter of both
   holds, so an argument of 7 and one of -1 deploy the same record and end the
   pan on the same pixel.  A handler that had kept the incoming value as its
   counter would hold the view for a different length of time for each of them,
   and -1 would run the first hold 13 times rather than 12. */
static void ch14_ignores_the_unit_index_argument(void)
{
    static int arguments[2] = {7, -1};
    int i;

    ch10_ensure_game_files();
    if (!ch10_files_ready) {
        return;
    }

    for (i = 0; i < 2; i++) {
        ch14_stage(CH14_SCHEDULED_TURN);
        ch14_run(arguments[i]);

        CHECK_EQ(data_fdps_map_unit_count, CH14_STAGED_UNITS + 1);
        CHECK_EQ((int) ch10_unit(CH14_ARRIVAL_UNIT_INDEX)->level,
                 CH14_WAVE4_LEVEL);
        CHECK_EQ(data_fdps_map_cursor_world_x,
                 CH14_MAP00_RECORD0_X * CH14_TILE_STEP);
        CHECK_EQ(data_fdps_map_cursor_world_y,
                 CH14_MAP00_RECORD0_Y * CH14_TILE_STEP);
    }
}

void run_chevt2_tests(void)
{
    RUN_TEST(ch08_sends_exactly_the_guest_mage);
    RUN_TEST(ch08_keeps_the_high_nibble);
    RUN_TEST(ch08_touches_no_neighbouring_byte);
    RUN_TEST(ch08_has_no_one_shot_latch);
    RUN_TEST(ch08_writes_without_a_unit_count_check);
    RUN_TEST(ch08_ignores_the_unit_index_argument);
    RUN_TEST(ch08_mode_is_walk_to_destination);
    RUN_TEST(ch13_record_shape_matches_the_offsets);
    RUN_TEST(ch13_advance_clears_exactly_the_range);
    RUN_TEST(ch13_advance_leaves_the_last_deployed_unit);
    RUN_TEST(ch13_advance_keeps_the_high_nibble);
    RUN_TEST(ch13_advance_has_no_one_shot_latch);
    RUN_TEST(ch13_advance_touches_no_neighbouring_byte);
    RUN_TEST(ch13_advance_ignores_the_unit_index_argument);
    RUN_TEST(ch10_side_is_the_byte_at_record_six);
    RUN_TEST(ch10_side_zero_does_not_fire_or_latch);
    RUN_TEST(ch10_reads_the_record_the_index_names);
    RUN_TEST(ch10_any_non_zero_latch_blocks);
    RUN_TEST(ch10_deploys_the_records_tagged_wave_10);
    RUN_TEST(ch10_map_number_comes_from_the_chapter_global);
    RUN_TEST(ch10_places_on_the_nearest_free_tile);
    RUN_TEST(ch10_side_test_is_unsigned);
    RUN_TEST(ch10_fires_once_only);
    RUN_TEST(ch08e_ignores_every_index_outside_the_four);
    RUN_TEST(ch08e_counts_the_escape_and_retires_the_villager);
    RUN_TEST(ch08e_all_four_villagers_are_in_range);
    RUN_TEST(ch08e_retire_is_a_whole_byte_assignment);
    RUN_TEST(ch08e_reward_needs_exactly_three_already_out);
    RUN_TEST(ch08e_reward_needs_more_than_one_escape);
    RUN_TEST(ch08e_reward_scales_with_the_escape_count);
    RUN_TEST(ch08e_reward_goes_to_the_guest_mage);
    RUN_TEST(ch08e_touches_no_neighbouring_flag_slot);
    RUN_TEST(ch08e_has_no_latch);
    RUN_TEST(ch08e_the_escape_count_is_an_unsigned_byte);
    RUN_TEST(ch08e_only_bit_zero_counts_as_out);
    RUN_TEST(ch09_deploys_the_records_tagged_wave_1);
    RUN_TEST(ch09_wave_is_a_literal_and_not_the_turn_counter);
    RUN_TEST(ch09_map_number_comes_from_the_chapter_global);
    RUN_TEST(ch09_places_on_the_nearest_free_tile);
    RUN_TEST(ch09_has_no_one_shot_latch);
    RUN_TEST(ch09_fires_again_on_every_call);
    RUN_TEST(ch09_a_wave_no_record_carries_deploys_nothing);
    RUN_TEST(ch09_ignores_the_unit_index_argument);
    RUN_TEST(ch10t_record_shape_matches_the_offsets);
    RUN_TEST(ch10t_announced_turn_deploys_wave_one_and_stops);
    RUN_TEST(ch10t_door_turn_asks_for_the_turn_less_four);
    RUN_TEST(ch10t_door_turn_pans_to_the_right_hand_door);
    RUN_TEST(ch10t_door_arm_has_no_lower_bound);
    RUN_TEST(ch10t_turn_past_the_bound_takes_the_last_wave);
    RUN_TEST(ch10t_map_number_comes_from_the_chapter_global);
    RUN_TEST(ch10t_has_no_latch_and_fires_every_call);
    RUN_TEST(ch10t_ignores_the_event_argument);
    RUN_TEST(ch11t_turn_four_deploys_wave_five_and_stops);
    RUN_TEST(ch11t_turn_seven_deploys_wave_four);
    RUN_TEST(ch11t_fall_through_has_no_turn_bound);
    RUN_TEST(ch11t_pan_ends_on_the_top_right_group);
    RUN_TEST(ch11t_map_number_comes_from_the_chapter_global);
    RUN_TEST(ch11t_places_on_the_nearest_free_tile);
    RUN_TEST(ch11t_has_no_latch_and_fires_every_call);
    RUN_TEST(ch11t_ignores_the_event_argument);
    RUN_TEST(ch14_deploys_the_records_tagged_wave_four);
    RUN_TEST(ch14_wave_is_a_literal_and_not_the_turn_counter);
    RUN_TEST(ch14_map_number_comes_from_the_chapter_global);
    RUN_TEST(ch14_places_on_the_nearest_free_tile);
    RUN_TEST(ch14_pan_ends_on_the_right_hand_group);
    RUN_TEST(ch14_pans_after_the_deployment);
    RUN_TEST(ch14_holds_both_pans_for_twelve_frames);
    RUN_TEST(ch14_has_no_latch_and_fires_every_call);
    RUN_TEST(ch14_ignores_the_unit_index_argument);
}
