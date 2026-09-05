/* unititem.h -- a unit's inventory and what it has equipped.
 *
 * A unit record carries eight 2-byte inventory entries at record offset 0x0a,
 * struct fdps_unit_record's inventory_slots[16] in src/fdpstype.h: entry i is
 * inventory_slots[i * 2], a flag byte whose bit 0x40 means "equipped" and bit
 * 0x80 means "no item here", followed by the item id byte.  Most of the
 * functions here read and edit that set; fdps_unit_can_equip_item is about
 * the unit's class rather than its inventory and asks the PROEQU.DAT class
 * equipment table whether an item's type is one the class may wear, and
 * fdps_unit_item_select_loop is the modal cursor the player moves over those
 * eight entries while the unit status window is up.  The
 * records themselves live in the block reached
 * through data_fdps_map_unit_array_ptr (gamedata.h) and are resolved through
 * fdps_get_unit_record (unit.h).  The file owns no state of its own.
 */
#ifndef UNITITEM_H
#define UNITITEM_H

/* Which of the unit's eight inventory slots holds the weapon, or the armour,
   that it currently has equipped?  Returns the slot index 0..7 of the FIRST
   entry that both carries the equipped bit 0x40 and whose item is of the
   requested kind, or -1 when there is no such entry.  Every caller treats -1
   as "nothing equipped": the two counter-attack checks and the AI's attack
   scorer abandon the attack, the battle action menu raises its "no weapon"
   flag, and the shop skips the trade-in.

   want_armor picks the kind, and it is a plain zero test and not a flag
   compare -- any non-zero value asks for armour:
     0        the equipped weapon, item type 1..0x15
     non-zero the equipped armour, item type 0x16..0x27
   The type is byte +0x00 of the item's ITEM.DAT record, so the two spans are
   the weapon and armour halves of the type field (assets/items.md).  Eight of
   the nine call sites push a literal 0; only fdps_shop_buy_loop pushes 1, and
   only when the shop item under the cursor is itself of an armour type.

   unit_index is a position in the current battle's unit array and is not range
   checked; the record is resolved through fdps_get_unit_record, so a call
   after the array has moved sees the new block.  No byte of the record and no
   byte of the item table is written.

   The weapon test rejects item type 0 as well as everything above 0x15.  That
   is behaviour and not a redundant guard: ITEM.DAT's records past id 0xe1 are
   all zero (assets/items.md), so a slot holding a bad id reads back type 0,
   and the single-sided `type <= 0x15` an author would write instead would
   report that blank record as the unit's equipped weapon. */
extern int fdps_unit_find_equipped_slot(int unit_index, int want_armor);
#pragma aux fdps_unit_find_equipped_slot "*" parm caller [];

/* What item is in this inventory slot?  Returns the id byte of entry `slot` of
   unit `unit_index` -- struct fdps_unit_record's inventory_slots[slot * 2 + 1]
   -- widened without sign, so the answer is 0..255 and an entry holding 0xff
   answers 255.  That is the id the callers hand to fdps_get_item_record, and
   every one of the fourteen call sites does exactly that with it.

   The entry's flag byte is NOT consulted.  An entry marked empty (bit 0x80) or
   merely carried rather than equipped answers with its id byte just the same,
   so the answer is only meaningful once the caller has established that the
   slot holds something.  Nothing is written and no other byte of the record is
   read.

   Neither argument is range checked, and that is load-bearing rather than an
   omission.  slot goes into the address as slot * 2 with nothing in between,
   so a slot outside 0..7 addresses bytes outside the inventory field:
   fdps_unit_resolve_attack_hit passes the result of
   fdps_unit_find_equipped_slot straight in without testing it for -1, and a
   unit with no equipped weapon therefore reads record offset 0x09 -- the
   reserved_09 byte in front of the inventory -- and hands that to
   fdps_get_item_record.  A bounds check on slot, or an early return of 0 or
   -1, changes what that path does.

   unit_index is a position in the current battle's unit array; the record is
   resolved through fdps_get_unit_record on every call, so a call after the
   array has moved sees the new block. */
extern int fdps_unit_get_item_id(int unit_index, int slot);
#pragma aux fdps_unit_get_item_id "*" parm caller [];

