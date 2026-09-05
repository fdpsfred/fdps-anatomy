/* statunit.h -- one unit inside the status window.
 *
 * What statwin.c's window is filled with: the panel of one unit's figures,
 * that unit's inventory list, and the loop that holds the finished window on
 * the screen.  The window's own geometry is statwin.h's.
 */
#ifndef STATUNIT_H
#define STATUNIT_H

/* 00016300.  Paints one unit's figures, gauges, portrait and three names into
   a surface that already holds the empty window artwork.  This is the body of
   the window; the animation below only moves the finished picture about.

   unit_index selects the record through fdps_get_unit_record (unit.h) and is
   not range checked.  dest is an 8bpp surface AT ITS ORIGIN, not offset: every
   one of the nineteen destinations is a fixed byte offset from it, computed
   against a pitch of 320 that is pushed as a literal at each call, so the
   surface has to be a whole 320-pitch frame and the caller cannot move the
   layout.  All four call sites hand it the Status.cel image
   fdps_load_status_cel_image returned.

   WHAT IS DRAWN COMES OUT OF THE RECORD, BUT THE PORTRAIT SLOT DOES NOT
   ALWAYS.  The 24x24 cell at row 10, column 161 is sprite 0 of a sprite-cache
   slot, and which slot is data_fdps_village_mode_flag's decision: clear, it is
   the record's own sprite_cache_slot; set, it is unit_index itself, used as a
   slot number with nothing bounding it.  Setting the flag also latches
   unit_index into data_fdps_village_status_window_unit_idx below, which is the
   only thing that ever writes that global.

   FOUR OF THE FIGURES CHANGE COLOUR AND THE RULES ARE NOT THE SAME.  The
   colour is data_fdps_number_glyph_color_row (gamedata.h), set before each
   figure: row 3 for a current HP or MP that is below its maximum, row 1 for
   each of the three stats whose buff timer is running -- attack for ap,
   defense for dp, dexterity for hit, ev and dx together -- and row 0
   otherwise.  The HP branch has no else: whatever the caller left in the
   global stands until the first figure has been drawn, and only then is the
   global put back to 0.  It is left at 0 on the way out.

   THE EXPERIENCE FIGURE IS DRAWN IN A FIELD IT CANNOT FIT.  Record exp_carry
   holds 0..99 for one of the player's units and 0xff for anything else, and
   the 0xff is replaced with 1000 before it is drawn -- into a three-digit
   field, which fdps_draw_number (text.h) fills with three '?' glyphs rather
   than truncating.  So a unit that earns the player nothing shows "???" for
   experience, and that is the intent rather than an overflow.

   NOTHING IS CHECKED AND THE PORTRAIT LOAD CAN END THE PROCESS.  The record
   pointer, the sprite cache pointer, the text block pointer and the two sheet
   pointers are all used without a test, and the FACE.CEL load this ends with
   exits on a sheet it cannot open (msgwin.h). */
extern void fdps_draw_unit_status_panel(int unit_index, unsigned char *dest);
#pragma aux fdps_draw_unit_status_panel "*" parm caller [];

/* 00063fb0.  Which unit fdps_unit_status_window_wait_input animates while a
 * village phase is running, in place of the index its caller passed.
 *
 * fdps_draw_unit_status_panel is its only writer (MOV [0x00063fb0],EAX at
 * 00016318) and the wait loop its only reader (MOV EAX,[0x00063fb0] at
 * 0001725d); a sweep of the image for 0x00063fb0 finds those two instructions
 * and nothing else.  So it is not a general "current unit": it is one panel
 * draw handing one number to the loop that follows it, and it is read only
 * when data_fdps_village_mode_flag is set.
 *
 * A signed int, and it indexes the sprite cache's slot table with nothing
 * bounding it.  It is uninitialised until a panel has been drawn. */
extern int data_fdps_village_status_window_unit_idx;

/* 00063fc0.  The timer tick fdps_unit_status_window_wait_input last drew a
 * frame on, which is what paces the window: a pass draws only when
 * data_fdps_timer_tick_counter differs from this, and the pass ends by
 * copying the counter into it.
 *
 * Private to that one function -- all four instructions that name 0x00063fc0
 * are inside it -- but a global rather than a local, and that is observable:
 * it keeps its value between calls, so a window opened again on the same tick
 * a previous one closed on draws nothing until the timer moves.
 *
 * IT IS SIGNED, AND IT IS THE TICK THE WALK FRAME IS TAKEN FROM.  The walk
 * cycle is (this % 16) / 4 through IDIV and SAR at 000171fa and 00017206, not
 * through a shift and a mask, so the frame chosen once the counter has passed
 * 0x7fffffff is the one a signed division gives.  It is also this tick and
 * not the live counter that the frame is drawn for.
 *
 * Never cleared.  Nothing resets it when the window closes or when a chapter
 * ends. */
