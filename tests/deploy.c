/* tests/deploy.c -- cover for src/deploy.c.
 *
 * fdps_deploy_unit takes its whole input from globals -- two resident file
 * blocks, the movement grid, the scene layers and the four static data tables
 * -- so every case here publishes its own staging into those globals and then
 * calls the function for real.  Nothing below asserts what any of those
 * globals holds on its own; ticket 23 owns that.
 *
 * Expected values come from the assembly at 000232b0 and from the record
 * layouts ticket 17 settled (src/fdpstype.h), never from the emitted C:
 *
 *   MOV dword ptr [EBP-0x14],0xff at 000232bc          search start distance
 *   IMUL EAX,[EBP+0x14],0x6 / ADD EAX,0xb at 0002330b  placement record
 *   MOVSX EAX,word ptr [EAX] at 00023320               anchor is signed
 *   AND AL,0x40 at 000233b9                            occupancy bit only
 *   CMP EAX,[EBP-0x14] / JLE at 000233e6               ties are accepted
 *   CMP EAX,0x5 / JGE at 00023404                      terrain limit
 *   IMUL EAX,[EBP+0x14],0x1a / ADD EAX,0x83 at 00023457 deployment record
 *   CMP dword ptr [EBP-0x5c],0x3c / JL at 0002348b     roster / enemy split
 *   DEC EAX at 00023564, DEC EDX at 00023587           HP and MP take LV - 1
 *   IMUL at 000235de, 000235f6, 0002361a               AP, DP, DX take LV
 *   CMP EAX,0xff / JZ at 00023693                      missing weapon
 *   CMP EAX,0x2 / JNZ at 000237cf                      player side
 *   INC dword ptr [0x00060150] at 0002381a             the new record is last
 *
 * The unit array is not staged: the function allocates it itself, mallocing a
 * single record when the count is zero and reallocing otherwise, so a case
 * that wants a live unit already on the map deploys one first and lets the
 * second call take the realloc path.
 *
 * The sprite cache slot is not asserted against a literal.  It is whatever
 * fdps_cache_cel_sprite_group hands back for that character, which depends on
 * what earlier tests have already cached, so each case primes the cache itself
 * and compares the record against the slot that call returned.  That reader
 * seeks and reads on the stream unconditionally, so these cases need the real
 * ICON.CEL staged through tests/gamefile.lst and skip themselves when it is
 * not there.
 *
 * fdps_deploy_wave's cases are at the bottom of the file and stage less,
 * because that function opens its own two resources: they publish the
 * deployment table and a map to stand on and let the real FIELD.VFS supply the
 * placement table.  Their own note says where their expected coordinates come
 * from.
 */
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include "testharn.h"
#include "fdpstype.h"
#include "gamedata.h"
#include "rsrc.h"
#include "deploy.h"

/* A three by three battle map: small enough to write every expected scan out
   by hand, wide enough that a row-major walk and a column-major one disagree
   on which cell comes last. */
#define GRID_W 3
#define GRID_H 3

/* Where the two resident blocks put the records the function reads, as the
   assembly forms the addresses. */
#define SPAWN_POS_COORD_BASE 0xb
#define SPAWN_POS_RECORD_STRIDE 6
#define SPAWN_TABLE_RECORD_BASE 0x83

/* The scene layer offsets fdps_map_load_tile_info reads through: the tile
   map's own width word at +7 with its 16-bit ids from +0xb, and the attribute
   table's 4-byte rows from +0x11 (src/maptile.h). */
#define TILE_MAP_WIDTH_OFFSET 7
#define TILE_MAP_IDS_OFFSET 0xb
#define TILE_ATTR_ROWS_OFFSET 0x11

/* The event-code layer's own width word and its first cell byte
   (struct fdps_map_cell_code_layer). */
#define EVENT_LAYER_WIDTH_OFFSET 7
#define EVENT_LAYER_CELLS_OFFSET 0x10

/* A terrain code the search accepts and one it rejects, either side of the
   CMP EAX,0x5 / JGE at 00023404. */
#define TERRAIN_WALKABLE 1
#define TERRAIN_BLOCKED 5

/* Enough tile ids for one distinct id per cell of the map above. */
#define TILE_ATTR_ROWS 16

#define CHAR_TABLE_ROWS 8
#define ENEMY_TABLE_ROWS 8

/* Every item id is a valid index into the staged item table, 0xff included:
   fdps_unit_recompute_combat_stats follows the equipped flag into
   fdps_get_item_record without a bounds check of any kind, and 0xff is a value
   this function really does put behind that flag. */
#define ITEM_TABLE_ROWS 256

#define ITEM_ID_NONE 0xff

static unsigned char stage_grid[4 + GRID_W * GRID_H * 2];
static unsigned char stage_spawn_pos[SPAWN_POS_COORD_BASE + 8 *
                                     SPAWN_POS_RECORD_STRIDE];
static unsigned char stage_spawn_table[SPAWN_TABLE_RECORD_BASE + 8 * 0x1a];
static unsigned char stage_tile_map[TILE_MAP_IDS_OFFSET + GRID_W * GRID_H * 2];
static unsigned char stage_tile_attr[TILE_ATTR_ROWS_OFFSET +
                                     TILE_ATTR_ROWS * 4];
static unsigned char stage_event_layer[EVENT_LAYER_CELLS_OFFSET +
                                       GRID_W * GRID_H];
static struct fdps_character_base_record stage_char_base[CHAR_TABLE_ROWS];
static struct fdps_character_growth stage_growth[CHAR_TABLE_ROWS];
static struct fdps_enemy_data stage_enemy[ENEMY_TABLE_ROWS];
static struct fdps_item_effect stage_items[ITEM_TABLE_ROWS];

/* The map fdps_deploy_wave's cases are played on.  It cannot be the three by
   three above: those cases take their tiles from the real MAP%02d.COD records
   inside FIELD.VFS, which name tiles as far out as (24, 12), and the marking
   pass fdps_deploy_unit runs writes the zone bits at a unit's own tile with no
   bound of any kind -- a unit deployed on (18, 0) writes clean past a
   three-wide grid rather than failing an assertion. */
#define WAVE_W 32
#define WAVE_H 16

static unsigned char stage_wave_grid[4 + WAVE_W * WAVE_H * 2];
static unsigned char stage_wave_tile_map[TILE_MAP_IDS_OFFSET +
                                         WAVE_W * WAVE_H * 2];
static unsigned char stage_wave_event_layer[EVENT_LAYER_CELLS_OFFSET +
                                            WAVE_W * WAVE_H];

/* Where the header of a MAP%02d.DAT block keeps the number of deployment
   records: MOV AL,byte ptr [EAX+0x2] at 000238bb, one unsigned byte. */
#define SPAWN_TABLE_COUNT_OFFSET 2

static int cel_ready = 0;
static int cel_checked = 0;
static int field_ready = 0;
static int field_checked = 0;

#define CEL_NAME "ICON.CEL"
#define FIELD_NAME "FIELD.VFS"

static void zero_bytes(void *block, int count)
{
    unsigned char *bytes;
    int i;

    bytes = (unsigned char *) block;
    for (i = 0; i < count; i++) {
        bytes[i] = 0;
    }
}

/* The reader takes a fixed 0x2970-byte bite out of the sheet's offset table,
   so a file shorter than that is not a smaller fixture -- it is one the reader
   runs off the end of.  Same guard tests/rsrc.c uses, for the same reason. */
static void ensure_cel_sheet(void)
{
    FILE *fp;
    long size;

    if (cel_checked) {
        return;
    }
    cel_checked = 1;
    fp = fopen(CEL_NAME, "rb");
    if (fp == NULL) {
        return;
    }
    fseek(fp, 0, SEEK_END);
    size = ftell(fp);
    fclose(fp);
    if (size >= (long) (15 + 0x2970)) {
        cel_ready = 1;
    }
}

/* fdps_deploy_wave opens the container itself and cannot be told not to, so a
   run without FIELD.VFS next to the executable would not fail a check -- it
   would hang in fdps_wait_any_key waiting for a keyboard interrupt the test
   build never raises.  Every wave case therefore skips itself unless the file
   is there to be opened. */
static void ensure_field_container(void)
{
    FILE *fp;

    if (field_checked) {
        return;
    }
    field_checked = 1;
    fp = fopen(FIELD_NAME, "rb");
    if (fp == NULL) {
        return;
    }
    fclose(fp);
    field_ready = 1;
}

static struct fdps_char_spawn_record *spawn_at(int index)
{
    return (struct fdps_char_spawn_record *)
           (stage_spawn_table + SPAWN_TABLE_RECORD_BASE) + index;
}

