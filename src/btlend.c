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

/* 0003a2e0.  The outer gate, one walk and one unguarded defeat store, in that
   order.

   The gate is CMP dword ptr [0x00069da0],0x0 / JNZ 0003a39a at 0003a2ec: a
   non-zero code jumps straight to the epilogue, so nothing below runs and a
   verdict some chapter event already recorded survives untouched.  The compare
   is against zero for equality, so the code's declared unsignedness does not
   enter it.

   MOV dword ptr [0x00069da0],0x2 at 0003a2f9 writes the cleared verdict up
   front, before anything has been examined; the walk's job is to take it away
   again.

   The walk is bounded by CMP EAX,[0x00060150] / JL at 0003a30d, so index and
   count are both signed and the test runs before the body -- a count of 0 or
   below examines no record.  Its two tests are spelled out here rather than
   asked of unit.h: CMP byte ptr [EAX+0x6],0x0 / JNZ at 0003a331 reads the side
   byte directly, and MOV AL,byte ptr [EAX+0x5] / AND AL,0x1 / AND EAX,0xff at
   0003a33a reads bit 0 of the flags byte in place instead of calling
   fdps_unit_is_retired with the index.  The && below keeps that order and the
   short circuit the JNZ gives it.  There is no early exit: the loop always
   runs to the count, and because nothing ever puts the 2 back, a single live
   enemy anywhere in the array settles the code at 0.

   The defeat test at 0003a356 is reached by falling out of the loop and not by
   any branch that knows what the loop found, so it is sequential and not an
   else.  The chapter id decides which slot it asks about -- 0x10 and 0x15
   take PUSH 0x3 at 0003a368, everything else PUSH 0x0 at 0003a382 -- and both
   arms store 1 with no test of the current code, which is why a defeat
   outranks a victory recorded moments earlier.  Rewriting this as
   if (victory) ... else if (defeat) ... inverts exactly that case: when the
   last enemy falls in the same action that retires the watched unit, the
   original records 1 and the rewrite records 2.

   Both stores go to the same global, so the last one to run is the answer. */
void fdps_battle_check_default_end_conditions(void)
{
    struct fdps_unit_record *unit;
    int unit_index;

    if (data_fdps_chapter_event_or_battle_end_code != 0) {
        return;
    }

    data_fdps_chapter_event_or_battle_end_code = 2;
    for (unit_index = 0; unit_index < data_fdps_map_unit_count; unit_index++) {
        unit = fdps_get_unit_record(unit_index);
        if (unit->side == 0 && (unit->flags & 1) == 0) {
            data_fdps_chapter_event_or_battle_end_code = 0;
        }
    }

    if (data_fdps_chapter_current_chapter_id == 0x10 ||
        data_fdps_chapter_current_chapter_id == 0x15) {
        if (fdps_unit_is_retired(3) != 0) {
            data_fdps_chapter_event_or_battle_end_code = 1;
        }
    } else {
        if (fdps_unit_is_retired(0) != 0) {
            data_fdps_chapter_event_or_battle_end_code = 1;
        }
    }
}
