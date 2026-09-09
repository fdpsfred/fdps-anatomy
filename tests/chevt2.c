/* tests/chevt2.c -- cover for src/chevt2.c.
 *
 * Every case stages a unit array of its own rather than reading one from a
 * game file, because each handler takes its whole effect through
 * data_fdps_map_unit_array_ptr -- pointing that global at a block the case
 * owns is the only way to see the stores.  What the global itself holds is
 * ticket 23's and is not asserted, so every case writes the state it wants
 * to see changed.
 *
 * The two death-triggered sections come first: the guard-death handler's,
 * then the villager-escape handler's, which stages bag entries and the
 * chapter's escape counter as well.  Both add a text-block fixture for the
 * draw on the end of the handler, and their own notes say what that draw can
 * and cannot be asserted about.
 *
 * The last section covers chapter 8's turn handler.  Two of its five arms
 * deploy a wave, so it needs a whole map under it and that is what the
 * fixture between the includes and the first section is for; two more play a
 * cut-scene, so it is also the only section that stages a container, a small
 * IconAni.vfs holding the two members those arms name, removed again in its
 * last case.  Its own note says why the shipped container cannot be used and
 * why leaving the fixture behind would matter to tests/icon.c.
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

/* The map fixture the chapter 8 turn-handler section at the bottom of this
   file stages through: a blank walkable map, a deployment table, the four
   character tables the deployment path reads, and a unit array a deployment
   can realloc.  It is the chapter 10 ambush section's fixture in
   tests/chevt2b.c, copied rather than shared because a fixture is file-local
   to the unit that uses it, with the two helpers and the two constants only
   the ambush's own cases reach left out.

   The cases that let a deployment run read back where the unit landed:

     MAP00.COD  record 0 (18, 0)  record 1 (22, 12)  record 2 (8, 10)
     MAP01.COD  record 0 (9, 4)

   -- the same records tests/deploy.c's own wave cases expect, staged through
   tests/gamefile.lst.  A run without ICON.CEL and FIELD.VFS would not fail a
   check, it would hang in fdps_wait_any_key, so every firing case skips
   itself when they are not there.

   The unit array is malloc'd rather than staged into a static block, because
   a deployment reallocs it: a static block handed to realloc is not a smaller
   fixture, it is undefined behaviour.  It is never freed, for the same reason
   tests/deploy.c does not free it -- the block moves under the global on every
   deployment. */

/* The slot of data_fdps_map_cell_event_triggered_flags the one-shot handlers
   latch -- element 0x10, the first the map's own event codes cannot reach,
   shared by every one-shot chapter handler in the game.  Same slot the chapter
   13 case in tests/chevt2b.c puts up to prove that handler ignores it. */
#define CH10_LATCH_SLOT 0x10

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

/* The terrain code the deployment search accepts, below the CMP EAX,0x5 / JGE
   at 00023404 that rejects the rest. */
#define CH10_TERRAIN_WALKABLE 1

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

/* Chapter 8's guard-death handler at 00037440, from here down.
 *
 * It is the chapter 13 handler's shape (tests/chevt2b.c) with the range
 * 0x13..0x13 in place of 9..0x2c and a behaviour code of 4 in place of 0, plus
 * one draw on the end, so it is covered the same way: both bounds and the mode
 * are literals in its
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
   later cases stage from where they expect. */
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