static void set_anchor(int index, int tile_x, int tile_y)
{
    short *coords;

    coords = (short *) (stage_spawn_pos + SPAWN_POS_COORD_BASE +
                        index * SPAWN_POS_RECORD_STRIDE);
    coords[0] = (short) tile_x;
    coords[1] = (short) tile_y;
}

static void set_terrain(int tile_id, int terrain)
{
    struct fdps_tile_attr_entry *rows;

    rows = (struct fdps_tile_attr_entry *)
           (stage_tile_attr + TILE_ATTR_ROWS_OFFSET);
    rows[tile_id].terrain_type = (unsigned char) terrain;
}

/* Blank staging with a walkable three by three map, an empty unit array and
   every data table zeroed, published into the globals the function reads.
   Each case then changes only what it is about. */
static void stage(void)
{
    short *tile_ids;
    int tile_x;
    int tile_y;
    int i;

    zero_bytes(stage_grid, (int) sizeof(stage_grid));
    zero_bytes(stage_spawn_pos, (int) sizeof(stage_spawn_pos));
    zero_bytes(stage_spawn_table, (int) sizeof(stage_spawn_table));
    zero_bytes(stage_tile_map, (int) sizeof(stage_tile_map));
    zero_bytes(stage_tile_attr, (int) sizeof(stage_tile_attr));
    zero_bytes(stage_event_layer, (int) sizeof(stage_event_layer));
    zero_bytes(stage_char_base, (int) sizeof(stage_char_base));
    zero_bytes(stage_growth, (int) sizeof(stage_growth));
    zero_bytes(stage_enemy, (int) sizeof(stage_enemy));
    zero_bytes(stage_items, (int) sizeof(stage_items));

    *(short *) stage_grid = (short) GRID_W;
    *(short *) (stage_grid + 2) = (short) GRID_H;

    *(short *) (stage_tile_map + TILE_MAP_WIDTH_OFFSET) = (short) GRID_W;
    tile_ids = (short *) (stage_tile_map + TILE_MAP_IDS_OFFSET);
    for (tile_y = 0; tile_y < GRID_H; tile_y++) {
        for (tile_x = 0; tile_x < GRID_W; tile_x++) {
            tile_ids[tile_y * GRID_W + tile_x] =
                (short) (tile_y * GRID_W + tile_x);
        }
    }
    for (i = 0; i < TILE_ATTR_ROWS; i++) {
        set_terrain(i, TERRAIN_WALKABLE);
    }

    *(short *) (stage_event_layer + EVENT_LAYER_WIDTH_OFFSET) = (short) GRID_W;

    data_fdps_battle_move_grid_ptr = stage_grid;
    data_fdps_map_spawn_pos_table_ptr = stage_spawn_pos;
    data_fdps_tile_event_data_table_ptr = stage_spawn_table;
    data_fdps_scene_layer_tile_map_ptrs[0] = stage_tile_map;
    data_fdps_scene_layer_tile_attr_ptr[0] = stage_tile_attr;
    data_fdps_map_cell_event_code_layer_ptr = stage_event_layer;
    data_fdps_battle_character_base_table_ptr =
        (unsigned char *) stage_char_base;
    data_fdps_battle_character_growth_table_ptr =
        (unsigned char *) stage_growth;
    data_fdps_battle_enemy_data_table_ptr = (unsigned char *) stage_enemy;
    data_fdps_item_effect_table_ptr = (unsigned char *) stage_items;

    data_fdps_map_unit_count = 0;
}

/* The same blank staging on the larger map the wave cases need, with the
   placement table left alone: fdps_deploy_wave loads that one itself out of
   FIELD.VFS and frees it again, and whatever stage() published is overwritten
   before anything reads it. */
static void stage_wave(void)
{
    short *tile_ids;
    int cell_index;

    stage();

    zero_bytes(stage_wave_grid, (int) sizeof(stage_wave_grid));
    zero_bytes(stage_wave_tile_map, (int) sizeof(stage_wave_tile_map));
    zero_bytes(stage_wave_event_layer, (int) sizeof(stage_wave_event_layer));

    *(short *) stage_wave_grid = (short) WAVE_W;
    *(short *) (stage_wave_grid + 2) = (short) WAVE_H;

    *(short *) (stage_wave_tile_map + TILE_MAP_WIDTH_OFFSET) = (short) WAVE_W;
    tile_ids = (short *) (stage_wave_tile_map + TILE_MAP_IDS_OFFSET);
    for (cell_index = 0; cell_index < WAVE_W * WAVE_H; cell_index++) {
        tile_ids[cell_index] = 0;
    }

    *(short *) (stage_wave_event_layer + EVENT_LAYER_WIDTH_OFFSET) =
        (short) WAVE_W;

    data_fdps_battle_move_grid_ptr = stage_wave_grid;
    data_fdps_scene_layer_tile_map_ptrs[0] = stage_wave_tile_map;
    data_fdps_map_cell_event_code_layer_ptr = stage_wave_event_layer;
}

/* Give one cell of the wave map a tile id of its own, so a case can hand that
   id a terrain code the search rejects. */
static void set_wave_tile(int tile_x, int tile_y, int tile_id)
{
    short *tile_ids;

    tile_ids = (short *) (stage_wave_tile_map + TILE_MAP_IDS_OFFSET);
    tile_ids[tile_y * WAVE_W + tile_x] = (short) tile_id;
}

/* One deployment record ready to be placed: a roster character with no
   equipment, so nothing about it varies between the wave cases except the two
   fields each case is actually about. */
static void set_plain_spawn(int index, int char_id, int wave_no)
{
    spawn_at(index)->char_id = (unsigned char) char_id;
    spawn_at(index)->level = 1;
    spawn_at(index)->side = 2;
    spawn_at(index)->equipped_item_0 = ITEM_ID_NONE;
    spawn_at(index)->equipped_item_1 = ITEM_ID_NONE;
    spawn_at(index)->carried_items[0] = ITEM_ID_NONE;
    spawn_at(index)->carried_items[1] = ITEM_ID_NONE;
    spawn_at(index)->carried_items[2] = ITEM_ID_NONE;
    spawn_at(index)->carried_items[3] = ITEM_ID_NONE;
    spawn_at(index)->carried_items[4] = ITEM_ID_NONE;
    spawn_at(index)->carried_items[5] = ITEM_ID_NONE;
    spawn_at(index)->wave_no = (unsigned char) wave_no;
}

/* The record the deployment at index unit_index landed in.  Read through the
   global the function republished, because the array moves on every call. */
static struct fdps_unit_record *deployed(int unit_index)
{
    return (struct fdps_unit_record *)
           (data_fdps_map_unit_array_ptr + unit_index * 0x50);
}

/* The cache slot the sheet's group for char_id already occupies, primed here
   so the record can be compared against it rather than against a literal. */
static int prime_sprite_slot(int char_id, FILE *fp)
{
    return fdps_cache_cel_sprite_group(char_id, fp);
}

/* The record offsets the emitted C reaches through field names have to be the
   ones the assembly writes, or every case below would agree with itself while
   addressing other bytes. */
static void record_offsets_match_the_assembly(void)
{
    CHECK_EQ((int) sizeof(struct fdps_unit_record), 0x50);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, inventory_slots), 0x0a);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, spells_known_bitmap),
             0x1a);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, level), 0x21);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, status_timers), 0x22);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, death_script_opcode),
             0x31);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, ai_behavior), 0x34);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, ap_base), 0x37);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, exp_carry), 0x3c);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, dx_base), 0x3e);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, hp_current), 0x40);
    CHECK_EQ((int) sizeof(struct fdps_char_spawn_record), 0x1a);
    CHECK_EQ((int) offsetof(struct fdps_char_spawn_record, level), 4);
    CHECK_EQ((int) offsetof(struct fdps_char_spawn_record, spell_mask), 0x0d);
    CHECK_EQ((int) offsetof(struct fdps_char_spawn_record, wave_no), 0x15);
}

/* place_exact non-zero puts the unit on the placement record's tile untouched,
   the record lands at the index the count held on entry, and the count is one
   larger afterwards (INC dword ptr [0x00060150] at 0002381a). */