/* How many things is this unit carrying?  Returns the number of the unit's
   eight inventory entries whose flag byte does NOT carry the empty bit 0x80 --
   0 through 8.  Both ends of that range are what the callers act on: 0 is how
   fdps_village_item_sell_loop and fdps_village_item_transfer_loop refuse a
   sale or a hand-over, and 8 is how fdps_shop_buy_loop, the four chapter
   events that award an item and fdps_chapter_26_end refuse to give the unit
   another one.  fdps_unit_find_item_slot uses it as the bound of a
   0..count-1 sweep over fdps_unit_get_item_id.

   All eight entries are examined and the scan does not stop at the first empty
   one, so a unit whose inventory has a hole in it is still counted correctly;
   the count is not an index of the first free slot.  The item id byte of an
   entry is never read, so the answer depends on the flag byte alone.

   unit_index is a position in the current battle's unit array on the battle
   side and a party member index on the village and shop side -- the same index
   the caller uses with fdps_get_unit_record and fdps_unit_get_item_id -- and
   is not range checked; the record is resolved through fdps_get_unit_record,
   so a call after the array has moved sees the new block.  Nothing is
   written. */
extern int fdps_unit_item_count(int unit_index);
#pragma aux fdps_unit_item_count "*" parm caller [];

/* Opens the unit status window on one unit's bag, runs the item list until the
   player picks an entry or backs out, closes the window again and answers what
   the list answered: 1 for a pick, with the entry in *selected_slot, and -1 for
   a cancel.  Every call site tests for -1 and skips the action.  The call does
   not return until the player has done one or the other.

   It owns everything the window needs for the duration and nothing outlives it:
   the 320x200 Status.cel frame, a copy of the visible screen taken on entry so
   the window can be taken away again, and the clean copy of the list rectangle
   the cursor loop erases its highlight with.  All three are released before the
   answer comes back, so a caller passes no buffer in and gets none out.

   THE OPENING DRAW HIGHLIGHTS ROW 0 AND NOT *selected_slot.  The caller seeds
   the cursor -- every current call site seeds 0 -- and the cursor loop honours
   that seed on its first repaint, but the one draw this function makes itself
   passes a literal 0.  With a seed of anything else the bar therefore jumps
   from row 0 to the seeded row on the first pass of the loop.

   usable_only is handed to fdps_unit_item_select_loop unchanged and means what
   it means there: 0 accepts any entry the cursor is on, non-zero accepts only
   an entry whose ITEM.DAT record has a non-zero use effect.  Only the battle
   "use item" path passes non-zero.

   unit_index is a position in the current battle's unit array on the battle
   side and a party member index in a village; it is not range checked and it
   reaches the status panel, the item list, the cursor loop and one record
   lookup of this function's own.  The window is drawn over whatever is on the
   screen when the call is made, and the screen the close puts back is composed
   from the scene rather than from that copy on the battle map (statwin.h). */
extern int fdps_unit_item_select_window(int unit_index, int usable_only,
                                        int *selected_slot);
#pragma aux fdps_unit_item_select_window "*" parm caller [];

/* Runs the item list of the unit status window until the player picks an entry
   or backs out, and answers 1 for a pick and -1 for a cancel.  The entry that
   was picked is left in *selected_slot, which the caller seeds and reads back;
   a cancel leaves the cursor wherever it had got to rather than restoring it.
   The call does not return until one of those two things happens -- everything
   in between is repaint, wait, dispatch.

   Up (0x48) and down (0x50) move the highlight and play Beep.wav; enter (0x1c)
   and space (0x39) confirm; escape (0x1) and delete (0x53) cancel.  Any other
   make code just repaints.

   THE CURSOR WRAPS AGAINST THE OCCUPIED COUNT TAKEN ONCE ON ENTRY, AND THAT
   COUNT CAN BE 0.  fdps_unit_equip_window opens this loop without asking
   whether the unit is carrying anything, so with an empty bag the wrap turns
   slot 0 into -1 on the way up and walks the index off the top on the way
   down; the original does no harm with that, because no bar is drawn for a row
   outside 0..7 and the caller checks fdps_unit_item_count once the loop is
   over.  A wrap written as a remainder instead divides by zero there
   (rebuild_info/pitfalls.md).  The count is also never recomputed, so an
   inventory that changes while the list is up does not move the wrap point.

   usable_only decides what a confirm accepts.  Zero accepts any entry, without
   the item table being touched at all.  Non-zero accepts an entry only when
   the ITEM.DAT record of its id byte has a non-zero use_effect at +0x0d;
   anything else is refused in silence -- no sound, no message, the list simply
   comes back.  Only the battle "use item" path passes non-zero.  THE ID BYTE
   IS READ WITHOUT CONSULTING THE ENTRY'S FLAG BYTE, so an empty entry inside
   the cursor's range is looked up on whatever id was last left in it.

   window_image is the whole 320x200 status window frame; the list is the
   0x97 x 0x95 rectangle at +0x3b58 and is the only part of it this writes,
   apart from the unit cell fdps_unit_status_window_wait_input (statunit.h)
   stamps in on a frame it draws.  list_backdrop is a tight 0x97 x 0x95 copy of
   that rectangle with no highlight on it, and it is laid down again at the top
   of every pass, so the caller must keep it alive for the whole call.

   unit_index is not range checked and is resolved through fdps_get_unit_record
   (unit.h) once, on entry; the sprite cache slot the window animates comes from
   that record and not from the index. */
