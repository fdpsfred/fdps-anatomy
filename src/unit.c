/* unit.c -- core access to one map unit record: lookup, flags and the small
 * derived readouts the rest of the game asks for a unit at a time.
 *
 * See unit.h.  Every function here reaches its record through
 * data_fdps_map_unit_array_ptr and works on what it finds; the file owns no
 * state.
 */
#include <stdlib.h>
#include <string.h>
#include "fdpstype.h"
#include "gamedata.h"
#include "table.h"
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

/* 000109b0.  The retired predicate, and the body is straight-line: PUSH EAX /
   CALL 0x0002d210 / ADD ESP,0x4 resolves the record, then MOV AL,byte ptr
   [EAX + 0x5] / AND AL,0x1 / AND EAX,0xff reads the flag byte and hands back
   bit 0.  No compare and no branch anywhere in it.

   The record offset is the literal 0x5, which is struct fdps_unit_record's
   flags byte, and bit 0 of that byte is the retired flag.  Bit 7 of the same
   byte is a different, shorter-lived per-turn redraw flag -- masking the byte
   with anything wider than 1, or testing it for non-zero, would report a unit
   as retired whenever that other bit happened to be set.

   The two ANDs are both load-bearing as a pair: AND AL,0x1 isolates the bit
   and AND EAX,0xff clears the upper three bytes of EAX, which at that point
   still hold the top of the record POINTER the CALL returned rather than
   anything derived from the flags.  The value the function returns is
   therefore exactly 0 or 1 and never the flag byte itself, and callers rely on
   the narrowing both ways round: fdps_battle_count_remaining_units_on_side at
   000183aa does TEST EAX,EAX / JZ to count the units this returns zero for.

   The record is resolved through fdps_get_unit_record on every call rather
   than taken as a pointer, so the same re-resolution rule applies here as
   there; unit_index is not range checked at either end and the bound is the
   caller's. */
int fdps_unit_is_retired(int unit_index)
{
    struct fdps_unit_record *record;

    record = fdps_get_unit_record(unit_index);
    return record->flags & 1;
}

/* 000138f0.  The retiring half of the pair above, and the body is straight-line
   with no compare and no branch in it: MOV EAX,dword ptr [EBP + 0x14] / PUSH
   EAX / CALL 0x0002d210 / ADD ESP,0x4 resolves the record, then MOV byte ptr
   [EAX + 0x5],0x1 (bytes c6 40 05 01) stores into the flags byte.  Nothing is
   read back and nothing is returned.

   The store is a whole-byte ASSIGNMENT of the literal 1, not a bit set: the
   encoding is the immediate move c6 40 05 01, an OR would be 80 48 05 01.  So
   retiring a unit also clears bit 7 of the same byte, the acted-this-turn flag
   that fdps_battle_mark_unit_done raises, along with every other bit that was
   standing.  Writing record->flags |= 1 by analogy with that function reads
   more carefully and is wrong: it would leave a retired unit carrying the
   per-turn flag.  The identical four bytes appear at ten further sites -- the
   inline retire in fdps_map_actor_behavior_step at 000104b8,
   fdps_play_death_animation_and_mark_dead at 0001d7f9, the initial state
   fdps_build_map_unit_array writes at 00022d36, and the chapter scene handlers
   at 00037724, 00037c69, 00037c7f, 00038c5b, 0003b064, 0003b39f and 0003b5b9
   -- so the idiom is the game's, not this function's.

   The record offset is the literal 0x5, which is struct fdps_unit_record's
   flags, and the record is resolved through fdps_get_unit_record on every call,
   so the same re-resolution rule applies here as there.  unit_index is not
   range checked at either end -- data_fdps_map_unit_count is not read in the
   body -- and the accessor's signed multiply carries a negative index
   backwards off the front of the array and stores there.

   Nothing in the image calls this function or takes its address: get_xrefs_to
   at 000138f0 finds no reference at all.  It is compiled in and unreferenced,
   which is why every one of the ten sites above spells the two steps out
   inline instead. */
void fdps_unit_mark_retired(int unit_index)
{
    struct fdps_unit_record *record;

    record = fdps_get_unit_record(unit_index);
    record->flags = 1;
}