/* Chapter 8's turn-scheduled handler at 000372d0, from here down.
 *
 * It is five equality tests on data_fdps_battle_turn_counter with no default
 * arm, so what the cases pin is which arm a turn lands in, what that arm asks
 * for, and that a turn none of the five names does nothing at all.
 *
 * The map fixture is the one between the includes and the first section, the
 * chapter 10 ambush section's own in tests/chevt2b.c, with three things added
 * on top of it.
 *
 * A TEXT BLOCK COVERING ALL THREE IDS THE HANDLER NAMES, 0x0c, 0x15 and 0x22,
 * with every entry holding the same offset onto the lone -1 that follows the
 * table, so whichever entry a draw resolves the stream ends before a glyph is
 * drawn and before any of the modal codes fdps_draw_text would stand a
 * keyboard wait on.  WHICH id each draw asks for is not asserted, for the
 * reason the two sections above give: fdps_draw_text takes its whole effect
 * through pixels at the VGA aperture, keeps no state and returns a cursor this
 * handler discards.  What the cases do pin about the draws is that none of
 * them stops what follows it.
 *
 * A CURSOR DRAW MODE SENTINEL.  fdps_icon_script_run ends by writing 1 into
 * data_fdps_map_cursor_draw_mode (src/icon.c), so a value no arm can leave
 * there tells a run that played a script from one that did not.  It is the
 * only witness the two script arms have that is independent of what the
 * scripts themselves do.
 *
 * A FIXTURE IconAni.vfs HOLDING THE TWO MEMBERS THE HANDLER NAMES.  The
 * interpreter holds the container name as a literal, so the only way to put a
 * chosen script in front of it is to have a container of that name hold the
 * member -- the same position tests/icon.c is in, and the fixture is built to
 * the layout in resource_info/vfs.md exactly as that file builds its own.  The
 * shipped container cannot be used: its Icon7-1.dat and Icon7-2.dat open on
 * opcodes that need a loaded chapter behind them.  Each fixture member is one
 * SET_VIEW_TILE opcode and then the end opcode, and the two members name
 * DIFFERENT tiles, which is what turns "the right script ran" into an
 * assertion: the view origin the run comes to rest on says which of the two
 * literals reached the loader, and swapping the two names in the emitted C
 * swaps the two origins.
 *
 * The staging REFUSES TO OVERWRITE a file of that name that is already there,
 * and the last case removes the fixture again.  Both matter beyond this file:
 * tests/icon.c stages a container of the same name for its own cases and skips
 * them all if one is already present, and tests run in file order, so a
 * fixture left behind here would silently take that whole section out.
 *
 * Every case that reaches a deployment needs ICON.CEL and FIELD.VFS, which
 * cannot be stood in for, and skips itself without them.  The coordinates
 * those cases expect are MAP00.COD's and MAP01.COD's own placement records.
 */

/* The five turns the ladder names, read off the compares at 000372e3,
   00037314, 00037330, 000373b0 and 000373f1. */
#define CH08T_OPENING_ORDERS_TURN 1
#define CH08T_GUEST_MAGE_TURN 3
#define CH08T_CELL_GUARDS_TURN 4
#define CH08T_FIRST_CAVALRY_TURN 10
#define CH08T_SECOND_CAVALRY_TURN 12

/* The two waves the last two arms ask for, PUSH 0x2 at 000373bc and PUSH 0x3
   at 000373fd. */
#define CH08T_FIRST_CAVALRY_WAVE 2
#define CH08T_SECOND_CAVALRY_WAVE 3

/* The one unit the turn-4 arm re-aims and the behaviour code it merges in,
   read off the bound constants at 0003734b and 00037352 and the mode constant
   at 00037359. */
#define CH08T_STANDING_GUARD_INDEX 0x0e
#define CH08T_CHASE_MODE 3

/* Enough units for both neighbours of the one that moves to be live records,
   and two past the last of them so an off-by-one has somewhere visible to
   land. */
#define CH08T_STAGED_UNITS 0x11

/* The unit-record stride, the IMUL 0x50 inside fdps_get_unit_record. */
#define CH08T_UNIT_STRIDE 0x50

/* The shared one-shot latch slot.  This handler does not use it, and that is
   what is asserted. */
#define CH08T_LATCH_SLOT 0x10

/* Enough entries for the highest id the handler names, 0x22, plus the
   terminator every entry points at. */
#define CH08T_TEXT_ENTRY_COUNT 0x23

/* The deployment records the fixture lays down, one per wave the cases ask
   about, at the table index that is also its MAP%02d.COD placement record.
   Records 0 and 1 are the two whose coordinates the ambush cases above already
   read back out of the real files. */
#define CH08T_WAVE2_RECORD 0
#define CH08T_WAVE3_RECORD 1
#define CH08T_WAVE1_RECORD 2
#define CH08T_WAVE4_RECORD 3
#define CH08T_SPAWN_RECORD_COUNT 4

/* The level each record carries, which is how the cases tell which one
   arrived. */
#define CH08T_WAVE2_LEVEL 22
#define CH08T_WAVE3_LEVEL 33
#define CH08T_WAVE1_LEVEL 11
#define CH08T_WAVE4_LEVEL 44

/* The character id every deployment record carries.  0x80 is
   PORTRAIT_ID_NO_MAP_SPRITE, which keeps the map compositor off the arrivals
   if anything ever draws them. */
#define CH08T_ARRIVAL_CHAR_ID 0x80

/* MAP00.COD's placement record 0 and record 1, and MAP01.COD's record 0 -- the
   same coordinates the ambush cases above read back out of the real files. */