extern int data_fdps_unit_status_window_last_tick;

/* Holds the assembled status window on the screen until the player picks
   something, and comes back with the scancode they picked with.
   fdps_draw_status_window_anim_frame slides the window in, this keeps it
   there, and the same animation run backwards takes it away again.

   window_image is a whole 320x200 frame holding the window at its resting
   position -- what fdps_load_status_cel_image loaded and
   fdps_draw_unit_status_panel painted over.  IT IS WRITTEN AS WELL AS READ:
   the unit's 24x24 cell is stamped into it at row 10, column 161 on every
   frame drawn, so a caller that reuses the image afterwards is reusing one
   with the last walk frame in it.

   unit_index selects the sprite cache slot the walk cycle comes from -- the
   same slot number fdps_blit_unit_sprite uses -- and is IGNORED while
   data_fdps_village_mode_flag is set, when
   data_fdps_village_status_window_unit_idx is substituted for it.

   allow_idle_animation offers rather than requests: non-zero draws one rand
   and arms the idle sequence only when the value is a multiple of 200.  Both
   call sites are fixed -- the battle window passes 1, the item window 0 --
   and the difference is visible in the CRT's random state as well as on the
   screen, because a zero does not call rand at all.

   THE RESULT IS EVERY SCANCODE AT OR BELOW 0x7f, NOT A MENU CHOICE.  The loop
   filters out only the 0xff the reader answers with when nothing has been
   pressed, and every break code; deciding which of the remaining codes means
   anything is the caller's, and THE TWO CALLERS DECIDE DIFFERENTLY.  The item
   window keeps the value and compares it against 0x48, 0x50, 0x1c, 0x39, 0x1
   and 0x53, treating the rest as "keep going".  The battle window does not
   look at it at all -- EAX is overwritten by the instruction after the call
   returns (LEA EAX,[EBP-0x44] at 00016bff) -- so that window closes on any
   code at or below 0x7f, whichever key it was.

   IT DRAWS ONLY WHEN THE TIMER HAS MOVED.  Every pass polls the keyboard, and
   a pass draws a frame only when data_fdps_timer_tick_counter has left
   data_fdps_unit_status_window_last_tick behind, so the animation runs at the
   timer's rate however fast the loop spins.  A frame takes three heap blocks
   and gives all three back, checks none of them, and reads the sprite cache
   pointer without testing it for null. */
extern int fdps_unit_status_window_wait_input(unsigned char *window_image,
                                              int unit_index,
                                              char allow_idle_animation);
#pragma aux fdps_unit_status_window_wait_input "*" parm caller [];

/* 00024ea0.  Draws one unit's eight-slot inventory list: the selection bar on
   the highlighted row, then, for each filled slot, the item's category icon,
   its name and one headline number.

   dest_base IS THE TOP-LEFT CORNER OF THE LIST, NOT THE SURFACE ORIGIN.
   Every coordinate is folded into the pointer as dest_base + y * pitch + x
   with no surface descriptor anywhere, so the caller decides where the list
   lands.  All four call sites hand in their 320x200 window buffer + 0x3b58
   with a pitch of 0x140, which puts the list at pixel (0x98, 0x2f).  A row is
   17 pixels tall.

   selected_slot is the row that carries the bar.  Only 0..7 draws one;
   anything else -- fdps_battle_show_unit_status_window passes -1 -- leaves the
   list unhighlighted.  unit_index is not range checked and goes straight to
   fdps_get_unit_record (unit.h).

   THE ITEM TYPE IS CLASSIFIED TWICE AND THE TWO TESTS DISAGREE ABOUT TYPE 0.
   The icon test is 1..0x15 weapon, 0x16..0x27 armour, everything else the
   catch-all; the caption test is 1..0x15 attack power, then everything at or
   below 0x27 -- type 0 included -- defence power.  So a slot holding an id
   past the end of Item.dat (the guide's FF bug item, or 0xE2..0xFA whose
   records are blank) draws the plain item icon beside a DP figure.  Computing
   the category once and reusing it changes that row, which is why the two
   tests stay apart in the source.

   ONLY use_effect 0x0b AND 0x0c PRINT A RECOVERY AMOUNT.  Effect 0x20 also
   restores HP but falls through to the plain caption with no figure beside it.

   Which colour row the figures come out of is not set here: whatever the
   caller left in data_fdps_number_glyph_color_row (gamedata.h) stands for the
   whole list.  Nothing is null-checked -- neither sheet pointer, neither table
   base, nor the record. */
extern void fdps_draw_unit_inventory(int unit_index, int selected_slot,
                                     unsigned char *dest_base, int pitch);
#pragma aux fdps_draw_unit_inventory "*" parm caller [];

#endif