static void exact_placement_uses_the_anchor(void)
{
    FILE *fp;
    struct fdps_unit_record *unit;
    int slot;

    ensure_cel_sheet();
    if (!cel_ready) {
        return;
    }
    stage();
    set_anchor(0, 2, 1);
    spawn_at(0)->char_id = 5;
    spawn_at(0)->level = 1;
    spawn_at(0)->side = 2;
    spawn_at(0)->equipped_item_0 = ITEM_ID_NONE;
    spawn_at(0)->equipped_item_1 = ITEM_ID_NONE;

    fp = fopen(CEL_NAME, "rb");
    slot = prime_sprite_slot(5, fp);
    fdps_deploy_unit(0, fp, 1);
    fclose(fp);

    CHECK_EQ(data_fdps_map_unit_count, 1);
    unit = deployed(0);
    CHECK_EQ((int) unit->pos_x, 2);
    CHECK_EQ((int) unit->pos_y, 1);
    CHECK_EQ((int) unit->sprite_cache_slot, slot);
    CHECK_EQ((int) unit->facing, 0);
    CHECK_EQ((int) unit->walk_step, 0);
    CHECK_EQ((int) unit->flags, 0);
    CHECK_EQ((int) unit->side, 2);
    CHECK_EQ((int) unit->portrait_id, 5);
    CHECK_EQ((int) unit->char_id, 5);
    CHECK_EQ((int) unit->reserved_09, 0);
}

/* A character id below 0x3c takes the FRIAPRDA.DAT base record and the
   FRILEVUP.DAT growth record, and the two scalings differ: HP and MP gain
   level - 1 steps, attack, defense and dexterity gain a full level of them.
   Level 4 with base 20 and hp_min 3 is 20 + 3 * 3 = 29, while base 7 with
   ap_min 2 is 7 + 2 * 4 = 15 -- the numbers separate the two scalings.

   The four derived combat stats are checked as well: the item table is all
   zeroes, so fdps_unit_recompute_combat_stats can only reproduce the bases it
   was handed, and that is what says it ran on the NEW record's index. */
static void roster_character_growth_is_asymmetric(void)
{
    FILE *fp;
    struct fdps_unit_record *unit;

    ensure_cel_sheet();
    if (!cel_ready) {
        return;
    }
    stage();
    set_anchor(0, 0, 0);
    spawn_at(0)->char_id = 5;
    spawn_at(0)->level = 4;
    spawn_at(0)->side = 2;
    spawn_at(0)->equipped_item_0 = ITEM_ID_NONE;
    spawn_at(0)->equipped_item_1 = ITEM_ID_NONE;

    stage_char_base[5].race_id = 3;
    stage_char_base[5].class_id = 9;
    stage_char_base[5].move = 6;
    stage_char_base[5].hp_base = 20;
    stage_char_base[5].mp_base = 10;
    stage_char_base[5].ap_base = 7;
    stage_char_base[5].dp_base = 6;
    stage_char_base[5].dx_base = 9;
    stage_growth[5].hp_min = 3;
    stage_growth[5].mp_min = 2;
    stage_growth[5].ap_min = 2;
    stage_growth[5].dp_min = 1;
    stage_growth[5].dx_min = 3;

    fp = fopen(CEL_NAME, "rb");
    fdps_deploy_unit(0, fp, 1);
    fclose(fp);

    unit = deployed(0);
    CHECK_EQ((int) unit->race, 3);
    CHECK_EQ((int) unit->clazz, 9);
    CHECK_EQ((int) unit->move, 6);
    CHECK_EQ((int) unit->level, 4);
    CHECK_EQ((int) unit->hp_current, 29);
    CHECK_EQ((int) unit->hp_max, 29);
    CHECK_EQ((int) unit->mp_current, 16);
    CHECK_EQ((int) unit->mp_max, 16);
    CHECK_EQ((int) unit->ap_base, 15);
    CHECK_EQ((int) unit->dp_base, 10);
    CHECK_EQ((int) unit->dx_base, 21);
    CHECK_EQ((int) unit->ap, 15);
    CHECK_EQ((int) unit->dp, 10);
    CHECK_EQ((int) unit->hit, 21);
    CHECK_EQ((int) unit->ev, 21);
}

/* At 0x3c and above the id indexes ENEMYDAT.DAT after 0x3c is subtracted, and
   every stat is a flat multiple of the level with no base term and no
   level - 1 anywhere.  Level 3 against coefficients 7, 4, 5, 6, 8 is
   21, 12, 15, 18, 24. */
static void enemy_stats_scale_flat_with_level(void)
{
    FILE *fp;
    struct fdps_unit_record *unit;

    ensure_cel_sheet();
    if (!cel_ready) {
        return;
    }
    stage();
    set_anchor(0, 0, 0);
    spawn_at(0)->char_id = 0x3c + 2;
    spawn_at(0)->level = 3;
    spawn_at(0)->side = 0;
    spawn_at(0)->equipped_item_0 = ITEM_ID_NONE;
    spawn_at(0)->equipped_item_1 = ITEM_ID_NONE;

    stage_enemy[2].race_id = 4;
    stage_enemy[2].class_id = 11;
    stage_enemy[2].hp = 7;
    stage_enemy[2].mp = 4;
    stage_enemy[2].ap = 5;
    stage_enemy[2].dp = 6;
    stage_enemy[2].dx = 8;
    stage_enemy[2].mv = 5;

    fp = fopen(CEL_NAME, "rb");
    fdps_deploy_unit(0, fp, 1);
    fclose(fp);

    unit = deployed(0);
    CHECK_EQ((int) unit->race, 4);
    CHECK_EQ((int) unit->clazz, 11);
    CHECK_EQ((int) unit->move, 5);
    CHECK_EQ((int) unit->hp_current, 21);
    CHECK_EQ((int) unit->hp_max, 21);
    CHECK_EQ((int) unit->mp_current, 12);
    CHECK_EQ((int) unit->mp_max, 12);
    CHECK_EQ((int) unit->ap_base, 15);
    CHECK_EQ((int) unit->dp_base, 18);
    CHECK_EQ((int) unit->dx_base, 24);
    CHECK_EQ((int) unit->char_id, 0x3e);
}

/* The two equipment bytes go into slots 0 and 1 flagged 0x40 without their
   values being looked at, and the six carried bytes into slots 2..7 flagged 0
   when the id is present and 0x80 when it is 0xff -- the id byte is written
   either way. */
static void inventory_flags_and_ids(void)
{
    FILE *fp;
    struct fdps_unit_record *unit;

    ensure_cel_sheet();
    if (!cel_ready) {
        return;
    }
    stage();
    set_anchor(0, 0, 0);
    spawn_at(0)->char_id = 5;
    spawn_at(0)->level = 1;
    spawn_at(0)->side = 2;
    spawn_at(0)->equipped_item_0 = 0x11;
    spawn_at(0)->equipped_item_1 = 0x12;
    spawn_at(0)->carried_items[0] = 0x20;
    spawn_at(0)->carried_items[1] = ITEM_ID_NONE;
    spawn_at(0)->carried_items[2] = 0x21;
    spawn_at(0)->carried_items[3] = ITEM_ID_NONE;
    spawn_at(0)->carried_items[4] = ITEM_ID_NONE;
    spawn_at(0)->carried_items[5] = 0x22;

    fp = fopen(CEL_NAME, "rb");
    fdps_deploy_unit(0, fp, 1);
    fclose(fp);

    unit = deployed(0);
    CHECK_EQ((int) unit->inventory_slots[0], 0x40);
    CHECK_EQ((int) unit->inventory_slots[1], 0x11);
    CHECK_EQ((int) unit->inventory_slots[2], 0x40);
    CHECK_EQ((int) unit->inventory_slots[3], 0x12);
    CHECK_EQ((int) unit->inventory_slots[4], 0x00);
    CHECK_EQ((int) unit->inventory_slots[5], 0x20);
    CHECK_EQ((int) unit->inventory_slots[6], 0x80);
    CHECK_EQ((int) unit->inventory_slots[7], 0xff);
    CHECK_EQ((int) unit->inventory_slots[8], 0x00);
    CHECK_EQ((int) unit->inventory_slots[9], 0x21);
    CHECK_EQ((int) unit->inventory_slots[10], 0x80);
    CHECK_EQ((int) unit->inventory_slots[11], 0xff);
    CHECK_EQ((int) unit->inventory_slots[12], 0x80);
    CHECK_EQ((int) unit->inventory_slots[13], 0xff);
    CHECK_EQ((int) unit->inventory_slots[14], 0x00);
    CHECK_EQ((int) unit->inventory_slots[15], 0x22);
}

/* When the first equipment byte is 0xff the SECOND item moves into slot 0 and
   is still flagged equipped, and slot 1 is flagged empty.  Slot 1's id byte is
   deliberately not asserted: that branch never writes it, so it holds whatever
   the allocator returned.

   Slot 0 keeping the equipped flag is what sends the recompute into the item
   table with whatever id landed there, so the second half of this case gives
   item 0x13 an attack bonus and checks that it reached the record. */
