/* table.c -- record accessors for the game's static data tables, and for the
 * party roster, which is addressed the same way.
 *
 * See table.h.  This file owns no state of its own: the base pointers it reads
 * are gamedata.c's.  The nine table bases are filled once at startup by
 * fdps_load_data_tables and released by fdps_free_global_resource_buffers; the
 * roster base is allocated once by fdps_load_global_resources and lives for
 * the whole run.
 */
#include "fdpstype.h"
#include "gamedata.h"
#include "table.h"

/* The stride of one FRIAPRDA.DAT record, as the original writes it: IMUL
   EAX,dword ptr [EBP+0x14],0x18.  It is a literal and not sizeof(struct
   fdps_character_base_record) on purpose -- the two agree only for as long as
   the struct stays byte-packed, and the file's stride is the fact that has to
   survive (rebuild_info/pitfalls.md). */
#define CHARACTER_BASE_RECORD_STRIDE 0x18

/* 00018ab0.  The whole body is IMUL EAX,dword ptr [EBP+0x14],0x18 / MOV
   EDX,dword ptr [0x00063fd8] / ADD EDX,EAX, spilled to a stack local and
   reloaded into EAX to be returned.  No compare, no branch, no CALL: the
   function has exactly one basic block.

   The multiply is IMUL, the signed form, so a negative char_id walks backwards
   off the front of the table rather than wrapping to a huge offset.  Nothing
   here can tell -- the low 32 bits of a signed and an unsigned multiply are
   the same bits -- but it is what decides the answer once the product is added
   to the base, and int is the type the parameter is read at.

   The table base is read fresh from the global on every call; it is not
   cached, and it is not tested for null, so a call before
   fdps_load_data_tables has run returns the offset alone as though it were an
   address.  Both callers run well after the load. */
struct fdps_character_base_record *fdps_get_character_base_record(int char_id)
{
    struct fdps_character_base_record *record;

    record = (struct fdps_character_base_record *)
        (data_fdps_battle_character_base_table_ptr
         + char_id * CHARACTER_BASE_RECORD_STRIDE);
    return record;
}

/* The stride of one FRILEVUP.DAT record, as the original writes it: IMUL
   EAX,dword ptr [EBP+0x14],0xb.  A literal and not sizeof(struct
   fdps_character_growth) for the same reason as the base table above -- the
   file's stride is the fact that has to survive, and the struct agrees with it
   only while it stays byte-packed (rebuild_info/pitfalls.md). */
#define CHARACTER_GROWTH_RECORD_STRIDE 0x0b

/* 00018ae0.  The same one-basic-block shape as the accessor above, over the
   other table: IMUL EAX,dword ptr [EBP+0x14],0xb / MOV EDX,dword ptr
   [0x00063fec] / ADD EDX,EAX, spilled to a stack local and reloaded into EAX
   to be returned.  No compare, no branch, no CALL.

   IMUL again, so a negative char_id walks backwards off the front of the table
   instead of wrapping; int is the width the parameter is read at, and all four
   callers push a value that fits it -- fdps_unit_award_exp_and_level_up loads
   unit record byte 0x07 and masks it with AND EAX,0xff before the PUSH, so
   what arrives is 0..255.

   The base is read out of the global on every call, uncached and untested, so
   a call before fdps_load_data_tables has filled it returns the offset alone
   as though it were an address. */
struct fdps_character_growth *fdps_get_growth_record(int char_id)
{
    struct fdps_character_growth *record;

    record = (struct fdps_character_growth *)
        (data_fdps_battle_character_growth_table_ptr
         + char_id * CHARACTER_GROWTH_RECORD_STRIDE);
    return record;
}

/* The stride of one ENEMYDAT.DAT record, as the original writes it: IMUL
   EAX,dword ptr [EBP+0x14],0xa.  A literal and not sizeof(struct
   fdps_enemy_data) for the same reason as the two tables above -- the file's
   stride is the fact that has to survive, and the struct agrees with it only
   while it stays byte-packed (rebuild_info/pitfalls.md). */
