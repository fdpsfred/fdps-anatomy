/* tests/chevt5.c -- cover for src/chevt5.c.
 *
 * Each handler in this file has its own section with its own banner and its own
 * fixture; the chapter 25 section further down builds on the chapter 24 stage
 * below rather than repeating it.
 *
 * Chapter 24's turn handler at 00038cc0 branches on one global and takes its
 * whole visible effect through fdps_deploy_wave, so the cases here stage a
 * battle map, put the turn counter on the turn they are about, and read the
 * unit array back.
 *
 * The deployment is real.  fdps_deploy_wave opens ICON.CEL and FIELD.VFS for
 * itself, staged through tests/gamefile.lst, and the coordinates read back are
 * the placement records inside this file's own MAP00.COD and MAP01.COD:
 *
 *   MAP00.COD  record 0 (18, 0)  record 1 (22, 12)
 *   MAP01.COD  record 0 (9, 4)
 *
 * -- the same records tests/deploy.c and tests/chevt4.c expect.  A run without
 * those two files would not fail a check, it would hang in fdps_wait_any_key,
 * so every case skips itself when they are not there.
 *
 * The line each turn speaks is drawn for real as well: the chapter text
 * pointer is aimed at a block whose every entry names one lone terminator, so
 * fdps_draw_text walks the entry, paints nothing and returns without needing a
 * font, a message panel or mode 13h.  The block runs to 0x15 entries because
 * PUSH 0x14 at 00038e2d asks for the last of them.  A draw that paints nothing
 * leaves nothing behind, which is why no case asserts a text id and why the
 * order of the draw against the deployment -- the one thing about this handler
 * a player would notice most -- is not assertable from here at all.  It is
 * pinned in src/chevt5.c against the instruction addresses instead.
 *
 * What the cases are really for is the pairing of turn to wave.  The five
 * arrival turns are 5, 7, 10, 13 and 15 and the waves they ask for are 2, 6, 4,
 * 3 and 5, so no offset from the turn number produces them and no ordering of
 * the turns produces them either: each pair is asserted on its own, each
 * against a record tagged one wave either side so a rebuild that drifted by one
 * would deploy the wrong group, and each against a record tagged with the turn
 * itself so a rebuild that passed the turn counter through -- which is what the
 * chapter 23 handler next door does, biased by four -- would deploy nothing.
 *
 * The expected wave numbers are read off the instruction stream: PUSH 0x2 at
 * 00038d02, PUSH 0x6 at 00038d54, PUSH 0x4 at 00038d98, PUSH 0x3 at 00038dff
 * and PUSH 0x5 at 00038e40.  The six turns are the six CMP dword ptr
 * [0x00069ce8] immediates, and they agree with MAP23.DAT's own turn-event
 * table, whose six live records are (turn 5, 8, 7, 10, 13, 15) and all six name
 * handler slot 36 -- the slot at 00060254 this function sits in.
 *
 * The unit array is malloc'd rather than staged into a static block, because a
 * deployment reallocs it: a static block handed to realloc is not a smaller
 * fixture, it is undefined behaviour.  It is never freed, for the same reason
 * tests/deploy.c does not free it -- the block moves under the global on every
 * deployment.
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
#include "mapdraw.h"
#include "chevt5.h"

/* The six turns the handler names, in the order it tests them. */
#define CH24T_WAVE_2_TURN 5
#define CH24T_LINE_ONLY_TURN 8
#define CH24T_SHAN_TURN 7
#define CH24T_WAVE_4_TURN 10
#define CH24T_WAVE_3_TURN 13
#define CH24T_WAVE_5_TURN 15

/* The wave each of the five deploying turns asks for. */
#define CH24T_TURN_5_WAVE 2
#define CH24T_TURN_7_WAVE 6
#define CH24T_TURN_10_WAVE 4
#define CH24T_TURN_13_WAVE 3
#define CH24T_TURN_15_WAVE 5

#define CH24T_ARRIVAL_TURNS 5

/* Turns the handler does nothing at all on: every turn from 1 to 16 the six
   tests do not name, plus one well past the last of them, which is the end a
   rebuild written as ">= 15" would get wrong. */
#define CH24T_QUIET_TURN_COUNT 11

/* The waves a quiet turn has to leave alone: every wave the handler can ask
   for on any turn, so a case that stages all of them fails whichever wrong
   wave a broken chain reached for. */
#define CH24T_LOWEST_WAVE 2
#define CH24T_HIGHEST_WAVE 6

/* The argument the dispatcher really pushes, PUSH 0x0 at 0002e13e, and a
   second value that is a valid unit index, so a case can say the body reads
   neither. */
#define CH24T_DISPATCHER_ARG 0
#define CH24T_OTHER_ARG 3

/* The character ids the decoy records carry, so a case can say which record
   was deployed: the record tagged with the wave the handler should ask for is
   the middle one and carries id 6. */
#define CH24T_TAGGED_CHAR_ID 6
#define CH24T_BELOW_CHAR_ID 5
#define CH24T_ABOVE_CHAR_ID 7

/* Where MAP00.COD puts deployment records 0 and 1, and where MAP01.COD puts
   its record 0. */
#define CH24T_MAP00_RECORD_1_X 22
#define CH24T_MAP00_RECORD_1_Y 12
#define CH24T_MAP00_RECORD_1_BLOCKED_Y 13
#define CH24T_MAP01_RECORD_0_X 9
#define CH24T_MAP01_RECORD_0_Y 4

/* A map big enough for MAP00.COD's records, which reach (22, 12).  The tile
   search fdps_deploy_unit runs on a place_exact of 0 walks the whole grid, and
   the marking pass writes at a unit's own tile with no bound, so the grid has
   to cover the coordinates the real placement file names. */
#define CH24T_GRID_W 32
#define CH24T_GRID_H 16

/* Where the deployment records start in the MAP%02d.DAT block and where its
   header keeps their count: ADD EAX,0x83 at 00023463 and MOV AL,byte ptr
   [EAX+0x2] at 000238bb. */
#define CH24T_SPAWN_TABLE_RECORD_BASE 0x83
#define CH24T_SPAWN_TABLE_COUNT_OFFSET 2
#define CH24T_SPAWN_TABLE_RECORDS 8

/* The scene-layer offsets fdps_map_load_tile_info reads through: the tile
   map's width word at +7 with its 16-bit ids from +0xb, the attribute table's
   4-byte rows from +0x11, and the event layer's width at +7 with its cells
   from +0x10 (src/maptile.c). */
#define CH24T_TILE_MAP_WIDTH_OFFSET 7
#define CH24T_TILE_MAP_IDS_OFFSET 0xb
#define CH24T_TILE_ATTR_ROWS_OFFSET 0x11
#define CH24T_EVENT_LAYER_WIDTH_OFFSET 7
#define CH24T_EVENT_LAYER_CELLS_OFFSET 0x10

/* A terrain code the deployment search accepts and one it rejects, either side
   of the CMP EAX,0x5 / JGE at 00023404. */
#define CH24T_TERRAIN_WALKABLE 1
#define CH24T_TERRAIN_BLOCKED 5

#define CH24T_TILE_ATTR_ROWS 16
#define CH24T_CHAR_TABLE_ROWS 8
#define CH24T_ENEMY_TABLE_ROWS 8

/* Every item id is a valid index into the staged item table, 0xff included:
   fdps_unit_recompute_combat_stats follows an equipped flag into
   fdps_get_item_record without a bounds check. */
#define CH24T_ITEM_TABLE_ROWS 256
#define CH24T_ITEM_ID_NONE 0xff

/* The unit array's stride, as the deployment path allocates it: PUSH 0x50 at
   000232cc, and the stride fdps_get_unit_record multiplies the index by. */
#define CH24T_UNIT_STRIDE 0x50

/* The chapter text block the six lines are spoken from: 0x15 entries, because
   PUSH 0x14 at 00038e2d asks for the last of them, each pointing at the same
   lone terminator so that a draw walks it, paints nothing and returns. */
#define CH24T_TEXT_IDS 0x15
#define CH24T_TEXT_EMPTY_AT 0x40
#define CH24T_TEXT_BLOCK_BYTES (CH24T_TEXT_EMPTY_AT + 2)
#define CH24T_TEXT_END (-1)

static unsigned char ch24t_grid[4 + CH24T_GRID_W * CH24T_GRID_H * 2];
static unsigned char ch24t_spawn_table[CH24T_SPAWN_TABLE_RECORD_BASE +
                                       CH24T_SPAWN_TABLE_RECORDS * 0x1a];
static unsigned char ch24t_tile_map[CH24T_TILE_MAP_IDS_OFFSET +
                                    CH24T_GRID_W * CH24T_GRID_H * 2];
static unsigned char ch24t_tile_attr[CH24T_TILE_ATTR_ROWS_OFFSET +
                                     CH24T_TILE_ATTR_ROWS * 4];
static unsigned char ch24t_event_layer[CH24T_EVENT_LAYER_CELLS_OFFSET +
                                       CH24T_GRID_W * CH24T_GRID_H];