#define CH08T_MAP00_RECORD0_X 18
#define CH08T_MAP00_RECORD0_Y 0
#define CH08T_MAP00_RECORD1_X 22
#define CH08T_MAP00_RECORD1_Y 12
#define CH08T_MAP01_RECORD0_X 9
#define CH08T_MAP01_RECORD0_Y 4

/* A cursor draw mode no arm of the handler can leave behind: the interpreter
   ends on 1 and clears to 0 on the way round its loop, so finding this value
   afterwards says no script ran. */
#define CH08T_NO_SCRIPT_DRAW_MODE 4

/* The AI byte every staged record starts on: behaviour code 2, the mode
   map07.dat deploys the two cell guards in, under a high nibble the merge has
   to carry across untouched. */
#define CH08T_STAGED_AI_BEHAVIOR 0x52

/* The container the interpreter holds as a literal, and the two members this
   handler names.  Member names are stored upper-cased, which is what the
   lookup compares against after it has upper-cased the caller's string where
   it stands. */
#define CH08T_ARCHIVE_FILE "IconAni.vfs"
#define CH08T_FIXTURE_MEMBERS 2

/* resource_info/vfs.md: 35-byte header, then one 26-byte entry per member,
   then the member bytes end to end with no gaps. */
#define CH08T_HEADER_BYTES 35
#define CH08T_ENTRY_BYTES 26
#define CH08T_NAME_FIELD_BYTES 13
#define CH08T_SIGNATURE_BYTES 24

/* The tile each fixture script scrolls the view to, and the pixels per tile
   the opcode scales them by (MAP_TILE_PIXELS, src/icon.c).  The two pairs are
   deliberately unlike each other and unlike anything the staging leaves
   behind. */
#define CH08T_SCRIPT1_TILE_X 11
#define CH08T_SCRIPT1_TILE_Y 13
#define CH08T_SCRIPT2_TILE_X 17
#define CH08T_SCRIPT2_TILE_Y 19
#define CH08T_TILE_PIXELS 24

static unsigned char ch08t_script1[] = {
    0x0d, CH08T_SCRIPT1_TILE_X, CH08T_SCRIPT1_TILE_Y, 0x00
};
static unsigned char ch08t_script2[] = {
    0x0d, CH08T_SCRIPT2_TILE_X, CH08T_SCRIPT2_TILE_Y, 0x00
};

static char *ch08t_member_names[CH08T_FIXTURE_MEMBERS] = {
    "ICON7-1.DAT", "ICON7-2.DAT"
};

static unsigned char *ch08t_member_bytes[CH08T_FIXTURE_MEMBERS] = {
    ch08t_script1, ch08t_script2
};

static int ch08t_member_lengths[CH08T_FIXTURE_MEMBERS] = {
    sizeof(ch08t_script1), sizeof(ch08t_script2)
};

/* 0 not tried yet, 1 staged by us, 2 unusable and every script case skips. */
static int ch08t_archive_state = 0;

static short ch08t_text_block[CH08T_TEXT_ENTRY_COUNT + 1];

static void ch08t_write_word(FILE *fp, int value)
{
    unsigned char bytes[2];

    bytes[0] = (unsigned char) (value & 0xff);
    bytes[1] = (unsigned char) ((value >> 8) & 0xff);
    fwrite(bytes, 1, 2, fp);
}

static void ch08t_write_dword(FILE *fp, long value)
{
    unsigned char bytes[4];

    bytes[0] = (unsigned char) (value & 0xff);
    bytes[1] = (unsigned char) ((value >> 8) & 0xff);
    bytes[2] = (unsigned char) ((value >> 16) & 0xff);
    bytes[3] = (unsigned char) ((value >> 24) & 0xff);
    fwrite(bytes, 1, 4, fp);
}

/* The 13-byte name field: the name, a terminator, and zeroes to the end. */
static void ch08t_write_name(FILE *fp, char *name)
{
    unsigned char field[CH08T_NAME_FIELD_BYTES];
    int i;

    for (i = 0; i < CH08T_NAME_FIELD_BYTES; i++) {
        field[i] = 0;
    }
    for (i = 0; i < CH08T_NAME_FIELD_BYTES - 1 && name[i] != '\0'; i++) {
        field[i] = (unsigned char) name[i];
    }
    fwrite(field, 1, CH08T_NAME_FIELD_BYTES, fp);
}

/* Builds the fixture container once and answers whether the script cases may
   run.  A file of that name that was already there is left alone and the
   answer is no: it would be the shipped 4.7 MB container, and overwriting it
   would cost the next run a copy of it. */