extern int fdps_unit_item_select_loop(int unit_index, int usable_only,
                                      unsigned char *window_image,
                                      unsigned char *list_backdrop,
                                      int *selected_slot);
#pragma aux fdps_unit_item_select_loop "*" parm caller [];

/* Take entry `slot` out of unit `unit_index`'s inventory and close the gap.
   Entries slot+1..7 move down one place, so the entries that are left stay
   packed at the front, and the last entry's flag byte is then set to the empty
   bit 0x80.  Nothing is returned and nothing else in the record is touched.

   The pairing with fdps_unit_add_item is what the callers rely on: the shop and
   village loops, the eighteen consumable branches of
   fdps_apply_item_effect_to_targets, and the chapter events that upgrade a
   weapon all remove the old entry and add the new one, and the packed order is
   what fdps_unit_item_count's 0-or-8 answers and
   fdps_unit_find_item_slot's 0..count-1 sweep depend on.

   Neither argument is range checked.  slot 7 is the no-op end of the range --
   the move is zero bytes long and only the empty marker is written -- and a
   slot above 7 hands memmove a negative and therefore huge byte count, so
   every caller has already established the slot: fdps_chapter_27_end tests its
   slot against -1 before calling, and the menu paths pass the slot the player
   selected.  A guard added here changes what an out-of-range call does.

   The empty marker is a ONE-BYTE store: only inventory_slots[14], the last
   entry's flag byte, is written, and the item id byte beside it at
   inventory_slots[15] keeps whatever the shift left there.  Clearing the whole
   2-byte entry instead zeroes that id byte, and fdps_unit_get_item_id hands
   back the id byte without consulting the flag byte, so any path that reads the
   last slot without checking it first would then see 0 rather than the stale id
   the original leaves.

   unit_index is a position in the current battle's unit array on the battle
   side and a party member index on the village and shop side; the record is
   resolved through fdps_get_unit_record on every call, so a call after the
   array has moved edits the new block. */
extern void fdps_unit_remove_item(int unit_index, int slot);
#pragma aux fdps_unit_remove_item "*" parm caller [];

/* Give unit `unit_index` the item `item_id`, in the first of its eight
   inventory entries that is empty.  Returns 1 when the item was stored and -1
   when all eight entries were already occupied, in which case not a byte of the
   record is written and the item is simply lost.  Both answers are acted on:
   fdps_battle_search_cell_at_cursor tests the result with CMP EAX,-0x1 before
   it declares the cell's contents picked up, and fdps_run_death_scripts does
   the same before it hands over a dead unit's bequest; the shop, the village
   transfer and the chapter events ignore the result, having already asked
   fdps_unit_item_count whether there was room.

   "Empty" is the flag bit 0x80 of the entry, tested as a mask -- an entry
   carrying any other bits alongside 0x80 is still empty, and one carrying only
   0x40 or nothing at all is occupied.  The scan does not stop at the first
   occupied entry, so an item lands in a hole left in the middle of an
   inventory rather than at the end.

   Taking the slot writes 0 over the WHOLE flag byte and then the id byte
   beside it.  Zeroing the flag byte is what makes the slot count as occupied
   (bit 0x80 goes) and the item unequipped (bit 0x40 goes); the entry's previous
   flags are not preserved in any part.  Nothing is shifted or compacted --
   fdps_unit_remove_item is the side that keeps the entries packed.

   Only the low byte of item_id reaches the record: the body reads the argument
   with MOV AL,byte ptr [EBP+0x18], so an id of 0x1ff stores 0xff.  Every call
   site pushes a full dword, five of them a literal item id.

   Neither argument is range checked.  unit_index is a position in the current
   battle's unit array on the battle side and a party member index on the
   village and shop side; the record is resolved through fdps_get_unit_record,
   so a call after the array has moved writes into the new block. */