static struct fdps_character_base_record ch24t_char_base[CH24T_CHAR_TABLE_ROWS];
static struct fdps_character_growth ch24t_growth[CH24T_CHAR_TABLE_ROWS];
static struct fdps_enemy_data ch24t_enemy[CH24T_ENEMY_TABLE_ROWS];
static struct fdps_item_effect ch24t_items[CH24T_ITEM_TABLE_ROWS];
static unsigned char ch24t_text_block[CH24T_TEXT_BLOCK_BYTES];

static int ch24t_files_checked = 0;
static int ch24t_files_ready = 0;

/* ICON.CEL is read by fdps_cache_cel_sprite_group, which takes a fixed
   0x2970-byte bite out of the sheet's offset table, so a shorter file is one
   that reader runs off the end of rather than a smaller fixture.  FIELD.VFS
   cannot be stood in for at all: a missing container sends fdps_deploy_wave
   into fdps_wait_any_key and a missing member into exit(1). */
static void ch24t_ensure_game_files(void)
{
    FILE *fp;
    long size;

    if (ch24t_files_checked) {
        return;
    }
    ch24t_files_checked = 1;

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
    ch24t_files_ready = 1;
}

static struct fdps_char_spawn_record *ch24t_spawn_at(int record_index)
{
    return (struct fdps_char_spawn_record *)
           (ch24t_spawn_table + CH24T_SPAWN_TABLE_RECORD_BASE) + record_index;
}

/* One deployment record with nothing equipped and nothing carried, tagged with
   the wave it belongs to. */
static void ch24t_set_spawn(int record_index, int char_id, int wave_no)
{
    ch24t_spawn_at(record_index)->char_id = (unsigned char) char_id;
    ch24t_spawn_at(record_index)->level = 1;
    ch24t_spawn_at(record_index)->side = 0;
    ch24t_spawn_at(record_index)->equipped_item_0 = CH24T_ITEM_ID_NONE;
    ch24t_spawn_at(record_index)->equipped_item_1 = CH24T_ITEM_ID_NONE;
    ch24t_spawn_at(record_index)->carried_items[0] = CH24T_ITEM_ID_NONE;
    ch24t_spawn_at(record_index)->carried_items[1] = CH24T_ITEM_ID_NONE;
    ch24t_spawn_at(record_index)->carried_items[2] = CH24T_ITEM_ID_NONE;
    ch24t_spawn_at(record_index)->carried_items[3] = CH24T_ITEM_ID_NONE;
    ch24t_spawn_at(record_index)->carried_items[4] = CH24T_ITEM_ID_NONE;
    ch24t_spawn_at(record_index)->carried_items[5] = CH24T_ITEM_ID_NONE;
    ch24t_spawn_at(record_index)->wave_no = (unsigned char) wave_no;
}

static void ch24t_set_terrain(int tile_id, int terrain)
{
    struct fdps_tile_attr_entry *rows;

    rows = (struct fdps_tile_attr_entry *)
           (ch24t_tile_attr + CH24T_TILE_ATTR_ROWS_OFFSET);
    rows[tile_id].terrain_type = (unsigned char) terrain;
}

/* The cell of the tile map at (tile_x, tile_y), so one tile can be given an id
   whose attribute row says the deployment search must not use it. */
static void ch24t_set_tile_id(int tile_x, int tile_y, int tile_id)
{
    short *tile_ids;

    tile_ids = (short *) (ch24t_tile_map + CH24T_TILE_MAP_IDS_OFFSET);
    tile_ids[tile_y * CH24T_GRID_W + tile_x] = (short) tile_id;
}

static struct fdps_unit_record *ch24t_unit(int unit_index)
{
    return (struct fdps_unit_record *)
           (data_fdps_map_unit_array_ptr + unit_index * CH24T_UNIT_STRIDE);
}

/* A blank walkable map with one unit already on it, the chapter global on map
   0, the text block in place and the turn counter on the turn the case is
   about.  The staged unit stands at (0, 0), clear of every placement record
   these cases read back. */
static void ch24t_stage(int turn)
{
    int i;

    memset(ch24t_grid, 0, sizeof(ch24t_grid));
    memset(ch24t_spawn_table, 0, sizeof(ch24t_spawn_table));
    memset(ch24t_tile_map, 0, sizeof(ch24t_tile_map));
    memset(ch24t_tile_attr, 0, sizeof(ch24t_tile_attr));
    memset(ch24t_event_layer, 0, sizeof(ch24t_event_layer));
    memset(ch24t_char_base, 0, sizeof(ch24t_char_base));
    memset(ch24t_growth, 0, sizeof(ch24t_growth));
    memset(ch24t_enemy, 0, sizeof(ch24t_enemy));
    memset(ch24t_items, 0, sizeof(ch24t_items));

    memset(ch24t_text_block, 0, (size_t) CH24T_TEXT_BLOCK_BYTES);
    *(short *) (ch24t_text_block + CH24T_TEXT_EMPTY_AT) = (short) CH24T_TEXT_END;
    for (i = 0; i < CH24T_TEXT_IDS; i++) {
        *(short *) (ch24t_text_block + i * 2) = (short) CH24T_TEXT_EMPTY_AT;
    }

    *(short *) ch24t_grid = (short) CH24T_GRID_W;
    *(short *) (ch24t_grid + 2) = (short) CH24T_GRID_H;

    *(short *) (ch24t_tile_map + CH24T_TILE_MAP_WIDTH_OFFSET) =
        (short) CH24T_GRID_W;
    for (i = 0; i < CH24T_TILE_ATTR_ROWS; i++) {
        ch24t_set_terrain(i, CH24T_TERRAIN_WALKABLE);
    }

    *(short *) (ch24t_event_layer + CH24T_EVENT_LAYER_WIDTH_OFFSET) =
        (short) CH24T_GRID_W;

    data_fdps_battle_move_grid_ptr = ch24t_grid;
    data_fdps_tile_event_data_table_ptr = ch24t_spawn_table;
    data_fdps_scene_layer_tile_map_ptrs[0] = ch24t_tile_map;
    data_fdps_scene_layer_tile_attr_ptr[0] = ch24t_tile_attr;
    data_fdps_map_cell_event_code_layer_ptr = ch24t_event_layer;
    data_fdps_battle_character_base_table_ptr =
        (unsigned char *) ch24t_char_base;
    data_fdps_battle_character_growth_table_ptr =
        (unsigned char *) ch24t_growth;
    data_fdps_battle_enemy_data_table_ptr = (unsigned char *) ch24t_enemy;
    data_fdps_item_effect_table_ptr = (unsigned char *) ch24t_items;
    data_fdps_current_chapter_text_ptr = ch24t_text_block;

    data_fdps_map_unit_array_ptr =
        (unsigned char *) malloc((size_t) CH24T_UNIT_STRIDE);
    memset(data_fdps_map_unit_array_ptr, 0, (size_t) CH24T_UNIT_STRIDE);
    data_fdps_map_unit_count = 1;
    ch24t_unit(0)->pos_x = 0;
    ch24t_unit(0)->pos_y = 0;

    data_fdps_chapter_current_chapter_id = 0;
    data_fdps_battle_turn_counter = turn;
}

/* Three records, the middle one tagged with the wave under test and the other
   two one wave either side of it, so a deployment that drifted by one deploys
   a different character id. */
static void ch24t_stage_wave_decoys(int wave_no)
{
    ch24t_spawn_table[CH24T_SPAWN_TABLE_COUNT_OFFSET] = 3;
    ch24t_set_spawn(0, CH24T_BELOW_CHAR_ID, wave_no - 1);
    ch24t_set_spawn(1, CH24T_TAGGED_CHAR_ID, wave_no);
    ch24t_set_spawn(2, CH24T_ABOVE_CHAR_ID, wave_no + 1);
}

/* Every wave the handler is able to ask for, one record each, so a case that
   expects no deployment fails whichever wave a broken chain reached for. */
static void ch24t_stage_every_wave(void)
{
    int wave_no;

    ch24t_spawn_table[CH24T_SPAWN_TABLE_COUNT_OFFSET] =
        (unsigned char) (CH24T_HIGHEST_WAVE - CH24T_LOWEST_WAVE + 1);
    for (wave_no = CH24T_LOWEST_WAVE; wave_no <= CH24T_HIGHEST_WAVE;
         wave_no++) {
        ch24t_set_spawn(wave_no - CH24T_LOWEST_WAVE, CH24T_TAGGED_CHAR_ID,
                        wave_no);
    }
}

static int ch24t_arrival_turns[CH24T_ARRIVAL_TURNS] = {
    CH24T_WAVE_2_TURN, CH24T_SHAN_TURN, CH24T_WAVE_4_TURN,
    CH24T_WAVE_3_TURN, CH24T_WAVE_5_TURN
};

static int ch24t_arrival_waves[CH24T_ARRIVAL_TURNS] = {
    CH24T_TURN_5_WAVE, CH24T_TURN_7_WAVE, CH24T_TURN_10_WAVE,
    CH24T_TURN_13_WAVE, CH24T_TURN_15_WAVE
};

