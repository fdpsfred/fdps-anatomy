/* chevt6.c -- the scripted chapter-event handlers of chapters 28 to 30.
 *
 * These are slots of the chapter-event handler table at 000601c4, called only
 * through it: a byte out of the loaded map file picks the slot and the
 * dispatchers -- the turn-event runner, the tile-trigger hand-off slot and the
 * death-script runner -- call it indirectly, so none of them appears as a
 * static caller.
 *
 * See chevt6.h for what each handler does.  chevt1.c is the same family for
 * chapters 2 to 7, chevt2.c for 8 to 14 and chevt3.c for 15 to 19.  Nothing
 * here owns state.
 */
#include "fdpstype.h"
#include "unit.h"
#include "chevt6.h"

/* The half of the AI byte the merge below keeps: AND DL,0xf0 at 0003964b.  The
   four bits it preserves are flags other code reads on their own -- 0x40 in
   fdps_map_actor_take_best_action and 0x80 in fdps_score_targets_for_item --
   while the four it drops are the behaviour code fdps_map_actor_behavior_step
   isolates with AND AL,0xf and dispatches on.  src/unit.c, src/chevt1.c,
   src/chevt2.c and src/chevt3.c spell the same mask out for the same field; it
   stays file-local at every end because no header owns it. */
#define AI_BEHAVIOR_FLAG_NIBBLE 0xf0

/* The behaviour code the merge ORs in, and it is 0: the constant parked at
   [EBP-0x1c] at 00039609 is 0x0, copied on into [EBP-0x18] at 0003961f, which
   is the slot MOV DH,byte ptr [EBP-0x18] at 00039651 reads.  Mode 0 is the
   default chain that paths a unit toward the nearest opposing unit; mode 2,
   which is what map28.dat deploys these units in, holds position and only
   scores attacks it can already make. */
#define AI_BEHAVIOR_MODE_ADVANCE 0

/* The other behaviour code this file's handlers OR in, and it is 0x0a: the
   constant parked at [EBP-0x30] at 0003970d, copied on into [EBP-0x3c] at
   00039723, which is the slot MOV DH,byte ptr [EBP-0x3c] at 00039755 reads.
   Mode 0x0a scores an item and uses it when the score reaches 6, then takes an
   attack of its own in the same step, and it never reaches the raw-distance
   pathing mode 0 falls through to, so a unit in it works from where it stands.
   It stays file-local because no header owns it. */
#define AI_BEHAVIOR_MODE_ITEM_THEN_ATTACK 0x0a

/* The side byte value the gate at 000395f6 tests for.  2 is the player side, 0
   the enemy and 1 the neutral one; fdps_roster_add_character writes 2 into the
   byte at record offset 6 and fdps_deploy_spawn_unit copies it from byte 0 of
   the scenario deployment record.  src/combat.c, src/deploy.c and src/unitatk.c
   spell the same constant out; it stays file-local at every end because no
   header owns it. */
#define PLAYER_SIDE 2

