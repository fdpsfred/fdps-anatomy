/* death.c -- the death-script pipeline: collecting what the units just killed
 * owe, playing the destruction sequence, and paying the collected records out.
 *
 * See death.h for what each entry point is asked and what its answer means,
 * and for why the collect has to run before the animation.  Nothing here owns
 * state: the battle units are reached through unit.h and their count through
 * gamedata.h.
 */
#include <string.h>
#include "fdpstype.h"
#include "gamedata.h"
#include "unit.h"
#include "death.h"

/* How wide one collected record is: PUSH 0x3 at 000261f2 is memmove's byte
   count, and LEA EAX,[EAX+EAX*0x2] at 000261fe scales the output index by the
   same three.  It is the opcode byte at record offset 0x31 followed by the
   signed 16-bit operand at 0x32, copied as three raw bytes rather than field
   by field. */
#define DEATH_SCRIPT_RECORD_BYTES 3

/* The "no script" sentinel: MOV AL,byte ptr [EAX+0x31] / AND EAX,0xff / CMP
   EAX,0xff at 000261d5.  The byte is widened without sign before the compare,
   so this is an equality against 255 and not a test for a negative opcode; an
   opcode of 0x80 is a real script and is collected. */
#define DEATH_SCRIPT_NONE 0xff

/* 00026180.  One walk over the battle unit array with three rejections and a
   packed append.

   The bound is data_fdps_map_unit_count compared with CMP EAX,[0x00060150] /
   JL at 0002619d, so index and count are both signed and the test runs before
   the body: a count of 0 or below returns 0 without resolving a record.

   The three tests each branch to the same place, the loop's increment at
   00026213, which is what the three continues below are.  In order: AND AL,0x1
   / TEST EAX,EAX / JNZ at 000261c7 on the flags byte at record offset 5 -- bit
   0 alone, so bit 7, the acted-this-turn flag, does not disqualify anyone; the
   sentinel compare at 000261d5; and CMP word ptr [EAX+0x40],0x0 / JLE at
   000261e9 on the hit-point word.  That last one is JLE and not JL, so a unit
   sitting at exactly zero HP qualifies, and it is the signed compare over a
   signed word, so a unit driven below zero qualifies as well -- reading
   hp_current unsigned turns -1 into 65535 and loses every overkill.

   The retirement test reads the flag bit inline rather than calling
   fdps_unit_is_retired; there is no CALL between the record lookup and the
   sentinel compare.

   The record pointer comes back from CALL 0x0002d210 into [EBP-0x8] and is
   re-resolved on every iteration rather than stepped by 0x50, which is what
   keeps the walk correct across an array that has moved.

   The append destination is out_scripts + script_count * 3 -- the OUTPUT
   counter, not the unit index -- so the records land packed from the front
   with no gaps whatever spread of units qualified.  Nothing bounds that write;
   see death.h for why the missing check stays missing. */
int fdps_collect_death_scripts(unsigned char *out_scripts)
{
    struct fdps_unit_record *unit;
    int unit_index;
    int script_count;

    script_count = 0;
    for (unit_index = 0; unit_index < data_fdps_map_unit_count; unit_index++) {
        unit = fdps_get_unit_record(unit_index);
        if ((unit->flags & 1) != 0) {
            continue;
        }
        if (unit->death_script_opcode == DEATH_SCRIPT_NONE) {
            continue;
        }
        if (unit->hp_current > 0) {
            continue;
        }
        memmove(out_scripts + script_count * DEATH_SCRIPT_RECORD_BYTES,
                &unit->death_script_opcode, DEATH_SCRIPT_RECORD_BYTES);
        script_count = script_count + 1;
    }
    return script_count;
}

/* The narrow opcode window this collector accepts, inclusive at both ends:
   CMP EAX,0x2 / JGE at 00027549 and CMP EAX,0x5 / JLE at 0002755b.  Both
   compares run on the opcode byte after AND EAX,0xff, so the widening carries
   no sign and 0xff, the "no script" sentinel, falls outside the window without
   needing a test of its own.

   2, 3, 4 and 5 are exactly the opcodes fdps_run_death_scripts executes with
   no further question -- the chapter-event call, the scripted line, and the
   two battle-end verdicts.  0 and 1, the item and the gold, are the two it
   guards on the unit index it was handed, and that guard's failure is a return
   out of the whole executor rather than a skip of the one record. */
#define DEATH_EVENT_OPCODE_MIN 2
#define DEATH_EVENT_OPCODE_MAX 5

/* 000274e0.  The same walk as fdps_collect_death_scripts with the sentinel
   test replaced by the window above, so the map AI's spell kills pay out the
   scripted chapter events and battle-end verdicts and nothing else.

   The bound is data_fdps_map_unit_count read through CMP EAX,[0x00060150] / JL
   at 000274fd, signed and tested before the body, so a count of 0 or below
   returns 0 without resolving a record.

   The three rejections all branch to the loop's increment at 0002750a, which
   is what the four continues below are.  In assembly order: AND AL,0x1 / TEST
   EAX,EAX / JNZ at 00027527 on bit 0 of the flags byte at record offset 5 --
   bit 0 alone, so bit 7, the acted-this-turn flag, disqualifies nobody; CMP
   word ptr [EAX+0x40],0x0 / JLE at 00027535 on the hit-point word, JLE and not
   JL so a unit resting at exactly zero HP qualifies, and a signed compare over
   a signed word so an overkilled unit qualifies too -- reading hp_current
   unsigned turns -1 into 65535 and loses every unit the fight took past zero;
   and then the two ends of the opcode window.

   The record pointer comes back from CALL 0x0002d210 into [EBP-0x8] and is
   re-resolved every iteration rather than stepped by 0x50, so the walk stays
   correct across an array that has moved.

   The append destination is out_events + event_count * 3 -- the OUTPUT
   counter, not the unit index -- so the records land packed from the front
   with no gaps whatever spread of units qualified.  Nothing bounds that write;
   see death.h for why the missing check stays missing. */
int fdps_collect_death_script_events(unsigned char *out_events)
{
    struct fdps_unit_record *unit;
    int unit_index;
    int event_count;

    event_count = 0;
    for (unit_index = 0; unit_index < data_fdps_map_unit_count; unit_index++) {
        unit = fdps_get_unit_record(unit_index);
        if ((unit->flags & 1) != 0) {
            continue;
        }
        if (unit->hp_current > 0) {
            continue;
        }
        if (unit->death_script_opcode < DEATH_EVENT_OPCODE_MIN) {
            continue;
        }
        if (unit->death_script_opcode > DEATH_EVENT_OPCODE_MAX) {
            continue;
        }
        memmove(out_events + event_count * DEATH_SCRIPT_RECORD_BYTES,
                &unit->death_script_opcode, DEATH_SCRIPT_RECORD_BYTES);
        event_count = event_count + 1;
    }
    return event_count;
}