extern int fdps_unit_add_item(int unit_index, int item_id);
#pragma aux fdps_unit_add_item "*" parm caller [];

/* Is unit `unit_index`'s class allowed to equip item `item_id`?  Returns 1
   when the item's type code is one of the six the class's PROEQU.DAT record
   lists, and 0 when it is not.  All four call sites use it as a plain boolean,
   TEST EAX,EAX on the instruction after the call: fdps_unit_equip_window at
   00025f2c performs the equip only on 1 and otherwise goes straight back round
   its menu loop without a word; fdps_shop_buy_loop uses it twice, at 00033c4d
   to set the byte flag it carries for the member under the cursor and at
   00033cd0 to abandon the purchase; and fdps_shop_draw_member_entry at
   00033382 branches past the member row it was about to draw.

   The two inputs are read one byte each and the rest of both records is
   ignored.  The class code is byte +0x20 of the unit record,
   fdps_unit_record's clazz, and it goes to fdps_get_class_equip_record RAW --
   without the +1 that callers of fdps_get_class_record apply, because
   PROEQU.DAT has no leading default row (table.h).  The item's type is byte
   +0x00 of its ITEM.DAT record, the same field fdps_unit_find_equipped_slot
   classifies with: weapons 0x01..0x15, armour 0x16..0x27, consumables and
   story items 0x28..0x2c (assets/items.md).

   All six positions of the class record are compared, in order, and the scan
   returns on the first equal one.  There is NO sentinel test: the unused
   positions of a class record hold 0xFF and are compared like any other, and
   so is a position holding 0x00.  Stopping the scan at the first 0xFF is the
   shape an author writing this from the file format would reach for, and it
   only agrees with the original because no item's type code is 0xFF; the
   original's answer for a class record that is not shaped that way -- class
   codes 0x24..0x27, whose records lie past the end of the 216-byte file -- is
   whatever those six bytes happen to hold.

   Neither argument is range checked and neither record pointer is tested for
   null.  The records are resolved through fdps_get_unit_record,
   fdps_get_class_equip_record and fdps_get_item_record on every call, so a
   call after the unit array or a table has moved sees the new block.  Nothing
   is written. */
extern int fdps_unit_can_equip_item(int unit_index, int item_id);
#pragma aux fdps_unit_can_equip_item "*" parm caller [];

/* Equip inventory entry `slot` of unit `unit_index`, taking off whatever the
   unit already wore in the same broad category.  Nothing is returned and both
   call sites discard EAX: fdps_unit_equip_window calls it at 00025f4e once
   fdps_unit_can_equip_item has said yes, and fdps_shop_buy_loop at 00033f5b
   with fdps_unit_item_count(unit_index) - 1, the entry fdps_unit_add_item has
   just filled.  Both then redraw the status window.

   "The same category" is the coarse split of the item type field at 0x15 --
   weapons 0x01..0x15 on one side, armour and everything above on the other --
   and NOT equality of the type byte and NOT the class's PROEQU.DAT equip list.
   Two items are in the same category exactly when both types are <= 0x15 or
   both are > 0x15.  Writing the intuitive "unequip the other entry holding an
   item of the same type" gives the wrong answer: equipping a 刀 (type 0x02)
   must take off an already worn 劍 (type 0x01), and equipping any armour must
   take off any other armour whatever its sub-kind.  The split is at 0x15 and
   not at the top of the armour span, so an item of type 0x28 and above -- a
   consumable or a story item -- counts as armour for this test and takes the
   unit's armour off.

   Only entries carrying the equipped bit 0x40 are considered, tested as a mask,
   and an equipped entry whose item record reads back type 0 -- which is what
   ITEM.DAT's all-zero tail records give -- is left alone.  Every one of the
   eight entries is examined: the loop has no early exit, so a unit that somehow
   wore two items of the same category loses both.  Unequipping stores a plain
   zero over the WHOLE flag byte rather than clearing the one bit, and the
   entry's item id byte beside it is not touched.

   The last store is unconditional: the flag byte of entry `slot` becomes
   exactly 0x40, whatever it held before, so an entry marked empty (0x80)
   becomes an occupied equipped one and an entry the loop had just zeroed
   becomes equipped again.  It is a store and not an OR.

   Nothing is range checked and nothing verifies that the entry holds anything:
   the item id is read at inventory_slots[slot * 2 + 1] and the flag written at
   inventory_slots[slot * 2] with no test in between, so a slot outside 0..7
   reads and writes bytes outside the inventory field.  The unit record is
   resolved through fdps_get_unit_record twice, once for the loop and the final
   store and once for the id read, so a call after the array has moved edits the
   new block. */
