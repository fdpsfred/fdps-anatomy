/* table.c -- record accessors for the game's static data tables.
 *
 * See table.h.  This file owns no state of its own: the table base pointers it
 * reads are gamedata.c's, filled once at startup by fdps_load_data_tables and
 * released by fdps_free_global_resource_buffers.
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

   record_index is the row number in the file, which the eleven callers form as
   the unit's class code PLUS ONE -- MOV AL,byte ptr [<unit>+0x20] / INC EAX /
   PUSH EAX -- because row 0 of PROMAP.DAT is a default row and not class 0x00
   (assets/tables/classes.md).  The two callers that want that default row,
   fdps_collect_targets_in_range at 00011e90 and fdps_map_actor_score_best_item
   at 00013056, push a literal 0 (6a 00) into the call.  The addition is the caller's and stays the
   caller's: nothing in here biases the index, so moving the +1 in here would
   shift both of those literal-zero calls onto class 0x00's row.

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
   the MOV AL,[unit+0x20] / INC EAX / PUSH EAX every caller of the class
   accessor writes.  Adding one here would shift every class's permitted
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