static void missing_weapon_moves_the_second_item_into_slot_0(void)
{
    FILE *fp;
    struct fdps_unit_record *unit;

    ensure_cel_sheet();
    if (!cel_ready) {
        return;
    }
    stage();
    set_anchor(0, 0, 0);
    spawn_at(0)->char_id = 5;
    spawn_at(0)->level = 1;
    spawn_at(0)->side = 2;
    spawn_at(0)->equipped_item_0 = ITEM_ID_NONE;
    spawn_at(0)->equipped_item_1 = 0x13;
    spawn_at(0)->carried_items[0] = ITEM_ID_NONE;
    spawn_at(0)->carried_items[1] = ITEM_ID_NONE;
    spawn_at(0)->carried_items[2] = ITEM_ID_NONE;
    spawn_at(0)->carried_items[3] = ITEM_ID_NONE;
    spawn_at(0)->carried_items[4] = ITEM_ID_NONE;
    spawn_at(0)->carried_items[5] = ITEM_ID_NONE;
    stage_char_base[5].ap_base = 4;
    stage_items[0x13].ap = 6;

    fp = fopen(CEL_NAME, "rb");
    fdps_deploy_unit(0, fp, 1);
    fclose(fp);

    unit = deployed(0);
    CHECK_EQ((int) unit->inventory_slots[0], 0x40);
    CHECK_EQ((int) unit->inventory_slots[1], 0x13);
    CHECK_EQ((int) unit->inventory_slots[2], 0x80);
    CHECK_EQ((int) unit->ap_base, 4);
    CHECK_EQ((int) unit->ap, 10);
}

/* The four spell-mask bytes are moved as a block and the fifth comes from a
   separate byte at the far end of the deployment record; the status timers are
   cleared; and the AI, death-script and event fields are copied one for one. */
static void spell_ai_and_script_fields_are_copied(void)
{
    FILE *fp;
    struct fdps_unit_record *unit;

    ensure_cel_sheet();
    if (!cel_ready) {
        return;
    }
    stage();
    set_anchor(0, 0, 0);
    spawn_at(0)->char_id = 5;
    spawn_at(0)->level = 7;
    spawn_at(0)->side = 2;
    spawn_at(0)->equipped_item_0 = ITEM_ID_NONE;
    spawn_at(0)->equipped_item_1 = ITEM_ID_NONE;
    spawn_at(0)->spell_mask[0] = 0x81;
    spawn_at(0)->spell_mask[1] = 0x42;
    spawn_at(0)->spell_mask[2] = 0x24;
    spawn_at(0)->spell_mask[3] = 0x18;
    spawn_at(0)->spell_mask_high = 0x7e;
    spawn_at(0)->ai_class = 0x23;
    spawn_at(0)->ai_dest_x = 9;
    spawn_at(0)->ai_dest_y = 4;
    spawn_at(0)->cell_event_code = 0x0c;
    spawn_at(0)->death_script_opcode = 0x0a;
    spawn_at(0)->death_script_operand = -3;

    fp = fopen(CEL_NAME, "rb");
    fdps_deploy_unit(0, fp, 1);
    fclose(fp);

    unit = deployed(0);
    CHECK_EQ((int) unit->spells_known_bitmap[0], 0x81);
    CHECK_EQ((int) unit->spells_known_bitmap[1], 0x42);
    CHECK_EQ((int) unit->spells_known_bitmap[2], 0x24);
    CHECK_EQ((int) unit->spells_known_bitmap[3], 0x18);
    CHECK_EQ((int) unit->spells_known_bitmap[4], 0x7e);
    CHECK_EQ((int) unit->status_timers[0], 0);
    CHECK_EQ((int) unit->status_timers[5], 0);
    CHECK_EQ((int) unit->level, 7);
    CHECK_EQ((int) unit->ai_behavior, 0x23);
    CHECK_EQ((int) unit->ai_dest_x, 9);
    CHECK_EQ((int) unit->ai_dest_y, 4);
    CHECK_EQ((int) unit->event_slot, 0x0c);
    CHECK_EQ((int) unit->death_script_opcode, 0x0a);
    CHECK_EQ((int) unit->death_script_operand, -3);
}

/* Record +0x3c takes 0 only when the side byte the record already holds is 2,
   and 0xff for every other side -- the test is on the record's own byte, not
   on the deployment record's. */
static void exp_carry_marks_the_player_side(void)
{
    FILE *fp;

    ensure_cel_sheet();
    if (!cel_ready) {
        return;
    }
    stage();
    set_anchor(0, 0, 0);
    set_anchor(1, 1, 0);
    set_anchor(2, 2, 0);
    spawn_at(0)->char_id = 5;
    spawn_at(0)->level = 1;
    spawn_at(0)->side = 2;
    spawn_at(0)->equipped_item_0 = ITEM_ID_NONE;
    spawn_at(0)->equipped_item_1 = ITEM_ID_NONE;
    spawn_at(1)->char_id = 5;
    spawn_at(1)->level = 1;
    spawn_at(1)->side = 0;
    spawn_at(1)->equipped_item_0 = ITEM_ID_NONE;
    spawn_at(1)->equipped_item_1 = ITEM_ID_NONE;
    spawn_at(2)->char_id = 5;
    spawn_at(2)->level = 1;
    spawn_at(2)->side = 1;
    spawn_at(2)->equipped_item_0 = ITEM_ID_NONE;
    spawn_at(2)->equipped_item_1 = ITEM_ID_NONE;

    fp = fopen(CEL_NAME, "rb");
    fdps_deploy_unit(0, fp, 1);
    fdps_deploy_unit(1, fp, 1);
    fdps_deploy_unit(2, fp, 1);
    fclose(fp);

    CHECK_EQ(data_fdps_map_unit_count, 3);
    CHECK_EQ((int) deployed(0)->exp_carry, 0x00);
    CHECK_EQ((int) deployed(1)->exp_carry, 0xff);
    CHECK_EQ((int) deployed(2)->exp_carry, 0xff);
    CHECK_EQ((int) deployed(1)->pos_x, 1);
    CHECK_EQ((int) deployed(2)->pos_x, 2);
}

/* The nearest-tile search, with a live unit standing on the anchor.
 *
 * Grid 3x3, anchor (1, 1), everything walkable, one unit already deployed on
 * (1, 1) with side 0 so the second marking pass puts 0x40 on its tile and 0x80
 * on its four neighbours.  Manhattan distances are
 *
 *     2 1 2        scan order is row-major, and the four cells at distance 1
 *     1 * 1        are reached at (1,0), (0,1), (2,1) and (1,2)
 *     2 1 2
 *
 * so the JLE at 000233e6 keeps replacing the winner on every tie and the last
 * one, (1, 2), is what the unit gets.  A search written with a strict less-than
 * would stop at (1, 0), and one that also rejected the 0x80 zone-of-control bit
 * would have no candidate at distance 1 at all and land on a corner. */
static void search_keeps_the_last_tile_at_the_winning_distance(void)
{
    FILE *fp;

    ensure_cel_sheet();
    if (!cel_ready) {
        return;
    }
    stage();
    set_anchor(0, 1, 1);
    set_anchor(1, 1, 1);
    spawn_at(0)->char_id = 5;
    spawn_at(0)->level = 1;
    spawn_at(0)->side = 0;
    spawn_at(0)->equipped_item_0 = ITEM_ID_NONE;
    spawn_at(0)->equipped_item_1 = ITEM_ID_NONE;
    spawn_at(1)->char_id = 6;
    spawn_at(1)->level = 1;
    spawn_at(1)->side = 2;
    spawn_at(1)->equipped_item_0 = ITEM_ID_NONE;
    spawn_at(1)->equipped_item_1 = ITEM_ID_NONE;

    fp = fopen(CEL_NAME, "rb");
    fdps_deploy_unit(0, fp, 1);
    fdps_deploy_unit(1, fp, 0);
    fclose(fp);

    CHECK_EQ(data_fdps_map_unit_count, 2);
    CHECK_EQ((int) deployed(0)->pos_x, 1);
    CHECK_EQ((int) deployed(0)->pos_y, 1);
    CHECK_EQ((int) deployed(1)->pos_x, 1);
    CHECK_EQ((int) deployed(1)->pos_y, 2);
}

/* The same search against terrain instead of occupancy.  Anchor (0, 0) with
 * nothing on the map, and tile id 0 -- the anchor's own cell -- given terrain
 * 5, which the JGE at 00023404 rejects.  Distances from (0,0) are
 *
 *     x 1 2        (0,0) is rejected on terrain, (1,0) and (0,1) both sit at
 *     1 2 3        distance 1, and the tie again goes to the later of the two
 *     2 3 4
 *
 * so the unit lands on (0, 1).  Rejecting on terrain 5 is what keeps it off
 * (0, 0); the tie rule is what keeps it off (1, 0). */