/* Each of the five arrival turns brings on its own wave and no other: turn 5
   wave 2, turn 7 wave 6, turn 10 wave 4, turn 13 wave 3 and turn 15 wave 5.
   Each turn is staged against three records tagged one below, the wave itself
   and one above, so only the middle one may be deployed and a rebuild that
   drifted by one wave in either direction deploys a different character.  The
   arrival is appended behind the unit already on the map and lands on
   MAP00.COD record 1's tile. */
static void ch24t_each_arrival_turn_deploys_its_own_wave(void)
{
    int i;

    ch24t_ensure_game_files();
    if (!ch24t_files_ready) {
        return;
    }
    for (i = 0; i < CH24T_ARRIVAL_TURNS; i++) {
        ch24t_stage(ch24t_arrival_turns[i]);
        ch24t_stage_wave_decoys(ch24t_arrival_waves[i]);

        fdps_chapter_24_event_deploy_wave_for_turn(CH24T_DISPATCHER_ARG);

        CHECK_EQ(data_fdps_map_unit_count, 2);
        CHECK_EQ((int) ch24t_unit(1)->char_id, CH24T_TAGGED_CHAR_ID);
        CHECK_EQ((int) ch24t_unit(1)->pos_x, CH24T_MAP00_RECORD_1_X);
        CHECK_EQ((int) ch24t_unit(1)->pos_y, CH24T_MAP00_RECORD_1_Y);
    }
}

/* The wave a turn asks for is a literal and never the turn counter itself: on
   each arrival turn a record tagged with the turn number is the only record
   present, and none of the five deploys it.  This is what tells the handler
   apart from the chapter 23 one next door, which passes the turn counter to
   fdps_deploy_wave biased by four. */
static void ch24t_the_wave_is_never_the_turn_number(void)
{
    int i;

    ch24t_ensure_game_files();
    if (!ch24t_files_ready) {
        return;
    }
    for (i = 0; i < CH24T_ARRIVAL_TURNS; i++) {
        ch24t_stage(ch24t_arrival_turns[i]);
        ch24t_spawn_table[CH24T_SPAWN_TABLE_COUNT_OFFSET] = 1;
        ch24t_set_spawn(0, CH24T_TAGGED_CHAR_ID, ch24t_arrival_turns[i]);

        fdps_chapter_24_event_deploy_wave_for_turn(CH24T_DISPATCHER_ARG);

        CHECK_EQ(data_fdps_map_unit_count, 1);
    }
}

/* Turn 8 is the one named turn that speaks without deploying: its case holds a
   draw and no call to fdps_deploy_wave at all, so a map carrying a record of
   every wave the handler knows keeps every one of them. */
static void ch24t_turn_eight_deploys_nothing(void)
{
    ch24t_ensure_game_files();
    if (!ch24t_files_ready) {
        return;
    }
    ch24t_stage(CH24T_LINE_ONLY_TURN);
    ch24t_stage_every_wave();

    fdps_chapter_24_event_deploy_wave_for_turn(CH24T_DISPATCHER_ARG);

    CHECK_EQ(data_fdps_map_unit_count, 1);
}

static int ch24t_quiet_turns[CH24T_QUIET_TURN_COUNT] = {
    1, 2, 3, 4, 6, 9, 11, 12, 14, 16, 20
};

/* The chain has no default branch, so every turn the six tests do not name
   leaves the map exactly as it was.  Sixteen is the first turn past the last
   test, which is the end a rebuild written as ">= 15" would get wrong, and
   twenty is well past it. */
static void ch24t_does_nothing_on_a_turn_it_does_not_name(void)
{
    int i;

    ch24t_ensure_game_files();
    if (!ch24t_files_ready) {
        return;
    }
    for (i = 0; i < CH24T_QUIET_TURN_COUNT; i++) {
        ch24t_stage(ch24t_quiet_turns[i]);
        ch24t_stage_every_wave();

        fdps_chapter_24_event_deploy_wave_for_turn(CH24T_DISPATCHER_ARG);

        CHECK_EQ(data_fdps_map_unit_count, 1);
    }
}

/* The incoming argument is stored over with 0 and never read, so a value the
   dispatcher never pushes changes nothing about what the handler does. */
static void ch24t_ignores_the_incoming_argument(void)
{
    ch24t_ensure_game_files();
    if (!ch24t_files_ready) {
        return;
    }
    ch24t_stage(CH24T_WAVE_2_TURN);
    ch24t_stage_wave_decoys(CH24T_TURN_5_WAVE);

    fdps_chapter_24_event_deploy_wave_for_turn(CH24T_DISPATCHER_ARG);

    CHECK_EQ(data_fdps_map_unit_count, 2);
    CHECK_EQ((int) ch24t_unit(1)->char_id, CH24T_TAGGED_CHAR_ID);

    ch24t_stage(CH24T_WAVE_2_TURN);
    ch24t_stage_wave_decoys(CH24T_TURN_5_WAVE);

    fdps_chapter_24_event_deploy_wave_for_turn(CH24T_OTHER_ARG);

    CHECK_EQ(data_fdps_map_unit_count, 2);
    CHECK_EQ((int) ch24t_unit(1)->char_id, CH24T_TAGGED_CHAR_ID);
}

/* There is no one-shot latch anywhere in the body: nothing is read before the
   turn test and nothing is written after the calls, so firing the same turn
   twice deploys the same wave twice.  The dispatcher is what keeps that from
   happening in play, not the handler. */
static void ch24t_has_no_one_shot_latch(void)
{
    ch24t_ensure_game_files();
    if (!ch24t_files_ready) {
        return;
    }
    ch24t_stage(CH24T_WAVE_2_TURN);
    ch24t_stage_wave_decoys(CH24T_TURN_5_WAVE);

    fdps_chapter_24_event_deploy_wave_for_turn(CH24T_DISPATCHER_ARG);
    CHECK_EQ(data_fdps_map_unit_count, 2);

    fdps_chapter_24_event_deploy_wave_for_turn(CH24T_DISPATCHER_ARG);
    CHECK_EQ(data_fdps_map_unit_count, 3);
}

/* The map number comes from data_fdps_chapter_current_chapter_id read at the
   call site and not from a literal 23: with the global on 1 the deployment
   reads MAP01.COD, whose record 0 is (9, 4), where map 0's record 0 is
   (18, 0). */
static void ch24t_map_number_comes_from_the_chapter_global(void)
{
    ch24t_ensure_game_files();
    if (!ch24t_files_ready) {
        return;
    }
    ch24t_stage(CH24T_WAVE_2_TURN);
    ch24t_spawn_table[CH24T_SPAWN_TABLE_COUNT_OFFSET] = 1;
    ch24t_set_spawn(0, CH24T_TAGGED_CHAR_ID, CH24T_TURN_5_WAVE);
    data_fdps_chapter_current_chapter_id = 1;

    fdps_chapter_24_event_deploy_wave_for_turn(CH24T_DISPATCHER_ARG);

    CHECK_EQ(data_fdps_map_unit_count, 2);
    CHECK_EQ((int) ch24t_unit(1)->pos_x, CH24T_MAP01_RECORD_0_X);
    CHECK_EQ((int) ch24t_unit(1)->pos_y, CH24T_MAP01_RECORD_0_Y);
}

/* The placement flag is 0 on every one of the five deployments, so an arrival
   goes on the nearest free walkable tile to its record's coordinates rather
   than on the coordinates themselves: with (22, 12) made unwalkable the unit
   lands at (22, 13) instead of standing on the blocked tile, which is what a
   place_exact of 1 would have done. */
static void ch24t_places_on_the_nearest_free_tile(void)
{
    ch24t_ensure_game_files();
    if (!ch24t_files_ready) {
        return;
    }
    ch24t_stage(CH24T_WAVE_2_TURN);
    ch24t_spawn_table[CH24T_SPAWN_TABLE_COUNT_OFFSET] = 2;
    ch24t_set_spawn(0, CH24T_BELOW_CHAR_ID, CH24T_TURN_5_WAVE - 1);
    ch24t_set_spawn(1, CH24T_TAGGED_CHAR_ID, CH24T_TURN_5_WAVE);
    ch24t_set_tile_id(CH24T_MAP00_RECORD_1_X, CH24T_MAP00_RECORD_1_Y, 1);
    ch24t_set_terrain(1, CH24T_TERRAIN_BLOCKED);

    fdps_chapter_24_event_deploy_wave_for_turn(CH24T_DISPATCHER_ARG);

    CHECK_EQ(data_fdps_map_unit_count, 2);
    CHECK_EQ((int) ch24t_unit(1)->char_id, CH24T_TAGGED_CHAR_ID);
    CHECK_EQ((int) ch24t_unit(1)->pos_x, CH24T_MAP00_RECORD_1_X);
    CHECK_EQ((int) ch24t_unit(1)->pos_y, CH24T_MAP00_RECORD_1_BLOCKED_Y);
}

