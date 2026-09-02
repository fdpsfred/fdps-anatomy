/* deploy.c -- building the battle's unit array out of a chapter's scripted
 * deployment table.
 *
 * The array is never sized up front.  Every deployment grows the block behind
 * data_fdps_map_unit_array_ptr by exactly one record and appends to it, so
 * data_fdps_map_unit_count is at once the index the new record lands at and,
 * once it has been incremented, what makes that record count as in play.
 *
 * Two tables are read per deployment and they are indexed by the same number:
 * the 0x1a-byte struct fdps_char_spawn_record in the resident MAP%02d.DAT
 * block (data_fdps_tile_event_data_table_ptr, gamedata.h) says who to place
 * and with what, and the 6-byte placement record in the MAP%02d.COD block
 * (data_fdps_map_spawn_pos_table_ptr, deploy.h) says where.
 */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "fdpstype.h"
#include "gamedata.h"
#include "movegrid.h"
#include "maptile.h"
#include "table.h"
#include "unit.h"
#include "rsrc.h"
#include "vfs.h"
#include "keybd.h"
#include "deploy.h"

/* The stride of one unit record, as the original writes it: IMUL EAX,EAX,0x50
   at 000232e3 for the realloc size and PUSH 0x50 at 000232cc for the first
   malloc.  A literal and not sizeof(struct fdps_unit_record) for the reason
   roster.c gives: 0x50 is the block's own layout and the struct agrees with it
   only while it stays byte-packed (rebuild_info/pitfalls.md). */
#define UNIT_RECORD_STRIDE 0x50

/* Where the deployment records start inside the MAP%02d.DAT block and how wide
   one is: ADD EAX,0x83 at 00023463 after IMUL EAX,[EBP+0x14],0x1a at 00023457.
   The width is struct fdps_char_spawn_record's, so the records are addressed
   through the struct and only the header offset is spelled out here. */
#define SPAWN_TABLE_RECORD_BASE 0x83

/* How many deployment records the MAP%02d.DAT block holds, and where that
   number sits in its header: MOV AL,byte ptr [EAX+0x2] / AND EAX,0xff at
   000238bb..000238be.  One unsigned byte, so a map can script at most 255
   deployments and the count is never negative however the file reads. */
#define SPAWN_TABLE_COUNT_OFFSET 2

/* The stack buffer fdps_deploy_wave formats the placement file's name into:
   the frame at SUB ESP,0x24 puts it at [EBP-0x24] with the first named local
   above it at [EBP-0x10], so it is 20 bytes.  "map%02d.cod" fills 12 of them
   for every map number the game has. */
#define PLACEMENT_NAME_SIZE 20

/* The placement table's first coordinate pair and the stride between pairs:
   IMUL EAX,[EBP+0x14],0x6 / ADD EAX,0xb at 0002330b..00023317.  Written as raw
   byte arithmetic because that is all the image ever does with this table --
   see the note on data_fdps_map_spawn_pos_table_ptr in deploy.h. */
#define SPAWN_POS_COORD_BASE 0xb
#define SPAWN_POS_RECORD_STRIDE 6

/* The move grid's cell array starts past its four-byte header of two signed
   16-bit dimensions: ADD EAX,EDX / MOV AL,byte ptr [EAX+0x4] at 000233b4. */
#define MOVE_GRID_CELL_BASE 4

/* Bit 0x40 of a grid cell's flags byte means a unit is standing on that tile.
   AND AL,0x40 at 000233b9 is the whole test: bit 0x80, the zone of control the
   two marking calls above also set, is deliberately NOT tested, so a deployed
   unit may land next to an enemy but never on top of one. */
#define GRID_FLAG_OCCUPIED 0x40

/* The best-so-far Manhattan distance the tile search starts from: MOV dword
   ptr [EBP-0x14],0xff at 000232bc.  A plain int and not a byte -- every
   candidate distance is compared against it as a signed dword. */