static int ch08t_stage_archive(void)
{
    FILE *fp;
    long member_at;
    int i;

    if (ch08t_archive_state != 0) {
        return ch08t_archive_state == 1;
    }

    fp = fopen(CH08T_ARCHIVE_FILE, "rb");
    if (fp != NULL) {
        fclose(fp);
        ch08t_archive_state = 2;
        return 0;
    }

    fp = fopen(CH08T_ARCHIVE_FILE, "wb");
    if (fp == NULL) {
        ch08t_archive_state = 2;
        return 0;
    }

    fwrite("VFS", 1, 3, fp);
    ch08t_write_word(fp, 1);
    ch08t_write_word(fp, CH08T_HEADER_BYTES);
    ch08t_write_dword(fp, (long) CH08T_FIXTURE_MEMBERS);
    fwrite("Dynasty Information Co.,", 1, CH08T_SIGNATURE_BYTES, fp);

    member_at = (long) CH08T_HEADER_BYTES
                + (long) CH08T_FIXTURE_MEMBERS * CH08T_ENTRY_BYTES;
    for (i = 0; i < CH08T_FIXTURE_MEMBERS; i++) {
        ch08t_write_name(fp, ch08t_member_names[i]);
        ch08t_write_dword(fp, (long) ch08t_member_lengths[i]);
        ch08t_write_dword(fp, (long) ch08t_member_lengths[i]);
        fputc(0, fp);
        ch08t_write_dword(fp, member_at);
        member_at += ch08t_member_lengths[i];
    }
    for (i = 0; i < CH08T_FIXTURE_MEMBERS; i++) {
        fwrite(ch08t_member_bytes[i], 1, ch08t_member_lengths[i], fp);
    }
    fclose(fp);

    ch08t_archive_state = 1;
    return 1;
}

/* The ambush section's map fixture with seventeen units on it, every one of
   them carrying the same AI byte, one deployment record per wave the cases ask
   about, the text block, the view globals at the origin and the draw-mode
   sentinel down. */
static void ch08t_stage(int battle_turn)
{
    int i;

    ch10_stage(CH08T_STAGED_UNITS);

    for (i = 0; i < CH08T_STAGED_UNITS; i++) {
        ch10_unit(i)->ai_behavior = (unsigned char) CH08T_STAGED_AI_BEHAVIOR;
        ch10_unit(i)->portrait_id = (unsigned char) CH08T_ARRIVAL_CHAR_ID;
    }

    ch10_spawn_table[CH10_SPAWN_TABLE_COUNT_OFFSET] =
        (unsigned char) CH08T_SPAWN_RECORD_COUNT;
    ch10_set_spawn(CH08T_WAVE2_RECORD, CH08T_ARRIVAL_CHAR_ID,
                   CH08T_FIRST_CAVALRY_WAVE);
    ch10_set_spawn(CH08T_WAVE3_RECORD, CH08T_ARRIVAL_CHAR_ID,
                   CH08T_SECOND_CAVALRY_WAVE);
    ch10_set_spawn(CH08T_WAVE1_RECORD, CH08T_ARRIVAL_CHAR_ID, 1);
    ch10_set_spawn(CH08T_WAVE4_RECORD, CH08T_ARRIVAL_CHAR_ID, 4);
    ch10_spawn_at(CH08T_WAVE2_RECORD)->level = (unsigned char) CH08T_WAVE2_LEVEL;
    ch10_spawn_at(CH08T_WAVE3_RECORD)->level = (unsigned char) CH08T_WAVE3_LEVEL;
    ch10_spawn_at(CH08T_WAVE1_RECORD)->level = (unsigned char) CH08T_WAVE1_LEVEL;
    ch10_spawn_at(CH08T_WAVE4_RECORD)->level = (unsigned char) CH08T_WAVE4_LEVEL;

    for (i = 0; i < CH08T_TEXT_ENTRY_COUNT; i++) {
        ch08t_text_block[i] = (short) (CH08T_TEXT_ENTRY_COUNT * 2);
    }
    ch08t_text_block[CH08T_TEXT_ENTRY_COUNT] = -1;
    data_fdps_current_chapter_text_ptr = (unsigned char *) ch08t_text_block;

    data_fdps_scene_layer_count = 0;
    data_fdps_ui_terrain_hud_user_enabled = 0;
    data_fdps_ui_play_active_flag = 0;
    data_fdps_battle_view_window_origin_x = 0;
    data_fdps_battle_view_window_origin_y = 0;
    data_fdps_map_cursor_world_x = 0;
    data_fdps_map_cursor_world_y = 0;
    data_fdps_map_cursor_draw_mode = CH08T_NO_SCRIPT_DRAW_MODE;

    data_fdps_battle_turn_counter = battle_turn;
}

