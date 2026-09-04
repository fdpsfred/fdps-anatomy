/* indicat.h -- the battle indicator popups: the little numbers and words that
 * float over a unit after it is hit, healed, cured or missed.
 *
 * A popup is not drawn where it is asked for.  Every producer in this file
 * appends fixed-width CELLS to one shared queue -- four cells for a number,
 * four for a word -- and fdps_play_indicator_queue (0001f340) then blits every
 * queued cell over its own unit for 22 bouncing frames and clears the queue.
 * That is why a caller that queues several popups gets them animated together
 * in one pass rather than one after another.
 *
 * The queue is three parallel arrays plus a cursor, and a cell is the same
 * index into all three.  The cursor, data_fdps_indicator_queue_count, is
 * declared in gamedata.h because the item code reads it too.
 */
#ifndef INDICAT_H
#define INDICAT_H

/* How many cells the queue holds: 200, so 50 four-cell popups.  Nothing in the
   image checks the cursor against it -- the four producers each add their cells
   and advance it unconditionally.  Where the cursor is put back to zero belongs
   to its own declaration, in gamedata.h. */
#define INDICATOR_QUEUE_CELLS 200

/* 00064120.  Cell x: where this cell's glyph sits, in pixels, relative to the
   left edge of its popup.  fdps_show_number_indicator lays its four digits out
   at 2, 8, 14 and 20; the fixed-word popup at 0001f690 lays its four glyphs out
   at 1, 8, 13 and 19 -- i * 6 + 1, with cell 1 alone nudged a pixel right by its
   own branch at 0001f758 -- so the producers are NOT one helper working from
   different glyph tables, and folding them together moves the digits. */
extern unsigned char data_fdps_indicator_queue_cell_x_offset[INDICATOR_QUEUE_CELLS];

/* 000641e8.  Which unit this cell floats over, as a byte: the popup follows the
   unit's tile position at playback time rather than a position latched when it
   was queued. */
extern unsigned char data_fdps_battle_indicator_queue_unit_idx[INDICATOR_QUEUE_CELLS];

/* 000642b0.  The glyph id the cell draws out of the Number.cel sheet, or 0xff
   for a blank cell, which the player skips entirely -- that is how a short
   number is right-aligned inside its four cells without moving the popup. */
extern unsigned char data_fdps_indicator_queue_glyph_ids[INDICATOR_QUEUE_CELLS];

/* 0001f510.  Floats a number over one battle unit -- damage taken, HP healed,
   MP restored or a stat gain -- by appending four cells to the shared queue.
   Nothing is drawn here and nothing waits; the caller drains the queue with
   fdps_play_indicator_queue once it has queued every popup of the batch.

   The request is culled against the visible tile window first, and the window
   is deliberately asymmetric (rebuild_info/pitfalls.md): with the view origins
   divided by the 24-pixel tile size into origin_tx and origin_ty, a unit is
   shown when its tile x is in origin_tx .. origin_tx + 12 and its tile y is in
   origin_ty - 1 .. origin_ty + 8.  The x window starts at the origin column
   while the y window starts one row ABOVE the origin row, so a unit on the row
   just off the top of the screen still queues a popup and one in the column
   just off the left does not.  A culled request queues nothing at all and
   leaves the cursor where it was.

   value is formatted with "%d" and right-aligned into the four cells: the cells
   a shorter number does not need are queued blank, and the digits stay in order
   in the cells that remain.  A number of five digits or more keeps only its
   first four.  value is signed and a negative one spends a cell on its minus
   sign, which is drawn with the glyph base + '-' - '0', three below the base.

   glyph_base is the id in the Number.cel sheet of the digit zero of the set the
   number is drawn with, and each digit's own id is glyph_base + digit, worked
   out in 8-bit arithmetic.  The callers use 0 for damage, 0x0d for MP restored
   and stat gains and 0x27 for healing.

   unit_index is a position in the current battle's unit array, resolved through
   fdps_get_unit_record and not range checked; it is also what goes into the
   queue, as a byte, so the popup follows that unit while it plays. */