#define TILE_SEARCH_START_DISTANCE 0xff

/* Terrain codes below this are walkable enough to deploy onto: CMP EAX,0x5 /
   JGE at 00023404, against the zero-extended byte
   data_fdps_map_tile_terrain_type that fdps_map_load_tile_info just wrote. */
#define TERRAIN_DEPLOYABLE_LIMIT 5

/* Character ids at or above this are enemies, indexed into ENEMYDAT.DAT after
   subtracting it; below it they are roster-side characters with a
   FRIAPRDA.DAT base record and a FRILEVUP.DAT growth record.  CMP dword ptr
   [EBP-0x5c],0x3c / JL at 0002348b. */
#define ENEMY_CHAR_ID_BASE 0x3c

/* The inventory flag bytes.  Every one of the eight 2-byte slots carries a
   flag byte then an item id: 0x40 is equipped, 0x80 is empty, 0 is carried. */
#define INVENTORY_FLAG_EQUIPPED 0x40
#define INVENTORY_FLAG_EMPTY 0x80
#define INVENTORY_FLAG_CARRIED 0

/* The item id that means "no item": CMP EAX,0xff at 00023693 for the two
   equipment bytes and at 00023701 for the six carried ones. */
#define ITEM_ID_NONE 0xff

/* How many carried items a deployment record holds, at its +7..+0xc: CMP dword
   ptr [EBP-0x40],0x6 / JL at 000236e3. */
#define CARRIED_ITEM_COUNT 6

/* The status-effect timer bytes cleared at record +0x22: PUSH 0x6 at
   0002373a. */
#define STATUS_TIMER_COUNT 6

/* The spell-mask bytes moved from the deployment record to the unit record:
   PUSH 0x4 at 0002374d.  The fifth byte of the unit's bitmap is a separate
   copy from a separate place in the deployment record and is not part of this
   block. */
#define SPELL_MASK_BYTES 4

/* The side code the player's own units carry, and what record +0x3c holds for
   a unit that is not on it: CMP EAX,0x2 / JNZ at 000237cf, then 0x0 or 0xff. */
#define PLAYER_SIDE 2
#define EXP_CARRY_NOT_PLAYER 0xff

/* 000232b0.  See deploy.h for what the two arguments select and what the
   function leaves behind.

   Two things here read as mistakes and are not tidied up, because tidying
   either one changes where units appear and what they hold.

   The nearest-tile search accepts a candidate whose Manhattan distance merely
   TIES the best so far -- CMP EAX,[EBP-0x14] / JLE at 000233e6, not JL -- so
   the tile that survives a row-major scan is the LAST cell at the winning
   distance, not the first.  Writing the obvious "if (distance <
   best_distance)" moves every searched-for deployment onto a different tile.
   The recompute of the same two abs() calls inside the accept branch
   (00023409..0002342b) is not reproduced: abs is a pure function of two values
   neither call changes, so assigning the distance already in hand is the same
   arithmetic (ADR-0001).

   And when the deployment record's first equipment byte is 0xff, the SECOND
   equipment byte is moved into inventory slot 0 and still flagged equipped --
   0xff included, because nothing tests the value that lands there.  That sends
   fdps_unit_recompute_combat_stats into fdps_get_item_record(0xff), past the
   end of ITEM.DAT's 251 records, which is the general no-bounds-check case
   rebuild_info/pitfalls.md already records for the nine data tables.  Slot 1's
   id byte is then never written at all in that branch and keeps whatever the
   allocator handed back.

   The two growth scalings differ the same way fdps_roster_add_character's do,
   and for the same reason: attack, defense and dexterity take a full LEVEL of
   growth steps (IMUL at 000235de, 000235f6 and 0002361a) while HP and MP take
   level - 1 (DEC EAX at 00023564 and DEC EDX at 00023587).  See
   assets/characters.md -- the strategy-guide numbers only fit under the
   asymmetric pair.

   Contract C is live throughout.  The anchor coordinates and the grid
   dimensions arrive through MOVSX and are signed, and both loop bounds are
   compared with JL; the character id, the level, the side and the terrain code
   arrive zero-extended out of bytes and so are 0..255 whatever the byte holds.
   Reading a grid dimension as unsigned turns a header of 0xffff into 65535 and
   the search walks off the block.

   If the search rejects every cell -- an entirely occupied or entirely
   unwalkable map -- the chosen tile is never assigned: the original writes
   [EBP-0x58] and [EBP-0x54] only from inside the accept branch at 0002342e,
   and the exit at 00023444 falls straight through to the record write.  The
   two locals are left uninitialised here for that reason.  Seeding them with
   the anchor is the obvious repair and it would place a unit the original
   places somewhere else entirely. */
