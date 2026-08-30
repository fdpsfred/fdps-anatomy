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
 */
#include <stddef.h>
#include <stdio.h>
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

static int cel_ready = 0;
static int cel_checked = 0;

#define CEL_NAME "ICON.CEL"

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
}
