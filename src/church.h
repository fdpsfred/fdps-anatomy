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

/* The church screen's whole class-change session: it promotes one member after
   another until the player cancels or nobody eligible is left.

   It is modal and it does not return until the session is over.  Every pass
   rebuilds the candidate list from scratch off the roster, so a member promoted
   on one pass has left the list by the next -- his level is back to 1 -- and a
   member whose badge was consumed is offered only his free route afterwards.

   WHO IS ELIGIBLE: level 20 or over AND a portrait id below 9.  The second test
   is on the portrait id at record +0x07 and not on the class code at +0x20, and
   it is what keeps out both the three characters RankUp.dat has no record for
   and everybody who has already been promoted -- promoted forms carry portrait
   ids 0x0f and up (rebuild_info/pitfalls.md).

   WHICH ROUTE A MEMBER IS OFFERED comes from his bag, one route each and never
   a choice between two: portrait id 0 (蘭迪斯) holding 0xdb 勇者徽章 gets
   route 3, otherwise 0xe0 光之徽章 gets route 1, 0xe1 暗之徽章 gets route
   2, and a member holding none of them gets route 0, the free promotion.  The
   badge is consumed only when the route it bought leads somewhere route 0 does
   not, so a badge that duplicates the free promotion is kept.

   WHAT IT WRITES INTO THE RECORD besides what fdps_church_promote_unit above
   writes: the route's movement bonus is added to +0x3b, the five maximum-growth
   bytes of the FRILEVUP.DAT row of the form being ENTERED are added to AP, DP,
   DX, HP and MP -- HP and MP to both the current and the maximum -- the level
   is reset to 1, and fdps_unit_recompute_combat_stats then refreshes the
   derived stats.

   THE SWEEP READS THE MAP UNIT ARRAY AND THE PICKER READS THE ROSTER.  This
   body resolves records through fdps_get_unit_record (src/unit.h) while the
   candidate list it hands the indices to resolves them through
   fdps_get_roster_record (src/table.h).  The two agree on the village screens
   and only there, because fdps_load_field_chapter_resources points
   data_fdps_map_unit_array_ptr at data_fdps_roster_array_ptr.

   `screen_page` is the caller's 320x200 8bpp page holding the church screen the
   window opens over.  It is handed straight to
   fdps_village_animate_window_zoom (src/village.h) and is also the memmove
   source that repaints the screen after each transformation animation, so it
   has to hold the full 64,000 bytes and it is only ever read.

   Nothing is returned.  On the way out the window is closed and the adapter is
   left holding the zoom's last frame. */
extern void fdps_church_promote_loop(unsigned char *screen_page);
#pragma aux fdps_church_promote_loop "*" parm caller [];

#endif
