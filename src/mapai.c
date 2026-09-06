/* mapai.c -- the map AI: one computer-controlled actor's behaviour on its
 * turn, and the map queries that decision needs.
 *
 * See mapai.h for what each entry point promises.  Nothing here owns state:
 * the map layers, the movement grid and the unit records all belong to other
 * files, and everything below works on what those pointers hold.
 */
#include <stdlib.h>
#include "gamedata.h"
#include "fdpstype.h"
#include "mapcur.h"
#include "maptile.h"
#include "movegrid.h"
#include "table.h"
#include "unit.h"
#include "mapai.h"

/* The two values this handler stores into data_fdps_map_cursor_draw_mode
   (gamedata.h): 0 at 0001276d so nothing is painted while the actor walks, and
   1 at 000127a6 afterwards.  THE SECOND IS A PLAIN STORE AND NOT A RESTORE --
   MOV dword ptr [0x00069cd0],0x1, with the entry value never read -- so a
   caller that was in any other mode comes back in the ordinary box mode. */
#define CURSOR_DRAW_MODE_HIDDEN 0
#define CURSOR_DRAW_MODE_BOX 1

/* The allowance handed to the flood fill, PUSH 0x64 at 00012701.  It is not a
   unit's real movement allowance: it is large enough to cover any map in the
   game, so the fill spreads as far as the terrain lets the actor walk. */
#define MAP_AI_UNLIMITED_ALLOWANCE 100

/* fdps_move_path_trace's mode 2, the nearest-unit search (movegrid.h), pushed
   as a full dword at 00012717 and read by the callee as one byte. */
#define TRACE_MODE_NEAREST_UNIT 2

/* The side filter that search is given, PUSH 0x0 at 0001271d.  In mode 2 the
   tracer's start_x argument is a truth value and 0 selects the units whose
   side byte is NON-ZERO.  It is a hard-coded literal and NOT side_select: see
   the note on the function below. */
#define MAP_AI_TRACE_SIDE_FILTER 0

/* 000126b0.  Walks one actor toward the opposing unit nearest to it over
   walkable terrain rather than nearest in a straight line.

   THE CLASS RECORD IS FETCHED WITHOUT THE +1 EVERY NEIGHBOUR APPLIES.  MOV
   AL,byte ptr [EDX+0x20] / MOV [EBP-8],EAX / PUSH EAX at 000126e7..000126f5,
   with no INC anywhere between the load and the push, while
   fdps_battle_move_unit_toward at 00011a27 and fdps_map_cursor_select_tile at
   0002b7f8 both push the same record byte incremented.  Row 0 of PROMAP.DAT is
   the default row, so the raw code names the row belonging to the class one
   below the actor's and this search floods the map with the neighbouring
   class's terrain costs.  Writing the obvious +1 changes which unit the map AI
   walks at (rebuild_info/pitfalls.md).

   THE SIDE FILTER IS A HARD-CODED 0 AND NOT side_select.  PUSH 0x0 at
   0001271d, with side_select at [EBP+0x18] untouched until the move call at
   00012783.  On the NPC phase, where side_select is 1, the acting unit's own
   record therefore passes the filter, wins the search at cost 0 on its own
   tile, and the handler returns 0 having moved nothing.

   THE GRID IS TAKEN TO BE BLANK ON ENTRY.  No fdps_map_grid_reset runs before
   the fill and no zone-of-control pass runs at all, so what the fill relaxes
   is whatever the caller left behind; the reset comes afterwards, on both arms
   (00012748 and 0001273a), so nothing this function computed survives it.

   BOTH TILE BYTES WIDEN THROUGH XOR EAX,EAX / MOV AL, at 0001274d and 00012755,
   so a coordinate of 0x80 or above is 128 and never -128; the two compares that
   follow are equalities, so neither is a signedness test.

   The tile handed to fdps_battle_move_unit_toward is the target unit's OWN
   occupied tile, which no walk can end on: that function is what retargets the
   request to the reachable tile nearest it (movegrid.h), so a non-zero result
   means the actor moved, not that it arrived.

   The answer is 1 only when the move reported steps.  fdps_map_actor_behavior_step
   reads a 0 as "this handler did not claim the actor" and moves on to the next
   handler in that behaviour nibble's chain. */