void fdps_deploy_unit(int deploy_index, FILE *icon_cel_fp,
                      unsigned char place_exact)
{
    struct fdps_unit_record *unit;
    struct fdps_char_spawn_record *spawn;
    struct fdps_character_base_record *base_record;
    struct fdps_character_growth *growth;
    struct fdps_enemy_data *enemy;
    struct fdps_move_grid_cell *cell;
    short *anchor;
    int anchor_x;
    int anchor_y;
    int grid_width;
    int grid_height;
    int scan_x;
    int scan_y;
    int distance;
    int best_distance;
    int chosen_x;
    int chosen_y;
    int char_id;
    int side;
    int level;
    int hp_start;
    int mp_start;
    int ap_base;
    int dp_base;
    int dx_base;
    int carried_index;

    best_distance = TILE_SEARCH_START_DISTANCE;

    if (data_fdps_map_unit_count == 0) {
        data_fdps_map_unit_array_ptr = malloc(UNIT_RECORD_STRIDE);
    } else {
        data_fdps_map_unit_array_ptr =
            realloc(data_fdps_map_unit_array_ptr,
                    (data_fdps_map_unit_count + 1) * UNIT_RECORD_STRIDE);
    }
    unit = fdps_get_unit_record(data_fdps_map_unit_count);

    anchor = (short *) (data_fdps_map_spawn_pos_table_ptr +
                        SPAWN_POS_COORD_BASE +
                        deploy_index * SPAWN_POS_RECORD_STRIDE);
    anchor_x = (int) anchor[0];
    anchor_y = (int) anchor[1];

    /* The occupancy the search reads is rebuilt here rather than trusted:
       the reset clears both zone bits off every cell and the two marking
       passes -- one per side select -- put 0x40 back on the tile of every unit
       still in play.  A second reset afterwards leaves the grid blank again
       for the movement code, whichever branch was taken. */
    fdps_map_grid_reset();
    fdps_move_grid_mark_opposing_zones_of_control(0);
    fdps_move_grid_mark_opposing_zones_of_control(1);

    grid_width = (int) *(short *) data_fdps_battle_move_grid_ptr;
    grid_height = (int) *(short *) (data_fdps_battle_move_grid_ptr + 2);

    if (place_exact == 0) {
        for (scan_y = 0; scan_y < grid_height; scan_y++) {
            for (scan_x = 0; scan_x < grid_width; scan_x++) {
                cell = (struct fdps_move_grid_cell *)
                       (data_fdps_battle_move_grid_ptr + MOVE_GRID_CELL_BASE) +
                       (scan_y * grid_width + scan_x);
                if ((cell->flags & GRID_FLAG_OCCUPIED) != 0) {
                    continue;
                }
                distance = abs(scan_x - anchor_x) + abs(scan_y - anchor_y);
                if (distance > best_distance) {
                    continue;
                }
                fdps_map_load_tile_info(scan_x, scan_y);
                if (data_fdps_map_tile_terrain_type <
                    TERRAIN_DEPLOYABLE_LIMIT) {
                    best_distance = distance;
                    chosen_x = scan_x;
                    chosen_y = scan_y;
                }
            }
        }
    } else {
        chosen_x = anchor_x;
        chosen_y = anchor_y;
    }

    fdps_map_grid_reset();

    spawn = (struct fdps_char_spawn_record *)
            (data_fdps_tile_event_data_table_ptr + SPAWN_TABLE_RECORD_BASE) +
            deploy_index;
    char_id = (int) spawn->char_id;
    level = (int) spawn->level;
    side = (int) spawn->side;

    if (char_id < ENEMY_CHAR_ID_BASE) {
        base_record = fdps_get_character_base_record(char_id);
        growth = fdps_get_growth_record(char_id);
        hp_start = (int) base_record->hp_base +
                   (int) growth->hp_min * (level - 1);
        mp_start = (int) base_record->mp_base +
                   (int) growth->mp_min * (level - 1);
        ap_base = (int) base_record->ap_base;
        dp_base = (int) base_record->dp_base;
        dx_base = (int) base_record->dx_base;
        unit->race = base_record->race_id;
        unit->clazz = base_record->class_id;
        unit->ap_base = (short) (ap_base + (int) growth->ap_min * level);
        unit->dp_base = (short) (dp_base + (int) growth->dp_min * level);
        unit->move = base_record->move;
        unit->dx_base = (short) (dx_base + (int) growth->dx_min * level);
    } else {
        enemy = fdps_get_enemy_record(char_id - ENEMY_CHAR_ID_BASE);
        hp_start = level * (int) enemy->hp;
        mp_start = level * (int) enemy->mp;
        unit->race = enemy->race_id;
        unit->clazz = enemy->class_id;
        unit->ap_base = (short) (level * (int) enemy->ap);
        unit->dp_base = (short) (level * (int) enemy->dp);
        unit->dx_base = (short) (level * (int) enemy->dx);
        unit->move = enemy->mv;
    }

    unit->pos_x = (unsigned char) chosen_x;
    unit->pos_y = (unsigned char) chosen_y;
    unit->sprite_cache_slot =
        (unsigned char) fdps_cache_cel_sprite_group(char_id, icon_cel_fp);
    unit->facing = 0;
    unit->walk_step = 0;
    unit->flags = 0;
    unit->side = (unsigned char) side;
    unit->portrait_id = (unsigned char) char_id;
    unit->char_id = (unsigned char) char_id;
    unit->reserved_09 = 0;

    if (spawn->equipped_item_0 == ITEM_ID_NONE) {
        unit->inventory_slots[0] = INVENTORY_FLAG_EQUIPPED;
        unit->inventory_slots[1] = spawn->equipped_item_1;
        unit->inventory_slots[2] = INVENTORY_FLAG_EMPTY;
    } else {
        unit->inventory_slots[0] = INVENTORY_FLAG_EQUIPPED;
        unit->inventory_slots[1] = spawn->equipped_item_0;
        unit->inventory_slots[2] = INVENTORY_FLAG_EQUIPPED;
        unit->inventory_slots[3] = spawn->equipped_item_1;
    }

    for (carried_index = 0;
         carried_index < CARRIED_ITEM_COUNT;
         carried_index++) {
        if (spawn->carried_items[carried_index] == ITEM_ID_NONE) {
            unit->inventory_slots[4 + carried_index * 2] =
                INVENTORY_FLAG_EMPTY;
        } else {
            unit->inventory_slots[4 + carried_index * 2] =
                INVENTORY_FLAG_CARRIED;
        }
        unit->inventory_slots[5 + carried_index * 2] =
            spawn->carried_items[carried_index];
    }

    memset(unit->status_timers, 0, STATUS_TIMER_COUNT);
    memmove(unit->spells_known_bitmap, spawn->spell_mask, SPELL_MASK_BYTES);
    unit->spells_known_bitmap[4] = spawn->spell_mask_high;
    unit->level = (unsigned char) level;
    unit->death_script_opcode = spawn->death_script_opcode;
    unit->death_script_operand = spawn->death_script_operand;
    unit->ai_behavior = spawn->ai_class;
    unit->ai_dest_x = spawn->ai_dest_x;
    unit->ai_dest_y = spawn->ai_dest_y;
    unit->event_slot = spawn->cell_event_code;

    if (unit->side == PLAYER_SIDE) {
        unit->exp_carry = 0;
    } else {
        unit->exp_carry = EXP_CARRY_NOT_PLAYER;
    }

    unit->hp_current = (short) hp_start;
    unit->hp_max = (short) hp_start;
    unit->mp_current = (short) mp_start;
    unit->mp_max = (short) mp_start;

    fdps_unit_recompute_combat_stats(data_fdps_map_unit_count);
    data_fdps_map_unit_count = data_fdps_map_unit_count + 1;
}