static void search_rejects_unwalkable_terrain(void)
{
    FILE *fp;

    ensure_cel_sheet();
    if (!cel_ready) {
        return;
    }
    stage();
    set_anchor(0, 0, 0);
    set_terrain(0, TERRAIN_BLOCKED);
    spawn_at(0)->char_id = 5;
    spawn_at(0)->level = 1;
    spawn_at(0)->side = 2;
    spawn_at(0)->equipped_item_0 = ITEM_ID_NONE;
    spawn_at(0)->equipped_item_1 = ITEM_ID_NONE;

    fp = fopen(CEL_NAME, "rb");
    fdps_deploy_unit(0, fp, 0);
    fclose(fp);

    CHECK_EQ((int) deployed(0)->pos_x, 0);
    CHECK_EQ((int) deployed(0)->pos_y, 1);
}

/* The anchor coordinates arrive through MOVSX and are signed.  An anchor one
 * row above the map, (1, -1), leaves distances
 *
 *     1 0 1  + 1 for the row      so (1, 0) alone sits at distance 1 and
 *     1 0 1  + 2 for the row      nothing later ties it
 *     1 0 1  + 3 for the row
 *
 * and the unit lands on (1, 0).  Read as unsigned, that -1 is 65535: every
 * distance then exceeds the 0xff the search starts from, the accept branch
 * never runs and the tile the unit is placed on is not the one below. */
static void anchor_coordinates_are_signed(void)
{
    FILE *fp;

    ensure_cel_sheet();
    if (!cel_ready) {
        return;
    }
    stage();
    set_anchor(0, 1, -1);
    spawn_at(0)->char_id = 5;
    spawn_at(0)->level = 1;
    spawn_at(0)->side = 2;
    spawn_at(0)->equipped_item_0 = ITEM_ID_NONE;
    spawn_at(0)->equipped_item_1 = ITEM_ID_NONE;

    fp = fopen(CEL_NAME, "rb");
    fdps_deploy_unit(0, fp, 0);
    fclose(fp);

    CHECK_EQ((int) deployed(0)->pos_x, 1);
    CHECK_EQ((int) deployed(0)->pos_y, 0);
}

/* fdps_deploy_wave, from here down.
 *
 * These cases run the function whole -- it opens ICON.CEL and FIELD.VFS for
 * itself, loads MAP%02d.COD out of the container and frees both again -- so the
 * placement records they expect are the ones in the shipped container:
 *
 *   MAP00.COD  record 0 (18, 0)  record 1 (22, 12)  record 2 (8, 10)
 *   MAP01.COD  record 0 (9, 4)
 *
 * read at base + 0xb + index * 6 as two signed 16-bit values, which is the
 * arithmetic at 0002330b..00023320 in fdps_deploy_unit.
 */

/* The walk hands fdps_deploy_unit the record's OWN index, not a count of the
 * ones that matched, and that index selects both tables at once.  Records
 * tagged 0, 1, 0, 2 with the wave 0 asked for leave records 0 and 2 deployed,
 * in that order, carrying record 0's and record 2's character ids AND record
 * 0's and record 2's placement coordinates.  A walk that passed a compacted
 * counter would put the second unit on record 1's tile with record 1's
 * character.
 *
 * The placement table is nulled on the way out (MOV dword ptr [0x00060140],0x0
 * at 00023928), after the free -- it is live only for the span of the call.
 */
static void wave_deploys_only_the_records_tagged_with_it(void)
{
    ensure_cel_sheet();
    ensure_field_container();
    if (!cel_ready || !field_ready) {
        return;
    }
    stage_wave();
    stage_spawn_table[SPAWN_TABLE_COUNT_OFFSET] = 4;
    set_plain_spawn(0, 5, 0);
    set_plain_spawn(1, 6, 1);
    set_plain_spawn(2, 7, 0);
    set_plain_spawn(3, 8, 2);

    fdps_deploy_wave(0, 0, 1);

    CHECK_EQ(data_fdps_map_unit_count, 2);
    CHECK_EQ((int) deployed(0)->char_id, 5);
    CHECK_EQ((int) deployed(1)->char_id, 7);
    CHECK_EQ((int) deployed(0)->pos_x, 18);
    CHECK_EQ((int) deployed(0)->pos_y, 0);
    CHECK_EQ((int) deployed(1)->pos_x, 8);
    CHECK_EQ((int) deployed(1)->pos_y, 10);
    CHECK_EQ(data_fdps_map_spawn_pos_table_ptr == NULL, 1);
}

/* The header byte at +2 of the MAP%02d.DAT block is the whole bound on the
 * walk: four records all tagged wave 0, a header saying two, and the last two
 * are never looked at. */
static void header_count_bounds_the_walk(void)
{
    ensure_cel_sheet();
    ensure_field_container();
    if (!cel_ready || !field_ready) {
        return;
    }
    stage_wave();
    stage_spawn_table[SPAWN_TABLE_COUNT_OFFSET] = 2;
    set_plain_spawn(0, 5, 0);
    set_plain_spawn(1, 6, 0);
    set_plain_spawn(2, 7, 0);
    set_plain_spawn(3, 8, 0);

    fdps_deploy_wave(0, 0, 1);

    CHECK_EQ(data_fdps_map_unit_count, 2);
    CHECK_EQ((int) deployed(0)->char_id, 5);
    CHECK_EQ((int) deployed(1)->char_id, 6);

    /* And a header of zero walks nothing at all, without that being an error:
       the container is still opened, the placement table still loaded and
       still released. */
    stage_wave();
    stage_spawn_table[SPAWN_TABLE_COUNT_OFFSET] = 0;
    set_plain_spawn(0, 5, 0);

    fdps_deploy_wave(0, 0, 1);

    CHECK_EQ(data_fdps_map_unit_count, 0);
    CHECK_EQ(data_fdps_map_spawn_pos_table_ptr == NULL, 1);
}

/* Contract C on the wave tag, in both directions.
 *
 * The tag is one unsigned byte zero-extended into a full dword before the
 * compare (MOV AL,byte ptr [EAX+0x15] / AND EAX,0xff / CMP EAX,[EBP+0x18] at
 * 000238e9..000238f1), so a tag of 0x80 is 128 and matches a wave_no of 128 --
 * read as a signed char it would be -128 and match nothing the callers pass.
 *
 * And the compare is 32 bits wide, so a wave_no of 256 matches no record at
 * all.  Compared a byte at a time, 256 would truncate to 0 and deploy the
 * whole opening wave. */
static void wave_tag_is_an_unsigned_byte_compared_full_width(void)
{
    ensure_cel_sheet();
    ensure_field_container();
    if (!cel_ready || !field_ready) {
        return;
    }
    stage_wave();
    stage_spawn_table[SPAWN_TABLE_COUNT_OFFSET] = 2;
    set_plain_spawn(0, 5, 0);
    set_plain_spawn(1, 6, 0x80);

    fdps_deploy_wave(0, 0x80, 1);

    CHECK_EQ(data_fdps_map_unit_count, 1);
    CHECK_EQ((int) deployed(0)->char_id, 6);

    stage_wave();
    stage_spawn_table[SPAWN_TABLE_COUNT_OFFSET] = 2;
    set_plain_spawn(0, 5, 0);
    set_plain_spawn(1, 6, 1);

    fdps_deploy_wave(0, 256, 1);

    CHECK_EQ(data_fdps_map_unit_count, 0);
}

/* map_no picks the placement file through "map%02d.cod", so the same
 * deployment record placed under map 1 lands on MAP01.COD's coordinates and
 * not on MAP00.COD's. */
static void map_number_picks_the_placement_file(void)
{
    ensure_cel_sheet();
    ensure_field_container();
    if (!cel_ready || !field_ready) {
        return;
    }
    stage_wave();
    stage_spawn_table[SPAWN_TABLE_COUNT_OFFSET] = 1;
    set_plain_spawn(0, 5, 0);

    fdps_deploy_wave(1, 0, 1);

    CHECK_EQ(data_fdps_map_unit_count, 1);
    CHECK_EQ((int) deployed(0)->pos_x, 9);
    CHECK_EQ((int) deployed(0)->pos_y, 4);
}

/* A wave number no record carries is not an error and not a short circuit: the
 * walk runs the whole table, matches nothing, and the open, the load and the
 * release all still happen. */
static void a_wave_nothing_matches_still_releases_the_table(void)
{
    ensure_cel_sheet();
    ensure_field_container();
    if (!cel_ready || !field_ready) {
        return;
    }
    stage_wave();
    stage_spawn_table[SPAWN_TABLE_COUNT_OFFSET] = 2;
    set_plain_spawn(0, 5, 1);
    set_plain_spawn(1, 6, 2);

    fdps_deploy_wave(0, 5, 1);

    CHECK_EQ(data_fdps_map_unit_count, 0);
    CHECK_EQ(data_fdps_map_spawn_pos_table_ptr == NULL, 1);
}

