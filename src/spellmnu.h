/* spellmnu.h -- the spell list UI and the in-battle spell command
 * (rebuild_info/code_layout.md).
 *
 * The list itself is one page of eight rows drawn into a caller-supplied 8bpp
 * surface; the menus above it own the scroll position and the cursor and hand
 * both in as list indices.
 */
#ifndef SPELLMNU_H
#define SPELLMNU_H

/* Draws one eight-row page of a unit's known-spell list into an 8bpp surface,
   with the row the cursor sits on backed by the selection bar.

   Each drawn row carries four things, in this order: the SelBar.cel bar when
   the cursor is on it, the Command.cel spell icon, the spell's name out of
   Fdetxt00.txt, and the Command.cel MP caption with the spell's MP cost as a
   four-digit figure beside it.  The MP cost is byte 5 of the MAGICDAT.DAT
   record, struct fdps_spell_effect's mp_cost.

   THE SPELL IDS ARE COLLECTED AFRESH ON EVERY CALL.  Nothing is cached: the
   unit's bitmap goes through fdps_unit_collect_known_spells into a 40-byte
   stack buffer each time, so a page drawn after the unit learned or lost a
   spell shows the new list, and the cost of the redraw is the whole walk.  A
   unit that knows no spell at all draws NOTHING -- not an empty frame, not a
   cleared area -- and the surface keeps whatever was on it.

   list_top AND cursor_index ARE BOTH INDICES INTO THE WHOLE LIST, not row
   numbers.  The row a spell lands on is its list index minus list_top, and the
   bar is drawn on the row whose list index equals cursor_index; so the two
   pages of the status window pass list_top 0 and then 8, and a menu scrolled
   to entry 8 with the cursor on entry 10 draws the bar on row 2.  A
   cursor_index that is not on this page -- fdps_battle_show_unit_status_window
   passes -1 -- simply draws no bar.

   NOTHING IS CLIPPED AND NOTHING IS RANGE CHECKED.  The eight rows are drawn
   down to the collected count and nothing else bounds them, so a surface
   shorter than 154 scanlines -- row 7's MP caption starts at 7 * 0x11 + 0x0d
   and the Command.cel sprite is 22 tall -- is written past its end;
   unit_index is handed straight to the collector; and both .CEL sheet globals
   and the text block pointer are dereferenced without a null test, so called
   before fdps_load_global_resources has filled them this dereferences a null
   pointer.

   dest is the top left of the list area and pitch its row stride -- 0x140 for
   a full-width page, 0x97 for the narrow status-window buffer.  The spell name
   goes through fdps_draw_text, whose three colours are the standard message
   set, and drawing into an offscreen page is safe because the forty entries
   0x1be..0x1e5 of the shipped Fdetxt00.txt are glyph tokens and a terminator
   with no control code among them -- the page break and the two speaker codes
   are what would take the pen to the visible screen (text.h). */
extern void fdps_draw_spell_list_page(int unit_index, int list_top,
                                      int cursor_index, unsigned char *dest,
                                      int pitch);
#pragma aux fdps_draw_spell_list_page "*" parm caller [];

#endif