int fdps_map_actor_move_toward_nearest_reachable_opponent(int unit_index,
                                                          int side_select)
{
    /* The acting unit's record inside the map unit array. */
    struct fdps_unit_record *actor;
    /* The tile the actor is standing on, record bytes +0 and +1. */
    int actor_x;
    int actor_y;
    /* The actor's class code, record byte +0x20, raw and not biased. */
    int actor_class;
    /* The PROMAP.DAT row the fill takes its per-terrain costs from. */
    struct fdps_class_record *class_move_cost;
    /* Where the search writes the winning unit's tile: column then row, two
       bytes, which is all fdps_move_path_trace's mode 2 stores. */
    unsigned char target_xy[2];
    /* The flooded cost of that unit's tile, or -1 when nothing matched. */
    int target_cost;
    /* The same tile widened out of the two bytes above. */
    int target_x;
    int target_y;
    /* The answer: 1 once the move has reported that it played a walk. */
    int moved;

    moved = 0;

    actor = fdps_get_unit_record(unit_index);
    actor_x = (int) actor->pos_x;
    actor_y = (int) actor->pos_y;
    actor_class = (int) actor->clazz;
    class_move_cost = fdps_get_class_record(actor_class);

    fdps_move_grid_flood_fill_range(class_move_cost, actor_x, actor_y,
                                    MAP_AI_UNLIMITED_ALLOWANCE);
    target_cost = fdps_move_path_trace(actor_x, actor_y, target_xy,
                                       MAP_AI_TRACE_SIDE_FILTER, 0,
                                       TRACE_MODE_NEAREST_UNIT);

    if (target_cost == -1) {
        fdps_map_grid_reset();
        return 0;
    }

    fdps_map_grid_reset();

    target_x = (int) target_xy[0];
    target_y = (int) target_xy[1];

    if (target_x != actor_x || target_y != actor_y) {
        data_fdps_map_cursor_draw_mode = CURSOR_DRAW_MODE_HIDDEN;
        fdps_map_cursor_move_to_unit(unit_index);
        if (fdps_battle_move_unit_toward(target_x, target_y, unit_index,
                                         side_select) != 0) {
            moved = 1;
        }
        data_fdps_map_cursor_draw_mode = CURSOR_DRAW_MODE_BOX;
    }

    return moved;
}

/* The distance the straight-line search starts from, MOV dword ptr
   [EBP-0x24],0xffff at 000127cc.  It is 65535 and not -1: the compare that
   guards the replacement is JGE, the signed one, and the largest sum two byte
   coordinates can be apart is 510, so the first candidate always beats it. */
#define MAP_AI_NO_DISTANCE 0xffff

/* The tile that search starts from, MOV dword ptr [EBP-0x14],0xffffffff at
   000127d3, and the value CMP dword ptr [EBP-0x14],-0x1 at 000128df tests for.
   A candidate's column is widened out of a record byte through XOR EAX,EAX /
   MOV AL, so it is 0..255 and can never collide with this. */
#define MAP_AI_NO_TILE (-1)