/* Waves accumulate.  Nothing here clears the unit array, so the second wave is
 * appended behind the first and the count is the total.  The second call also
 * proves the placement table is reloaded rather than remembered: the first call
 * nulled the pointer, so a second wave that did not load again would place its
 * unit through a null table. */
static void a_second_wave_appends_to_the_first(void)
{
    ensure_cel_sheet();
    ensure_field_container();
    if (!cel_ready || !field_ready) {
        return;
    }
    stage_wave();
    stage_spawn_table[SPAWN_TABLE_COUNT_OFFSET] = 2;
    set_plain_spawn(0, 5, 0);
    set_plain_spawn(1, 6, 1);

    fdps_deploy_wave(0, 0, 1);
    CHECK_EQ(data_fdps_map_unit_count, 1);

    fdps_deploy_wave(0, 1, 1);

    CHECK_EQ(data_fdps_map_unit_count, 2);
    CHECK_EQ((int) deployed(0)->char_id, 5);
    CHECK_EQ((int) deployed(1)->char_id, 6);
    CHECK_EQ((int) deployed(1)->pos_x, 22);
    CHECK_EQ((int) deployed(1)->pos_y, 12);
}

/* place_exact reaches fdps_deploy_unit as it was handed in, which only shows
 * on an anchor the search would refuse.  Record 0's tile in MAP00.COD is
 * (18, 0); give that one cell a tile id whose terrain code is 5, the value the
 * JGE at 00023404 rejects.
 *
 * Non-zero puts the unit on (18, 0) anyway, terrain and all.  Zero searches:
 * the anchor is out, the three cells at distance 1 are (17, 0), (19, 0) and
 * (18, 1), and the row-major scan with its accept-on-tie takes the last of
 * them.  A pass-through replaced by a constant would answer the same in one of
 * the two calls and not in both. */
static void place_exact_reaches_the_deployment(void)
{
    ensure_cel_sheet();
    ensure_field_container();
    if (!cel_ready || !field_ready) {
        return;
    }
    stage_wave();
    stage_spawn_table[SPAWN_TABLE_COUNT_OFFSET] = 1;
    set_plain_spawn(0, 5, 0);
    set_wave_tile(18, 0, 1);
    set_terrain(1, TERRAIN_BLOCKED);

    fdps_deploy_wave(0, 0, 1);

    CHECK_EQ((int) deployed(0)->pos_x, 18);
    CHECK_EQ((int) deployed(0)->pos_y, 0);

    stage_wave();
    stage_spawn_table[SPAWN_TABLE_COUNT_OFFSET] = 1;
    set_plain_spawn(0, 5, 0);
    set_wave_tile(18, 0, 1);
    set_terrain(1, TERRAIN_BLOCKED);

    fdps_deploy_wave(0, 0, 0);

    CHECK_EQ((int) deployed(0)->pos_x, 18);
    CHECK_EQ((int) deployed(0)->pos_y, 1);
}

/* fdps_build_map_unit_array at 00022be0, from here down.
 *
 * This one is run whole against the shipped containers, like the wave cases
 * above and for the same reason: its first line is
 * fdps_field_load_chapter_resources, which formats seven member names out of
 * data_fdps_chapter_current_chapter_id and hands each to fdps_vfs_load_entry,
 * and a member that is not there ends the process rather than failing a check.
 * FIELD.VFS, FIELD1.VFS and FIELD2.VFS are all staged through
 * tests/gamefile.lst; every case here skips itself unless all three, and
 * ICON.CEL, are next to the executable.
 *
 * What the cases can control is the party: the roster block, the roster member
 * count and the four static tables are published from here, so the two counts
 * the loop runs on are the only inputs that come from the shipped files.
 *
 * Every expected number below is a byte of a shipped resource, read with
 * tools/vfs_dump, or the layout the assembly at 00022be0 imposes on it:
 *
 *   MAP00.DAT bytes +1 and +2 are 1 and 22, MAP05.DAT's are 4 and 32 -- the
 *     player slot count and the scripted deployment count of each map
 *   MAP00.COD is 147 bytes: a 9-byte header and 23 six-byte records, which is
 *     22 scripted deployments plus 1 player slot.  Read at +0xb + index * 6 as
 *     two signed 16-bit values, record 0 is (18, 0) and record 22 is (16, 22)
 *   MAP05.COD is 225 bytes: 36 records, 32 plus 4.  Records 32..35 are
 *     (4, 8), (4, 10), (2, 10) and (3, 10)
 *   MAP05.DAT tags exactly two of its 32 deployment records with wave 0 --
 *     record 0, character 98 on MAP05.COD record 0 at (15, 4), and record 31,
 *     character 12 at (3, 8).  MAP00.DAT tags none of its 22 with wave 0
 *
 * The chapter 0 cases are therefore the ones that isolate this function: with
 * no wave-0 record in the map, nothing fdps_deploy_wave does can account for
 * what ends up in the array.
 */

#define CH_ONE_SLOT 0
#define CH_FOUR_SLOT 5

#define CH0_PLAYER_SLOTS 1
#define CH0_CHAR_SPAWNS 22
#define CH0_PARTY_TILE_X 16
#define CH0_PARTY_TILE_Y 22
#define CH0_SCRIPTED_TILE_X 18
#define CH0_SCRIPTED_TILE_Y 0

#define CH5_PLAYER_SLOTS 4
#define CH5_CHAR_SPAWNS 32
#define CH5_WAVE0_UNITS 2
#define CH5_PARTY0_TILE_X 4
#define CH5_PARTY0_TILE_Y 8
#define CH5_PARTY1_TILE_X 4
#define CH5_PARTY1_TILE_Y 10
#define CH5_WAVE0_CHAR_A 98
#define CH5_WAVE0_TILE_A_X 15
#define CH5_WAVE0_TILE_A_Y 4
#define CH5_WAVE0_CHAR_B 12
#define CH5_WAVE0_TILE_B_X 3
#define CH5_WAVE0_TILE_B_Y 8

#define FIELD1_NAME "FIELD1.VFS"
#define FIELD2_NAME "FIELD2.VFS"

/* The party the cases stage.  Every field carries a value the function either
   has to keep or has to overwrite, so which of the two it did is visible in
   the record afterwards:
     char_id and the death-script operand are kept, and say the memmove ran
     the position, facing, walk step, flags, side and script opcode are
       overwritten with the map's own facts
     hp_current and mp_current are below their maxima and come back equal to
       them
     the six status timers are non-zero and come back cleared, which the four
       derived stats then measure a second time -- 0x3e7 is not any sum of the
       staged bases, and a timer left running would scale the two totals
   Both members share one portrait id and carry different character ids, which
   is what separates record +7 from record +8. */
#define PARTY_PORTRAIT 3
#define PARTY_CHAR_ID_BASE 0x30
#define PARTY_LEVEL_BASE 3
#define PARTY_AP_BASE 11
#define PARTY_DP_BASE 12
#define PARTY_DX_BASE 13
#define PARTY_HP_MAX_BASE 50
#define PARTY_MP_MAX_BASE 20
#define PARTY_STALE_DERIVED 0x3e7
#define PARTY_STALE_TIMER 5
#define PARTY_DEATH_OPERAND 0x2222
#define DEATH_SCRIPT_NONE 0xff
#define UNIT_FLAG_RETIRED 1
#define PLAYER_SIDE 2

/* Wide enough for every character and enemy id the two maps' wave-0 records
   name: character 12 indexes the roster tables directly, character 98 indexes
   the enemy table at 98 - 60. */
#define BIG_TABLE_ROWS 64

#define PARTY_SLOTS 8

static struct fdps_unit_record stage_party[PARTY_SLOTS];
static struct fdps_character_base_record stage_big_char[BIG_TABLE_ROWS];
static struct fdps_character_growth stage_big_growth[BIG_TABLE_ROWS];
static struct fdps_enemy_data stage_big_enemy[BIG_TABLE_ROWS];

static int chapter_ready = 0;
static int chapter_checked = 0;

static void ensure_chapter_containers(void)
{
    FILE *fp;

    if (chapter_checked) {
        return;
    }
    chapter_checked = 1;
    ensure_cel_sheet();
    ensure_field_container();
    if (!cel_ready || !field_ready) {
        return;
    }
    fp = fopen(FIELD1_NAME, "rb");
    if (fp == NULL) {
        return;
    }
    fclose(fp);
    fp = fopen(FIELD2_NAME, "rb");
    if (fp == NULL) {
        return;
    }
    fclose(fp);
    chapter_ready = 1;
}

/* Nulling, not freeing.  Every case above this one points the scene layers,
   the movement grid, the resident map block and the placement table at its own
   static arrays, and fdps_field_load_chapter_resources frees all of them on
   entry -- with a layer count of zero and null everywhere else it frees
   nothing, which is the state a freshly started process is in.  Handing a
   static array to free() is not something a later check would get to
   report. */