/* 00023830.  See deploy.h for what the three arguments select.

   The two resources the walk needs are opened here and released here, and
   neither open is checked in a way that can keep the function from using it.
   The ICON.CEL stream is not tested at all -- a missing sheet reaches
   fdps_cache_cel_sprite_group as a null stream through every deployment, and
   fclose gets the same null at the end.

   The archive is tested, and the test is the trap.  A missing Field.vfs prints
   a line and waits for a key, and then falls THROUGH to
   fdps_vfs_load_file_or_exit holding the null handle: the JNZ at 00023868
   jumps over the message, not over the load, and there is no second exit out
   of the function.  That call ends the process either way, because the loader
   it wraps cannot find a member in a container it has no handle for.  Writing
   the return the message reads like would leave the game running with the wave
   never deployed, which is the one outcome the original does not produce
   (rebuild_info/pitfalls.md).

   The loop bound is re-read from the block on every pass -- the assembly
   reloads data_fdps_tile_event_data_table_ptr and its header byte at 000238b6
   at the top of each iteration -- and it is written that way here.  It changes
   nothing in practice: fdps_deploy_unit grows the unit array, never the
   deployment table.

   Contract C: the record count and the wave tag are both single unsigned
   bytes, zero-extended with AND EAX,0xff before use, and both comparisons that
   follow are 32-bit and signed (JG at 000238c6, JNZ at 000238f4).  Reading
   either as a signed char would make a count or a tag of 0x80 and up negative,
   which stops the walk before it starts and matches waves it must not match.

   The placement table is published into a global rather than kept in a local,
   because fdps_deploy_unit reads it from there, and it is freed and nulled
   again before the return -- so it is live only for the span of this call. */