/* The two fields the cases read back and the stride they are indexed by.  The
   behaviour byte is the one the merge rewrites and the level byte is what
   tells one arrival from another, so both offsets have to be the ones the
   emitted code and fdps_deploy_unit address. */
static void ch08t_record_shape_matches_the_offsets(void)
{
    CHECK_EQ((int) sizeof(struct fdps_unit_record), CH08T_UNIT_STRIDE);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, ai_behavior), 0x34);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, level), 0x21);
}

/* Turn 1 speaks and does nothing else: no script is played, no unit arrives
   and no behaviour byte moves.  The first arm is the one an arm order that put
   a deployment or the re-aim in front of the ladder would show up in. */
static void ch08t_turn_one_only_speaks(void)
{
    int i;

    ch08t_stage(CH08T_OPENING_ORDERS_TURN);
    fdps_chapter_08_event_for_turn(0);

    CHECK_EQ(data_fdps_map_unit_count, CH08T_STAGED_UNITS);
    CHECK_EQ(data_fdps_map_cursor_draw_mode, CH08T_NO_SCRIPT_DRAW_MODE);
    CHECK_EQ(data_fdps_battle_view_window_origin_x, 0);
    for (i = 0; i < CH08T_STAGED_UNITS; i++) {
        CHECK_EQ((int) ch10_unit(i)->ai_behavior, CH08T_STAGED_AI_BEHAVIOR);
    }
}

/* THE LADDER HAS NO DEFAULT ARM.  Six turns none of the five compares names --
   one below the first, three between the scheduled ones, one between the last
   two and one far above them -- each leave the unit array, the behaviour bytes
   and the draw-mode sentinel exactly as the staging left them.  Every arm of
   the handler writes at least one of those three, so a default arm of any kind
   would show here. */
static void ch08t_an_unnamed_turn_does_nothing(void)
{
    static int turns[6] = {0, 2, 5, 9, 11, 100};
    int i;
    int unit;

    for (i = 0; i < 6; i++) {
        ch08t_stage(turns[i]);
        fdps_chapter_08_event_for_turn(0);

        CHECK_EQ(data_fdps_map_unit_count, CH08T_STAGED_UNITS);
        CHECK_EQ(data_fdps_map_cursor_draw_mode, CH08T_NO_SCRIPT_DRAW_MODE);
        for (unit = 0; unit < CH08T_STAGED_UNITS; unit++) {
            CHECK_EQ((int) ch10_unit(unit)->ai_behavior,
                     CH08T_STAGED_AI_BEHAVIOR);
        }
    }
}

/* Turn 3 plays Icon7-1.dat and stops there.  The fixture member of that name
   scrolls the view to tile (11, 13) and the one named by the other arm scrolls
   it to (17, 19), so the origin the run comes to rest on is what says which of
   the two literals reached the loader -- this is the case that fails if the
   two names are swapped in the emitted C.  Nothing else happens: no unit
   arrives and no behaviour byte moves, so the re-aim belongs to the turn-4 arm
   and not to this one. */
static void ch08t_turn_three_plays_the_first_script(void)
{
    int i;

    if (!ch08t_stage_archive()) {
        return;
    }

    ch08t_stage(CH08T_GUEST_MAGE_TURN);
    fdps_chapter_08_event_for_turn(0);

    CHECK_EQ(data_fdps_battle_view_window_origin_x,
             CH08T_SCRIPT1_TILE_X * CH08T_TILE_PIXELS);
    CHECK_EQ(data_fdps_battle_view_window_origin_y,
             CH08T_SCRIPT1_TILE_Y * CH08T_TILE_PIXELS);
    CHECK_EQ(data_fdps_map_cursor_draw_mode, 1);
    CHECK_EQ(data_fdps_map_unit_count, CH08T_STAGED_UNITS);
    for (i = 0; i < CH08T_STAGED_UNITS; i++) {
        CHECK_EQ((int) ch10_unit(i)->ai_behavior, CH08T_STAGED_AI_BEHAVIOR);
    }
}

/* Turn 4 plays Icon7-2.dat -- the other member, so the other origin -- and
   then re-aims the guard left standing.  Both halves are asserted in one case
   because the order matters: the re-aim follows the script in the instruction
   stream, and a rebuild that ran them the other way round would still leave
   these values behind.  What pins the order is the case below. */
