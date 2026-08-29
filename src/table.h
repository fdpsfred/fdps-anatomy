/* table.h -- record accessors for the game's static data tables.
 *
 * The nine files fdps_load_data_tables reads out of the VFS container at
 * startup are each held whole in one heap block, addressed through a pointer
 * global that gamedata.c owns (see gamedata.h).  Every one of them is a flat
 * array of fixed-stride records, and this file holds the one accessor per
 * table that turns a record id into a pointer into that block.
 *
 * They are all the same shape: multiply the id by the table's stride, add the
 * base pointer, return it.  None of them bounds-checks, none of them tests the
 * base for null, and none of them touches the record it hands back -- the
 * range test lives in the caller that knows which table an id belongs to.
 *
 * The record layouts are struct definitions in fdpstype.h, byte-packed there
 * because the original's strides are the file's own and not what an aligning
 * compiler would pick (rebuild_info/pitfalls.md).
 */
#ifndef TABLE_H
#define TABLE_H

#include "fdpstype.h"

/* Returns a pointer to record char_id of the FRIAPRDA.DAT character base-stat
   table: race, class, starting level, base HP/MP/AP/DP/DX, movement, the
   initial learned-spell bitmap and the initial equipment, in the 24 bytes of
   struct fdps_character_base_record.

   char_id is the character's portrait id, 0..59, the same id that indexes the
   per-level growth table through fdps_get_growth_record.  Nothing is checked:
   the table holds 60 records and any id outside that range addresses memory
   past one end of it or the other, because the multiply is signed.  The bound
   is the caller's -- fdps_deploy_unit compares the id against 0x3c and sends
   0x3c and above to fdps_get_enemy_record instead.  The returned pointer is
   never null in the sense of being tested here; it is the table base plus an
   offset, and it is whatever the base holds before fdps_load_data_tables has
   run.

   Reads only the table base global, calls nothing, and dereferences nothing. */
extern struct fdps_character_base_record *fdps_get_character_base_record(int char_id);
#pragma aux fdps_get_character_base_record "*" parm caller [];

/* Returns a pointer to record char_id of the FRILEVUP.DAT per-level growth
   table: the five {min, exclusive max} byte pairs the level-up roll draws each
   of AP, DP, DX, HP and MP from, then the GETMGTAB.DAT spell-learning index of
   this character form, in the 11 bytes of struct fdps_character_growth.

   char_id is the same portrait id that indexes the base table through
   fdps_get_character_base_record: 0..59, of which 0x00-0x0b are the twelve
   playable characters in their starting form and 0x0f-0x23 their promoted
   forms.  Nothing is checked -- the table holds 60 records and any id outside
   that range addresses memory past one end of it or the other, because the
   multiply is signed -- and the base is not tested for null either.

   The four callers read it both ways round: fdps_roster_add_character takes
   hp_min and mp_min as the starting HP and MP of a unit it is enrolling,
   fdps_unit_award_exp_and_level_up takes each {min, max} pair as the range of
   one level-up gain, fdps_deploy_unit reads it while placing a scripted unit,
   and fdps_church_promote_loop re-reads it for the form a promotion turns the
   character into.

   Reads only the table base global, calls nothing, and dereferences nothing. */
extern struct fdps_character_growth *fdps_get_growth_record(int char_id);
#pragma aux fdps_get_growth_record "*" parm caller [];

/* Returns a pointer to record enemy_index of the ENEMYDAT.DAT enemy stat
   table: race and class, the per-level HP coefficient, the per-level MP, AP,
   DP and DX coefficients, the absolute movement allowance and the experience
   multiplier the kill reward is scaled by, in the 10 bytes of struct
   fdps_enemy_data.

   enemy_index is the portrait id minus 0x3c, 0..90 over the 91 records the
   910-byte file holds (resource_info/data_tables.md): portrait ids 0x00-0x3b
   are the playable characters and go to the two tables above instead, and
   0x3c upwards are the enemy forms this table describes.  Nothing is checked
   -- no bound at either end, and the multiply is signed, so an index below
   zero addresses memory in front of the table just as one past 90 addresses
   memory behind it -- and the base is not tested for null either.

   The subtraction is the caller's and so is the guard that makes it safe:
   fdps_unit_apply_damage and fdps_combat_compute_hit_outcome reach it only
   after testing the unit's side byte, and fdps_deploy_unit only after
   comparing the portrait id itself against 0x3c.  What the callers want out of
   the record is the experience multiplier at +0x9, which both damage paths
   read straight after the call.

   Reads only the table base global, calls nothing, and dereferences nothing. */
extern struct fdps_enemy_data *fdps_get_enemy_record(int enemy_index);
#pragma aux fdps_get_enemy_record "*" parm caller [];

