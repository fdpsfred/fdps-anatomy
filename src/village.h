/* village.h -- the village phase: the between-battle town screen.
 *
 * The village is what runs between two chapters' battles: the party walks to a
 * destination, a signboard menu offers the shops and the roster screens, and a
 * gold readout sits in the corner.  This header declares that phase's entry
 * points and the one piece of state it keeps entirely to itself, the running
 * position inside the chapter's secret-shop unlock code.
 */
#ifndef VILLAGE_H
#define VILLAGE_H

/* 00060174 and 0006018c.  Where on the village map each of the six signboard
   destinations puts the party-member marker: the x table first, the y table
   0x18 bytes later, six int each.  The six-entry length is fixed by the
   addresses themselves -- 0x00060174 + 0x18 is exactly 0x0006018c and
   0x0006018c + 0x18 is exactly 0x000601a4, the next global below -- and the
   index is the destination number the signboard menu is on, 0..5, with 5 the
   hidden secret shop.

   Three functions read them, each with its own index.
   fdps_village_animate_walk_to_destination reads both entries of both tables
   on every call, the journey's two endpoints.  fdps_village_signboard_menu
   reads one entry of each per frame, at the cursor's current destination, to
   place the party-member marker.  fdps_run_village_phase reads one entry of
   each twice, at 0003139e/000313ae and 000313dd/000313ed, and hands the pair
   to fdps_transition_zoom as the centre the village zooms out from on the way
   into the menu and back into on the way out. */
extern int data_fdps_village_signboard_destination_x_table[6];
extern int data_fdps_village_destination_marker_y_table[6];

/* 000601a4.  Which .CEL cache slot the marker walking about the village map is
   drawn out of, so which party member the signboard menu is currently showing.
   The signboard menu cycles it over 0..data_fdps_roster_member_count - 1 and
   the walk animation indexes the slot table behind
   data_fdps_cel_sprite_cache_ptr with it (struct fdps_cel_cache_slot,
   src/fdpstype.h).  Nothing range-checks it against the thirty slots that
   table holds. */
extern int data_fdps_village_marker_roster_idx;

/* 000601c0.  How many scancodes of the current chapter's secret-shop unlock
   code the player has entered in a row.  It is the index of the next byte of
   the chapter's eight-byte code row that has to be matched, so it counts 0..7
   and never reaches 8 with the shipped table, whose longest code is seven
   keystrokes plus a terminator.

   fdps_check_secret_code_key is the only reader and the only writer in the
   image, and nothing resets it when a village is entered or left: the count
   carries across village visits and across chapters, and is cleared only by a
   keystroke that fails to extend the code. */
extern int data_fdps_secret_code_match_pos;

/* Feeds one keyboard scancode into the current chapter's secret-shop unlock
   code and answers 1 when this keystroke just completed it, 0 otherwise.

   The scancode is a make code, 0x01..0x7e: fdps_village_signboard_menu masks
   the value from fdps_read_keyboard_queue to a byte and calls only when it is
   below 0x7f, so the empty-queue 0xff and every break code are filtered out
   before they arrive.  A 1 makes that caller select the hidden sixth signboard
   entry, the secret shop. */
extern int fdps_check_secret_code_key(int scancode);
#pragma aux fdps_check_secret_code_key "*" parm caller [];

/* Walks the village map's party-member marker from one signboard destination
   to the next in six tick-paced frames, swapping the destination name plate
   over as it goes, and leaves the last frame on the adapter.

   `background` is the caller's own 320x200 8bpp page holding the painted town
   screen.  It is only read: every frame takes a fresh 64,000-byte page from
   the heap, copies this over it, draws into that and frees it again, so
   nothing the animation draws is left in the caller's page.

   `from_destination` and `to_destination` are signboard destination numbers,
   0..5, and index both coordinate tables above; the plate that shrinks away
   over the first three frames and the one that grows in over the last three
   are the sprites of those two numbers in `signboard_cel`.  Nothing checks
   either against the tables' six entries.

   `signboard_cel` is the "CanBan.cel" sheet out of "MISC.VFS": sprites 0..5
   are the destination name plates and sprite 6 the empty plate frame that is
   redrawn under each of them.

   THE MARKER'S FACING COMES FROM THE WHOLE JOURNEY AND IS FIXED FOR ALL SIX
   FRAMES, and only cell 0 of that facing is ever drawn, so the marker slides
   across without cycling its legs.  It also draws no walk cycle to interrupt:
   the six frames are the animation, and it is the caller that decides how
   often one runs. */
extern void fdps_village_animate_walk_to_destination(unsigned char *background,
                                                     int from_destination,
                                                     int to_destination,
                                                     unsigned char
                                                         *signboard_cel);
#pragma aux fdps_village_animate_walk_to_destination "*" parm caller [];

/* Runs the village signboard's modal menu and returns when the player has
   chosen a destination.  The choice is `*selection` on return; there is no
   other answer and no way to cancel.

   `background` is the caller's own 320x200 8bpp page holding the painted town
   screen, and it is only read: every frame takes a fresh 64,000-byte page from
   the heap, copies this over it, draws into that and frees it again.

   `selection` is both the starting position of the cursor and where the answer
   is written, in place, on every keystroke that moves it.  It is a signboard
   destination number: 0..4 are the five entries the arrow keys wrap over, and
   5 is the hidden secret shop, which is reachable ONLY by entering the
   chapter's unlock code (fdps_check_secret_code_key above) while the menu is
   up.  A caller that starts the cursor outside 0..5 indexes both coordinate
   tables out of range, and nothing here checks it.

   The keys are Left and Up for the previous entry, Right and Down for the
   next, Enter and Space to confirm, and Tab to show the next party member as
   the marker walking about the map.  Every one of them is fed to the unlock
   matcher first, so a keystroke that completes the code jumps to the secret
   shop instead of doing its usual job.

   WHAT THE MENU NEEDS IN PLACE BEFORE IT IS CALLED.  It loads "CanBan.cel" out
   of "MISC.VFS" itself, so the archive has to be readable -- a miss ends the
   process inside fdps_vfs_load_entry (vfs.h).  It draws the marker out of the
   block behind data_fdps_cel_sprite_cache_ptr, so that cache has to hold the
   roster member's group.  It paces every frame on data_fdps_timer_tick_counter
   and on the vertical retrace, so with the timer interrupt not installed the
   frame wait never ends.  And its entry check reads
   data_fdps_roster_member_count: a count of zero divides by zero the first
   time Tab is pressed.

   IT ANIMATES DAC ENTRIES 0xf0 TO 0xf4 AND NEVER PUTS THEM BACK.  Those five
   entries are stepped through a rotating six-bit ramp on every frame and are
   left wherever the last frame put them, so a caller that wants them at a
   fixed colour afterwards has to write them itself. */
extern void fdps_village_signboard_menu(unsigned char *background,
                                        int *selection);
#pragma aux fdps_village_signboard_menu "*" parm caller [];

#endif