/* ------------------------------------------------------------------
 * fdps_chapter_25_event_deploy_wave_1 at 00038e60.
 *
 * The chapter 25 ambush is the chapter 24 handler's fixture with three things
 * added, the way tests/chevt3.c stages its own pan-bearing case:
 *
 *   a sixteen-record unit array, so an index really names one record out of
 *   several, the twelve party slots the sweep must not touch are all present,
 *   and the sweep has real records above them to write;
 *
 *   the globals the frame compositor reads -- no scene layers, both HUD flags
 *   down, the view at the origin and a sentinel in the frame latch that a run
 *   composing no frame at all leaves behind;
 *
 *   an enemy table long enough for character id 0x80, which is the id every
 *   staged deployment record carries because it is the one the compositor
 *   drops.  The eight-row table the chapter 24 fixture points at is not that
 *   long.
 *
 * WHICH RECORD ARRIVED IS READ OFF ITS LEVEL AND NOT ITS CHARACTER ID, for that
 * same reason: every record has to carry the dropped id, so the level is what
 * tells the wave-1 record from the wave-0 and wave-2 records either side of it.
 *
 * THE CASES THAT REACH THE BODY RUN WITH THE TIMER INSTALLED AND THE ADAPTER IN
 * MODE 13H, because the two twelve-frame holds spin on the tick counter inside
 * fdps_render_view_frame and nothing else advances it.  They also reach
 * fdps_deploy_wave, which opens ICON.CEL and FIELD.VFS for itself, so they skip
 * themselves when those are not staged.  The cases whose gate refuses the body
 * need none of that -- nothing is opened and no frame is composed -- so they
 * are free to sweep several latch values and several sides.
 *
 * EACH FIRING COSTS SEVERAL SECONDS OF REAL TIME, which is why so many claims
 * are asserted out of one of them: twenty-four held frames plus a frame for
 * every pan step that dragged the view, each waiting for a timer tick.  Only
 * the number of firings can be made smaller, so the grouping below is
 * deliberate.
 *
 * WHICH TEXT ENTRY EACH DRAW ASKS FOR IS NOT ASSERTED.  fdps_draw_text takes
 * its whole effect through pixels at the VGA aperture, keeps no state and
 * returns a cursor this handler discards, so nothing it did is readable
 * afterwards.  The two ids and the three colours are literals in the
 * instruction stream (PUSH 0x12 at 00038eac, PUSH 0x13 at 00038f50, and
 * PUSH 0xd0 / PUSH 0x0 / PUSH 0x6d at 00038e9d, 00038e9b and 00038e99), and the
 * text block is staged as entries that are a lone terminator so a draw walks
 * one, paints nothing and returns at once.  The order of the draws against the
 * deployment is pinned in src/chevt5.c against those addresses instead.
 * ------------------------------------------------------------------ */

/* The element of data_fdps_map_cell_event_triggered_flags the handler latches,
   byte ptr [0x000640e8] -- element 0x10 of the array based at 0x000640d8. */
#define CH25W1_LATCH_SLOT 0x10

/* The side the gate demands, CMP EAX,0x2 at 00038e8f, and three that it
   refuses: the enemy's, the guest's, and one past the values the game uses so
   that a rebuild written as ">= 2" is caught. */
#define CH25W1_SIDE_PLAYER 2
#define CH25W1_SIDE_ENEMY 0
#define CH25W1_SIDE_GUEST 1
#define CH25W1_SIDE_ABOVE_PLAYER 3

/* The wave the ambush asks for, PUSH 0x1 at 00038ebf, and the two waves parked
   either side of it so that asking for the wrong one is visible. */
#define CH25W1_WAVE 1
#define CH25W1_WAVE_BELOW 0
#define CH25W1_WAVE_ABOVE 2

/* Which table index each of the three sits on.  The wave the handler asks for
   is on record 0, whose MAP00.COD and MAP01.COD coordinates the chapter 24
   cases above already read back out of the real files. */
#define CH25W1_WAVE_RECORD 0
#define CH25W1_WAVE_BELOW_RECORD 1
#define CH25W1_WAVE_ABOVE_RECORD 2
#define CH25W1_SPAWN_RECORD_COUNT 3

/* The level each record carries, which is how the cases tell which one
   arrived.  The wave-1 record's level is deliberately not its wave number. */
#define CH25W1_WAVE_LEVEL 21
#define CH25W1_WAVE_BELOW_LEVEL 5
#define CH25W1_WAVE_ABOVE_LEVEL 7

/* The id the compositor drops, and how many enemy rows a table has to have for
   fdps_deploy_unit to resolve it inside itself: 0x80 - ENEMY_CHAR_ID_BASE + 1
   (src/deploy.c). */
#define CH25W1_ARRIVAL_CHAR_ID 0x80
#define CH25W1_ENEMY_TABLE_ROWS (0x80 - 0x3c + 1)

/* MAP00.COD's placement record 0 and MAP01.COD's record 0. */
#define CH25W1_MAP00_RECORD0_X 18
#define CH25W1_MAP00_RECORD0_Y 0
#define CH25W1_MAP01_RECORD0_X 9
#define CH25W1_MAP01_RECORD0_Y 4

/* Units already on the map when the handler runs, and the index the arrival
   therefore lands on.  Sixteen puts four records above the twelve party slots,
   so the sweep's start bound has both something below it to leave alone and
   something above it to write. */
#define CH25W1_STAGED_UNITS 16
#define CH25W1_ARRIVAL_UNIT_INDEX CH25W1_STAGED_UNITS

/* The unit the fixture puts on the side that springs the ambush.  It is neither
   the first record nor the last, so a handler that read unit 0, or the last
   unit, or ignored the argument would fire on the wrong call. */
#define CH25W1_TRIGGERING_UNIT_INDEX 5

/* The first index the sweep covers, MOV dword ptr [EBP-0x28],0xc at 00038f60,
   and the last party slot below it. */
#define CH25W1_FIRST_ALERTED_INDEX 0xc
#define CH25W1_LAST_PARTY_INDEX (CH25W1_FIRST_ALERTED_INDEX - 1)

/* What every staged record's AI byte holds before a firing and what the merge
   is required to leave behind: the low nibble goes to 0x0b and the high nibble
   -- the two flag bits the target scorers read -- is carried across untouched,
   AND DL,0xf0 / OR DH,DL at 00038fb2.  A rebuild that wrote the mode as a whole
   byte would leave 0x0b here instead of 0x2b. */
#define CH25W1_STAGED_AI_BYTE 0x25
#define CH25W1_ALERTED_AI_BYTE 0x2b

/* The cursor mode the fixture parks in data_fdps_map_cursor_draw_mode before
   every run: neither of the two values the handler writes, so a run that left
   it alone, a run that hid the cursor and never put it back, and a run that
   restored what it found are all told apart from the mode the handler is
   supposed to leave behind. */
#define CH25W1_STAGED_CURSOR_MODE 4
#define CH25W1_CURSOR_MODE_NORMAL 1

/* Where the cursor starts and where the two pans leave it.  The start is one
   whole tile east of the first pan's target, so that pan really walks; the end
   is PUSH 0x108 / PUSH 0x1e0 at 00038f03, tile (20, 11), and the walk arrives
   on it exactly because 0x1e0 is a whole multiple of the 24-pixel tile
   (mapcur.h). */
#define CH25W1_PAN_START_X 0x18
#define CH25W1_PAN_START_Y 0
#define CH25W1_PAN_END_X 0x1e0
#define CH25W1_PAN_END_Y 0x108

/* How long each hold lasts, CMP dword ptr [EBP-0x4],0xc / JL at 00038eec and
   00038f1c, and the least the tick counter can move across both of them.  The
   bound is one-sided on purpose -- a slow machine spends more ticks than this,
   never fewer, and the frames the pans themselves compose are on top of it --
   so it cannot fail spuriously, while a rebuild that dropped one of the two
   loops cannot meet it at any sane speed. */
#define CH25W1_HOLD_FRAMES 0xc
#define CH25W1_LEAST_HOLD_TICKS (2 * CH25W1_HOLD_FRAMES - 1)

/* A value the tick counter cannot legitimately hold, parked in the frame latch
   so a run that composed nothing is distinguishable from one that did. */
#define CH25W1_FRAME_SENTINEL 0x5a5a5a5aU

/* Any turn at all: nothing in this handler reads the turn counter, and the
   fixture sets one because the chapter 24 stage it is built on wants one. */
#define CH25W1_ANY_TURN 4

#define CH25W1_VGA_MODE_TEXT 0x03
#define CH25W1_VGA_MODE_320X200X256 0x13
#define CH25W1_TIMER_VECTOR 8

static struct fdps_enemy_data ch25w1_enemy[CH25W1_ENEMY_TABLE_ROWS];
static unsigned int ch25w1_ticks_before;
static unsigned int ch25w1_ticks_after;
static void (__interrupt __far *ch25w1_saved_timer)();

static void __interrupt __far ch25w1_timer_isr(void)
{
    ++data_fdps_timer_tick_counter;
    _chain_intr(ch25w1_saved_timer);
}

static void ch25w1_set_mode(int mode)
{
    union REGS regs;

    memset(&regs, 0, sizeof(regs));
    regs.x.eax = (unsigned) mode;
    int386(0x10, &regs, &regs);
}