void fdps_deploy_wave(int map_no, int wave_no, unsigned char place_exact)
{
    char placement_file_name[PLACEMENT_NAME_SIZE];
    void *field_vfs;
    FILE *icon_cel_fp;
    struct fdps_char_spawn_record *spawn;
    int deploy_index;

    icon_cel_fp = fopen("ICON.CEL", "rb");

    field_vfs = fdps_vfs_open("Field.vfs");
    if (field_vfs == NULL) {
        printf("file not found: '%s'\n", "Field.vfs");
        fdps_wait_any_key();
    }

    sprintf(placement_file_name, "map%02d.cod", map_no);
    fdps_vfs_load_file_or_exit(field_vfs, placement_file_name,
                               (void **) &data_fdps_map_spawn_pos_table_ptr);

    for (deploy_index = 0;
         deploy_index <
             data_fdps_tile_event_data_table_ptr[SPAWN_TABLE_COUNT_OFFSET];
         deploy_index++) {
        spawn = (struct fdps_char_spawn_record *)
                (data_fdps_tile_event_data_table_ptr +
                 SPAWN_TABLE_RECORD_BASE) + deploy_index;
        if (spawn->wave_no == wave_no) {
            fdps_deploy_unit(deploy_index, icon_cel_fp, place_exact);
        }
    }

    fclose(icon_cel_fp);
    free(data_fdps_map_spawn_pos_table_ptr);
    data_fdps_map_spawn_pos_table_ptr = NULL;
    free(field_vfs);
}