extern void fdps_show_number_indicator(int value, unsigned char glyph_base,
                                       int unit_index);
#pragma aux fdps_show_number_indicator "*" parm caller [];

/* 0001f690.  Floats the word MISS over one battle unit, to report that a spell
   or an item did nothing to that target, by appending four cells to the shared
   queue -- one per glyph of M, I, S, S.  Nothing is drawn here and nothing
   waits; the caller drains the queue with fdps_play_indicator_queue once it has
   queued every popup of the batch.

   The glyph ids are fixed at 0x34, 0x35, 0x36, 0x36 in the Number.cel sheet and
   there is no way to ask for a different word: the CURE popup at 0001f7d0 is a
   separate function with its own four ids, and the caller-supplied variant is
   fdps_show_sprite_indicator.

   The request is culled against the same asymmetric tile window
   fdps_show_number_indicator uses (rebuild_info/pitfalls.md): with the view
   origins divided by the 24-pixel tile size into origin_tx and origin_ty, the
   unit is shown when its tile x is in origin_tx .. origin_tx + 12 and its tile
   y is in origin_ty - 1 .. origin_ty + 8.  A culled request queues nothing and
   leaves the cursor where it was.

   The four cells sit 1, 8, 13 and 19 pixels into the popup -- i * 6 + 1 with
   cell 1 alone nudged a pixel right -- rather than the number popup's 2, 8, 14
   and 20.

   unit_index is a position in the current battle's unit array, resolved through
   fdps_get_unit_record and not range checked; it is also what goes into the
   queue, as a byte, so the popup follows that unit while it plays. */
extern void fdps_show_miss_indicator(int unit_index);
#pragma aux fdps_show_miss_indicator "*" parm caller [];

/* 0001f7d0.  Floats the word CURE over one battle unit, to report that a status
   ailment has just been lifted from it, by appending four cells to the shared
   queue -- one per glyph of C, U, R, E.  Nothing is drawn here and nothing
   waits; the caller drains the queue with fdps_play_indicator_queue once it has
   queued every popup of the batch.

   The glyph ids are fixed at 0x37, 0x38, 0x39, 0x3a in the Number.cel sheet and
   there is no way to ask for a different word, exactly as with
   fdps_show_miss_indicator; the caller-supplied variant is
   fdps_show_sprite_indicator.

   The three shipped call sites all fire on the same shape: the caller has found
   at least one of the target's three status-ailment bytes set, calls here, and
   only then clears them.  fdps_apply_item_effect_to_targets calls it for item
   effect 0x16 when the byte at record +0x25 is set and for effect 0x18 when
   +0x26 is set; fdps_cast_spell_on_targets calls it when any of the three is
   set.  So the popup marks a cure that actually took effect and never one
   applied to a unit that was not ailing -- a rebuild that queued it before
   testing the bytes would show CURE over every target of a curing spell.

   The request is culled against the same asymmetric tile window
   fdps_show_number_indicator uses (rebuild_info/pitfalls.md): with the view
   origins divided by the 24-pixel tile size into origin_tx and origin_ty, the
   unit is shown when its tile x is in origin_tx .. origin_tx + 12 and its tile
   y is in origin_ty - 1 .. origin_ty + 8.  A culled request queues nothing and
   leaves the cursor where it was.

   The four cells sit 1, 8, 13 and 19 pixels into the popup, the same offsets
   the MISS popup uses: the extra pixel on cell 1 is on the cell index and not
   on the letter, so here it moves the U.

   unit_index is a position in the current battle's unit array, resolved through
   fdps_get_unit_record and not range checked; it is also what goes into the
   queue, as a byte, so the popup follows that unit while it plays. */
extern void fdps_show_cure_indicator(int unit_index);
#pragma aux fdps_show_cure_indicator "*" parm caller [];