/* The chapter 24 fixture given a sixteen-record unit array every one of whose
   units is on the enemy's side and wears the dropped portrait id, three
   deployment records at the waves the cases ask about, an enemy table long
   enough for the id they carry, and the compositor's own globals.  Only the
   record at CH25W1_TRIGGERING_UNIT_INDEX is able to spring the ambush. */
static void ch25w1_stage(void)
{
    int i;

    ch24t_stage(CH25W1_ANY_TURN);

    memset(ch25w1_enemy, 0, sizeof(ch25w1_enemy));
    data_fdps_battle_enemy_data_table_ptr = (unsigned char *) ch25w1_enemy;

    data_fdps_map_unit_array_ptr = (unsigned char *)
        malloc((size_t) (CH25W1_STAGED_UNITS * CH24T_UNIT_STRIDE));
    memset(data_fdps_map_unit_array_ptr, 0,
           (size_t) (CH25W1_STAGED_UNITS * CH24T_UNIT_STRIDE));
    data_fdps_map_unit_count = CH25W1_STAGED_UNITS;
    for (i = 0; i < CH25W1_STAGED_UNITS; i++) {
        ch24t_unit(i)->portrait_id = (unsigned char) CH25W1_ARRIVAL_CHAR_ID;
        ch24t_unit(i)->side = (unsigned char) CH25W1_SIDE_ENEMY;
        ch24t_unit(i)->ai_behavior = (unsigned char) CH25W1_STAGED_AI_BYTE;
        ch24t_unit(i)->pos_x = (unsigned char) i;
        ch24t_unit(i)->pos_y = 0;
    }
    ch24t_unit(CH25W1_TRIGGERING_UNIT_INDEX)->side =
        (unsigned char) CH25W1_SIDE_PLAYER;

    ch24t_spawn_table[CH24T_SPAWN_TABLE_COUNT_OFFSET] =
        (unsigned char) CH25W1_SPAWN_RECORD_COUNT;
    ch24t_set_spawn(CH25W1_WAVE_RECORD, CH25W1_ARRIVAL_CHAR_ID, CH25W1_WAVE);
    ch24t_set_spawn(CH25W1_WAVE_BELOW_RECORD, CH25W1_ARRIVAL_CHAR_ID,
                    CH25W1_WAVE_BELOW);
    ch24t_set_spawn(CH25W1_WAVE_ABOVE_RECORD, CH25W1_ARRIVAL_CHAR_ID,
                    CH25W1_WAVE_ABOVE);
    ch24t_spawn_at(CH25W1_WAVE_RECORD)->level = (unsigned char) CH25W1_WAVE_LEVEL;
    ch24t_spawn_at(CH25W1_WAVE_BELOW_RECORD)->level =
        (unsigned char) CH25W1_WAVE_BELOW_LEVEL;
    ch24t_spawn_at(CH25W1_WAVE_ABOVE_RECORD)->level =
        (unsigned char) CH25W1_WAVE_ABOVE_LEVEL;
    ch24t_spawn_at(CH25W1_WAVE_RECORD)->ai_class =
        (unsigned char) CH25W1_STAGED_AI_BYTE;

    data_fdps_scene_layer_count = 0;
    data_fdps_ui_terrain_hud_user_enabled = 0;
    data_fdps_ui_play_active_flag = 0;
    data_fdps_battle_view_window_origin_x = 0;
    data_fdps_battle_view_window_origin_y = 0;
    data_fdps_map_cursor_world_x = CH25W1_PAN_START_X;
    data_fdps_map_cursor_world_y = CH25W1_PAN_START_Y;
    data_fdps_view_frame_last_tick = CH25W1_FRAME_SENTINEL;
    data_fdps_map_cursor_draw_mode = CH25W1_STAGED_CURSOR_MODE;

    data_fdps_map_cell_event_triggered_flags[CH25W1_LATCH_SLOT] = 0;
}

/* One firing, with the timer running and the adapter in the mode the frames
   present through, and the tick counter sampled either side so the length of
   the two holds can be read back.  Text mode is back before anything is
   asserted, so a failure prints on a readable screen. */
static void ch25w1_run(int unit_index)
{
    ch25w1_set_mode(CH25W1_VGA_MODE_320X200X256);
    ch25w1_saved_timer = _dos_getvect(CH25W1_TIMER_VECTOR);
    _dos_setvect(CH25W1_TIMER_VECTOR, ch25w1_timer_isr);
    ch25w1_ticks_before = data_fdps_timer_tick_counter;
    fdps_chapter_25_event_deploy_wave_1(unit_index);
    ch25w1_ticks_after = data_fdps_timer_tick_counter;
    _dos_setvect(CH25W1_TIMER_VECTOR, ch25w1_saved_timer);
    ch25w1_set_mode(CH25W1_VGA_MODE_TEXT);
}

/* The three fields the handler reaches through and the stride they are indexed
   by.  CMP EAX,0x2 is applied to byte [EAX+0x6] at 00038e87 and the merge to
   byte [EAX+0x34] at 00038faf, so those are the offsets the emitted field names
   have to sit at; the wave byte is what the deployment walk matches 1 against. */
static void ch25w1_record_shape_matches_the_offsets(void)
{
    CHECK_EQ((int) sizeof(struct fdps_unit_record), CH24T_UNIT_STRIDE);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, side), 6);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, ai_behavior), 0x34);
    CHECK_EQ((int) offsetof(struct fdps_char_spawn_record, wave_no), 0x15);
}

/* A latch that is already up refuses the whole body, and it is tested against 0
   rather than against 1 -- CMP byte ptr [0x000640e8],0x0 / JNZ at 00038e7b --
   so any non-zero value in the slot blocks it.  Nothing is deployed, no frame
   is composed, no AI byte moves and the cursor mode is left exactly as it was
   found, which is what says the store of 0 at 00038ecf is inside the gate and
   not ahead of it. */
static void ch25w1_latch_blocks_the_whole_body(void)
{
    static int latch_values[2] = {1, 0x7f};
    int i;

    for (i = 0; i < 2; i++) {
        ch25w1_stage();
        data_fdps_map_cell_event_triggered_flags[CH25W1_LATCH_SLOT] =
            (unsigned char) latch_values[i];

        fdps_chapter_25_event_deploy_wave_1(CH25W1_TRIGGERING_UNIT_INDEX);

        CHECK_EQ(data_fdps_map_unit_count, CH25W1_STAGED_UNITS);
        CHECK_EQ(data_fdps_map_cell_event_triggered_flags[CH25W1_LATCH_SLOT],
                 latch_values[i]);
        CHECK_EQ(data_fdps_map_cursor_draw_mode, CH25W1_STAGED_CURSOR_MODE);
        CHECK_EQ(data_fdps_view_frame_last_tick == CH25W1_FRAME_SENTINEL, 1);
        CHECK_EQ((int) ch24t_unit(CH25W1_FIRST_ALERTED_INDEX)->ai_behavior,
                 CH25W1_STAGED_AI_BYTE);
    }
}

/* The side gate is an equality against 2 and not a non-zero test and not a
   ">= 2" test: sides 0, 1 and 3 are all refused and all leave the ambush armed,
   so an enemy or a guest crossing the tile does not consume the event and a
   side above the player's does not spring it either. */
static void ch25w1_only_the_player_side_fires(void)
{
    static int refused_sides[3] = {
        CH25W1_SIDE_ENEMY, CH25W1_SIDE_GUEST, CH25W1_SIDE_ABOVE_PLAYER
    };
    int i;

    for (i = 0; i < 3; i++) {
        ch25w1_stage();
        ch24t_unit(CH25W1_TRIGGERING_UNIT_INDEX)->side =
            (unsigned char) refused_sides[i];

        fdps_chapter_25_event_deploy_wave_1(CH25W1_TRIGGERING_UNIT_INDEX);

        CHECK_EQ(data_fdps_map_unit_count, CH25W1_STAGED_UNITS);
        CHECK_EQ(data_fdps_map_cell_event_triggered_flags[CH25W1_LATCH_SLOT], 0);
        CHECK_EQ(data_fdps_map_cursor_draw_mode, CH25W1_STAGED_CURSOR_MODE);
        CHECK_EQ(data_fdps_view_frame_last_tick == CH25W1_FRAME_SENTINEL, 1);
        CHECK_EQ((int) ch24t_unit(CH25W1_FIRST_ALERTED_INDEX)->ai_behavior,
                 CH25W1_STAGED_AI_BYTE);
    }
}