/* 000395d0.  Chapter 29's mid-map ambush trigger: the enemy groups holding the
   right, lower-right and upper-middle of the map stop guarding their ground and
   start hunting the party, but only when the unit that walked over the trigger
   tile is one of the party's own.

   The body is a side gate in front of one copy of the inline expansion the
   chapter 2, 5, 7, 13, 15 and 16 handlers in chevt1.c, chevt2.c and chevt3.c
   carry -- fdps_object_set_field34_low_nibble_range (00036b60) with the
   constant argument triple (0x24, 0x59, 0) -- and it has the same fingerprint:
   the three constants are parked at [EBP-0x24], [EBP-0x20] and [EBP-0x1c]
   (000395fb..00039609), copied into a second set of slots at [EBP-0x10],
   [EBP-0x14] and [EBP-0x18] (00039610..0003961f), and only then is the counter
   at [EBP-0xc] seeded from the first of them at 00039622.  There is no CALL to
   that helper in the body; the only CALLs are the two to fdps_get_unit_record,
   at 000395e0 for the acting unit and at 0003963a once per iteration, so
   writing the range as a call to the helper would put a CALL in the rebuild
   that the original does not make.

   The gate is MOV EAX,[EBP-0x4] / MOV AL,byte ptr [EAX+0x6] / AND EAX,0xff /
   CMP EAX,0x2 / JNZ 0003965b at 000395eb..000395f9: the side byte is
   zero-extended to a full word before the compare, so it is an unsigned
   equality on the byte and nothing but the value 2 reaches the loop.  The jump
   target is the epilogue, so a non-player unit crossing the same tile leaves
   the map exactly as it was.  This is what separates this handler from the rest
   of the family: its argument is not overwritten with 0 on entry, it is pushed
   at 000395df and the record that comes back decides whether anything happens.

   The record pointer for the acting unit comes back in EAX from the CALL at
   000395e0 and is stored to [EBP-0x4] at 000395e8; the loop's record pointer
   comes back in EAX from the CALL at 0003963a and is stored to a different slot
   at [EBP-0x8] (00039642), so the loop does not disturb the acting unit's
   pointer -- two separate locals, not one reused.

   The compare at 0003962b -- CMP EAX,dword ptr [EBP-0x14] / JLE 00039637 -- is
   signed and inclusive, so the range is unit indices 0x24 through 0x59 and 0x59
   is the last index written, not one past the end.  map28.dat declares 12
   player slots in byte 1 and 80 scenario units in byte 2, so unit indices
   0x00..0x0b are the party and 0x0c..0x5b are deployment records 0..79; the
   range is records 24..77.  Records 24..59 carry behaviour byte 2 at deployment
   record offset 0x11 and are the 36 units this event releases, records 60..77
   already carry 0 and advanced from the first turn, and the top bound stops one
   short of unit index 0x5a: deployment records 78 and 79, the two level 30
   Guardian Dragons the chapter is named for, also carry behaviour 2 and are
   left holding their ground for the map's second trigger, slot 46, to release.
   Nothing is range checked and data_fdps_map_unit_count is not consulted; both
   bounds are literals in the instruction stream.

   The merge is the same read-modify-write of the one byte as its siblings --
   MOV DL,[EAX+0x34] / AND DL,0xf0 / MOV DH,[EBP-0x18] / OR DH,DL /
   MOV [EAX+0x34],DH at 00039648..00039656 -- so the behaviour code goes to 0
   and the two AI flag bits in the high nibble are carried across untouched.

   There is no one-shot latch: the instruction after the gate's JNZ is the first
   of the three constant stores, with no compare between them, and nothing
   records that the loop ran.  What makes it fire once is that the tile-trigger
   hand-off arms the slot and the turn driver resets it to 0xff before the next
   unit acts.

   Nothing sets EAX before the RET at 00039661 and no dispatcher reads what
   comes back, so the result is void. */
void fdps_chapter_29_event_activate_enemy_groups(int unit_index)
{
    struct fdps_unit_record *acting_unit;
    struct fdps_unit_record *released_unit;
    int released_unit_index;

    acting_unit = fdps_get_unit_record(unit_index);
    if (acting_unit->side == PLAYER_SIDE) {
        for (released_unit_index = 0x24;
             released_unit_index <= 0x59;
             released_unit_index++) {
            released_unit = fdps_get_unit_record(released_unit_index);
            released_unit->ai_behavior = (unsigned char)
                ((released_unit->ai_behavior & AI_BEHAVIOR_FLAG_NIBBLE) |
                 AI_BEHAVIOR_MODE_ADVANCE);
        }
    }
}