/* 0001f910.  Marks a list of battle units out on screen by flashing them all in
   one palette colour, and does not come back until it has finished: eight whole
   scenes composed offscreen and presented one after another, each waiting for
   the vertical retrace and then for the timer tick to move on.  A call
   therefore costs about eight ticks -- roughly four tenths of a second at the
   18.2 Hz the game's interrupt runs at -- and nothing else happens while it
   runs.  Unlike the popup producers above nothing is queued and nothing is left
   behind: the last frame is the picture the caller is left looking at.

   The listed units are drawn flat in `flash_color` on frames 2, 3, 6 and 7 and
   drawn normally on the other four, so the player sees them blink twice.  Every
   other unit on the map is painted by the scene compositor as usual, so a unit
   that is not listed shows through untouched.

   unit_count is how many entries of unit_indices to flash and zero is legal --
   the call is then an eight-tick pause with the scene redrawn under it.

   unit_indices is an array of unit_count BYTE indices into the current battle's
   unit array, widened unsigned, so an index above 127 names the unit it looks
   like it names.  Nothing range checks them; each one goes straight to
   fdps_blit_unit_sprite (sprite.h), which silently draws nothing for a unit
   whose sprite origin lies outside the visible scene.

   flash_color is the palette index every pixel of a flashed sprite becomes.
   The shipped callers pass 0xff -- the item and heal paths -- and 0x2b, which
   fdps_cast_spell_on_targets uses for effect id 0x11 and calls twice in a row
   so that effect flashes four times rather than two.

   The pacing depends on the timer interrupt actually advancing
   data_fdps_timer_tick_counter: with the interrupt not installed the frame
   waits never end and the call does not return. */
extern void fdps_flash_units_in_color(int unit_count,
                                      unsigned char *unit_indices,
                                      unsigned int flash_color);
#pragma aux fdps_flash_units_in_color "*" parm caller [];

/* 0001fc00.  Floats a caller-supplied word over one battle unit by appending
   one cell per glyph to the shared queue.  This is the only producer of the
   family whose message is an argument: fdps_show_miss_indicator and
   fdps_show_cure_indicator each carry one fixed word, and
   fdps_show_number_indicator formats digits.  Nothing is drawn here and nothing
   waits; the caller drains the queue with fdps_play_indicator_queue.

   sprite_ids points at four Number.cel glyph ids, one per cell, and an id of 0
   means the cell is unused.  An unused cell is NOT queued blank -- the queue's
   blank marker is 0xff and this function never writes it; the cell is simply
   left out.  A zero in the middle of the four is skipped and the cells after it
   are still queued, at their own cell positions.

   The cursor therefore moves by the number of cells actually queued, not by
   four, while each cell is stored at cursor + its cell index.  The two agree
   only while the non-zero ids form a prefix of the four, which holds for every
   label the shipped caller passes and is not checked here
   (rebuild_info/pitfalls.md).  The one shipped caller,
   fdps_cast_spell_on_targets, passes a row of a three-row table that spells
   Att, Def and Dex, each row three ids followed by a zero.

   The request is culled against the same asymmetric tile window
   fdps_show_number_indicator uses: with the view origins divided by the
   24-pixel tile size into origin_tx and origin_ty, the unit is shown when its
   tile x is in origin_tx .. origin_tx + 12 and its tile y is in
   origin_ty - 1 .. origin_ty + 8.  A culled request queues nothing and leaves
   the cursor where it was.

   The cells sit 1, 8, 13 and 19 pixels into the popup, the fixed-word offsets
   rather than the number popup's, with cell 1 nudged a pixel right whether or
   not the cells before it were used.

   unit_index is a position in the current battle's unit array, resolved through
   fdps_get_unit_record and not range checked; it is also what goes into the
   queue, as a byte, so the popup follows that unit while it plays. */
extern void fdps_show_sprite_indicator(int unit_index,
                                       unsigned char *sprite_ids);
#pragma aux fdps_show_sprite_indicator "*" parm caller [];

#endif
