/* vilmenu.h -- the village's party-member picker and the item loops built on
 * it.
 *
 * The picker is the 3x2 grid of walking icons that drops across the bottom of
 * every village screen that has to be told WHICH member it is about: sell,
 * transfer, equip, status, and the shop's "who is this for".  It answers a
 * roster index and nothing else; what the caller then does with that member is
 * the caller's business.
 *
 * The cursor and the scroll top are this file's own two globals below.  They
 * are declared here rather than in gamedata.h because no other translation
 * unit reads them.
 */
#ifndef VILMENU_H
#define VILMENU_H

/* 000601bc.  Which roster entry the picker's highlight is standing on, indexed
   over the party roster exactly as data_fdps_roster_member_count counts it --
   this is a roster index and not a position within the six visible cells.

   IT SURVIVES BETWEEN VISITS AND THAT IS THE POINT.  fdps_village_select_member
   is the only code in the image that touches it and it does not seed it on
   entry, not even to clamp it against the current member count, so the grid
   reopens on the member picked last time -- including when that pick was made
   from a different village screen.  Making it a local seeded to 0 would open
   the grid on the party leader every time. */
extern int data_fdps_village_member_select_cursor_idx;

/* 000601b8.  The roster entry drawn in the grid's top left cell: the window
   over the roster, six entries at a time in three columns of two.

   It moves in steps of three, one row, and only when the cursor leaves the six
   it shows -- forward when the cursor reaches this + 6, back when it drops
   below this.  Like the cursor above it is never seeded and never clamped on
   its own, so a party that has shrunk since the last visit can be opened with
   the window past the end of it and the grid then paints bare cells. */
extern int data_fdps_village_member_grid_scroll_offset;

/* Runs the village party-member grid to a decision and answers the roster
   index of the member the player confirmed, 0 to data_fdps_roster_member_count
   - 1, or -1 when the player backed out.

   IT IS MODAL AND IT PACES ITSELF.  The call does not return until a confirm
   or a cancel is read: each pass polls the CD music, takes one scancode
   through the auto-repeat filter (keybd.h), rebuilds the whole window into a
   fresh page, presents it on the retrace and ends by waiting for the timer
   tick to advance.  Escape and Delete both cancel; Enter and Space both
   confirm.

   FROM CHAPTER INDEX 0x17 ON, ROSTER SLOT 3 CANNOT BE CONFIRMED.  That slot is
   法蓮娜, who has left the party by then.  The refusal is silent and it is
   only a refusal: the cursor still stops on the entry, the entry is still
   drawn, and the confirm simply does nothing for that pass.  The same member
   is also drawn ghosted from that chapter on, but by a separate test on the
   record's character id rather than on the slot -- the two conditions are not
   one condition (rebuild_info/pitfalls.md).

   Reads data_fdps_roster_member_count for every bound, so a caller that has
   changed the party size does not have to tell the picker anything. */
extern int fdps_village_select_member(void);
#pragma aux fdps_village_select_member "*" parm caller [];

#endif