/* 00039670.  Chapter 29's final ambush trigger, and the twin of the handler
   above: the same side gate, but behind it two of the family's inline range
   merges back to back instead of one, and between them they cover every unit
   map28.dat deploys.

   The gate is MOV EAX,[EBP-0x4] / MOV AL,byte ptr [EAX+0x6] / AND EAX,0xff /
   CMP EAX,0x2 / JNZ 0003975f at 0003968b..00039699 -- the record's side byte is
   zero-extended to a full word before the compare, so it is an unsigned
   equality on the byte and only the value 2 reaches the loops.  The jump target
   is the epilogue, so an enemy or neutral unit crossing the same tile leaves
   the map exactly as it was.  As in the handler above the argument is not
   overwritten on entry: it is pushed at 0003967f and the record that comes back
   decides whether anything happens.

   Both bodies are inline expansions of fdps_object_set_field34_low_nibble_range
   (00036b60) with the same fingerprint the rest of the family carries -- the
   three constants parked at one set of slots and copied into a second set
   before the counter is seeded from the first of them.  The first expansion
   parks (0x0c, 0x59, 0) at [EBP-0x24], [EBP-0x20] and [EBP-0x1c]
   (0003969f..000396ad), copies them to [EBP-0x10], [EBP-0x14] and [EBP-0x18]
   (000396b4..000396c3) and seeds [EBP-0xc] at 000396c6.  The second parks
   (0x5a, 0x5b, 0x0a) at [EBP-0x28], [EBP-0x2c] and [EBP-0x30]
   (000396ff..0003970d), copies them to [EBP-0x34], [EBP-0x38] and [EBP-0x3c]
   (00039714..00039723) and seeds [EBP-0x40] at 00039726.  There is no CALL to
   that helper anywhere in the body; the only CALLs are the three to
   fdps_get_unit_record, at 00039680 for the acting unit and at 000396de and
   0003973e once per iteration of each loop, so writing either range as a call
   to the helper would put a CALL in the rebuild the original does not make.

   The three record pointers come back in EAX and go to three different slots --
   [EBP-0x4] at 00039688, [EBP-0x8] at 000396e6 and [EBP-0x44] at 00039746 -- so
   neither loop disturbs the acting unit's pointer.  Both compares are signed
   and inclusive: CMP EAX,[EBP-0x14] / JLE at 000396cc..000396d2 and CMP
   EAX,[EBP-0x38] / JLE at 0003972c..00039732, so 0x59 and 0x5b are the last
   indices written rather than one past the end.  Nothing is range checked and
   data_fdps_map_unit_count is not consulted; all four bounds are literals in
   the instruction stream.

   Both merges are the same read-modify-write of the one byte -- MOV
   DL,[EAX+0x34] / AND DL,0xf0 / MOV DH,[EBP-0x18 or -0x3c] / OR DH,DL / MOV
   [EAX+0x34],DH at 000396ec..000396fa and 0003974c..0003975a -- so only the
   behaviour code in the low nibble moves and the two AI flag bits above it are
   carried across untouched.

   What the two ranges mean comes from map28.dat: 12 player slots in byte 1 and
   80 scenario units in byte 2, so unit indices 0x00..0x0b are the party and
   0x0c..0x5b are deployment records 0..79.  The two ranges are disjoint: the
   first is 0x0c..0x59, deployment records 0..77, on mode 0, and the second is
   0x5a..0x5b, records 78 and 79 -- the two level 30 Guardian Dragons -- on mode
   0x0a.  Together they are all 80 records with each written exactly once, and
   running the two loops in the other order would leave the same state.

   There is no one-shot latch: the instruction after the gate's JNZ is the first
   of the first expansion's constant stores, with no compare between them, and
   nothing records that the loops ran.  What makes it fire once is that the
   tile-trigger hand-off arms the slot and the turn driver resets it to 0xff
   before the next unit acts.

   Nothing sets EAX before the RET at 00039765 and no dispatcher reads what
   comes back, so the result is void. */
void fdps_chapter_29_event_activate_all_enemies(int unit_index)
{
    struct fdps_unit_record *acting_unit;
    struct fdps_unit_record *released_unit;
    struct fdps_unit_record *guardian_unit;
    int released_unit_index;
    int guardian_unit_index;

    acting_unit = fdps_get_unit_record(unit_index);
    if (acting_unit->side == PLAYER_SIDE) {
        for (released_unit_index = 0x0c;
             released_unit_index <= 0x59;
             released_unit_index++) {
            released_unit = fdps_get_unit_record(released_unit_index);
            released_unit->ai_behavior = (unsigned char)
                ((released_unit->ai_behavior & AI_BEHAVIOR_FLAG_NIBBLE) |
                 AI_BEHAVIOR_MODE_ADVANCE);
        }
        for (guardian_unit_index = 0x5a;
             guardian_unit_index <= 0x5b;
             guardian_unit_index++) {
            guardian_unit = fdps_get_unit_record(guardian_unit_index);
            guardian_unit->ai_behavior = (unsigned char)
                ((guardian_unit->ai_behavior & AI_BEHAVIOR_FLAG_NIBBLE) |
                 AI_BEHAVIOR_MODE_ITEM_THEN_ATTACK);
        }
    }
}