#define ENEMY_RECORD_STRIDE 0x0a

/* 00018b10.  The third accessor of the same one-basic-block shape: IMUL
   EAX,dword ptr [EBP+0x14],0xa / MOV EDX,dword ptr [0x00063fd4] / ADD EDX,EAX,
   spilled to a stack local and reloaded into EAX to be returned.  No compare,
   no branch, no CALL.

   IMUL again, the signed form, and here a negative index is not hypothetical:
   every caller forms the argument as the unit record's portrait_id byte minus
   0x3c, so a unit whose portrait_id is below 0x3c would address memory in
   front of the table.  What keeps that from happening is the caller's own
   guard on a different field -- fdps_unit_apply_damage tests the unit's side
   byte at +0x6 for zero before it forms the index, and
   fdps_combat_compute_hit_outcome tests the two sides of the exchange the same
   way -- not anything in here.

   The base is read out of the global on every call, uncached and untested, so
   a call before fdps_load_data_tables has filled it returns the offset alone
   as though it were an address. */
struct fdps_enemy_data *fdps_get_enemy_record(int enemy_index)
{
    struct fdps_enemy_data *record;

    record = (struct fdps_enemy_data *)
        (data_fdps_battle_enemy_data_table_ptr
         + enemy_index * ENEMY_RECORD_STRIDE);
    return record;
}

/* The stride of one ITEM.DAT record, as the original writes it: IMUL
   EAX,dword ptr [EBP+0x14],0x17.  A literal and not sizeof(struct
   fdps_item_effect) for the same reason as the three tables above -- the
   file's stride is the fact that has to survive, and the struct agrees with it
   only while it stays byte-packed.  Written naturally, with the four i16 stat
   fields at +0x01, +0x03, +0x05 and +0x07 aligned, the struct would measure 24
   and every lookup past index 0 would land on the wrong item
   (rebuild_info/pitfalls.md). */
#define ITEM_RECORD_STRIDE 0x17

/* 00018b40.  The fourth accessor of the same one-basic-block shape: IMUL
   EAX,dword ptr [EBP+0x14],0x17 / MOV EDX,dword ptr [0x00063fe0] / ADD
   EDX,EAX, spilled to a stack local and reloaded into EAX to be returned.  No
   compare, no branch, no CALL.

   IMUL again, the signed form, so a negative id steps backwards off the front
   of the table instead of becoming a four-gigabyte offset.  Whether the game
   can reach that half is not settled here.  There are twenty-five callers and
   the equipment path is the one read so far: fdps_unit_equip_slot forms both
   of its arguments by zero-extending a byte -- 000260b9 XOR EAX,EAX / 000260bb
   MOV AL,byte ptr [EDX+0xb] into the PUSH at 000260ca, and 0002611b MOV
   AL,byte ptr [EAX+0x1] / 0002611e AND EAX,0xff into the PUSH at 00026123 --
   so on that path what arrives is 0..255.  fdps_unit_can_equip_item does not
   narrow it: at 00026012 it loads its own second parameter, MOV EAX,dword ptr
   [EBP+0x18], and pushes it unmasked, so there the range is its callers'
   business.  (The AND EAX,0xff at 00026001, three instructions earlier,
   belongs to the CALL 0x00018ba0 above it -- fdps_get_class_equip_record, a
   different accessor -- and says nothing about this argument.)  The remaining
   callers answer the question as they are emitted.

   The absent bound is load-bearing rather than an oversight.  ITEM.DAT holds
   251 records, ids 0x00-0xFA (resource_info/data_tables.md), of which only
   0x00-0xE1 carry content; item id 0xFF is reachable in play and its record
   address lands 92 bytes past the end of the 5,773-byte table, which is what
   makes the guide's "FF BUG item" read different values from one run to the
   next.  Clamping or rejecting an id above 0xFA removes that behaviour
   (rebuild_info/pitfalls.md).

   The base is read out of the global on every call, uncached and untested, so
   a call before fdps_load_data_tables has filled it returns the offset alone
   as though it were an address. */