extern void fdps_unit_equip_slot(int unit_index, int slot);
#pragma aux fdps_unit_equip_slot "*" parm caller [];

/* Which of unit `unit_index`'s inventory entries holds item id `item_id`?
   Returns the index of the LOWEST entry whose id byte equals item_id, and -1
   both when the unit is carrying nothing and when no entry holds that id.  The
   answer goes straight to fdps_unit_remove_item at most call sites, so the two
   ways of getting -1 are not told apart by anybody.

   Twenty-five call sites in twelve functions ask this, and every one of them is
   "does this unit have X, and where": fdps_apply_item_effect_to_targets asks
   twice for `A3` 金屬礦, fdps_battle_advance_turn asks for the four items whose
   effect is a per-turn recovery -- `A6` 妖刀村正, `A7` 妖刀正宗, `B1` 形見指環
   and `B3` 魔精石碎片 (assets/items.md), each against a fixed unit index --
   fdps_church_promote_loop for the promotion badges `DB` 勇者徽章, `E0` 光之徽章
   and `E1` 暗之徽章, and the chapter events for the story items they take back
   or upgrade, among them `58` 修佩魯, `A0` 灼烈之劍 and `DC` 反禁制器.

   Most call sites compare the answer with -1 before acting -- CMP EAX,-0x1
   straight after the ADD ESP,0x8, or a store to a local and a CMP against -0x1
   on the next instruction -- but not all of them do:
   fdps_apply_item_effect_to_targets's second ask at 00026d7d goes straight into
   fdps_unit_remove_item at 00026d97 with no compare in between, so a -1 from
   here can reach that function as a slot index.

   The scan is bounded by fdps_unit_item_count(unit_index) -- the number of
   entries whose flag byte does NOT carry the empty bit 0x80 -- and NOT by the
   eight physical entries.  So the function only ever looks at entries
   0..count-1, and it depends on the inventory being packed at the front, which
   is the invariant fdps_unit_add_item and fdps_unit_remove_item maintain
   between them.  Writing the obvious `for (slot = 0; slot < 8; slot++)`
   instead changes the answer in both directions the moment an empty entry sits
   below an occupied one:
     - an entry ABOVE a hole is outside the bound and is not found at all, so
       the original answers -1 for an item the unit is really holding;
     - an EMPTY entry below the bound is still read, because
       fdps_unit_get_item_id never consults the flag byte, so the original can
       answer with the index of an empty entry whose stale id byte happens to
       match -- and the caller then removes an item that is not there.
   Neither is a bug the rebuild may fix: the bound is the count and nothing
   else.

   The comparison is a full 32-bit equality test against item_id as the caller
   pushed it, and fdps_unit_get_item_id answers 0..255, so an item_id outside
   that range -- 0x100, or -1 for an entry holding 0xff -- matches nothing.

   unit_index is not range checked here and neither callee checks it either.  No
   record is resolved in this body at all: both accessors go through
   fdps_get_unit_record on every call of their own, so the count and each id
   read see whatever data_fdps_map_unit_array_ptr points at when they run.
   Nothing is written. */
extern int fdps_unit_find_item_slot(int unit_index, int item_id);
#pragma aux fdps_unit_find_item_slot "*" parm caller [];

#endif
