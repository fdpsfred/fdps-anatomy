/* unit.c -- core access to one map unit record: lookup, flags and the small
 * derived readouts the rest of the game asks for a unit at a time.
 *
 * See unit.h.  Every function here reaches its record through
 * data_fdps_map_unit_array_ptr and works on what it finds; the file owns no
 * state.
 */
#include "fdpstype.h"
#include "gamedata.h"
#include "unit.h"

/* 0002d210.  The map unit array's element accessor, and the whole body is one
   basic block: IMUL EAX,dword ptr [EBP+0x14],0x50 / MOV EDX,dword ptr
   [0x00069cd8] / ADD EDX,EAX, spilled to the frame local at [EBP-0x4] and
   reloaded into EAX to be returned.  No compare, no branch, no CALL, and
   nothing is dereferenced here -- the address is computed and handed back.

   The stride is the literal 0x50, which is sizeof(struct fdps_unit_record).

   The multiply is IMUL, the signed form, so a negative unit_index steps
   backwards off the front of the array rather than becoming a four-gigabyte
   offset.  Nothing bounds it: data_fdps_map_unit_count at 0x00060150 is not
   read here and the base is not tested for null, so the bound is the caller's.
   Every walk that reaches this function carries its own -- for one example
   fdps_battle_count_remaining_units_on_side at 00018350 compares its counter
   against the count (CMP EAX,dword ptr [0x00060150] / JL) before it pushes it
   -- and a bound added here would move that responsibility rather than add
   safety.

   The base is re-read from the global on every call, and that is load-bearing
   rather than incidental: fdps_relocate_unit_array at 0002df90 moves the array
   to a fresh heap block, wipes the old storage and frees it, and
   fdps_battle_unit_turn, fdps_battle_npc_turn_phase and
   fdps_battle_enemy_turn_phase call it once per unit iteration and re-resolve
   through here immediately afterwards.  A cached base, or a record pointer
   held across that call, addresses freed and zeroed memory. */
struct fdps_unit_record *fdps_get_unit_record(int unit_index)
{
    struct fdps_unit_record *record;

    record = (struct fdps_unit_record *)
        (data_fdps_map_unit_array_ptr +
         unit_index * (int) sizeof(struct fdps_unit_record));
    return record;
}

/* 0002ccd0.  Two passes over the same five record bytes: count how many status
   timers are running, then walk them again and hand back the one the rotation
   counter has come round to.

   The five offsets are a local array initialiser, which the compiler expands
   into the read-only five-byte copy at 0002b28e and a MOVSD/MOVSB pair at
   0002cceb -- the offsets are data, not a switch, and the order they are in is
   the whole content of the table.  That order is 0x23, 0x22, 0x24, 0x25, 0x27
   (rebuild_info/pitfalls.md): the first two are SWAPPED with respect to
   struct fdps_unit_record's status_timers[], and status_timers[4] at +0x26 has
   no entry at all.  Writing the obvious loop over the six timers in record
   order swaps two icons on the map and invents a sixth frame index that
   IconSts.cel does not have.  The returned slot is used by
   fdps_draw_map_unit directly as that frame index, so the slot number and the
   table position are the same thing and neither may be reordered.

   The record byte tests are CMP byte ptr [EAX],0x0 / JZ -- a timer counts as
   active on any non-zero value, never on a particular one -- and the offsets
   are zero-extended out of the table (AND EAX,0xff at 0002cd1a and 0002cd6c),
   so the table is unsigned.

   The rotation is signed: IDIV dword ptr [EBP-0x8] at 0002cd47 with EDX
   sign-extended from cycle by SAR EDX,0x1f, then INC EDX.  Both halves are
   load-bearing.  Signed division truncates towards zero on this target, so a
   negative cycle gives a remainder in (-count, 0]; +1 puts it at or below 1,
   and the second pass then decrements past zero without ever hitting it and
   falls out at -1 -- except for an exact multiple, whose remainder is 0 and
   which therefore selects the first active slot.  An unsigned modulo, or a
   count taken as unsigned, would instead pick a slot for every negative cycle.
   fdps_draw_map_unit passes data_fdps_map_unit_status_icon_cycle, which it
   only ever increments, so the negative arm is unreachable through that caller
   and is reproduced because the arithmetic is what the function is.

   The second pass decrements on every active timer it passes, not only on the
   selected one, and returns the slot at the instant the counter reaches zero
   (ADD dword ptr [EBP+0x18],-0x1 / CMP / JNZ at 0002cd79).  Testing for <= 0
   rather than == 0 would agree on every non-negative cycle and would turn the
   negative arm above into a returned slot.

   The stride is the literal IMUL EAX,dword ptr [EBP+0x14],0x50 at 0002cced,
   which is sizeof(struct fdps_unit_record).  unit_index is not checked against
   data_fdps_map_unit_count or against anything else. */
int fdps_unit_select_status_icon(int unit_index, int cycle)
{
    /* status_timers[1], [0], [2], [3] and [5] of struct fdps_unit_record, in
       icon order.  Written as the local's initialiser because that is what the
       compiler turns into the read-only five-byte copy at 0002b28e and the
       MOVSD/MOVSB that fetches it onto the frame. */
    unsigned char status_field_offsets[5] = { 0x23, 0x22, 0x24, 0x25, 0x27 };
    unsigned char *unit_record;
    int active_count;
    int slot;

    active_count = 0;
    unit_record = data_fdps_map_unit_array_ptr +
                  unit_index * (int) sizeof(struct fdps_unit_record);

    for (slot = 0; slot < 5; slot++) {
        if (unit_record[status_field_offsets[slot]] != 0) {
            active_count++;
        }
    }

    if (active_count == 0) {
        return -1;
    }

    cycle = cycle % active_count + 1;

    for (slot = 0; slot < 5; slot++) {
        if (unit_record[status_field_offsets[slot]] != 0) {
            cycle--;
            if (cycle == 0) {
                return slot;
            }
        }
    }

    return -1;
}