/* 000127c0.  Walks one actor toward the opposing unit nearest to it in a
   straight line, ignoring terrain entirely.  This is the fallback
   fdps_map_actor_behavior_step reaches for at all five of its call sites once
   the path-cost sibling above has declined the actor.

   THE SIDE FILTER COMPARES TRUTH VALUES AND NEVER SIDE NUMBERS.  CMP dword ptr
   [EBP+0x18],0x0 then CMP byte ptr [EAX+0x6],0x0 on each arm at 00012833..
   0001284f: side_select 0 keeps every unit whose side byte is non-zero, and any
   non-zero side_select keeps only the units whose side byte is 0.  Side byte 6
   is 0 for the enemy side, 1 for neutral and 2 for the player, so the obvious
   record->side != own_side is wrong in both directions -- it would make neutral
   units targets for a player-driven actor and player units targets for a
   neutral one (rebuild_info/pitfalls.md).

   THE ACTOR SCORES ITSELF WHENEVER ITS OWN SIDE BYTE PASSES THAT FILTER.  The
   sweep runs over every index from 0 and nothing excludes unit_index, so an
   actor of side 1 driven with side_select 0 wins its own tile at distance 0,
   the equality test below then holds and the handler returns 0 having moved
   nothing.  This is the same shape as the sibling's hard-coded filter and is
   equally load-bearing.

   THE RUNNING BEST IS REPLACED ONLY ON A STRICTLY SMALLER DISTANCE.  CMP EAX,
   [EBP-0x24] / JGE at 000128c3, so a tie keeps the candidate already held and
   the lowest unit index wins it.

   The distance is the SUM of the two axes, abs on each through the CRT call at
   0003d364 twice per candidate (000128a2 and 000128b3) with ADD EBX,EAX
   between: the diagonal metric would pick a different unit wherever the two
   sums tie and the larger axes do not.

   The retired test is bit 0 of the flags byte alone -- AND AL,0x1 at 00012876
   inside an inline expansion of fdps_unit_is_retired, whose out-of-line body at
   000109b0 this replays instruction for instruction (rebuild_info/build_flags.md
   on inline expansion under -od).  Bit 7, the acted-this-turn flag, is not part
   of the answer.

   The tile handed to fdps_battle_move_unit_toward is the winning unit's OWN
   occupied tile, which no walk can end on; that function is what retargets the
   request to the reachable tile nearest it (movegrid.h), so a non-zero result
   means the actor moved, not that it arrived.

   The cursor draw mode is written 0 and then 1 around the walk, at 000128fe and
   00012937.  THE SECOND IS A PLAIN STORE AND NOT A RESTORE, and neither runs at
   all when the winning tile is the one the actor already stands on.

   The answer is 1 only when the move reported steps; 0 covers "no candidate",
   "the winner is standing where I am" and "the walk played nothing", and
   fdps_map_actor_behavior_step reads all three as "this handler did not claim
   the actor". */
int fdps_map_actor_move_toward_nearest_opponent(int unit_index, int side_select)
{
    /* The acting unit's record inside the map unit array. */
    struct fdps_unit_record *actor;
    /* The record the sweep is looking at this iteration. */
    struct fdps_unit_record *candidate;
    /* The tile the actor is standing on, record bytes +0 and +1, both widened
       as unsigned so a coordinate of 0x80 or above is 128 and never -128. */
    int actor_x;
    int actor_y;
    /* The unit index the sweep is at. */
    int index;
    /* The tile this candidate is standing on. */
    int candidate_x;
    int candidate_y;
    /* This candidate's straight-line distance from the actor. */
    int distance;
    /* The smallest distance seen so far. */
    int best_distance;
    /* The tile that distance belongs to -- the winning unit's own tile, which
       is what gets handed to the move.  target_x carries the "nothing found"
       sentinel; target_y is only ever read once target_x has left it, which is
       the same iteration that writes it. */
    int target_x;
    int target_y;
    /* The answer: 1 once the move has reported that it played a walk. */
    int moved;

    best_distance = MAP_AI_NO_DISTANCE;
    target_x = MAP_AI_NO_TILE;
    moved = 0;

    actor = fdps_get_unit_record(unit_index);
    actor_x = (int) actor->pos_x;
    actor_y = (int) actor->pos_y;

    for (index = 0; index < data_fdps_map_unit_count; index++) {
        candidate = fdps_get_unit_record(index);

        if (!((side_select == 0 && candidate->side != 0) ||
              (side_select != 0 && candidate->side == 0))) {
            continue;
        }
        if (fdps_unit_is_retired(index) != 0) {
            continue;
        }

        candidate_x = (int) candidate->pos_x;
        candidate_y = (int) candidate->pos_y;
        distance = abs(actor_x - candidate_x) + abs(actor_y - candidate_y);

        if (distance < best_distance) {
            target_x = candidate_x;
            target_y = candidate_y;
            best_distance = distance;
        }
    }

    if (target_x == MAP_AI_NO_TILE) {
        return 0;
    }

    if (target_x != actor_x || target_y != actor_y) {
        data_fdps_map_cursor_draw_mode = CURSOR_DRAW_MODE_HIDDEN;
        fdps_map_cursor_move_to_unit(unit_index);
        if (fdps_battle_move_unit_toward(target_x, target_y, unit_index,
                                         side_select) != 0) {
            moved = 1;
        }
        data_fdps_map_cursor_draw_mode = CURSOR_DRAW_MODE_BOX;
    }

    return moved;
}