static void clear_chapter_globals(void)
{
    int layer;

    for (layer = 0; layer < 6; layer++) {
        data_fdps_scene_layer_tile_map_ptrs[layer] = NULL;
        data_fdps_scene_layer_tile_sheet_ptrs[layer] = NULL;
        data_fdps_scene_layer_tile_attr_ptr[layer] = NULL;
    }
    data_fdps_scene_layer_count = 0;
    data_fdps_current_chapter_text_ptr = NULL;
    data_fdps_tile_event_data_table_ptr = NULL;
    data_fdps_map_cell_event_code_layer_ptr = NULL;
    data_fdps_battle_move_grid_ptr = NULL;
    data_fdps_map_spawn_pos_table_ptr = NULL;
}

/* The blocks the load left behind, released and the globals put back the way a
   fresh process has them, so the cases that run after these see what they
   would have seen if these had never run. */
static void free_chapter_globals(void)
{
    int layer;

    for (layer = 0; layer < data_fdps_scene_layer_count; layer++) {
        free(data_fdps_scene_layer_tile_map_ptrs[layer]);
        free(data_fdps_scene_layer_tile_sheet_ptrs[layer]);
        free(data_fdps_scene_layer_tile_attr_ptr[layer]);
    }
    free(data_fdps_current_chapter_text_ptr);
    free(data_fdps_tile_event_data_table_ptr);
    free(data_fdps_map_cell_event_code_layer_ptr);
    free(data_fdps_battle_move_grid_ptr);
    clear_chapter_globals();
}

/* Blank staging for a whole-chapter build: the four data tables wide enough
   for the wave-0 records the shipped maps carry, a party of roster_members
   distinguishable records, an empty unit array, and the two counts the loop
   runs on deliberately set to values the map does NOT have -- the resource
   load on the function's first line has to replace both before anything reads
   them. */
static void stage_chapter(int roster_members)
{
    int member;
    int timer;

    stage();
    clear_chapter_globals();

    zero_bytes(stage_big_char, (int) sizeof(stage_big_char));
    zero_bytes(stage_big_growth, (int) sizeof(stage_big_growth));
    zero_bytes(stage_big_enemy, (int) sizeof(stage_big_enemy));
    zero_bytes(stage_party, (int) sizeof(stage_party));

    data_fdps_battle_character_base_table_ptr =
        (unsigned char *) stage_big_char;
    data_fdps_battle_character_growth_table_ptr =
        (unsigned char *) stage_big_growth;
    data_fdps_battle_enemy_data_table_ptr = (unsigned char *) stage_big_enemy;

    for (member = 0; member < PARTY_SLOTS; member++) {
        stage_party[member].pos_x = 0x55;
        stage_party[member].pos_y = 0x66;
        stage_party[member].sprite_cache_slot = 0x77;
        stage_party[member].facing = 9;
        stage_party[member].walk_step = 8;
        stage_party[member].flags = 0x77;
        stage_party[member].side = 9;
        stage_party[member].portrait_id = PARTY_PORTRAIT;
        stage_party[member].char_id =
            (unsigned char) (PARTY_CHAR_ID_BASE + member);
        stage_party[member].level = (unsigned char) (PARTY_LEVEL_BASE + member);
        stage_party[member].death_script_opcode = 0x11;
        stage_party[member].death_script_operand = PARTY_DEATH_OPERAND;
        stage_party[member].ap_base = (short) (PARTY_AP_BASE + member);
        stage_party[member].dp_base = (short) (PARTY_DP_BASE + member);
        stage_party[member].dx_base = (short) (PARTY_DX_BASE + member);
        stage_party[member].hp_max = (short) (PARTY_HP_MAX_BASE + member);
        stage_party[member].hp_current = 1;
        stage_party[member].mp_max = (short) (PARTY_MP_MAX_BASE + member);
        stage_party[member].mp_current = 0;
        stage_party[member].ap = PARTY_STALE_DERIVED;
        stage_party[member].dp = PARTY_STALE_DERIVED;
        stage_party[member].hit = PARTY_STALE_DERIVED;
        stage_party[member].ev = PARTY_STALE_DERIVED;
        for (timer = 0; timer < 6; timer++) {
            stage_party[member].status_timers[timer] = PARTY_STALE_TIMER;
        }
    }

    data_fdps_roster_array_ptr = (unsigned char *) stage_party;
    data_fdps_roster_member_count = roster_members;

    data_fdps_map_player_slot_count = 99;
    data_fdps_map_char_spawn_count = 0;
    data_fdps_map_unit_count = 0;
    data_fdps_map_unit_array_ptr = NULL;
    data_fdps_cel_sprite_cache_count = 0;
    data_fdps_cel_sprite_cache_ptr = NULL;
}

/* The placement record a party slot stands on is the scripted deployment count
 * PLUS the slot number, not the slot number.  Chapter 0 fields one player slot
 * behind 22 scripted deployments, so slot 0 takes MAP00.COD record 22 at
 * (16, 22); record 0, which indexing by the slot number alone would reach, is
 * the scripted unit's tile (18, 0).
 *
 * Both counts are staged wrong on purpose beforehand -- 99 slots and 0
 * scripted records -- so this also says the resource load on the first line
 * really did republish them before the loop read them.  Cached across that
 * call, the array would be 99 records long and slot 0 would land on (18, 0).
 *
 * MAP00.DAT tags none of its records with wave 0, so the count the array ends
 * up with is the map's player slot count and nothing else. */
static void build_places_the_party_past_the_scripted_records(void)
{
    ensure_chapter_containers();
    CHECK_EQ(chapter_ready, 1);
    if (!chapter_ready) {
        return;
    }
    stage_chapter(1);
    data_fdps_chapter_current_chapter_id = CH_ONE_SLOT;

    fdps_build_map_unit_array(CH_ONE_SLOT);

    CHECK_EQ(data_fdps_map_player_slot_count, CH0_PLAYER_SLOTS);
    CHECK_EQ(data_fdps_map_char_spawn_count, CH0_CHAR_SPAWNS);
    CHECK_EQ(data_fdps_map_unit_count, CH0_PLAYER_SLOTS);
    CHECK_EQ((int) deployed(0)->pos_x, CH0_PARTY_TILE_X);
    CHECK_EQ((int) deployed(0)->pos_y, CH0_PARTY_TILE_Y);
    CHECK_EQ((int) deployed(0)->pos_x == CH0_SCRIPTED_TILE_X
             && (int) deployed(0)->pos_y == CH0_SCRIPTED_TILE_Y, 0);
    CHECK_EQ(data_fdps_map_spawn_pos_table_ptr == NULL, 1);

    free_chapter_globals();
}

/* The roster record arrives whole and the map's own facts are written on top
 * of it.  Kept: the character id and the death-script operand, neither of
 * which the function touches.  Overwritten: the tile, the facing, the walk
 * step, the flags, the side and the death-script opcode.
 *
 * The heal is unconditional -- hp_current comes back at hp_max and mp_current
 * at mp_max for a member staged with 1 HP and no MP -- and the six status
 * timers are cleared.  The four derived stats then say the clear happened
 * BEFORE fdps_unit_recompute_combat_stats ran: with the timers still at 5 that
 * routine scales attack and defense by 1.15 and adds 15 to both dexterity
 * totals, so 11, 12, 13, 13 would read 12, 13, 28, 28 instead.  They also say
 * the recompute ran at all: the staged records carry 0x3e7 in all four. */
static void build_copies_the_roster_and_clears_the_map_fields(void)
{
    int timer;

    ensure_chapter_containers();
    if (!chapter_ready) {
        return;
    }
    stage_chapter(1);
    data_fdps_chapter_current_chapter_id = CH_ONE_SLOT;

    fdps_build_map_unit_array(CH_ONE_SLOT);

    CHECK_EQ((int) deployed(0)->char_id, PARTY_CHAR_ID_BASE);
    CHECK_EQ((int) deployed(0)->level, PARTY_LEVEL_BASE);
    CHECK_EQ((int) deployed(0)->death_script_operand, PARTY_DEATH_OPERAND);

    CHECK_EQ((int) deployed(0)->facing, 0);
    CHECK_EQ((int) deployed(0)->walk_step, 0);
    CHECK_EQ((int) deployed(0)->flags, 0);
    CHECK_EQ((int) deployed(0)->side, PLAYER_SIDE);
    CHECK_EQ((int) deployed(0)->death_script_opcode, DEATH_SCRIPT_NONE);

    CHECK_EQ((int) deployed(0)->hp_current, PARTY_HP_MAX_BASE);
    CHECK_EQ((int) deployed(0)->hp_max, PARTY_HP_MAX_BASE);
    CHECK_EQ((int) deployed(0)->mp_current, PARTY_MP_MAX_BASE);
    CHECK_EQ((int) deployed(0)->mp_max, PARTY_MP_MAX_BASE);

    for (timer = 0; timer < 6; timer++) {
        CHECK_EQ((int) deployed(0)->status_timers[timer], 0);
    }

    CHECK_EQ((int) deployed(0)->ap, PARTY_AP_BASE);
    CHECK_EQ((int) deployed(0)->dp, PARTY_DP_BASE);
    CHECK_EQ((int) deployed(0)->hit, PARTY_DX_BASE);
    CHECK_EQ((int) deployed(0)->ev, PARTY_DX_BASE);

    free_chapter_globals();
}

