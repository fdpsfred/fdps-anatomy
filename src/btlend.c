/* btlend.c -- the end of a battle: the win and fail tests and the window that
 * reports the outcome.
 *
 * See btlend.h for what each entry point is asked and what its answer means.
 * Nothing here owns state: the battle units are reached through unit.h and
 * their count through gamedata.h.
 */
#include "fdpstype.h"
#include "gamedata.h"
#include "unit.h"
#include "btlend.h"

/* 00018350.  One walk over the battle unit array with two tests per unit and a
   counter.

   The bound is data_fdps_map_unit_count compared with CMP EAX,[0x00060150] /
   JL at 0001836d, so both the index and the count are signed and the test runs
   before the body: a count of 0 or below returns 0 without resolving a single
   record.

   The side test is the record byte at +6 widened by MOV AL / AND EAX,0xff at
   00018391 and then compared with the argument by CMP / JNZ at 00018399.  Zero
   extension is behaviour and not spelling: reading the side byte through a
   signed char would make a code of 0x80 or above compare as a negative number
   and never match the positive code a caller passes.  The comparison is an
   equality, so a side code is matched exactly and nothing is treated as a
   range or as a truth value.

   The retirement test is only reached when the side matched -- the JNZ at
   0001839c jumps past the CALL -- so a unit of another side is never handed to
   fdps_unit_is_retired.  The && below keeps that order and that short circuit.
   The call's answer is taken from EAX and tested against zero alone (TEST
   EAX,EAX / JZ at 000183aa), so the counter is bumped for a unit the flag
   reports as still in the battle.

   The record pointer is re-resolved on every iteration rather than stepped by
   0x50, which is what the original does and what keeps the walk correct across
   an array that has moved. */
int fdps_battle_count_remaining_units_on_side(int side)
{
    struct fdps_unit_record *unit;
    int unit_index;
    int remaining;

    remaining = 0;
    for (unit_index = 0; unit_index < data_fdps_map_unit_count; unit_index++) {
        unit = fdps_get_unit_record(unit_index);
        if (unit->side == side && fdps_unit_is_retired(unit_index) == 0) {
            remaining = remaining + 1;
        }
    }
    return remaining;
}