/* Everything the body does, out of one firing, because a firing is expensive.
   The fixture puts fifteen of its sixteen units on side 0 and leaves one able
   to spring the ambush, so the indices that name a side-0 unit are refused --
   they leave the latch down, which is what lets them run without restaging --
   and only the index that names the fifteenth fires.

   THE SIDE BYTE READ BELONGS TO THE RECORD THE ARGUMENT NAMES.  A handler that
   read unit 0, or the last unit, or ignored the argument would fire on one of
   the three refused calls.

   WAVE 1 IS WHAT ARRIVES, on MAP00.COD record 0's tile.  The one record tagged
   1 is deployed, carrying its own level, and the records tagged 0 and 2 are
   left where they are: the unit count moves by exactly one, and the level is
   what says which record moved, every record carrying the same character id.
   A rebuild that drifted by one wave in either direction deploys a different
   level.

   THE PAN ENDS ON THE SECOND TARGET.  The cursor lands on (0x1e0, 0x108), which
   is the second of the two pushes and not the first, so the two pans ran in the
   order the instruction stream has them.

   THE CURSOR MODE IS 1 AFTERWARDS AND IS NOT THE MODE THE RUN FOUND.  The
   fixture parks 4 in the global, so a rebuild that saved and restored it, or
   that left the blank 0 behind, is caught.

   BOTH HOLDS RAN.  The tick counter moves by at least 2 * 12 - 1 across the
   firing, which no rebuild missing one of the two loops can reach.

   THE SWEEP STARTS AT 12, IS INCLUSIVE AT THE TOP, AND KEEPS THE HIGH NIBBLE.
   Every party slot from 0 to 11 still holds the staged 0x25; every index from
   12 to the last live unit holds 0x2b -- mode 0x0b merged under the preserved
   0x20 -- and the last of those is the arrival itself, at index 16, which is
   the count less one.

   THE ARRIVAL IS SWEPT WITH THE GARRISON.  Its deployment record carries AI
   class 0x25 and it comes back holding 0x2b, so the unit count the loop bound
   is taken from was read after the deployment appended it.

   THE LATCH IS SPENT.  It is 1 afterwards, and a second call with the same
   index deploys nothing more. */
static void ch25w1_fires_once_for_the_unit_the_index_names(void)
{
    static int enemy_indices[3] = {0, 1, CH25W1_STAGED_UNITS - 1};
    int i;

    ch25w1_stage();
    for (i = 0; i < 3; i++) {
        fdps_chapter_25_event_deploy_wave_1(enemy_indices[i]);
        CHECK_EQ(data_fdps_map_cell_event_triggered_flags[CH25W1_LATCH_SLOT], 0);
        CHECK_EQ(data_fdps_map_unit_count, CH25W1_STAGED_UNITS);
    }

    ch24t_ensure_game_files();
    if (!ch24t_files_ready) {
        return;
    }

    ch25w1_run(CH25W1_TRIGGERING_UNIT_INDEX);

    CHECK_EQ(data_fdps_map_cell_event_triggered_flags[CH25W1_LATCH_SLOT], 1);
    CHECK_EQ(data_fdps_map_unit_count, CH25W1_STAGED_UNITS + 1);
    CHECK_EQ((int) ch24t_unit(CH25W1_ARRIVAL_UNIT_INDEX)->level,
             CH25W1_WAVE_LEVEL);
    CHECK_EQ((int) ch24t_unit(CH25W1_ARRIVAL_UNIT_INDEX)->pos_x,
             CH25W1_MAP00_RECORD0_X);
    CHECK_EQ((int) ch24t_unit(CH25W1_ARRIVAL_UNIT_INDEX)->pos_y,
             CH25W1_MAP00_RECORD0_Y);

    CHECK_EQ(data_fdps_map_cursor_world_x, CH25W1_PAN_END_X);
    CHECK_EQ(data_fdps_map_cursor_world_y, CH25W1_PAN_END_Y);
    CHECK_EQ(data_fdps_map_cursor_draw_mode, CH25W1_CURSOR_MODE_NORMAL);
    CHECK_EQ(ch25w1_ticks_after - ch25w1_ticks_before
                 >= (unsigned int) CH25W1_LEAST_HOLD_TICKS,
             1);

    for (i = 0; i <= CH25W1_LAST_PARTY_INDEX; i++) {
        CHECK_EQ((int) ch24t_unit(i)->ai_behavior, CH25W1_STAGED_AI_BYTE);
    }
    for (i = CH25W1_FIRST_ALERTED_INDEX;
         i <= CH25W1_ARRIVAL_UNIT_INDEX;
         i++) {
        CHECK_EQ((int) ch24t_unit(i)->ai_behavior, CH25W1_ALERTED_AI_BYTE);
    }

    fdps_chapter_25_event_deploy_wave_1(CH25W1_TRIGGERING_UNIT_INDEX);
    CHECK_EQ(data_fdps_map_unit_count, CH25W1_STAGED_UNITS + 1);
}

/* The map the wave is placed under is read from
   data_fdps_chapter_current_chapter_id at the call site and is not a literal:
   the same record placed while that global says 1 lands on MAP01.COD's record 0
   at (9, 4), where the case above -- which fires with that global on 0 -- has it
   landing on MAP00.COD's (18, 0).  The two halves of the claim are in two cases
   because each of them costs a firing. */
static void ch25w1_map_number_comes_from_the_chapter_global(void)
{
    ch24t_ensure_game_files();
    if (!ch24t_files_ready) {
        return;
    }

    ch25w1_stage();
    data_fdps_chapter_current_chapter_id = 1;

    ch25w1_run(CH25W1_TRIGGERING_UNIT_INDEX);

    CHECK_EQ(data_fdps_map_unit_count, CH25W1_STAGED_UNITS + 1);
    CHECK_EQ((int) ch24t_unit(CH25W1_ARRIVAL_UNIT_INDEX)->level,
             CH25W1_WAVE_LEVEL);
    CHECK_EQ((int) ch24t_unit(CH25W1_ARRIVAL_UNIT_INDEX)->pos_x,
             CH25W1_MAP01_RECORD0_X);
    CHECK_EQ((int) ch24t_unit(CH25W1_ARRIVAL_UNIT_INDEX)->pos_y,
             CH25W1_MAP01_RECORD0_Y);
}

/* ---------------------------------------------------------------------- */

/* Chapter 25's sword upgrade at 00038fd0.
 *
 * This handler needs no game file at all: it takes its whole effect through one
 * unit record, so the unit array is a local block that
 * data_fdps_map_unit_array_ptr is aimed at, and the line it speaks is drawn out
 * of a text block whose every entry names one lone terminator -- fdps_draw_text
 * walks the entry, paints nothing and returns without a font, a message panel
 * or mode 13h.  That is all these cases can see of the draw; the text id is
 * read off PUSH 0x14 at 00039011 and is not assertable here, because a draw
 * that paints nothing leaves nothing behind to tell one entry from another.
 *
 * What the cases are for is the pair of gates and the order of the two
 * inventory calls.  The gates are CMP dword ptr [EBP+0x14],0x0 at 00038ff0 and
 * CMP dword ptr [EBP-0x4],-0x1 at 00038ff6, and both refuse the whole body.
 * There is no turn test anywhere in the body -- unlike the chapter 20 link of
 * the same chain, which closes at turn 20 -- and no write to
 * data_fdps_map_cell_event_triggered_flags, so a firing on a late turn and a
 * firing with the shared latch already raised both have to still happen, and a
 * re-armed bag has to fire a second time.
 *
 * The removal at 00039029 comes before the addition at 0003903a, and with a
 * full bag that is the difference between keeping the sword and losing it:
 * fdps_unit_add_item stores nothing when all eight entries are occupied
 * (unititem.h), so the full-bag case is what pins the order down.
 *
 * The expected inventory shape after a firing comes from the two callees: the
 * removal memmoves the entries above the slot down and empties the last one,
 * and the add takes the first entry whose flag carries bit 0x80, so the new
 * sword lands in the entry the old one vacated.
 */

/* The two swords, read off PUSH 0xa1 at 00038fdc and PUSH 0xa2 at 00039031:
   火光之劍 and 真炎龍劍 (assets/items.md). */
#define CH25U_FLAME_SWORD 0xa1
#define CH25U_TRUE_DRAGON_SWORD 0xa2

/* The sword one link earlier in the chain, 灼烈之劍, and a plain carried item
   with no combat effect, 金屬礦 -- neither of them is what this handler looks
   for, so a case can carry them to say which entry moved and which did not. */
#define CH25U_BLAZING_SWORD 0xa0
#define CH25U_OTHER_ITEM 0xa3

/* Battle unit 0 is Randis; the other two staged records stand in for units the
   gate has to refuse. */
#define CH25U_RANDIS 0
#define CH25U_OTHER_UNIT_A 1
#define CH25U_OTHER_UNIT_B 2
#define CH25U_STAGE_UNITS 3

/* An inventory entry is a flag byte and an id byte; bit 0x80 of the flag means
   the entry is empty, and a plain 0 flag means carried and not equipped
   (unititem.h). */
#define CH25U_EMPTY_FLAG 0x80
#define CH25U_EMPTY_ID 0xff
#define CH25U_CARRIED_FLAG 0x00
#define CH25U_INVENTORY_ENTRIES 8

/* A text block whose 0x15 entries all name one lone terminator, so the draw of
   entry 0x14 walks it and paints nothing. */