static void ch08t_turn_four_plays_the_second_script_and_re_aims(void)
{
    if (!ch08t_stage_archive()) {
        return;
    }

    ch08t_stage(CH08T_CELL_GUARDS_TURN);
    fdps_chapter_08_event_for_turn(0);

    CHECK_EQ(data_fdps_battle_view_window_origin_x,
             CH08T_SCRIPT2_TILE_X * CH08T_TILE_PIXELS);
    CHECK_EQ(data_fdps_battle_view_window_origin_y,
             CH08T_SCRIPT2_TILE_Y * CH08T_TILE_PIXELS);
    CHECK_EQ(data_fdps_map_cursor_draw_mode, 1);
    CHECK_EQ((int) ch10_unit(CH08T_STANDING_GUARD_INDEX)->ai_behavior,
             (CH08T_STAGED_AI_BEHAVIOR & 0xf0) | CH08T_CHASE_MODE);
}

/* Exactly one record moves and every other one is left as it was.  The two
   neighbours are the witnesses that matter: 0x0d is the guard Icon7-2.dat
   walks away and retires, and 0x0f is the first record past the pair, so both
   ends of the one-element range have something to land on if either bound were
   read as exclusive or as a count. */
static void ch08t_turn_four_re_aims_exactly_one_unit(void)
{
    int i;

    if (!ch08t_stage_archive()) {
        return;
    }

    ch08t_stage(CH08T_CELL_GUARDS_TURN);
    fdps_chapter_08_event_for_turn(0);

    for (i = 0; i < CH08T_STAGED_UNITS; i++) {
        if (i == CH08T_STANDING_GUARD_INDEX) {
            CHECK_EQ((int) ch10_unit(i)->ai_behavior,
                     (CH08T_STAGED_AI_BEHAVIOR & 0xf0) | CH08T_CHASE_MODE);
        } else {
            CHECK_EQ((int) ch10_unit(i)->ai_behavior,
                     CH08T_STAGED_AI_BEHAVIOR);
        }
    }
}

/* The merge keeps the high nibble.  Three staged bytes carry the two flag bits
   in different combinations and each comes out with its high nibble intact and
   its low nibble 3; a whole-byte assignment would leave 3 in all three cases
   and clear the bits fdps_map_actor_take_best_action and
   fdps_score_targets_for_item read on their own. */
static void ch08t_turn_four_keeps_the_high_nibble(void)
{
    static int staged[3] = {0x42, 0x82, 0xc2};
    int i;

    if (!ch08t_stage_archive()) {
        return;
    }

    for (i = 0; i < 3; i++) {
        ch08t_stage(CH08T_CELL_GUARDS_TURN);
        ch10_unit(CH08T_STANDING_GUARD_INDEX)->ai_behavior =
            (unsigned char) staged[i];
        fdps_chapter_08_event_for_turn(0);

        CHECK_EQ((int) ch10_unit(CH08T_STANDING_GUARD_INDEX)->ai_behavior,
                 (staged[i] & 0xf0) | CH08T_CHASE_MODE);
    }
}

/* The re-aim writes the byte next to the one it merges and nothing else.  The
   record either side of ai_behavior is read back so that a merge addressing
   0x33 or 0x35 instead would be caught even though the value it wrote there
   would look like a plausible behaviour byte. */
static void ch08t_turn_four_touches_no_neighbouring_byte(void)
{
    unsigned char *record;

    if (!ch08t_stage_archive()) {
        return;
    }

    ch08t_stage(CH08T_CELL_GUARDS_TURN);
    record = (unsigned char *) ch10_unit(CH08T_STANDING_GUARD_INDEX);
    record[0x33] = 0x5a;
    record[0x35] = 0xa5;
    fdps_chapter_08_event_for_turn(0);

    CHECK_EQ((int) record[0x33], 0x5a);
    CHECK_EQ((int) record[0x35], 0xa5);
    CHECK_EQ((int) record[0x34],
             (CH08T_STAGED_AI_BEHAVIOR & 0xf0) | CH08T_CHASE_MODE);
}

/* NOTHING GUARDS THE TURN-4 WRITE.  There is no compare against
   data_fdps_map_unit_count in front of the walk, so a live count of zero --
   which is what a battle whose enemies are all dead comes to -- still has the
   handler write index 0x0e.  The unit array is left where the staging put it
   so the write has somewhere to land; the original writes past the end of a
   shorter allocation, which is the behaviour this case stands in for. */