struct fdps_item_effect *fdps_get_item_record(int item_id)
{
    struct fdps_item_effect *record;

    record = (struct fdps_item_effect *)
        (data_fdps_item_effect_table_ptr + item_id * ITEM_RECORD_STRIDE);
    return record;
}

/* The stride of one PROMAP.DAT record, as the original writes it: IMUL
   EAX,dword ptr [EBP+0x14],0xa.  A literal and not sizeof(struct
   fdps_class_record) for the same reason as the four tables above -- the
   file's stride is the fact that has to survive.  This record is ten
   unsigned chars and would measure ten however it were declared, which is
   exactly why writing sizeof here would look right and record nothing. */
#define CLASS_RECORD_STRIDE 0x0a

/* 00018b70.  The fifth accessor of the same one-basic-block shape: IMUL
   EAX,dword ptr [EBP+0x14],0xa / MOV EDX,dword ptr [0x00063fd0] / ADD EDX,EAX,
   spilled to a stack local and reloaded into EAX to be returned.  No compare,
   no branch, no CALL.

   The stride matches fdps_get_enemy_record's 0xa exactly, so an accessor that
   named the wrong global would still step by the right amount and only be
   caught by reading a record: the base here is 0x00063fd0 and the enemy
   table's is 0x00063fd4, four bytes apart in bss (contract B).

   record_index is the row number in the file.  Seven of the eleven callers
   form it as the unit's class code PLUS ONE -- MOV AL,byte ptr [<unit>+0x20] /
   INC EAX / PUSH EAX -- because row 0 of PROMAP.DAT is a default row and not
   class 0x00 (assets/tables/classes.md).  The three callers that want that
   default row, fdps_collect_targets_in_range at 00011e90,
   fdps_map_actor_score_best_item at 00013056 and
   fdps_map_actor_score_best_spell at 00013436, push a literal 0 (6a 00) into
   the call.  The eleventh, the call at 000126f6 in
   fdps_map_actor_move_toward_nearest_reachable_opponent, pushes the class code
   without the INC and reads the previous class's row, an original defect
   (program_info/known_bugs.md).  The addition is the caller's and stays the
   caller's: nothing in here biases the index, so moving the +1 in here would
   shift all three literal-zero calls onto class 0x00's row.

   IMUL again, the signed form, so a negative index steps backwards off the
   front of the table instead of becoming a four-gigabyte offset.  Nothing is
   bounded either: the file holds 41 records over its 410 bytes
   (resource_info/data_tables.md) and an index past the last one is multiplied
   and added like any other.

   The base is read out of the global on every call, uncached and untested, so
   a call before fdps_load_data_tables has filled it returns the offset alone
   as though it were an address. */
struct fdps_class_record *fdps_get_class_record(int record_index)
{
    struct fdps_class_record *record;

    record = (struct fdps_class_record *)
        (data_fdps_class_table_ptr + record_index * CLASS_RECORD_STRIDE);
    return record;
}

/* The stride of one PROEQU.DAT record, as the original writes it: IMUL
   EAX,dword ptr [EBP+0x14],0x6.  A literal and not sizeof(struct
   fdps_class_equip_record) for the same reason as the five tables above -- the
   file's stride is the fact that has to survive.  This record is six unsigned
   chars and would measure six however it were declared, which is exactly why
   writing sizeof here would look right and record nothing. */
#define CLASS_EQUIP_RECORD_STRIDE 0x06