/* 00012550.  The flying predicate.  PUSH EAX / CALL 0x0002d210 / ADD ESP,0x4
   resolves the record, then XOR EAX,EAX / MOV AL,byte ptr [EDX + 0x20] reads
   the class code and spills it to the frame local at [EBP-0x8], and five
   CMP dword ptr [EBP + -0x8],<code> with JZ/JNZ pick between MOV dword ptr
   [EBP + -0x4],0x1 and the same store of 0.

   The record offset is the literal 0x20, which is struct fdps_unit_record's
   clazz, and the five codes are 0x16 技師, 0x17 機械伯爵, 0x18 機械大師,
   0x1f 飛兵 and 0x25 惡靈.  0x26 活屍 is NOT one of them, and neither is
   0x19 機兵: the chain compares against exactly those five values and against
   nothing else.

   The widening is XOR EAX,EAX before the byte load, so the class code is
   zero-extended and the comparisons are made on a full dword.  Every
   comparison is an equality, so the byte's signedness cannot reach the answer
   for the five codes involved, but the field is unsigned and is read as such.

   Callers take the answer as a plain truth value: all six call sites --
   00013ba6, 0001a0b9, 0001a10f, 0001c61b, 0001c671 and 00028416 -- follow the
   CALL with ADD ESP,0x4 / TEST EAX,EAX / JNZ, so the two combat resolvers skip
   the terrain modifier on 1 and the two spell paths fail 裂地術 and 封神裂震
   outright against a flying target.

   Rebuild note: the flying set is this hard-coded list of five class codes and
   is not derived from PROMAP.DAT.  The same five are the classes whose class
   record leaves the sixth terrain column passable (assets/classes.md), so a
   lookup of that column agrees with the chain on the shipped data file and
   reads cleaner -- and it silently makes the data file able to change which
   classes ignore terrain modifiers and which ones the two ground-shock spells
   can hit, which the original binary cannot do.

   The record is resolved through fdps_get_unit_record on every call, so the
   same re-resolution rule applies here as there; unit_index is not range
   checked at either end and the bound is the caller's. */