#define CH25U_TEXT_IDS 0x15
#define CH25U_TEXT_EMPTY_AT 0x40
#define CH25U_TEXT_BLOCK_BYTES (CH25U_TEXT_EMPTY_AT + 2)
#define CH25U_TEXT_END (-1)

/* The base stats the rebuild writes through with nothing equipped, and the
   sentinel the derived fields start from so a case can say the rebuild ran. */
#define CH25U_AP_BASE 43
#define CH25U_DP_BASE 33
#define CH25U_DX_BASE 23
#define CH25U_STAT_SENTINEL 0x7777

/* Item.dat has 256 records, and a table of zeros is enough for the rebuild
   because none of the staged bags holds an equipped entry. */
#define CH25U_ITEM_TABLE_ROWS 256

/* The shared one-shot latch the sibling chapter events raise; this handler
   never touches it, so the cases raise it and watch the body run anyway. */
#define CH25U_LATCH_SLOT 0x10
#define CH25U_LATCH_RAISED 1

/* A turn well past the chapter 20 link's deadline, to show there is no turn
   test in this one. */
#define CH25U_LATE_TURN 99

static unsigned char ch25u_text_block[CH25U_TEXT_BLOCK_BYTES];
static struct fdps_unit_record ch25u_units[CH25U_STAGE_UNITS];
static struct fdps_item_effect ch25u_items[CH25U_ITEM_TABLE_ROWS];

static void ch25u_stage_text(void)
{
    int text_id;

    memset(ch25u_text_block, 0, (size_t) CH25U_TEXT_BLOCK_BYTES);
    *(short *) (ch25u_text_block + CH25U_TEXT_EMPTY_AT) = (short) CH25U_TEXT_END;
    for (text_id = 0; text_id < CH25U_TEXT_IDS; text_id++) {
        *(short *) (ch25u_text_block + text_id * 2) =
            (short) CH25U_TEXT_EMPTY_AT;
    }
}

static void ch25u_blank_unit(int unit_index)
{
    int entry;

    for (entry = 0; entry < CH25U_INVENTORY_ENTRIES; entry++) {
        ch25u_units[unit_index].inventory_slots[entry * 2] = CH25U_EMPTY_FLAG;
        ch25u_units[unit_index].inventory_slots[entry * 2 + 1] = CH25U_EMPTY_ID;
    }
    ch25u_units[unit_index].ap_base = CH25U_AP_BASE;
    ch25u_units[unit_index].dp_base = CH25U_DP_BASE;
    ch25u_units[unit_index].dx_base = CH25U_DX_BASE;
    ch25u_units[unit_index].ap = CH25U_STAT_SENTINEL;
    ch25u_units[unit_index].dp = CH25U_STAT_SENTINEL;
    ch25u_units[unit_index].hit = CH25U_STAT_SENTINEL;
    ch25u_units[unit_index].ev = CH25U_STAT_SENTINEL;
}

static void ch25u_carry(int unit_index, int entry, int item_id)
{
    ch25u_units[unit_index].inventory_slots[entry * 2] = CH25U_CARRIED_FLAG;
    ch25u_units[unit_index].inventory_slots[entry * 2 + 1] =
        (unsigned char) item_id;
}

/* Everything the handler and its callees read: three blank records, an item
   table for the stat rebuild, the text block, a turn counter well past the
   chapter 20 deadline and the shared latch raised, so a body that consulted
   either of those two would be seen refusing to run. */
static void ch25u_stage(void)
{
    int unit_index;

    ch25u_stage_text();

    memset(ch25u_units, 0, sizeof(ch25u_units));
    memset(ch25u_items, 0, sizeof(ch25u_items));
    for (unit_index = 0; unit_index < CH25U_STAGE_UNITS; unit_index++) {
        ch25u_blank_unit(unit_index);
    }

    data_fdps_map_unit_array_ptr = (unsigned char *) ch25u_units;
    data_fdps_item_effect_table_ptr = (unsigned char *) ch25u_items;
    data_fdps_current_chapter_text_ptr = ch25u_text_block;
    data_fdps_battle_turn_counter = CH25U_LATE_TURN;
    data_fdps_map_cell_event_triggered_flags[CH25U_LATCH_SLOT] =
        CH25U_LATCH_RAISED;
}

/* The state most cases start from: every staged unit carrying 火光之劍 in its
   first entry, so a firing for the wrong index would be visible. */
static void ch25u_stage_armed(void)
{
    ch25u_stage();
    ch25u_carry(CH25U_RANDIS, 0, CH25U_FLAME_SWORD);
    ch25u_carry(CH25U_OTHER_UNIT_A, 0, CH25U_FLAME_SWORD);
    ch25u_carry(CH25U_OTHER_UNIT_B, 0, CH25U_FLAME_SWORD);
}

static int ch25u_entry_flag(int unit_index, int entry)
{
    return (int) ch25u_units[unit_index].inventory_slots[entry * 2];
}

static int ch25u_entry_id(int unit_index, int entry)
{
    return (int) ch25u_units[unit_index].inventory_slots[entry * 2 + 1];
}

/* Whether any occupied entry holds that id, which is how a case says an item
   was or was not handed over. */
static int ch25u_carries(int unit_index, int item_id)
{
    int entry;

    for (entry = 0; entry < CH25U_INVENTORY_ENTRIES; entry++) {
        if (ch25u_entry_flag(unit_index, entry) != CH25U_EMPTY_FLAG
                && ch25u_entry_id(unit_index, entry) == item_id) {
            return 1;
        }
    }
    return 0;
}

/* Whether the four derived stats still hold the sentinel, which is how a case
   says the rebuild did or did not run. */
static int ch25u_stats_untouched(int unit_index)
{
    return (int) ch25u_units[unit_index].ap == CH25U_STAT_SENTINEL
           && (int) ch25u_units[unit_index].dp == CH25U_STAT_SENTINEL
           && (int) ch25u_units[unit_index].hit == CH25U_STAT_SENTINEL
           && (int) ch25u_units[unit_index].ev == CH25U_STAT_SENTINEL;
}

/* The record fields these cases read back and the stride they are indexed by.
   Every one of them would agree with itself while addressing another byte if
   the layout were wrong. */
static void ch25u_record_shape_matches_the_offsets(void)
{
    CHECK_EQ((int) sizeof(struct fdps_unit_record), 0x50);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, inventory_slots), 0x0a);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, ap), 0x48);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, ev), 0x4e);
}

/* The whole taken path.  火光之劍 leaves the bag and 真炎龍劍 takes the entry
   it vacated -- the removal compacts the entries above the slot down and the
   add takes the first empty entry, which is that one -- the rest of the bag is
   still empty, and the four derived stats have lost the sentinel and hold the
   bases the rebuild writes with nothing equipped. */
static void ch25u_swaps_the_sword_for_randis(void)
{
    ch25u_stage_armed();

    fdps_chapter_25_event_upgrade_randis_sword(CH25U_RANDIS);

    CHECK_EQ(ch25u_carries(CH25U_RANDIS, CH25U_FLAME_SWORD), 0);
    CHECK_EQ(ch25u_carries(CH25U_RANDIS, CH25U_TRUE_DRAGON_SWORD), 1);
    CHECK_EQ(ch25u_entry_flag(CH25U_RANDIS, 0), CH25U_CARRIED_FLAG);
    CHECK_EQ(ch25u_entry_id(CH25U_RANDIS, 0), CH25U_TRUE_DRAGON_SWORD);
    CHECK_EQ(ch25u_entry_flag(CH25U_RANDIS, 1), CH25U_EMPTY_FLAG);
    CHECK_EQ(ch25u_entry_flag(CH25U_RANDIS, CH25U_INVENTORY_ENTRIES - 1),
             CH25U_EMPTY_FLAG);
    CHECK_EQ((int) ch25u_units[CH25U_RANDIS].ap, CH25U_AP_BASE);
    CHECK_EQ((int) ch25u_units[CH25U_RANDIS].dp, CH25U_DP_BASE);
    CHECK_EQ((int) ch25u_units[CH25U_RANDIS].hit, CH25U_DX_BASE);
    CHECK_EQ((int) ch25u_units[CH25U_RANDIS].ev, CH25U_DX_BASE);
}

/* The item looked for is 0xa1 and nothing else.  A Randis carrying only the
   previous link of the chain, 灼烈之劍, is refused: the search answers -1, the
   second gate closes, and neither his bag nor his stats are touched. */
static void ch25u_only_the_flame_sword_qualifies(void)
{
    ch25u_stage();
    ch25u_carry(CH25U_RANDIS, 0, CH25U_BLAZING_SWORD);
    ch25u_carry(CH25U_RANDIS, 1, CH25U_OTHER_ITEM);

    fdps_chapter_25_event_upgrade_randis_sword(CH25U_RANDIS);

    CHECK_EQ(ch25u_carries(CH25U_RANDIS, CH25U_TRUE_DRAGON_SWORD), 0);
    CHECK_EQ(ch25u_entry_id(CH25U_RANDIS, 0), CH25U_BLAZING_SWORD);
    CHECK_EQ(ch25u_entry_id(CH25U_RANDIS, 1), CH25U_OTHER_ITEM);
    CHECK_EQ(ch25u_stats_untouched(CH25U_RANDIS), 1);
}