/* 00018ba0.  The sixth accessor of the same one-basic-block shape: IMUL
   EAX,dword ptr [EBP+0x14],0x6 / MOV EDX,dword ptr [0x00063fe4] / ADD EDX,EAX,
   spilled to a stack local and reloaded into EAX to be returned.  No compare,
   no branch, no CALL.

   class_index is the class code RAW, not biased.  That is the one thing about
   this accessor that is not arithmetic, and it is the opposite of its
   neighbour: fdps_get_class_record above is fed class code PLUS ONE because
   row 0 of PROMAP.DAT is a default row, while PROEQU.DAT has no such row and
   class 0x00 is record 0.  The only caller says so outright -- 00025ffe MOV
   AL,byte ptr [EAX+0x20] / 00026001 AND EAX,0xff / 00026006 PUSH EAX in
   fdps_unit_can_equip_item, with no INC between the load and the push, against
   the MOV AL,[unit+0x20] / INC EAX / PUSH EAX the per-unit callers of the
   class accessor write.  Adding one here would shift every class's permitted
   equipment list onto the next class's record (rebuild_info/pitfalls.md).

   The base at 0x00063fe4 is the sixth of the nine table pointers and is a
   different global from the class table's at 0x00063fd0, four records' worth
   of bss away (contract B).

   IMUL again, the signed form, so a negative index steps backwards off the
   front of the table instead of becoming a four-gigabyte offset.  Nothing is
   bounded either, and here the gap is reachable rather than hypothetical: the
   216-byte file holds 36 records covering class codes 0x00-0x23
   (resource_info/data_tables.md) while class codes run to 0x27, so a unit of
   class 0x24-0x27 is handed an address past the end of the table and the
   caller scans six bytes there.  A bound added here would change what those
   four classes are allowed to equip.

   The base is read out of the global on every call, uncached and untested, so
   a call before fdps_load_data_tables has filled it returns the offset alone
   as though it were an address. */
struct fdps_class_equip_record *fdps_get_class_equip_record(int class_index)
{
    struct fdps_class_equip_record *record;

    record = (struct fdps_class_equip_record *)
        (data_fdps_class_equip_table_ptr
         + class_index * CLASS_EQUIP_RECORD_STRIDE);
    return record;
}

/* The stride of one MAGICDAT.DAT record, as the original writes it: IMUL
   EAX,dword ptr [EBP+0x14],0x7.  A literal and not sizeof(struct
   fdps_spell_effect), and here that distinction is the sharpest of the seven:
   the record opens with a signed 16-bit power and carries five single bytes
   behind it, so declared without the pack pragma the struct measures 8 and
   every lookup from spell 0x01 onward lands one byte further into the table
   than the file says.  The file's stride is the fact that has to survive
   (rebuild_info/pitfalls.md). */
#define SPELL_RECORD_STRIDE 0x07

/* 00018bd0.  The seventh accessor of the same one-basic-block shape: IMUL
   EAX,dword ptr [EBP+0x14],0x7 / MOV EDX,dword ptr [0x00063ff0] / ADD EDX,EAX,
   spilled to a stack local and reloaded into EAX to be returned.  No compare,
   no branch, no CALL.

   IMUL again, the signed form, so a negative id steps backwards off the front
   of the table instead of becoming a four-gigabyte offset.  int is the width
   the parameter is read at, and the callers read so far push a value that fits
   it: fdps_unit_apply_status_effect at 00028fc8 replaces its effect code with a
   literal 0x14 for everything outside 0x11..0x13 before the PUSH at 00028fd2,
   and fdps_spell_heal_unit and fdps_spell_deduct_mp_cost forward their own
   third parameter unchanged.

   Nothing is bounded: MAGICDAT.DAT holds 40 records over its 280 bytes, ids
   0x00-0x27 with no gap (assets/spells.md), and an id past 0x27 is multiplied
   and added like any other.

   What the callers do with the pointer is where the record's own widths show:
   00028591 MOVSX EAX,word ptr [EAX] in fdps_spell_heal_unit and 00013945 in
   fdps_score_targets_for_spell take power as a SIGNED word -- it is negative
   for the eight attack-multiplier spells -- while 000285f9 MOV DL,byte ptr
   [EAX+0x5] after XOR EDX,EDX takes the MP cost and 00028fe3 MOV AL,byte ptr
   [EDX+0x2] after XOR EAX,EAX takes the hit rate, both zero-extended.

   The base is read out of the global on every call, uncached and untested, so
   a call before fdps_load_data_tables has filled it returns the offset alone
   as though it were an address. */