static void ch08t_turn_four_writes_without_a_unit_count_check(void)
{
    if (!ch08t_stage_archive()) {
        return;
    }

    ch08t_stage(CH08T_CELL_GUARDS_TURN);
    data_fdps_map_unit_count = 0;
    fdps_chapter_08_event_for_turn(0);

    CHECK_EQ((int) ch10_unit(CH08T_STANDING_GUARD_INDEX)->ai_behavior,
             (CH08T_STAGED_AI_BEHAVIOR & 0xf0) | CH08T_CHASE_MODE);
}

/* Turn 10 brings on wave 2 and nothing else.  Four records tagged 2, 3, 1 and
   4 leave exactly the first deployed, carrying its own level and MAP00.COD
   record 0's coordinates; the wave-1 and wave-3 records sit either side of it
   in the same table, so a handler that passed the turn counter, or the turn
   less something, lands on a different record or on none.  No behaviour byte
   moves, which is what keeps the re-aim on the turn-4 arm. */
static void ch08t_turn_ten_deploys_wave_two(void)
{
    ch10_ensure_game_files();
    if (!ch10_files_ready) {
        return;
    }

    ch08t_stage(CH08T_FIRST_CAVALRY_TURN);
    fdps_chapter_08_event_for_turn(0);

    CHECK_EQ(data_fdps_map_unit_count, CH08T_STAGED_UNITS + 1);
    CHECK_EQ((int) ch10_unit(CH08T_STAGED_UNITS)->level, CH08T_WAVE2_LEVEL);
    CHECK_EQ((int) ch10_unit(CH08T_STAGED_UNITS)->pos_x,
             CH08T_MAP00_RECORD0_X);
    CHECK_EQ((int) ch10_unit(CH08T_STAGED_UNITS)->pos_y,
             CH08T_MAP00_RECORD0_Y);
    CHECK_EQ((int) ch10_unit(CH08T_STANDING_GUARD_INDEX)->ai_behavior,
             CH08T_STAGED_AI_BEHAVIOR);
    CHECK_EQ(data_fdps_map_cursor_draw_mode, CH08T_NO_SCRIPT_DRAW_MODE);
}

/* Turn 12 brings on wave 3, the next record along, landing on MAP00.COD record
   1's coordinates rather than record 0's.  Together with the case above this
   is what pins the pair of wave numbers to the pair of turns: reading either
   compare as the other's turn swaps which record arrives. */
static void ch08t_turn_twelve_deploys_wave_three(void)
{
    ch10_ensure_game_files();
    if (!ch10_files_ready) {
        return;
    }

    ch08t_stage(CH08T_SECOND_CAVALRY_TURN);
    fdps_chapter_08_event_for_turn(0);

    CHECK_EQ(data_fdps_map_unit_count, CH08T_STAGED_UNITS + 1);
    CHECK_EQ((int) ch10_unit(CH08T_STAGED_UNITS)->level, CH08T_WAVE3_LEVEL);
    CHECK_EQ((int) ch10_unit(CH08T_STAGED_UNITS)->pos_x,
             CH08T_MAP00_RECORD1_X);
    CHECK_EQ((int) ch10_unit(CH08T_STAGED_UNITS)->pos_y,
             CH08T_MAP00_RECORD1_Y);
    CHECK_EQ((int) ch10_unit(CH08T_STANDING_GUARD_INDEX)->ai_behavior,
             CH08T_STAGED_AI_BEHAVIOR);
}

/* The map number both deploying arms place under is
   data_fdps_chapter_current_chapter_id and not a literal: the same wave under
   chapter 0 and under chapter 1 puts the arrival on MAP00.COD's record 0 and
   on MAP01.COD's, which are different tiles. */
static void ch08t_map_number_comes_from_the_chapter_global(void)
{
    ch10_ensure_game_files();
    if (!ch10_files_ready) {
        return;
    }

    ch08t_stage(CH08T_FIRST_CAVALRY_TURN);
    data_fdps_chapter_current_chapter_id = 0;
    fdps_chapter_08_event_for_turn(0);
    CHECK_EQ((int) ch10_unit(CH08T_STAGED_UNITS)->pos_x,
             CH08T_MAP00_RECORD0_X);
    CHECK_EQ((int) ch10_unit(CH08T_STAGED_UNITS)->pos_y,
             CH08T_MAP00_RECORD0_Y);

    ch08t_stage(CH08T_FIRST_CAVALRY_TURN);
    data_fdps_chapter_current_chapter_id = 1;
    fdps_chapter_08_event_for_turn(0);
    CHECK_EQ((int) ch10_unit(CH08T_STAGED_UNITS)->pos_x,
             CH08T_MAP01_RECORD0_X);
    CHECK_EQ((int) ch10_unit(CH08T_STAGED_UNITS)->pos_y,
             CH08T_MAP01_RECORD0_Y);
}

