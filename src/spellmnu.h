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

/* 00064390.  The tick the spell-list window last drew a frame for.  The pacing
 * state of the wait loop below -- a global rather than a local, and that is
 * observable: it keeps its value between calls, so a window opened again on
 * the tick a previous one closed on draws nothing until the timer moves.
 *
 * IT IS SIGNED, AND BOTH ANIMATION PHASES ARE TAKEN FROM IT RATHER THAN FROM
 * THE LIVE COUNTER.  The arrow blink is (this / 8) & 1 through a SAR pair at
 * 00027932, and the walk frame is (this % 16) / 4 through IDIV and SAR at
 * 00027ab7 and 00027ac3; every one of those divisions is signed, so the phase
 * chosen once the counter has passed 0x7fffffff is the one signed division
 * gives.  The latch is advanced to data_fdps_timer_tick_counter only on the
 * last line of the frame.
 *
 * Never cleared.  Nothing resets it when the window closes or a chapter
 * ends. */
extern int data_fdps_spell_list_window_last_tick;

/* Holds the assembled spell-list window on the screen, keeps the caster
   walking and the scroll arrows blinking in it, and comes back with the first
   input code the caller can act on.

   window_buf is the composed 320-pitch window image, and it is READ AND
   WRITTEN.  Every drawn frame rewrites four regions of it -- the list area at
   row 47 column 152, the up arrow at row 49 column 220, the down arrow at row
   191 column 220 and the caster's 24x24 cell at row 10 column 161 -- and then
   stamps its columns 15..305 into the frame that goes to the adapter.  Both
   shipped callers hand in a 64000-byte 320x200 page, and the down arrow's
   sprite is declared 22 rows tall, so its rectangle nominally reaches row 212;
   Command.cel sprites 0x46 and 0x47 encode pixels only in their first five
   rows and skip the rest, so nothing is stored past the end of that page.

   panel_src is the pristine 151 x 149 spell-panel background at stride 0x97,
   blitted over the list area before the rows are redrawn so the previous
   frame's rows are erased.  Neither buffer is checked for null.

   list_top AND cursor_index ARE LIST INDICES, the same pair
   fdps_draw_spell_list_page takes, and list_top also decides the arrows: the
   up arrow is drawn when it is non-zero and the down arrow when list_top + 8
   is below the spell count.  THE COUNT IS COLLECTED ONCE, BEFORE THE LOOP --
   the caller may scroll the page between frames, but a spell learned or lost
   while the window is up does not change which arrows appear.

   unit_index picks the caster, and it is read three ways: it goes to the
   collector and to the page drawer, and it is the sprite cache slot ITSELF
   during a village phase.  Outside one the slot is byte +2 of the record
   fdps_get_unit_record returns -- struct fdps_unit_record's sprite_cache_slot
   -- and the record is fetched on both paths, so the lookup happens and its
   result is dropped when the village flag is set.

   The result is the scancode, and everything above 0x7f keeps the loop
   running.  ONE PASS IS NOT ONE FRAME: the loop polls the keyboard as fast as
   it can and draws only when the timer tick has moved since the last frame it
   drew, so the frame rate is the timer's and the poll rate is the machine's.

   NOTHING HERE CHECKS A malloc.  Three blocks are taken per frame -- a 360x240
   scene page, a whole 320x200 frame and a 24x24 cell -- and all three are given
   back before the pass ends; none is compared against NULL, and neither the
   sprite cache pointer nor the shadow sheet pointer is tested either
   (gamedata.h).  During a village phase the scene page is filled and then
   freed without ever being read. */
extern int fdps_spell_list_window_wait_input(unsigned char *window_buf,
                                             unsigned char *panel_src,
                                             int unit_index, int list_top,
                                             int cursor_index);
#pragma aux fdps_spell_list_window_wait_input "*" parm caller [];

/* Runs the battle spell submenu's selection loop: repaints the list, waits for
   a key and acts on it, until the player either confirms a spell the caster
   can pay for or cancels.  1 for a confirm, -1 for a cancel, and no other
   value: those two stores are the only ways out of a loop that has no exit
   condition of its own.

   THE CHOICE COMES BACK THROUGH cursor_index, AND SO DOES THE SCROLL THROUGH
   list_top.  Both are in/out and both are dereferenced afresh on every repaint
   and every key, so the caller sees the position the player left the cursor in
   and recovers the spell by collecting the ids again and reading
   ids[*cursor_index].  Neither is bounded on the way in: the up key stops at 0
   and the down key at the collected count - 1, but a cursor that arrives above
   that count keeps its value until the player walks it down.

   The eight-row window follows the cursor only when the cursor leaves it --
   the scroll goes to the cursor at the top edge, and to cursor - 7 at the
   bottom -- so an entry made with the cursor already off the page shows the
   caller's page until an edge is crossed.

   THE SPELL COUNT IS COLLECTED ONCE, BEFORE THE LOOP.  A spell learned or lost
   while the menu is up does not change how far the cursor may travel, although
   the list drawn under it does change, because the page drawer collects afresh
   on every frame.

   A CONFIRM THE CASTER CANNOT PAY FOR IS SILENT.  The MP cost is compared with
   the caster's current MP as signed values and equal MP pays; a shortfall
   makes no sound, prints nothing and does not move the cursor, it simply goes
   round again.  Nothing here spends MP.

   window_buf is the 320x200 window image the menu is composed into, read and
   written, and panel_src the pristine 151 x 149 copy of its list area laid
   back over it before every repaint.  Neither is checked for null, and
   unit_index is not range checked. */
extern int fdps_spell_list_select_loop(int unit_index,
                                       unsigned char *window_buf,
                                       unsigned char *panel_src, int *list_top,
                                       int *cursor_index);
#pragma aux fdps_spell_list_select_loop "*" parm caller [];

#endif