struct fdps_spell_effect *fdps_get_spell_record(int spell_id)
{
    struct fdps_spell_effect *record;

    record = (struct fdps_spell_effect *)
        (data_fdps_battle_spell_effect_table_ptr
         + spell_id * SPELL_RECORD_STRIDE);
    return record;
}

/* The stride of one GETMGTAB.DAT record, as the original writes it: IMUL
   EAX,dword ptr [EBP+0x14],0xc.  A literal and not sizeof(struct
   fdps_spell_learning_record) for the same reason as the seven tables above --
   the file's stride is the fact that has to survive.  This record is twelve
   unsigned chars and would measure twelve however it were declared, which is
   exactly why writing sizeof here would look right and record nothing. */
#define SPELL_LEARNING_RECORD_STRIDE 0x0c

/* 00018c00.  The eighth and last accessor of the same one-basic-block shape:
   IMUL EAX,dword ptr [EBP+0x14],0xc / MOV EDX,dword ptr [0x00063fe8] / ADD
   EDX,EAX, spilled to a stack local and reloaded into EAX to be returned.  No
   compare, no branch, no CALL.

   learn_index is the spell-learning schedule number, not a character id and
   not a class code: it is byte +0x0a of the character's FRILEVUP.DAT growth
   record, which is why two forms of the same character have two different
   schedules and why several forms share one (assets/characters.md).  The one
   caller, fdps_unit_award_exp_and_level_up, forms it by zero-extension --
   0001e02b XOR EAX,EAX / 0001e030 MOV AL,byte ptr [EDX+0xa] -- so what arrives
   is 0..255, and int is the width the parameter is read at.

   The 0xff that means "this form learns no spells" is tested by that caller and
   not here: 0001e036 CMP dword ptr [EBP+-0x2c],0xff / JZ skips the call
   entirely.  Nothing in this body rejects it, so index 0xff multiplied by the
   stride addresses 2,340 bytes past the end of the 720-byte table.  Moving the
   sentinel test in here -- returning null, or clamping -- would change what the
   caller has to cope with, and the caller copes with it by never asking
   (rebuild_info/pitfalls.md).

   IMUL again, the signed form, so a negative index steps backwards off the
   front of the table instead of becoming a four-gigabyte offset.  Nothing is
   bounded either: the file holds 60 records over its 720 bytes
   (resource_info/data_tables.md), one per FRIAPRDA.DAT character slot, of which
   38 are entirely 0xff.

   The base is read out of the global on every call, uncached and untested, so
   a call before fdps_load_data_tables has filled it returns the offset alone
   as though it were an address.  Its global at 0x00063fe8 is the seventh of
   the nine table pointers, sitting between the class-equip table's 0x00063fe4
   and the growth table's 0x00063fec in bss (contract B). */
struct fdps_spell_learning_record *fdps_get_spell_learn_record(int learn_index)
{
    struct fdps_spell_learning_record *record;

    record = (struct fdps_spell_learning_record *)
        (data_fdps_spell_learning_table_ptr
         + learn_index * SPELL_LEARNING_RECORD_STRIDE);
    return record;
}

/* The stride of one RankUp.dat record, as the original writes it: IMUL
   EAX,dword ptr [EBP+0x14],0xc.  A literal and not sizeof(struct
   fdps_promotion_record) for the same reason as the eight tables above -- the
   file's stride is the fact that has to survive.  This record is twelve
   unsigned chars and would measure twelve however it were declared, which is
   exactly why writing sizeof here would look right and record nothing. */
#define PROMOTION_RECORD_STRIDE 0x0c