/* 00013e10.  The map dimensions come from the MOVEMENT GRID's header -- MOVSX
   word ptr [EAX] and MOVSX word ptr [EAX+2] on data_fdps_battle_move_grid_ptr
   at 00013e21 and 00013e2c -- and not from the terrain layer's header at +7,
   which is the width fdps_map_load_tile_info goes on to index all three layers
   with.  The two agree in practice because the layers cover the same map; the
   scan simply trusts that, and there is no bounds check and no null test
   anywhere in the function, on the grid pointer or on anything else.

   Both header words are read with MOVSX, signed, and the loop tests are JL,
   the signed compare.  A header word of 0xffff has to come out as -1 so the
   loop body never runs; read unsigned it would walk 65535 rows.

   Both dimensions are latched into their own stack slots before the loops
   ([EBP-0xc] and [EBP-8]) and nothing in the body writes either, so they are
   two ordinary locals and not a per-iteration reload.

   The kind test is CMP EAX,0x20 after AND AL,0x60 -- an equality on the
   two-bit field, not a bit test.  The obvious (attr & 0x20) also accepts kind
   0x60, and widening it to the 0x20/0x40 pair that the player-side cursor
   search accepts adds buried treasure, which the original never sends an AI
   actor to (rebuild_info/pitfalls.md).

   The event code is compared as a full int: MOVSX EAX,word ptr [0x00069d06]
   then CMP EAX,[EBP+0x14].  The global is signed and only ever receives a
   zero-extended cell byte, so the comparison is against 0..255.

   The coordinates go out as byte stores, MOV byte ptr [EDX] and MOV byte ptr
   [EDX+1]: the caller's buffer is two bytes and nothing past them is written.

   The return sense is 0 for found and -1 for not found, which is what the
   caller's TEST EAX,EAX / JZ at 000102d0 gates on. */
int fdps_map_find_chest_cell(int cell_code, unsigned char *out_xy)
{
    int grid_width;
    int grid_height;
    int tile_x;
    int tile_y;

    grid_width = (int) *(short *) data_fdps_battle_move_grid_ptr;
    grid_height = (int) *(short *) (data_fdps_battle_move_grid_ptr + 2);

    for (tile_y = 0; tile_y < grid_height; tile_y++) {
        for (tile_x = 0; tile_x < grid_width; tile_x++) {
            fdps_map_load_tile_info(tile_x, tile_y);

            if ((data_fdps_map_current_tile_attr_flags & 0x60) == 0x20 &&
                (int) data_fdps_map_current_cell_event_code == cell_code) {
                out_xy[0] = (unsigned char) tile_x;
                out_xy[1] = (unsigned char) tile_y;
                return 0;
            }
        }
    }

    return -1;
}
