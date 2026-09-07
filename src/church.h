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

/* Runs the church's class-change candidate list: shows up to three of the
   caller's candidates side by side with their walking icon, name, target class
   and level, and answers which one the player picked.

   It is modal and it draws its own frames.  Each pass takes one code from
   fdps_read_scancode_auto_repeat (src/keybd.h): Escape cancels, Enter and
   Space confirm, and the four arrow keys move the cursor -- right and left by
   one, down and up by three -- each guarded against the ends of the list and
   each playing Beep.wav.  The pass then composes the whole panel onto a page it
   mallocs and frees, presents it across one vertical retrace and waits for the
   timer tick to move on.

   `candidate_count` is how many entries the two arrays hold and is the only
   bound: nothing checks it against the roster, and a count of zero would draw
   an empty panel that still answers 0 on a confirm.  The sole caller,
   fdps_church_promote_loop, never hands over a zero -- it prints its own "no
   candidates" message instead -- so that arm is unreachable in the shipped
   game.

   `roster_indices` holds one party-roster index per candidate.  Each is
   resolved through fdps_get_roster_record (src/table.h) for the name and the
   level, AND is used unchanged as the walking icon's group in the sprite cache
   -- the two uses are the same number, so a caller cannot separate them.

   `promotion_choices` holds one route number per candidate, 0 to 3, picking
   which 3-byte entry of that character's RankUp.dat record names the target
   class the slot spells out.  Neither array is range checked at either end.

   THE CURSOR AND THE WINDOW ARE LOCAL AND ALWAYS START AT 0, so the list
   opens on the first candidate every time; the village member grid, which
   looks the same on screen, instead remembers where it was left.

   Returns the index into both arrays of the candidate the player confirmed, or
   -1 when the player cancelled.

   THE ADAPTER IS LEFT SHOWING THE LAST FRAME: the panel is presented once more
   after the key that ends the loop, and nothing here clears it.  The caller
   repaints. */
extern int fdps_church_select_promote_candidate(int candidate_count,
                                                int *roster_indices,
                                                int *promotion_choices);
#pragma aux fdps_church_select_promote_candidate "*" parm caller [];

#endif