int fdps_unit_is_flying(int unit_index)
{
    struct fdps_unit_record *record;
    int unit_class;

    record = fdps_get_unit_record(unit_index);
    unit_class = record->clazz;

    if (unit_class == 0x16 || unit_class == 0x17 || unit_class == 0x18 ||
        unit_class == 0x1f || unit_class == 0x25) {
        return 1;
    }

    return 0;
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

/* 0001c2e0.  Turns one unit to face another.  Both records are resolved through
   fdps_get_unit_record (PUSH EAX / CALL 0x0002d210 / ADD ESP,0x4 twice, the
   pointers spilled to [EBP-0x10] and [EBP-0xc]), the two tile coordinates are
   differenced and passed to the CRT abs at 0x0003d364, and one of four literal
   direction codes is stored into the acting unit's facing byte at record
   offset 3.  The target's record is never written.

   The axis test is CMP EAX,dword ptr [EBP + -0x4] / JLE at 0001c354, so the
   horizontal facing is taken only when the absolute x difference is STRICTLY
   greater than the absolute y one.  Ties fall into the vertical branch, which
   makes a target on a perfect diagonal turn the unit up or down, and makes a
   call with both units on the same tile force facing 0 rather than leave the
   facing where it was.  Writing the symmetric form -- x_distance >= y_distance
   picks horizontal -- flips every diagonal case and visibly turns sprites the
   wrong way.  It stays here rather than in rebuild_info/pitfalls.md because
   that file collects patterns that recur across functions and this one is
   local to this body.

   The four codes are the ones the movement playback writes into the same byte
   as it walks, which is what fixes their meaning: 0002d590 stores 1 and then
   DEC byte ptr [EAX] on pos_x, so 1 is -x/left; 0002d360 stores 0 and then
   INC byte ptr [EAX + 0x1] on pos_y, so 0 is +y/down; 2 is -y/up (0002d480)
   and 3 is +x/right (0002d6a0).  So facing 1 when the unit's x exceeds the
   target's is "the target is to my left", and the pairing is not arbitrary.

   The two coordinate compares are CMP DL,byte ptr [EAX] / JBE at 0001c360 and
   0001c380 -- unsigned, matching pos_x and pos_y being unsigned char.  The
   differences fed to abs are built by AND EDX,0xff / AND EAX,0xff before the
   SUB, so both operands are zero-extended and the subtraction happens at int
   width; a difference computed in unsigned char would wrap instead.

   abs is a real call here, not an inline NEG: the flag set carries no -oi, so
   __INLINE_FUNCTIONS__ is not defined and stdlib.h leaves it a call, which is
   the same treatment fdps_collect_targets_in_area gets in aitarget.c.

   Nothing is returned.  EAX is left holding the acting unit's record pointer
   at the RET and no caller reads it: all six call sites -- 00012eb0, 00012f13,
   00012f93, 00012fa5, 000272f9 and 00015f97 -- follow the CALL with ADD ESP,
   0x8 and then overwrite EAX.

   Neither index is range checked; both records are resolved through
   fdps_get_unit_record, so the same re-resolution rule applies here as
   there. */
void fdps_unit_face_target(int unit_index, int target_unit_index)
{
    struct fdps_unit_record *unit;
    struct fdps_unit_record *target;
    int x_distance;
    int y_distance;

    unit = fdps_get_unit_record(unit_index);
    target = fdps_get_unit_record(target_unit_index);

    x_distance = abs((int) unit->pos_x - (int) target->pos_x);
    y_distance = abs((int) unit->pos_y - (int) target->pos_y);

    if (x_distance > y_distance) {
        if (unit->pos_x > target->pos_x) {
            unit->facing = 1;
        } else {
            unit->facing = 3;
        }
    } else {
        if (unit->pos_y > target->pos_y) {
            unit->facing = 2;
        } else {
            unit->facing = 0;
        }
    }
}

/* How many inventory entries the equipment scan covers: CMP dword ptr
   [EBP-0x24],0x8 at 00024dc3.  All eight of the 2-byte entries at record
   +0x0a, not just the two an enrolled character carries flagged equipped. */
#define EQUIPMENT_SCAN_ENTRY_COUNT 8

/* The dexterity the +0x24 timer is worth while it runs: ADD dword ptr
   [EBP-0x18],0xf at 00024db2, applied to the seed BEFORE it is copied, so it
   lifts hit and evade alike. */
#define DEXTERITY_BUFF_BONUS 15

/* The attack and defense buff multiplier, held as two separate doubles at
   00061b1c and 00061b24 that hold the same value.  It is a DOUBLE multiply
   followed by a truncation toward zero (FILD / FMUL qword / CALL __CHP /
   FISTP), not percentage arithmetic on integers: the nearest double to 1.15
   is a shade below it, so a total of 100 comes out 114 and one of 200 comes
   out 229, where total * 115 / 100 would give 115 and 230
   (rebuild_info/pitfalls.md). */
#define STAT_BUFF_MULTIPLIER 1.15

/* 00024d70.  The battle side's stat recompute: base stats, plus every equipped
   item's four modifiers, plus whatever the three stat buffs are worth right
   now, stored back over the record's four derived stat words.

   The record is resolved through fdps_get_unit_record -- PUSH EAX / CALL
   0x0002d210 / ADD ESP,0x4 at 00024d80, a real call here where the roster half
   spells the same multiply out inline -- and is not re-resolved afterwards,
   because nothing in the body can move the array.

   Hit and evade are seeded from the ONE dexterity word.  MOVSX EAX,word ptr
   [EAX+0x3e] at 00024da2 lands in [EBP-0x18] and 00024db6 copies that same
   value into [EBP-0x14]; there is no second base field.  Reaching for +0x40
   for evade because the other stats are consecutive would read hp_current
   (rebuild_info/pitfalls.md).  The +15 goes on before the copy, so the
   dexterity buff raises both.

   Which item field goes where does not follow field order: [EBP-0x14] takes
   the item's +0x03 (hit) and is stored to record +0x4c, [EBP-0x18] takes +0x07
   (ev) and is stored to record +0x4e.

   All four accumulators are 32-bit and everything entering them arrives
   through MOVSX -- the three base stats and all four item modifiers (contract
   C) -- while the equipped flag byte, the item id byte and the three timer
   bytes are all read unsigned.  The four stores are word stores (MOV word ptr
   [EDX+0x48],AX), so a total outside 16 bits is truncated on the way into the
   record rather than saturated.

   The equipped test is bit 0x40 alone (AND AL,0x40 at 00024de6), so an entry's
   other flag bits decide nothing, and the id is zero-extended out of the
   entry's second byte (XOR EAX,EAX / MOV AL,byte ptr [EDX+0x1] at 00024df1),
   so ids run 0..255 and item 0xff reads past the end of ITEM.DAT unchecked.

   The three timer tests are CMP byte ptr [EAX+0x2x],0x0 / JZ -- a buff counts
   as running on any non-zero count, never on a particular one -- and the two
   multiplies happen AFTER the equipment sum, so a buff scales the equipped
   total and not the base.

   The loop counter is post-incremented: 00024dcb is MOV EAX,dword ptr
   [EBP-0x24] followed by INC dword ptr [EBP-0x24], the shape i++ produces at
   -od, where i = i + 1 would load, increment in EAX and store back.  The id
   goes through a frame slot of its own before the push (MOV dword ptr
   [EBP-0x10],EAX / MOV EAX,dword ptr [EBP-0x10] / PUSH EAX at 00024df9), which
   is the extra local the original's SUB ESP,0x24 pays for.

   unit_index is not bounded and no total is clamped.  This is the battle half
   of a pair: fdps_roster_recompute_combat_stats runs the identical base and
   equipment sum over a roster record and applies NONE of the three buffs. */
void fdps_unit_recompute_combat_stats(int unit_index)
{
    struct fdps_unit_record *unit;
    struct fdps_item_effect *item;
    unsigned char *inventory_entry;
    int equipped_item_id;
    int attack_total;
    int defense_total;
    int hit_total;
    int evade_total;
    int entry_index;

    unit = fdps_get_unit_record(unit_index);

    attack_total = (int) unit->ap_base;
    defense_total = (int) unit->dp_base;
    evade_total = (int) unit->dx_base;
    if (unit->status_timers[2] != 0) {
        evade_total += DEXTERITY_BUFF_BONUS;
    }
    hit_total = evade_total;

    for (entry_index = 0;
         entry_index < EQUIPMENT_SCAN_ENTRY_COUNT;
         entry_index++) {
        inventory_entry = &unit->inventory_slots[entry_index * 2];
        if ((inventory_entry[0] & 0x40) != 0) {
            equipped_item_id = (int) inventory_entry[1];
            item = fdps_get_item_record(equipped_item_id);
            attack_total += (int) item->ap;
            defense_total += (int) item->dp;
            hit_total += (int) item->hit;
            evade_total += (int) item->ev;
        }
    }

    if (unit->status_timers[0] != 0) {
        attack_total = (int) (attack_total * STAT_BUFF_MULTIPLIER);
    }
    if (unit->status_timers[1] != 0) {
        defense_total = (int) (defense_total * STAT_BUFF_MULTIPLIER);
    }

    unit->ap = (short) attack_total;
    unit->dp = (short) defense_total;
    unit->hit = (short) hit_total;
    unit->ev = (short) evade_total;
}

/* Both halves of the split are the same 8, and the assembly spells them
   differently: the byte is SAR EDX,0x1f / SHL EDX,0x3 / SBB EAX,EDX / SAR
   EAX,0x3 at 000282db, the compiler's signed divide by eight, and the bit is
   MOV EBX,0x8 / CDQ / IDIV EBX at 000282e9.  Eight is the bits in a bitmap
   byte, so an id's block and its position inside that block come out of one
   number and the ids run on across a byte boundary with no gap -- which is the
   same arithmetic fdps_unit_collect_known_spells inverts when it turns a set
   bit back into an id. */
#define SPELL_BITS_PER_BITMAP_BYTE 8

/* 000282b0.  Sets one bit of a unit's learned-spell bitmap.  Straight-line, no
   compare and no branch: the record is resolved through fdps_get_unit_record
   (PUSH EAX / CALL 0x0002d210 / ADD ESP,0x4 at 000282ca), the id is split into
   a byte and a bit, and OR byte ptr [EAX + 0x1a],DL at 00028309 puts the bit
   in.  Nothing is returned; EAX is left holding the record pointer and no
   caller reads it -- all five call sites do ADD ESP,0x8 and then overwrite it.

   The field is struct fdps_unit_record's spells_known_bitmap, the five bytes at
   record offset 0x1a, which is the literal displacement on that OR.  The id
   space it covers is therefore 0..39, and MAGICDAT.DAT holds exactly forty
   spells numbered 0x00..0x27 with no gap (assets/spells.md).  The four literal
   ids the image passes fill that span out: 0x27 萬神降臨 and 0x0b 封神裂震 in
   fdps_title_demo at 0002acdf and 0002aceb, 0x1d 轟神砲 in
   fdps_apply_item_effect_to_targets at 00026b85, and 0x00 業火 in
   fdps_chapter_01_end at 0003a41c.  The fifth site,
   fdps_unit_award_exp_and_level_up at 0001e0bd, passes an id it zero-extends
   out of the second byte of a two-byte level/spell table entry after matching
   the first byte against the unit's new level.

   The eight mask values are a local array initialiser, not a global table: the
   compiler expands it into the read-only eight bytes at 00027660 (01 02 04 08
   10 20 40 80) and the MOVSD pair at 000282c4 that copies them onto the frame,
   which is the same treatment the offsets table in
   fdps_unit_select_status_icon gets.  Writing the mask as 1 << (id % 8)
   computes the same value with a variable shift and no table.

   It ORs one bit and touches nothing else, so an id already known stays known
   and the unit's other spells survive.  Nothing bounds the id: an id of 40 or
   more indexes past the five bitmap bytes into race, clazz, level and the
   status timers, and a negative one reads the mask table off its front.
   Neither is reachable through the five call sites -- four are non-negative
   literals and the fifth is a zero-extended byte -- and a guard added here
   would be behaviour the original does not have.

   unit_index is a position in the current battle's unit array and is not range
   checked; the record is resolved through fdps_get_unit_record, so a call after
   the array has moved writes into the new block. */
void fdps_set_flag_bit(int unit_index, int spell_id)
{
    /* 01 02 04 08 10 20 40 80 at 00027660, ascending bit order, so index n is
       the mask for bit n. */
    unsigned char bit_mask[8] = {
        0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80
    };
    int bit_in_byte;
    int byte_index;
    struct fdps_unit_record *unit;

    unit = fdps_get_unit_record(unit_index);
    byte_index = spell_id / SPELL_BITS_PER_BITMAP_BYTE;
    bit_in_byte = spell_id % SPELL_BITS_PER_BITMAP_BYTE;

    unit->spells_known_bitmap[byte_index] |= bit_mask[bit_in_byte];
}

/* One map tile is 24 pixels square and the cursor globals are in pixels, so a
   division by this recovers the tile the cursor is on.  The literal is loaded
   into the divisor register twice, MOV EBX,0x18 at 0002dae8 and again at
   0002db02, once for each axis. */
#define UNIT_MAP_TILE_SIZE 0x18

/* 0002daa0.  Finds the unit standing under the map cursor.  Takes no
   arguments -- the six call sites push nothing and do no stack cleanup after
   the CALL, they just read EAX and compare it against -1 (MOV dword ptr
   [EBP-0x18],EAX / CMP dword ptr [EBP-0x18],-0x1 / JZ at 00013770 in
   fdps_collect_targets_in_line, and the same three at 0002bd2c, 0002de81 and
   the rest) -- and every input is a global.

   The walk is a plain linear scan of the map unit array from index 0.  The
   base is taken from data_fdps_map_unit_array_ptr once, at 0002daac, into the
   frame slot at [EBP-0x14], and from then on the record is stepped by ADD
   dword ptr [EBP-0x14],0x50 at 0002db38 rather than re-multiplied through
   fdps_get_unit_record.  Nothing the loop calls relocates the array, so the
   held pointer stays valid for the length of the scan.

   The bound is data_fdps_map_unit_count, re-read from 0x00060150 on every
   iteration, and the compare is JL at 0002dac4: signed, so a count of zero or
   a negative one ends the scan before the first record is looked at.

   The tile test is the cursor pixel divided by 24 against the record's own
   tile bytes: pos_x at record offset 0 (XOR EAX,EAX / MOV AL,byte ptr [EBX] at
   0002dad5) against data_fdps_map_cursor_world_x, then pos_y at offset 1
   against data_fdps_map_cursor_world_y.  Both record bytes are zero-extended,
   so they are the unsigned 0..255 the layout declares, while both cursor
   globals are divided with SAR EDX,0x1f / IDIV -- signed, truncating towards
   zero, so a negative cursor pixel down to -23 still names tile 0.  Both axes
   have to match; the x mismatch at 0002db00 jumps straight past the y test.

   A unit whose tile matches is only accepted when fdps_unit_is_retired says it
   has not left the battle: PUSH EAX / CALL 0x000109b0 / ADD ESP,0x4 / TEST
   EAX,EAX / JZ at 0002db1e, so a non-zero answer falls through to the pointer
   advance and the scan carries on.  Two units can therefore share a tile with
   only the live one found, and among several live ones the lowest index wins
   because the first match returns.

   Falling off the end stores -1 (0002db3e).  Every caller tests for exactly
   that value, so the result is an index or the sentinel and never a count. */
int fdps_battle_find_unit_at_cursor(void)
{
    struct fdps_unit_record *unit;
    int unit_index;
    int unit_tile_x;
    int unit_tile_y;

    unit = (struct fdps_unit_record *) data_fdps_map_unit_array_ptr;
    for (unit_index = 0;
         unit_index < data_fdps_map_unit_count;
         unit_index++) {
        unit_tile_x = (int) unit->pos_x;
        unit_tile_y = (int) unit->pos_y;
        if (data_fdps_map_cursor_world_x / UNIT_MAP_TILE_SIZE == unit_tile_x
            && data_fdps_map_cursor_world_y / UNIT_MAP_TILE_SIZE
               == unit_tile_y
            && fdps_unit_is_retired(unit_index) == 0) {
            return unit_index;
        }
        unit++;
    }

    return -1;
}

/* 0002db50.  Clears the acted-this-turn flag on every unit in the battle, so
   that whatever raised it is forgotten and all of them may act again.  Takes
   no arguments and returns nothing: all seven call sites -- 0002adcb and
   0002adea in fdps_title_demo, 00014d58 in fdps_battle_system_menu, 0001e5b5
   and 0001e61b in fdps_battle_advance_turn, 0002166e in fdps_icon_script_run
   and 0002126e in fdps_chapter_15_init -- push nothing before the CALL, clean
   nothing off the stack after it and read no result out of EAX.

   The record address is formed inline, IMUL EAX,dword ptr [EBP-0x8],0x50 / MOV
   EDX,dword ptr [0x00069cd8] / ADD EDX,EAX at 0002db78, and not by a call to
   fdps_get_unit_record: there is no CALL anywhere in the body.  The base is
   therefore re-read from the global on every iteration, so a relocation of the
   array between two passes would be picked up -- the same property the
   accessor has, spelled out here rather than borrowed.

   The bound is data_fdps_map_unit_count, re-read from 0x00060150 on every pass
   (CMP EAX,dword ptr [0x00060150] / JL at 0002db66), and the compare is the
   signed JL, so a count of zero or a negative one leaves every record alone
   instead of walking the whole address space.

   AND byte ptr [EAX + 0x5],0x7f at 0002db8a is the whole of the work.  The
   flags byte at record offset 5 keeps bits 0 to 6 and loses bit 7 only, and
   both halves of that matter: bit 7 is the acted-this-turn flag
   fdps_battle_mark_unit_done raises, while bit 0 of the same byte is the
   retired flag.  A store of 0 here, or any wider mask, would put every unit
   that has left the battle back on the field. */
void fdps_units_clear_status_bit7(void)
{
    struct fdps_unit_record *unit;
    int unit_index;

    for (unit_index = 0;
         unit_index < data_fdps_map_unit_count;
         unit_index++) {
        unit = (struct fdps_unit_record *)
               (data_fdps_map_unit_array_ptr +
                unit_index * (int) sizeof(struct fdps_unit_record));
        unit->flags = (unsigned char) (unit->flags & 0x7f);
    }
}

/* 0002dc20.  Finds the unit standing on the map for one character id, and hands
   back two things: the index as the return value and the record itself through
   the caller's pointer.

   The two arguments are stack slots [EBP+0x14] and [EBP+0x18] under a PUSH EBX
   / PUSH ESI / PUSH EDI / PUSH EBP / MOV EBP,ESP prologue, and both call sites
   push the out pointer first and clean 8 bytes off themselves afterwards (LEA
   EAX,[EBP-0x10] / PUSH EAX / PUSH the id / CALL / ADD ESP,0x8 at 00020264 and
   the same shape at 00010183 and 000104d5), so character_id is the first
   argument and out_record the second.

   *out_record is cleared at 0002dc37, before the scan and before anything else,
   and the caller in fdps_draw_text reads it back on BOTH exits: CMP dword ptr
   [EBP-0x10],0x0 / JZ at 00020277 guards a read of the record's portrait_id at
   +7, and only after that does it compare the return value against -1.  So what
   the pointer is left holding is part of the answer and not a by-product.

   The walk is a plain linear scan from index 0.  The base is taken from
   data_fdps_map_unit_array_ptr once, at 0002dc2c, and the record is then stepped
   by ADD dword ptr [EBP-0xc],0x50 at 0002dc89 rather than re-multiplied; nothing
   the loop calls relocates the array, so the held pointer stays valid for the
   length of the scan.  The bound is data_fdps_map_unit_count, re-read from
   0x00060150 every iteration, and the compare is JL at 0002dc4d: signed, so a
   count of zero or a negative one leaves *out_record null and returns -1 without
   looking at a record.

   The match is on the record's char_id at +8, zero-extended (MOV AL,byte ptr
   [EAX + 0x8] / AND EAX,0xff at 0002dc5c) and compared for equality against the
   whole argument.  The zero extension is what keeps the domain 0..255: a
   character_id of -1 or of 0x100 matches nothing, where a sign-extended byte
   would make 0xff answer to -1 instead of to 255.  It is the id
   fdps_roster_add_character stamps into the record when the character joins,
   not the unit index and not the portrait id at +7 next door.

   A match publishes its record through out_record FIRST and only then asks
   fdps_unit_is_retired(record_index) -- PUSH EAX / CALL 0x000109b0 / ADD ESP,0x4
   / TEST EAX,EAX / JNZ at 0002dc75, so the value tested is that function's
   answer in EAX -- and a non-zero answer falls through to the pointer advance
   and carries the scan on.  The order matters at the -1 exit: a character whose
   every unit has retired returns -1 with *out_record still naming the LAST of
   them, which is the record fdps_draw_text then prints a portrait for.  Storing
   the pointer only for the unit that is finally returned would hand that caller
   a null instead.

   The guard after the scan tests the POINTER out_record against 0 (CMP dword
   ptr [EBP + 0x18],0x0 at 0002dc8f), not *out_record, so the roster scan behind
   it cannot run: 0002dc34 dereferenced that same pointer on entry and a null
   would have faulted there.  It is emitted as written because writing the
   condition the author plainly meant, *out_record == 0, revives the scan and
   changes what the callers see -- see the comment at the branch itself. */
int fdps_battle_find_unit_by_character_id(int character_id,
                                          struct fdps_unit_record **out_record)
{
    struct fdps_unit_record *record;
    int record_index;

    record = (struct fdps_unit_record *) data_fdps_map_unit_array_ptr;
    *out_record = (struct fdps_unit_record *) 0;

    for (record_index = 0;
         record_index < data_fdps_map_unit_count;
         record_index++) {
        if ((int) record->char_id == character_id) {
            *out_record = record;
            if (fdps_unit_is_retired(record_index) == 0) {
                return record_index;
            }
        }
        record++;
    }

    /* Unreachable, and reproduced unreachable.  The original compares the out
       pointer itself with zero here; every caller passes the address of one of
       its own locals and the pointer was dereferenced at the top of this
       function, so the branch is never taken.  Rewriting the test as
       *out_record == 0 -- the condition this roster scan was plainly meant to
       have -- would make a character who is not on the map come back with a
       party-roster record instead of a null pointer, and fdps_draw_text reads
       the record it is given on exactly that path. */
    if (out_record == (struct fdps_unit_record **) 0) {
        record = (struct fdps_unit_record *) data_fdps_roster_array_ptr;
        for (record_index = 0;
             record_index < data_fdps_roster_member_count;
             record_index++) {
            if ((int) record->char_id == character_id) {
                *out_record = record;
            }
            record++;
        }
    }

    return -1;
}

/* The stride of one unit record as this routine writes it: IMUL EAX,EAX,0x50 at
   0002dfc2 sizes the new block and SUB EAX,0x50 at 0002dff0 takes one record
   back off it for the wipe.  A literal and not sizeof(struct fdps_unit_record)
   for the reason deploy.c gives: 0x50 is the block's own layout and the struct
   agrees with it only while it stays byte-packed (rebuild_info/pitfalls.md). */
#define UNIT_RECORD_STRIDE 0x50

/* MOV EBX,0x400 at 0002dfa3, the divisor of the IDIV that sizes the spacer
   block.  Nothing ever reads that block; only its being held across the array's
   own malloc matters, so the size is a displacement and not a capacity. */
#define SPACER_SIZE_MODULUS 0x400

/* 0002df90.  Moves the whole unit-record array to a fresh heap block and
   destroys the copy left behind, so no record keeps one address for longer than
   a single unit's turn.  Takes nothing and reports nothing: the new base is
   published through data_fdps_map_unit_array_ptr, and the three callers --
   fdps_battle_unit_turn at 000154e0, fdps_battle_enemy_turn_phase at 00012995
   and fdps_battle_npc_turn_phase at 00012b5f -- each overwrite EAX with their
   own loop counter on the instruction after the CALL and then re-resolve their
   record through fdps_get_unit_record.  EAX does still hold the new block at
   the RET, but nothing reads it and the function is void.

   Two mechanisms run at once and only make sense together.  The spacer block is
   malloc'd first and freed last, so it is still held when the array's new block
   is taken: the new block therefore lands a random distance from wherever the
   old one sat instead of being handed straight back the storage just released.
   And the old storage is wiped before it is freed, so the stale copy of the
   records does not survive in the free list.  Reordering either -- freeing the
   spacer early, or freeing the old block before the memset -- undoes one of
   them without changing anything a unit test can see.

   The two lengths deliberately disagree by one record and NEITHER may be
   "corrected" (rebuild_info/pitfalls.md):

     - the memmove length is the length of the NEW block, (count + 1) * 0x50,
       and not the length of the source: the size stored for the malloc at
       0002dfc5 is the same local pushed as the copy length at 0002dfd7.  Both
       fdps_deploy_unit and fdps_build_map_unit_array leave the old block at
       exactly count * 0x50 bytes, so after either of them the copy reads 0x50
       bytes past the end of that allocation and the spare tail record of the
       new block comes up holding whatever the heap had there.  (After
       fdps_load_savegame the old block is a flat 0x1e00 bytes and the same read
       lands inside it.)
     - the memset length is count * 0x50, one record short of the block this
       routine itself hands out, so on the second and every later call the old
       block's final record is the one record the wipe does not reach.

   The count at 0x00060150 is neither written nor checked.  Not written, so the
   array does not gain an entry and the spare record at the top of the new block
   stays outside every walk's bound: this is a move, not a resize.

   The scratch size is rand() % 0x400 + 1 taken with the signed IDIV at
   0002dfad, so over the 0..0x7fff that rand() returns it is 1..0x400 and never
   zero.  Neither malloc result is tested against null.

   A record pointer, or a cached array base, held across this call addresses
   freed and zeroed memory; every caller re-resolves through
   fdps_get_unit_record immediately afterwards. */
void fdps_relocate_unit_array(void)
{
    void *spacer_block;
    unsigned char *new_array;
    int new_block_bytes;

    spacer_block = malloc(rand() % SPACER_SIZE_MODULUS + 1);

    new_block_bytes = (data_fdps_map_unit_count + 1) * UNIT_RECORD_STRIDE;
    new_array = malloc(new_block_bytes);

    memmove(new_array, data_fdps_map_unit_array_ptr, new_block_bytes);
    memset(data_fdps_map_unit_array_ptr, 0,
           new_block_bytes - UNIT_RECORD_STRIDE);

    free(data_fdps_map_unit_array_ptr);
    free(spacer_block);

    data_fdps_map_unit_array_ptr = new_array;
}

/* The half of the AI byte this routine keeps: AND DL,0xf0 at 00036b99, applied
   to the record byte before the caller's value is ORed over it.  The four bits
   it preserves are read as flags elsewhere -- bit 0x40 in
   fdps_map_actor_take_best_action at 00012c72 and bit 0x80 in
   fdps_score_targets_for_item at 00013380 -- while the four it drops are the
   behaviour code fdps_map_actor_behavior_step takes with AND AL,0xf at 00010065
   and dispatches on against 0x0..0xb. */
#define AI_BEHAVIOR_FLAG_NIBBLE 0xf0

/* 00036b60.  Walks the inclusive unit index range and rewrites the low nibble
   of each record's ai_behavior byte at record offset 0x34, leaving the high
   nibble as it found it.

   The loop is the plain -od for-shape: [EBP-0x8] is seeded from the first
   argument at 00036b6c, the test at 00036b72 is CMP EAX,dword ptr [EBP+0x18] /
   JLE, and the increment block at 00036b7c is MOV EAX,dword ptr [EBP-0x8] /
   INC dword ptr [EBP-0x8], the shape index++ produces.  The compare is the
   SIGNED JLE and the bound is inclusive, so a range whose first index is
   greater than its last writes nothing at all (the JMP to the epilogue at
   00036b7a) and a negative first index is walked rather than treated as huge.

   The record is resolved through fdps_get_unit_record on every iteration --
   PUSH EAX / CALL 0x0002d210 / ADD ESP,0x4 at 00036b88, the pointer spilled to
   [EBP-0x4] -- so the array base is re-read per record and nothing bounds the
   index: data_fdps_map_unit_count is not read here and the accessor's multiply
   is signed.

   The merge is a read-modify-write of the one byte and NOT an assignment: MOV
   DL,byte ptr [EAX+0x34] / AND DL,0xf0 / MOV DH,byte ptr [EBP+0x1c] / OR DH,DL
   / MOV byte ptr [EAX+0x34],DH.  Storing behavior_mode whole -- which is what
   fdps_map_actor_behavior_step itself does at 0001039a with MOV byte ptr
   [EAX+0x34],0x7 -- would clear the two AI flag bits the scorers read out of
   the high nibble.

   The caller's value is ORed in UNMASKED: there is no AND with 0x0f anywhere in
   the body, so a value above 0x0f sets high-nibble bits too and the "low
   nibble" in the name is a property of the values the game passes rather than
   of the code.  All four call sites pass the literal 0, so what the shipped
   image ever does with this is clear the behaviour code back to 0 -- the title
   demo over units 0..0xb at 0002acd1, and fdps_chapter_20_post_action over
   three one-unit ranges at 0003b165, 0003b181 and 0003b19d, each of them the
   same index pushed twice. */
void fdps_object_set_field34_low_nibble_range(int first_unit_index,
                                              int last_unit_index,
                                              unsigned char behavior_mode)
{
    struct fdps_unit_record *unit;
    int unit_index;

    for (unit_index = first_unit_index;
         unit_index <= last_unit_index;
         unit_index++) {
        unit = fdps_get_unit_record(unit_index);
        unit->ai_behavior = (unsigned char)
            ((unit->ai_behavior & AI_BEHAVIOR_FLAG_NIBBLE) | behavior_mode);
    }
}