/* A map that fields more player slots than the party has members fills the
 * spare ones with a zeroed record whose flags byte is 1, the bit
 * fdps_unit_is_retired reads.  Chapter 5 fields four slots; staged with two
 * members, slots 2 and 3 are the spares.
 *
 * Zeroed means zeroed: the spare arm runs before anything else is written into
 * the record, so the tile the placement table names for that slot does not
 * reach it either -- MAP05.COD records 34 and 35 are (2, 10) and (3, 10), and
 * a spare slot standing on one of them would be a unit on the map. */
static void build_fills_slots_past_the_roster_as_retired(void)
{
    ensure_chapter_containers();
    if (!chapter_ready) {
        return;
    }
    stage_chapter(2);
    data_fdps_chapter_current_chapter_id = CH_FOUR_SLOT;

    fdps_build_map_unit_array(CH_FOUR_SLOT);

    CHECK_EQ(data_fdps_map_player_slot_count, CH5_PLAYER_SLOTS);
    CHECK_EQ(data_fdps_map_char_spawn_count, CH5_CHAR_SPAWNS);

    CHECK_EQ((int) deployed(0)->pos_x, CH5_PARTY0_TILE_X);
    CHECK_EQ((int) deployed(0)->pos_y, CH5_PARTY0_TILE_Y);
    CHECK_EQ((int) deployed(1)->pos_x, CH5_PARTY1_TILE_X);
    CHECK_EQ((int) deployed(1)->pos_y, CH5_PARTY1_TILE_Y);
    CHECK_EQ((int) deployed(1)->char_id, PARTY_CHAR_ID_BASE + 1);

    CHECK_EQ((int) deployed(2)->flags, UNIT_FLAG_RETIRED);
    CHECK_EQ((int) deployed(2)->pos_x, 0);
    CHECK_EQ((int) deployed(2)->pos_y, 0);
    CHECK_EQ((int) deployed(2)->char_id, 0);
    CHECK_EQ((int) deployed(2)->side, 0);
    CHECK_EQ((int) deployed(2)->hp_max, 0);
    CHECK_EQ((int) deployed(3)->flags, UNIT_FLAG_RETIRED);
    CHECK_EQ((int) deployed(3)->pos_x, 0);
    CHECK_EQ((int) deployed(3)->hp_max, 0);

    free_chapter_globals();
}

/* The sprite group is the PORTRAIT id at record +7, not the character id at
 * +8, and the cache it is looked up in is emptied first.
 *
 * The cache is primed with one group of its own before the build, so it holds
 * one entry and its buffer is a real allocation.  Both party members then
 * share portrait 3 and carry different character ids, so:
 *
 *   read from +7 on an emptied cache, both members get slot 0
 *   read from +8, they are two different groups and land on slots 0 and 1
 *   read from +7 on a cache that was NOT emptied, the primed group holds slot
 *     0 and both members get slot 1
 *
 * Each of the three answers is distinct, so this one comparison separates all
 * of them. */
static void build_takes_the_sprite_group_from_the_portrait_id(void)
{
    FILE *fp;
    int primed_slot;

    ensure_chapter_containers();
    if (!chapter_ready) {
        return;
    }
    stage_chapter(2);
    data_fdps_chapter_current_chapter_id = CH_FOUR_SLOT;

    fp = fopen(CEL_NAME, "rb");
    primed_slot = fdps_cache_cel_sprite_group(9, fp);
    fclose(fp);
    CHECK_EQ(primed_slot, 0);
    CHECK_EQ(data_fdps_cel_sprite_cache_count, 1);

    fdps_build_map_unit_array(CH_FOUR_SLOT);

    CHECK_EQ((int) deployed(0)->sprite_cache_slot, 0);
    CHECK_EQ((int) deployed(1)->sprite_cache_slot, 0);
    CHECK_EQ((int) deployed(0)->char_id, PARTY_CHAR_ID_BASE);
    CHECK_EQ((int) deployed(1)->char_id, PARTY_CHAR_ID_BASE + 1);

    free_chapter_globals();
}

/* The array is started over, and wave 0 is appended to it.
 *
 * Chapter 5 tags two of its 32 deployment records with wave 0 -- record 0,
 * character 98, and record 31, character 12 -- so a build leaves four player
 * slots followed by those two, six records in all, on the tiles MAP05.COD
 * names for records 0 and 31.  The wave number is what picks them: chapter 5's
 * wave 1 carries 23 records, so any other wave gives a different count.
 *
 * Running the build a second time answers the other half.  Nothing is appended
 * to what the first one left: the count comes back at six rather than twelve,
 * and slot 0 is the party member again rather than a wave unit shifted along.
 * The second run is also the one that takes the free path over a real
 * allocation, the first having been staged with an empty array. */
static void build_starts_the_array_over_and_appends_wave_zero(void)
{
    ensure_chapter_containers();
    if (!chapter_ready) {
        return;
    }
    stage_chapter(2);
    data_fdps_chapter_current_chapter_id = CH_FOUR_SLOT;

    fdps_build_map_unit_array(CH_FOUR_SLOT);

    CHECK_EQ(data_fdps_map_unit_count, CH5_PLAYER_SLOTS + CH5_WAVE0_UNITS);
    CHECK_EQ((int) deployed(4)->char_id, CH5_WAVE0_CHAR_A);
    CHECK_EQ((int) deployed(4)->pos_x, CH5_WAVE0_TILE_A_X);
    CHECK_EQ((int) deployed(4)->pos_y, CH5_WAVE0_TILE_A_Y);
    CHECK_EQ((int) deployed(5)->char_id, CH5_WAVE0_CHAR_B);
    CHECK_EQ((int) deployed(5)->pos_x, CH5_WAVE0_TILE_B_X);
    CHECK_EQ((int) deployed(5)->pos_y, CH5_WAVE0_TILE_B_Y);

    fdps_build_map_unit_array(CH_FOUR_SLOT);

    CHECK_EQ(data_fdps_map_unit_count, CH5_PLAYER_SLOTS + CH5_WAVE0_UNITS);
    CHECK_EQ((int) deployed(0)->char_id, PARTY_CHAR_ID_BASE);
    CHECK_EQ((int) deployed(0)->pos_x, CH5_PARTY0_TILE_X);
    CHECK_EQ((int) deployed(4)->char_id, CH5_WAVE0_CHAR_A);

    free_chapter_globals();
}

void run_deploy_tests(void)
{
    RUN_TEST(record_offsets_match_the_assembly);
    RUN_TEST(exact_placement_uses_the_anchor);
    RUN_TEST(roster_character_growth_is_asymmetric);
    RUN_TEST(enemy_stats_scale_flat_with_level);
    RUN_TEST(inventory_flags_and_ids);
    RUN_TEST(missing_weapon_moves_the_second_item_into_slot_0);
    RUN_TEST(spell_ai_and_script_fields_are_copied);
    RUN_TEST(exp_carry_marks_the_player_side);
    RUN_TEST(search_keeps_the_last_tile_at_the_winning_distance);
    RUN_TEST(search_rejects_unwalkable_terrain);
    RUN_TEST(anchor_coordinates_are_signed);
    RUN_TEST(wave_deploys_only_the_records_tagged_with_it);
    RUN_TEST(header_count_bounds_the_walk);
    RUN_TEST(wave_tag_is_an_unsigned_byte_compared_full_width);
    RUN_TEST(map_number_picks_the_placement_file);
    RUN_TEST(a_wave_nothing_matches_still_releases_the_table);
    RUN_TEST(a_second_wave_appends_to_the_first);
    RUN_TEST(place_exact_reaches_the_deployment);
    RUN_TEST(build_places_the_party_past_the_scripted_records);
    RUN_TEST(build_copies_the_roster_and_clears_the_map_fields);
    RUN_TEST(build_fills_slots_past_the_roster_as_retired);
    RUN_TEST(build_takes_the_sprite_group_from_the_portrait_id);
    RUN_TEST(build_starts_the_array_over_and_appends_wave_zero);
}