/* There is no guard of any kind: the family's shared one-shot slot raised does
   not block the handler, and a second call on the same turn deploys a second
   time rather than being refused.  The latch is raised rather than assumed
   clear because its starting value is ticket 23's. */
static void ch08t_has_no_latch_and_fires_every_call(void)
{
    ch10_ensure_game_files();
    if (!ch10_files_ready) {
        return;
    }

    ch08t_stage(CH08T_FIRST_CAVALRY_TURN);
    data_fdps_map_cell_event_triggered_flags[CH08T_LATCH_SLOT] = 1;

    fdps_chapter_08_event_for_turn(0);
    CHECK_EQ(data_fdps_map_unit_count, CH08T_STAGED_UNITS + 1);
    CHECK_EQ((int) ch10_unit(CH08T_STAGED_UNITS)->level, CH08T_WAVE2_LEVEL);

    fdps_chapter_08_event_for_turn(0);
    CHECK_EQ(data_fdps_map_unit_count, CH08T_STAGED_UNITS + 2);
    CHECK_EQ((int) ch10_unit(CH08T_STAGED_UNITS + 1)->level,
             CH08T_WAVE2_LEVEL);
    CHECK_EQ(data_fdps_map_cell_event_triggered_flags[CH08T_LATCH_SLOT], 1);
}

/* The incoming argument cannot reach anything.  It is overwritten with 0 at
   000372dc before the first turn compare and never read back, so an argument
   of 7 and one of -1 deploy the same record onto the same tile.  A handler
   that had let the value through would have to use it somewhere, and the only
   thing in the body an index could feed is the record the re-aim fetches. */
static void ch08t_ignores_the_event_argument(void)
{
    static int arguments[2] = {7, -1};
    int i;

    ch10_ensure_game_files();
    if (!ch10_files_ready) {
        return;
    }

    for (i = 0; i < 2; i++) {
        ch08t_stage(CH08T_FIRST_CAVALRY_TURN);
        fdps_chapter_08_event_for_turn(arguments[i]);

        CHECK_EQ(data_fdps_map_unit_count, CH08T_STAGED_UNITS + 1);
        CHECK_EQ((int) ch10_unit(CH08T_STAGED_UNITS)->level,
                 CH08T_WAVE2_LEVEL);
        CHECK_EQ((int) ch10_unit(CH08T_STAGED_UNITS)->pos_x,
                 CH08T_MAP00_RECORD0_X);
    }
}

/* Removes the fixture container again.  It is a test rather than a teardown
   hook because the harness has no hook, and it asserts the removal so that a
   fixture left behind is reported instead of silently taking out the cover for
   the interpreter in tests/icon.c, which stages a container of the same name
   and skips every one of its script cases if one is already there. */
static void ch08t_the_fixture_container_is_cleaned_up(void)
{
    FILE *fp;

    if (ch08t_archive_state != 1) {
        return;
    }
    remove(CH08T_ARCHIVE_FILE);
    ch08t_archive_state = 2;

    fp = fopen(CH08T_ARCHIVE_FILE, "rb");
    if (fp != NULL) {
        fclose(fp);
    }
    CHECK_EQ(fp == NULL, 1);
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
    RUN_TEST(ch08t_record_shape_matches_the_offsets);
    RUN_TEST(ch08t_turn_one_only_speaks);
    RUN_TEST(ch08t_an_unnamed_turn_does_nothing);
    RUN_TEST(ch08t_turn_three_plays_the_first_script);
    RUN_TEST(ch08t_turn_four_plays_the_second_script_and_re_aims);
    RUN_TEST(ch08t_turn_four_re_aims_exactly_one_unit);
    RUN_TEST(ch08t_turn_four_keeps_the_high_nibble);
    RUN_TEST(ch08t_turn_four_touches_no_neighbouring_byte);
    RUN_TEST(ch08t_turn_four_writes_without_a_unit_count_check);
    RUN_TEST(ch08t_turn_ten_deploys_wave_two);
    RUN_TEST(ch08t_turn_twelve_deploys_wave_three);
    RUN_TEST(ch08t_map_number_comes_from_the_chapter_global);
    RUN_TEST(ch08t_has_no_latch_and_fires_every_call);
    RUN_TEST(ch08t_ignores_the_event_argument);
    RUN_TEST(ch08t_the_fixture_container_is_cleaned_up);
}
