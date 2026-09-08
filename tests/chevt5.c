/* tests/chevt5.c -- cover for src/chevt5.c.
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
#include "testharn.h"
#include "fdpstype.h"
#include "gamedata.h"
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
}