/* Returns a pointer to record item_id of the ITEM.DAT item table: the type
   code that says whether the entry is a weapon, a piece of armour, a
   consumable or a promotion badge, the four signed equipment stat modifiers
   ap/hit/dp/ev, the weapon hit effect and its rate, the attack reach, the use
   effect with its amount, distance, target mode and radius, the shop price and
   the selection mode, in the 23 bytes of struct fdps_item_effect.

   item_id is the item number, the same byte an equipment or bag slot of a unit
   record carries and the same number the shop and sell lists hold.  The table
   has 251 records, ids 0x00-0xFA, of which 0x00-0xE1 carry content and the
   remainder are blank (assets/items.md).  Nothing is checked -- no bound at
   either end, and the multiply is signed -- and the base is not tested for
   null either.  The absence of the bound is behaviour and not an oversight:
   item id 0xFF occurs in play, its record lies past the end of the table, and
   the varying values read there are the guide's "FF BUG item".

   Callers read byte +0x00 to classify the item, the four stat words to move a
   unit's combat totals, byte +0x0d to dispatch a use effect and the word at
   +0x13 for the shop price.

   Reads only the table base global, calls nothing, and dereferences nothing. */
extern struct fdps_item_effect *fdps_get_item_record(int item_id);
#pragma aux fdps_get_item_record "*" parm caller [];

/* Returns a pointer to row record_index of the PROMAP.DAT class table: the
   eight per-terrain movement costs, the class's critical rate and its
   magic-resistance complement, in the 10 bytes of struct fdps_class_record.

   record_index is the row number in the file and NOT the class code: row 0 is
   a default row -- eight movement costs of 1, critical 0, magic resistance
   complement 0 -- and class 0x00 lives in row 1, so every caller that has a
   unit in hand forms the argument as its class code plus one
   (assets/tables/classes.md).  The two callers that are not asking about a
   particular unit, fdps_collect_targets_in_range and
   fdps_map_actor_score_best_item, push a literal 0 and get the default row on
   purpose.  One caller,
   fdps_map_actor_move_toward_nearest_reachable_opponent at 000126b0, omits the
   INC and so reads the previous class's row; that is an original defect the
   rebuild copies rather than repairs (rebuild_info/pitfalls.md).

   The table has 41 rows over the file's 410 bytes.  Nothing is checked -- no
   bound at either end, and the multiply is signed -- and the base is not
   tested for null either.

   The two combat callers read a single byte straight after the call, each
   zero-extended: 0001a00c MOV AL,byte ptr [EDX+0x8] in
   fdps_combat_compute_hit_outcome takes the critical rate, and 0002835e MOV
   AL,byte ptr [EDX+0x9] in fdps_spell_damage_unit takes the magic-resistance
   complement.  The movement and AI callers keep the pointer in a stack local
   and index the eight move_cost bytes by terrain type further on.

   Reads only the table base global, calls nothing, and dereferences nothing. */
extern struct fdps_class_record *fdps_get_class_record(int record_index);
#pragma aux fdps_get_class_record "*" parm caller [];

/* Returns a pointer to record class_index of the PROEQU.DAT class equipment
   table: the six item type codes a class is allowed to equip, in the 6 bytes
   of struct fdps_class_equip_record.

   class_index is the class code RAW -- byte +0x20 of the unit record, pushed
   without the INC that every caller of fdps_get_class_record applies, because
   PROEQU.DAT has no leading default row and class 0x00 is record 0
   (assets/tables/classes.md covers the PROMAP.DAT bias this table does not
   share).  Nothing is checked: the 216-byte file holds 36 records covering
   class codes 0x00-0x23 (resource_info/data_tables.md) while class codes run
   to 0x27, so classes 0x24-0x27 address memory past its end, and the multiply
   is signed, so a negative index addresses memory in front of it.  The base is
   not tested for null either.

   The record is a variable-length set and not six fixed slots: the types used
   are stored in ascending order and the unused positions hold 0xFF, never
   0x00, which is itself a live item type.  The one caller,
   fdps_unit_can_equip_item, scans all six positions with no sentinel test at
   all and returns 1 when one of them equals the type byte at +0x00 of the item
   record it was asked about (rebuild_info/pitfalls.md).

   Reads only the table base global, calls nothing, and dereferences nothing. */
extern struct fdps_class_equip_record *fdps_get_class_equip_record(int class_index);
#pragma aux fdps_get_class_equip_record "*" parm caller [];