/* 00018c30.  The ninth accessor of the same one-basic-block shape: IMUL
   EAX,dword ptr [EBP+0x14],0xc / MOV EDX,dword ptr [0x00063fdc] / ADD EDX,EAX,
   spilled to a stack local and reloaded into EAX to be returned.  No compare,
   no branch, no CALL.

   The stride matches fdps_get_spell_learn_record's 0xc exactly, so an accessor
   that named the wrong global would still step by the right amount and only be
   caught by reading a record: the base here is 0x00063fdc and the
   spell-learning table's is 0x00063fe8, three pointers apart in bss
   (contract B).

   char_id is the unit record's char_id byte at +0x08 and not its portrait id
   at +0x07 -- the identity that survives a promotion, against the one a
   promotion overwrites.  All three callers form it by zero-extension --
   00034797, 00034cdd and 00035468 each MOV AL,byte ptr [<unit>+0x8] followed by
   AND EAX,0xff before the PUSH -- so what arrives is 0..255, and int is the
   width the parameter is read at.

   Nothing is bounded: the 108-byte file holds nine records, one per promotable
   character (resource_info/data_tables.md), and an id past the ninth is
   multiplied and added like any other.  The bound is the caller's --
   fdps_church_promote_loop reaches the call only for units whose portrait id is
   below 9 -- so a clamp added here would change which record the church screen
   reads rather than protect it.

   The four class-change routes inside the record are the caller's to choose and
   the caller's to address: 00034cee MOV EDX,dword ptr [EBP+0x18] / LEA
   EDX,[EDX+EDX*2] / ADD EAX,EDX scales the route number by the 3-byte triple
   itself.  Nothing in here indexes past the record base.

   The base is read out of the global on every call, uncached and untested, so
   a call before fdps_load_data_tables has filled it returns the offset alone
   as though it were an address. */
struct fdps_promotion_record *fdps_get_promotion_record(int char_id)
{
    struct fdps_promotion_record *record;

    record = (struct fdps_promotion_record *)
        (data_fdps_promotion_table_ptr + char_id * PROMOTION_RECORD_STRIDE);
    return record;
}

/* The stride of one party-roster record, as the original writes it: IMUL
   EAX,dword ptr [EBP+0x14],0x50.  A literal and not sizeof(struct
   fdps_unit_record), for the same reason as the nine tables above and with the
   same consequence if it drifts: the record carries 16-bit fields at the ODD
   offsets 0x37, 0x39 and 0x3e, so an aligning compiler would pad it past 0x50
   and shift every field from 0x37 on, while the array the save file restores
   keeps the original spacing (rebuild_info/pitfalls.md).

   It is also the stride of the map unit array, which is a different global --
   0x00069cd8 against 0x00064108 -- holding the same record type.  Nothing
   derives one stride from the other; both are written as this literal at each
   use. */
#define ROSTER_RECORD_STRIDE 0x50

/* 00023950.  The same one-basic-block shape as the nine table accessors above,
   over the party roster instead of a file table: IMUL EAX,dword ptr
   [EBP+0x14],0x50 / MOV EDX,dword ptr [0x00064108] / ADD EDX,EAX, spilled to a
   stack local and reloaded into EAX to be returned.  No compare, no branch, no
   CALL.

   roster_index is a position in the roster, and the bound on it is the
   caller's: nothing here compares it against data_fdps_roster_member_count at
   0x00064114, and the seven callers each carry their own guard --
   fdps_roster_revive_fallen_members and fdps_play_ending_credit_roll pass a
   loop counter the count already bounds, fdps_village_select_member passes the
   list's scroll base plus the cursor offset, and the three shop and church
   callers pass a position handed to them from further up.  A bound added here
   would move that responsibility rather than add safety.

   IMUL again, the signed form, so a negative index steps backwards off the
   front of the array instead of becoming a four-gigabyte offset.

   The array is 0xa00 bytes -- fdps_load_global_resources allocates it with PUSH
   0xa00 / CALL malloc at 000296b8 -- which is exactly 32 records, and it is
   allocated once and never moved, so unlike the nine table bases a pointer into
   it stays valid across a resource load.  The base is still re-read from the
   global on every call and is not tested for null. */
struct fdps_unit_record *fdps_get_roster_record(int roster_index)
{
    struct fdps_unit_record *record;

    record = (struct fdps_unit_record *)
        (data_fdps_roster_array_ptr + roster_index * ROSTER_RECORD_STRIDE);
    return record;
}
