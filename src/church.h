/* church.h -- the village church, where a party member changes class.
 *
 * The church screen is one of the buildings the village menu opens.  It picks
 * a promotable member off the roster, works out which of the four promotion
 * routes the member's badge earns him, writes the new form and class into his
 * record and plays the transformation animation over the whole screen.
 *
 * A member is named by his index in the array reached through
 * data_fdps_map_unit_array_ptr (src/gamedata.h) -- on the village screens that
 * array is the party roster -- and the record layout is struct
 * fdps_unit_record in src/fdpstype.h.  The promotion routes come out of
 * RankUp.dat through fdps_get_promotion_record (src/table.h).
 */
#ifndef CHURCH_H
#define CHURCH_H

/* Promotes one roster member: writes the new form id and class code into his
   unit record and then plays the full-screen transformation animation that
   shows the change.  Nothing is returned and nothing is validated.

   `unit_index` is the member's index into the array
   data_fdps_map_unit_array_ptr points at, resolved through
   fdps_get_unit_record (src/unit.h) and not range checked.

   `promotion_entry` picks one of the four 3-byte routes of that character's
   RankUp.dat record, 0..3, and is not range checked either: the byte pair it
   selects is read at record + promotion_entry * 3.  fdps_church_promote_loop
   picks 3 for a unit carrying item 0xdb (勇者徽章), 1 for 0xe0 (光之徽章),
   2 for 0xe1 (暗之徽章) and 0 for a unit carrying none of the three.

   THE RECORD IS WRITTEN BEFORE THE FIRST FRAME IS DRAWN, so a promotion that
   is interrupted has already happened.  Byte 0 of the route becomes the unit's
   portrait id at +0x07 and byte 1 its class code at +0x20.

   THE "BEFORE" FIGURE IS BUILT FROM THE CHARACTER ID AT +0x08, not from the
   portrait id at +0x07 this call overwrites.  The two hold the same value for
   every unit the church offers promotion to, because the caller only offers it
   while the portrait id is below 9, but reading +0x07 for the opening clip
   would show the promoted figure transforming into itself.

   The two shared blend tables are swapped to their fight copies for the
   length of the animation and the field copies are read back at the end, so
   the tables data_fdps_palette_shade_ramp_table and
   data_fdps_inverse_palette_cube hold the map values again on return.  Neither
   fopen result is tested, so a missing .tmp faults inside fread.

   The DAC is left holding the FIELD palette and the adapter is left showing
   the last frame of the animation: nothing here clears the screen on the way
   out.  The caller repaints. */
extern void fdps_church_promote_unit(int unit_index, int promotion_entry);
#pragma aux fdps_church_promote_unit "*" parm caller [];

#endif