/* Returns a pointer to record spell_id of the MAGICDAT.DAT spell table: the
   signed power word -- damage, heal amount, or, when negative, the attack
   multiplier as a percentage -- the hit rate, the cast distance with its
   straight-line bit 0x10, the blast radius, the MP cost and the target side,
   in the 7 bytes of struct fdps_spell_effect.

   spell_id is the spell number, 0x00-0x27 over the 40 records the 280-byte
   file holds with no gap (assets/spells.md).  It is also the bit number in a
   unit's five-byte known-spell bitmap, so the ids a caster can reach are
   exactly the ids this table has records for.  Nothing is checked -- no bound
   at either end, and the multiply is signed, so an id below zero addresses
   memory in front of the table just as one past 0x27 addresses memory behind
   it -- and the base is not tested for null either.

   Twelve callers, and what they read decides the field widths: power at +0x00
   is taken with MOVSX by fdps_spell_heal_unit and fdps_score_targets_for_spell
   because the eight attack-multiplier spells store it negative, while the MP
   cost at +0x05 and the hit rate at +0x02 are taken a byte at a time and
   zero-extended.  fdps_unit_apply_status_effect is the one caller that does not
   pass the id it was given: outside effect codes 0x11..0x13 it substitutes
   0x14, the generic status-ailment spell, and asks for that record instead.

   Reads only the table base global, calls nothing, and dereferences nothing. */
extern struct fdps_spell_effect *fdps_get_spell_record(int spell_id);
#pragma aux fdps_get_spell_record "*" parm caller [];

/* Returns a pointer to record learn_index of the GETMGTAB.DAT spell-learning
   table: the six (level, spell id) byte pairs at which one character form
   learns its spells, in ascending level order, an unused pair being
   (0xff, 0xff), in the 12 bytes of struct fdps_spell_learning_record.

   learn_index is neither a character id nor a class code but a schedule
   number of its own: byte +0x0a of the character's FRILEVUP.DAT growth
   record, which fdps_get_growth_record hands back (assets/characters.md).
   The value 0xff there means the form learns no spells, and rejecting it is
   the caller's job -- fdps_unit_award_exp_and_level_up compares against 0xff
   and skips the call, because nothing in the accessor treats that index
   differently from any other.  Nothing else is checked either: the table holds
   60 records over the file's 720 bytes and the multiply is signed, so an index
   outside 0..59 addresses memory past one end of it or the other, and the base
   is not tested for null.

   Of the 60 records only 22 carry content; the remaining 38 are twelve 0xff
   bytes, which is how a form that learns nothing is spelt when its growth
   record does name a schedule (assets/characters.md).  The one caller walks
   all six pairs with no sentinel test, comparing each pair's level byte
   against the unit's own level at +0x21 of the unit record and granting the
   pair's spell id on a match, so an all-0xff record simply never matches a
   level that fits in the game's 1..99 range.

   Reads only the table base global, calls nothing, and dereferences nothing. */
extern struct fdps_spell_learning_record *fdps_get_spell_learn_record(int learn_index);
#pragma aux fdps_get_spell_learn_record "*" parm caller [];

/* Returns a pointer to record char_id of the RankUp.dat promotion table: the
   four class-change routes of one promotable character as four 3-byte triples
   -- no badge, 光之徽章, 暗之徽章, 勇者徽章 -- in the 12 bytes of struct
   fdps_promotion_record.

   char_id is the unit record's own char_id byte at +0x08, the base identity
   the roster seeds when the character is enrolled and nothing rewrites
   afterwards; it is NOT the portrait id at +0x07, which a promotion changes.
   All three callers form the argument the same way, MOV AL,byte ptr
   [<unit>+0x8] / AND EAX,0xff / PUSH EAX, so what arrives is 0..255.  The
   108-byte file holds nine records, one per promotable character
   (resource_info/data_tables.md), and nothing here is checked -- no bound at
   either end, and the multiply is signed, so an id outside 0..8 addresses
   memory past one end of the table or the other.  The base is not tested for
   null either.  What keeps the callers inside the table is their own guard:
   the church screen reaches the call only for units whose portrait id is
   below 9.

   Which of the four triples applies is the caller's choice and the caller's
   arithmetic: fdps_church_promote_loop picks triple 3 for promotion item 0xdb,
   1 for 0xe0, 2 for 0xe1 and 0 for a unit holding none of them, and scales the
   triple number by 3 itself -- LEA EDX,[EDX+EDX*2] / ADD EAX,EDX -- so this
   accessor hands back the record and never the entry.  Byte 0 of the chosen
   triple becomes the unit's portrait id at +0x07 and byte 1 its class code at
   +0x20.

   Reads only the table base global, calls nothing, and dereferences nothing. */
extern struct fdps_promotion_record *fdps_get_promotion_record(int char_id);
#pragma aux fdps_get_promotion_record "*" parm caller [];

#endif