/* An empty-handed Randis is refused as well, which is the other way the search
   answers -1. */
static void ch25u_an_empty_bag_is_refused(void)
{
    ch25u_stage();

    fdps_chapter_25_event_upgrade_randis_sword(CH25U_RANDIS);

    CHECK_EQ(ch25u_carries(CH25U_RANDIS, CH25U_TRUE_DRAGON_SWORD), 0);
    CHECK_EQ(ch25u_entry_flag(CH25U_RANDIS, 0), CH25U_EMPTY_FLAG);
    CHECK_EQ(ch25u_stats_untouched(CH25U_RANDIS), 1);
}

/* The removal is given the slot the search returned and not a literal 0: with
   金屬礦 in the first entry and the sword in the second, it is the second that
   is emptied and refilled and the first is left exactly as it was. */
static void ch25u_removes_the_slot_the_search_found(void)
{
    ch25u_stage();
    ch25u_carry(CH25U_RANDIS, 0, CH25U_OTHER_ITEM);
    ch25u_carry(CH25U_RANDIS, 1, CH25U_FLAME_SWORD);

    fdps_chapter_25_event_upgrade_randis_sword(CH25U_RANDIS);

    CHECK_EQ(ch25u_entry_flag(CH25U_RANDIS, 0), CH25U_CARRIED_FLAG);
    CHECK_EQ(ch25u_entry_id(CH25U_RANDIS, 0), CH25U_OTHER_ITEM);
    CHECK_EQ(ch25u_entry_flag(CH25U_RANDIS, 1), CH25U_CARRIED_FLAG);
    CHECK_EQ(ch25u_entry_id(CH25U_RANDIS, 1), CH25U_TRUE_DRAGON_SWORD);
    CHECK_EQ(ch25u_carries(CH25U_RANDIS, CH25U_FLAME_SWORD), 0);
    CHECK_EQ(ch25u_entry_flag(CH25U_RANDIS, 2), CH25U_EMPTY_FLAG);
}

/* The removal runs before the addition, and a full bag is where the two orders
   part company.  All eight entries occupied, the sword in the last of them:
   emptying first leaves the eighth entry free for the add, so Randis ends the
   firing holding 真炎龍劍 and the seven other items.  Adding first would find
   the bag full, store nothing, and then take 火光之劍 away for nothing. */
static void ch25u_removes_before_it_adds(void)
{
    int entry;

    ch25u_stage();
    for (entry = 0; entry < CH25U_INVENTORY_ENTRIES - 1; entry++) {
        ch25u_carry(CH25U_RANDIS, entry, CH25U_OTHER_ITEM);
    }
    ch25u_carry(CH25U_RANDIS, CH25U_INVENTORY_ENTRIES - 1, CH25U_FLAME_SWORD);

    fdps_chapter_25_event_upgrade_randis_sword(CH25U_RANDIS);

    CHECK_EQ(ch25u_carries(CH25U_RANDIS, CH25U_TRUE_DRAGON_SWORD), 1);
    CHECK_EQ(ch25u_carries(CH25U_RANDIS, CH25U_FLAME_SWORD), 0);
    CHECK_EQ(ch25u_entry_flag(CH25U_RANDIS, CH25U_INVENTORY_ENTRIES - 1),
             CH25U_CARRIED_FLAG);
    CHECK_EQ(ch25u_entry_id(CH25U_RANDIS, CH25U_INVENTORY_ENTRIES - 1),
             CH25U_TRUE_DRAGON_SWORD);
    CHECK_EQ(ch25u_entry_id(CH25U_RANDIS, 0), CH25U_OTHER_ITEM);
}

/* No unit but battle unit 0 springs the event, and carrying the sword is not
   what qualifies one: both other staged units carry 火光之劍, neither is given
   真炎龍劍, neither has its stats rebuilt, and Randis's own bag is left alone
   while they walk over the tile. */
static void ch25u_fires_for_no_unit_but_randis(void)
{
    static int other_units[2] = {CH25U_OTHER_UNIT_A, CH25U_OTHER_UNIT_B};
    int i;

    for (i = 0; i < 2; i++) {
        ch25u_stage_armed();

        fdps_chapter_25_event_upgrade_randis_sword(other_units[i]);

        CHECK_EQ(ch25u_carries(other_units[i], CH25U_TRUE_DRAGON_SWORD), 0);
        CHECK_EQ(ch25u_carries(other_units[i], CH25U_FLAME_SWORD), 1);
        CHECK_EQ(ch25u_stats_untouched(other_units[i]), 1);
        CHECK_EQ(ch25u_carries(CH25U_RANDIS, CH25U_FLAME_SWORD), 1);
        CHECK_EQ(ch25u_carries(CH25U_RANDIS, CH25U_TRUE_DRAGON_SWORD), 0);
    }
}

/* There is no latch and no turn deadline.  The shared one-shot byte is up and
   the turn counter is at 99, well past the chapter 20 link's turn 20, and the
   exchange happens anyway; the byte is still exactly as the case left it
   afterwards, so nothing in the body writes it.  Re-arming the bag fires the
   event a second time, which is what "the item is the flag" means. */
static void ch25u_has_no_latch_and_no_deadline(void)
{
    ch25u_stage_armed();

    fdps_chapter_25_event_upgrade_randis_sword(CH25U_RANDIS);

    CHECK_EQ(ch25u_carries(CH25U_RANDIS, CH25U_TRUE_DRAGON_SWORD), 1);
    CHECK_EQ((int) data_fdps_map_cell_event_triggered_flags[CH25U_LATCH_SLOT],
             CH25U_LATCH_RAISED);

    ch25u_stage_armed();
    data_fdps_map_cell_event_triggered_flags[CH25U_LATCH_SLOT] = 0;

    fdps_chapter_25_event_upgrade_randis_sword(CH25U_RANDIS);

    CHECK_EQ(ch25u_carries(CH25U_RANDIS, CH25U_TRUE_DRAGON_SWORD), 1);
    CHECK_EQ((int) data_fdps_map_cell_event_triggered_flags[CH25U_LATCH_SLOT],
             0);
}

/* A second firing on a bag that now holds 真炎龍劍 does nothing: the search
   for 火光之劍 misses, so the exchange cannot run twice off one sword and no
   second 真炎龍劍 appears. */
static void ch25u_does_not_fire_twice_off_one_sword(void)
{
    ch25u_stage_armed();

    fdps_chapter_25_event_upgrade_randis_sword(CH25U_RANDIS);
    ch25u_units[CH25U_RANDIS].ap = CH25U_STAT_SENTINEL;
    ch25u_units[CH25U_RANDIS].dp = CH25U_STAT_SENTINEL;
    ch25u_units[CH25U_RANDIS].hit = CH25U_STAT_SENTINEL;
    ch25u_units[CH25U_RANDIS].ev = CH25U_STAT_SENTINEL;

    fdps_chapter_25_event_upgrade_randis_sword(CH25U_RANDIS);

    CHECK_EQ(ch25u_entry_id(CH25U_RANDIS, 0), CH25U_TRUE_DRAGON_SWORD);
    CHECK_EQ(ch25u_entry_flag(CH25U_RANDIS, 1), CH25U_EMPTY_FLAG);
    CHECK_EQ(ch25u_stats_untouched(CH25U_RANDIS), 1);
}

void run_chevt5_tests(void)
{
    RUN_TEST(ch24t_each_arrival_turn_deploys_its_own_wave);
    RUN_TEST(ch24t_the_wave_is_never_the_turn_number);
    RUN_TEST(ch24t_turn_eight_deploys_nothing);
    RUN_TEST(ch24t_does_nothing_on_a_turn_it_does_not_name);
    RUN_TEST(ch24t_ignores_the_incoming_argument);
    RUN_TEST(ch24t_has_no_one_shot_latch);
    RUN_TEST(ch24t_map_number_comes_from_the_chapter_global);
    RUN_TEST(ch24t_places_on_the_nearest_free_tile);

    RUN_TEST(ch25w1_record_shape_matches_the_offsets);
    RUN_TEST(ch25w1_latch_blocks_the_whole_body);
    RUN_TEST(ch25w1_only_the_player_side_fires);
    RUN_TEST(ch25w1_fires_once_for_the_unit_the_index_names);
    RUN_TEST(ch25w1_map_number_comes_from_the_chapter_global);

    RUN_TEST(ch25u_record_shape_matches_the_offsets);
    RUN_TEST(ch25u_swaps_the_sword_for_randis);
    RUN_TEST(ch25u_only_the_flame_sword_qualifies);
    RUN_TEST(ch25u_an_empty_bag_is_refused);
    RUN_TEST(ch25u_removes_the_slot_the_search_found);
    RUN_TEST(ch25u_removes_before_it_adds);
    RUN_TEST(ch25u_fires_for_no_unit_but_randis);
    RUN_TEST(ch25u_has_no_latch_and_no_deadline);
    RUN_TEST(ch25u_does_not_fire_twice_off_one_sword);
}
