/* village.c -- the village phase: the between-battle town screen.
 *
 * See village.h for what the phase covers.  This file holds the phase driver
 * itself -- the routine that decides whether this chapter has a town at all
 * and, if it has, loops the signboard menu over the five town screens -- the
 * signboard menu that loop runs, the walk animation that carries the marker
 * from one destination to the next, the gold readout and the window zoom every
 * shop opens with, and the menu's hidden entry: the per-chapter secret-shop
 * unlock code and the state machine that matches one keystroke at a time
 * against it.
 *
 * abs, malloc and free come from <stdlib.h>, memmove and memset from
 * <string.h> and inp and outp from <conio.h>, which is where Watcom 10.0a
 * declares each of them, and every one of the seven is a real call in the
 * original, because the flag set carries no -oi (rebuild_info/build_flags.md)
 * and the plain declarations are what reproduce them:
 *
 *   abs      0x0003d364  00031fdc 00031fea
 *   malloc   0x0003d375  000312b9 00031341 00031d9e 00032064
 *   free     0x0003d478  000312e7 00031386 00031403 000314a4 000314b2
 *                        00031f39 00031f4a 00032352
 *   memmove  0x0003d514  00031db6 00031f1a 0003207c 00032333
 *   memset   0x00042cd0  00031291 000312cf
 *   inp      0x0003d4e4  00031e51 00031f00 00032308 00032319
 *   outp     0x00042cb8  00031e9a 00031eb7 00031ed4 00031ef1
 */
#include <stdlib.h>
#include <string.h>
#include <conio.h>
#include "fdpstype.h"
#include "gamedata.h"
#include "audio.h"
#include "blit.h"
#include "cdaudio.h"
#include "chapter.h"
#include "keybd.h"
#include "palette.h"
#include "rsrc.h"
#include "save.h"
#include "sprite.h"
#include "text.h"
#include "transit.h"
#include "vfs.h"
#include "vilbar.h"
#include "village.h"
#include "vilmenu.h"
#include "vilshop.h"

/* The 320x200 8bpp page the whole game composes in, its pitch and its size,
   and the adapter's own linear frame buffer.  PUSH 0xfa00 at 00031d99,
   00031da9, 00031f0c, 0003205f and 00032325, PUSH 0x140 at 00031dc2 and
   000320c4 and PUSH 0xa0000 at 00031f15 and 0003232e.  0xa0000 is written as a
   literal because it is where the display adapter answers and not the address
   of anything this rebuild places. */
#define VGA_SCREEN_BASE 0x000a0000
#define VGA_SCREEN_PITCH 0x140
#define VGA_SCREEN_BYTES 0xfa00

/* The adapter's input status register and its vertical retrace bit -- PUSH
   0x3da / TEST AL,0x8 at 00031e4c, 00031efb, 00032303 and 00032314. */
#define VGA_INPUT_STATUS_1 0x3da
#define VGA_STATUS_VERTICAL_RETRACE 0x08

/* The DAC write port pair: the entry number goes to 0x3c8 and its red, green
   and blue components follow on 0x3c9, six bits each.  PUSH 0x3c8 at 00031e95
   and PUSH 0x3c9 at 00031eb2, 00031ecf and 00031ef1. */
#define VGA_DAC_WRITE_INDEX 0x3c8
#define VGA_DAC_DATA 0x3c9

/* The blit modes this file asks fdps_blit_dispatch for (blit.h): the opaque
   pass-through for the marker and the empty plate frame, the scaler for the
   name plate that is growing or shrinking, and the translucent kernel for the
   marker's afterimages. */
#define BLIT_MODE_OPAQUE 0
#define BLIT_MODE_SCALED 4
#define BLIT_MODE_TRANSLUCENT 9

/* The marker sprite's size, PUSH 0x18 / PUSH 0x18 at 00031dc7 and 000320c9. */
#define MARKER_W 0x18
#define MARKER_H 0x18

/* A cache slot's twelve stream offsets are four facings of three walk cells
   (struct fdps_cel_cache_slot, src/fdpstype.h), so the first three of the
   twelve are the three cells of facing 0. */
#define WALK_CELLS_PER_FACING 3

/* Where the destination name plate goes on the screen: PUSH 0xae / PUSH 0x3 at
   00031e0e and 000320ed for the corner.  Both functions draw the plate in the
   same box, and the sheet's own sprites are exactly that box's 0x48 by 0x18. */
#define PLATE_BOX_X 3
#define PLATE_BOX_Y 0xae

/* Sprite 6 of the sheet is the empty plate frame, redrawn opaque under every
   name plate: PUSH 0x6 at 00031e19 and 000320fd. */
#define PLATE_FRAME_SPRITE 6

/* --- fdps_run_village_phase @ 00031210 ---------------------------------- */

/* The town's own two sheets and the container they live in.  Both member
   names reach fdps_vfs_load_entry, which upper-cases the MEMBER name in the
   caller's own storage before the compare (vfs.h), so these two literals are
   folded to VILLAGE.CEL and SHOPWIN.CEL by the first call and cannot live in
   read-only storage (rebuild_info/pitfalls.md).  The container name is copied
   raw and is left as it stands: the original holds one "MISC.VFS" at 0x60128
   that every push in the image points at, and whether this translation unit
   ends up with one copy of that text or two is not observable, because
   nothing ever writes through a container path.  Sprite 0 of Village.cel is
   the whole 320x200 town picture, PUSH 0x0 as the index at 00031374. */
#define VILLAGE_ARCHIVE "MISC.VFS"
#define VILLAGE_MAP_SHEET "Village.cel"
#define VILLAGE_WINDOW_SHEET "ShopWin.Cel"
#define VILLAGE_MAP_SPRITE 0

/* The chapters that have no town between them and the battle.  Ids 0x10 and
   0x11 are the player's chapters 17 and 18, 0x15 and 0x16 his 22 and 23, and
   every id above 0x19 is chapter 27 onwards -- the run the story never comes
   back to a village in.

   It is a macro and not a helper because the original has no call here: the
   five compares stand inline twice, at 0003123d..0003126c on the way in and
   again at 00031432..00031461 after the bar shop, and both times read
   data_fdps_chapter_current_chapter_id afresh for each one.  Every compare is
   SIGNED -- JLE and not JBE -- so a negative id falls below the first range
   rather than above the last (contract C). */
#define NO_VILLAGE_RUN_FIRST 0x10
#define NO_VILLAGE_RUN_LAST 0x11
#define NO_VILLAGE_CHAPTER_A 0x15
#define NO_VILLAGE_CHAPTER_B 0x16
#define LAST_CHAPTER_WITH_VILLAGE 0x19

#define CHAPTER_HAS_NO_VILLAGE(chapter_id)                                  \
    ((((chapter_id) >= NO_VILLAGE_RUN_FIRST) &&                             \
      ((chapter_id) <= NO_VILLAGE_RUN_LAST)) ||                             \
     ((chapter_id) == NO_VILLAGE_CHAPTER_A) ||                              \
     ((chapter_id) == NO_VILLAGE_CHAPTER_B) ||                              \
     ((chapter_id) > LAST_CHAPTER_WITH_VILLAGE))

/* Where the signboard cursor can be left standing, which is what the jump
   table at 00031328 turns into a screen.  0 is the entry that ends the phase
   and is the only value the do-while stops on; the other five are the five
   town screens, in the order the table lists them. */
#define VILLAGE_DESTINATION_LEAVE 0
#define VILLAGE_DESTINATION_BAR 1
#define VILLAGE_DESTINATION_CHURCH 2
#define VILLAGE_DESTINATION_ITEMS 3
#define VILLAGE_DESTINATION_WEAPONS 4
#define VILLAGE_DESTINATION_SECRET 5

/* The two directions fdps_transition_zoom is asked for (transit.h): non-zero
   pulls the picture back out of the magnification and leaves it up, zero runs
   the same ramp backwards and leaves the screen blank.  MOV EAX,0x1 at
   0003138e opens the menu and XOR EAX,EAX at 000313d0 closes it, both centred
   on the signboard the cursor is standing on. */
#define ZOOM_PULL_BACK 1
#define ZOOM_IN 0

/* The music the phase asks for: index 2 is the town theme, PUSH 0x2 at
   00031319, and -1 is silence, PUSH -0x1 at 00031272 and 000314e6.  Both are
   0-based music indices and not CD track numbers (cdaudio.h). */
#define VILLAGE_MUSIC_INDEX 2
#define MUSIC_SILENCE -1

/* The chapter track table has two columns and the village asks for the first,
   PUSH 0x0 at 0003151a (cdaudio.h). */
#define VILLAGE_TRACK_SLOT 0

/* The whole DAC, PUSH 0xff / PUSH 0x0 at 0003129f and 000312a4, at no bias. */
#define FIRST_DAC_ENTRY 0
#define LAST_DAC_ENTRY 0xff

/* 00031210.  One whole village phase.  See village.h for what a caller gets
   back and what has to be in place before the call; what follows is how the
   assembly maps onto the C.

   THE KEYBOARD IS FLUSHED BEFORE THE QUIT IS TESTED.  CALL 0x000567b3 at
   00031223 comes before CMP byte ptr [0x000643eb],0x0 at 00031228, so even
   the phase that returns at once throws the pending keystrokes away.  That is
   the only thing the early-out does.

   THE TWO HALVES ARE A CHAPTER TEST AND THEY SHARE A TAIL.  The gate falls
   through into the no-village half and jumps to the town at 000312f4, which
   is an `if (no village) ... else ...` and not the other way round; the
   doubled JMPs at 00031258 and 00031263 are what -od makes of an || chain.
   Both halves then land on 000314d5, so the terrain-HUD save, the fade to
   silence, the chapter script and the disc check run exactly once whichever
   half ran.

   THE PAGE LOCAL IS ONE LOCAL.  [EBP-0x10] is the blanked page the save
   screen is handed on the no-village side and the composed town page inside
   the menu loop on the other, and the frame is only 0x18 bytes -- six dwords
   for a return temp, the saved HUD byte, the selection, this page, the map
   sheet and the switch temp -- so the two uses are one variable and not two
   that happened to share a slot.

   THE MENU LOOP RELOADS EVERYTHING EVERY PASS.  The malloc at 00031341 and
   the Village.cel load at 00031358 are both inside the loop and both are
   released before the switch, so a phase that visited three screens made
   three allocations and three loads.  Only ShopWin.Cel is loaded once,
   outside, and held in data_fdps_village_window_sheet_ptr for the window
   animations the shop screens play (village.h).  The answer from malloc is
   not tested, the same as the original.

   THE SELECTION SURVIVES THE SCREEN THAT RAN.  fdps_village_signboard_menu
   writes it in place, the switch dispatches on it, and it is still what the
   do-while tests afterwards -- so the next pass opens the zoom centred on the
   signboard the player just used, and a screen that wanted the phase to end
   would have to set it to 0.  Only the bar shop does: after it the chapter
   test runs a second time and forces the selection back to 0 for a chapter
   that has since stopped having a village, which is how a bar that advanced
   the story ends the phase instead of returning to the signboard.

   THE TERRAIN-HUD SETTING IS SAVED AS AN INT AND PUT BACK AS A BYTE.  XOR
   EAX,EAX / MOV AL,[0x00060158] at 000314d5 widens the byte into the dword
   local, and MOV AL,[EBP-0x8] / MOV [0x00060158],AL at 0003150b narrows it
   again; the global is one unsigned byte read only as a boolean (gamedata.h),
   so nothing here is sign-sensitive.

   THE CHAPTER SCRIPT IS NOT RANGE-CHECKED.  CALL dword ptr [EAX + 0x60074] at
   00031505 scales the id by four and calls, the id being whatever the chapter
   that just ended left behind; see chapter.h for what the table is.  The disc
   check after it is NOT guarded by the quit flag, so a phase the player quit
   out of still stops the drive and may still ask for a disc. */
int fdps_run_village_phase(void)
{
    int selection;
    int saved_terrain_hud_enabled;
    unsigned char *screen_page;
    unsigned char *village_map_sheet;

    selection = VILLAGE_DESTINATION_LEAVE;
    fdps_flush_keyboard_queue();
    if (data_fdps_shared_quit_game_requested != 0) {
        return 0;
    }

    if (CHAPTER_HAS_NO_VILLAGE(data_fdps_chapter_current_chapter_id)) {
        fdps_cd_set_music_track(MUSIC_SILENCE);
        if (data_fdps_village_skip_save_prompt_flag == 0) {
            memset((void *) VGA_SCREEN_BASE, 0, (size_t) VGA_SCREEN_BYTES);
            fdps_set_palette_range(
                (struct fdps_palette_entry *) data_fdps_vga_main_palette_ptr,
                FIRST_DAC_ENTRY, LAST_DAC_ENTRY, 0, 0, 0);
            screen_page = (unsigned char *) malloc((size_t) VGA_SCREEN_BYTES);
            memset(screen_page, 0, (size_t) VGA_SCREEN_BYTES);
            fdps_save_game_screen(screen_page);
            free(screen_page);
        }
    } else {
        data_fdps_village_mode_flag = 1;
        data_fdps_village_window_sheet_ptr = (unsigned char *)
            fdps_vfs_load_entry(VILLAGE_ARCHIVE, VILLAGE_WINDOW_SHEET);
        fdps_load_field_chapter_resources();
        fdps_cd_set_music_track(VILLAGE_MUSIC_INDEX);

        do {
            screen_page = (unsigned char *) malloc((size_t) VGA_SCREEN_BYTES);
            village_map_sheet = (unsigned char *)
                fdps_vfs_load_entry(VILLAGE_ARCHIVE, VILLAGE_MAP_SHEET);
            fdps_cel_blit_sprite(village_map_sheet, VILLAGE_MAP_SPRITE,
                                 screen_page, VGA_SCREEN_PITCH, 0, 0, 0,
                                 BLIT_MODE_OPAQUE);
            free(village_map_sheet);

            fdps_transition_zoom(
                screen_page,
                data_fdps_village_signboard_destination_x_table[selection],
                data_fdps_village_destination_marker_y_table[selection],
                ZOOM_PULL_BACK);
            fdps_village_signboard_menu(screen_page, &selection);
            fdps_transition_zoom(
                screen_page,
                data_fdps_village_signboard_destination_x_table[selection],
                data_fdps_village_destination_marker_y_table[selection],
                ZOOM_IN);
            free(screen_page);

            switch (selection) {
            case VILLAGE_DESTINATION_BAR:
                fdps_run_bar_shop();
                if (CHAPTER_HAS_NO_VILLAGE(
                        data_fdps_chapter_current_chapter_id)) {
                    selection = VILLAGE_DESTINATION_LEAVE;
                }
                break;
            case VILLAGE_DESTINATION_CHURCH:
                fdps_run_church_screen();
                break;
            case VILLAGE_DESTINATION_ITEMS:
                fdps_village_item_menu();
                break;
            case VILLAGE_DESTINATION_WEAPONS:
                fdps_run_weapon_shop();
                break;
            case VILLAGE_DESTINATION_SECRET:
                fdps_run_secret_menu();
                break;
            }

            fdps_flush_keyboard_queue();
        } while (selection != VILLAGE_DESTINATION_LEAVE &&
                 data_fdps_shared_quit_game_requested == 0);

        free(data_fdps_village_window_sheet_ptr);
        free(data_fdps_shop_stock_table_ptr);
        data_fdps_village_mode_flag = 0;
        data_fdps_map_unit_count = 0;
        data_fdps_map_unit_array_ptr = NULL;
    }

    saved_terrain_hud_enabled = data_fdps_ui_terrain_hud_user_enabled;
    data_fdps_ui_terrain_hud_user_enabled = 0;
    fdps_cd_set_music_track(MUSIC_SILENCE);
    if (data_fdps_shared_quit_game_requested == 0) {
        (*data_fdps_chapter_init_handler_table[
            data_fdps_chapter_current_chapter_id])();
    }
    data_fdps_ui_terrain_hud_user_enabled =
        (unsigned char) saved_terrain_hud_enabled;
    data_fdps_village_skip_save_prompt_flag = 0;
    fdps_cd_verify_disc_and_play_track(data_fdps_chapter_current_chapter_id,
                                       VILLAGE_TRACK_SLOT);
    return selection;
}

/* The archive, the sheet and the cursor click.  All three are literals the
   original keeps in its writable data segment -- "MISC.VFS" at 0x60128 and
   "CanBan.cel" at 0x61f40 are the two pushes at 00031c13 and 00031c0d, and
   "Beep.wav" at 0x61f4c is pushed at 00031c95 and 00031ce6 -- and two of the
   three are written to.  The member name reaches strupr inside
   fdps_vfs_load_entry and the cue name reaches it inside fdps_play_sfx, both
   of which upper-case the CALLER'S own storage in place (vfs.h, audio.h), so
   those two literals are folded to upper case by the first pass through and
   cannot live in read-only storage (rebuild_info/pitfalls.md).  The archive
   path is not folded: fdps_vfs_open copies a path raw. */
#define SIGNBOARD_ARCHIVE "MISC.VFS"
#define SIGNBOARD_SHEET "CanBan.cel"
#define SIGNBOARD_MOVE_SFX "Beep.wav"

/* The five entries the cursor wraps over, MOV EBX,0x5 / IDIV at 00031ca6 and
   00031cf5.  Stepping back is +4 rather than -1, which is what keeps the
   remainder off the negative side of a SIGNED division for any selection
   already in 0..4 (SAR EDX,0x1f before both IDIVs). */
#define SIGNBOARD_ENTRIES 5
#define SIGNBOARD_STEP_BACK 4

/* The hidden sixth destination, MOV dword ptr [EAX],0x5 at 00031c60: the
   secret shop, which only the chapter's unlock code selects.  It is a
   destination like any other afterwards -- both coordinate tables have a
   sixth entry (village.h) and the sheet has a sixth name plate -- and the next
   arrow key takes the wrapping cursor straight back into 0..4. */
#define SIGNBOARD_SECRET_SHOP 5

/* The make codes the loop acts on, off the CMP immediates at 00031c84,
   00031c8a, 00031cd5, 00031cdb, 00031d21, 00031d27 and 00031d33.  They are set
   1 scancodes as fdps_keyboard_isr queues them (keybd.h), so Enter is 0x1c and
   Tab is 0x0f rather than any ASCII value. */
#define SIGNBOARD_KEY_TAB 0x0f
#define SIGNBOARD_KEY_ENTER 0x1c
#define SIGNBOARD_KEY_SPACE 0x39
#define SIGNBOARD_KEY_UP 0x48
#define SIGNBOARD_KEY_LEFT 0x4b
#define SIGNBOARD_KEY_RIGHT 0x4d
#define SIGNBOARD_KEY_DOWN 0x50

/* The lowest code the loop throws away.  CMP dword ptr [EBP-0xc],0x7f / JGE at
   00031c43, a SIGNED compare on the byte the reader widened, so 0x7f itself is
   ignored along with everything above it -- which is what drops the 0xff
   fdps_read_keyboard_queue answers with when the ring is empty (keybd.h) and
   keeps every break code out of the unlock-code matcher. */
#define SIGNBOARD_SCANCODE_IGNORED_FROM 0x7f

/* The marker's leg cycle: MOV EBX,0x5 / DIV / AND EAX,0x3 at 00031d53, then
   CMP dword ptr [EBP-0x14],0x3 / MOV 0x1 at 00031d65.  Five timer ticks to a
   cell, four cells to a cycle and the fourth folded onto the second, so the
   visible order is 0, 1, 2, 1 over the three cells of the slot's first facing.
   The division is DIV and not IDIV because data_fdps_timer_tick_counter is
   unsigned, which is what makes the cycle survive the counter wrapping. */
#define MARKER_CELL_TICKS 5
#define MARKER_CELL_MASK 3
#define MARKER_CELL_ABSENT 3
#define MARKER_CELL_FOLDED_TO 1

/* The five DAC entries the menu animates and how the ramp slides over them.
   ADD EAX,0xf0 at 00031e8f is the first entry and CMP dword ptr [EBP-0x8],0x5
   at 00031e7c the count; MOV EBX,0x3 / DIV then MOV EBX,0x5 / DIV at 00031e5d
   through 00031e70 is the phase -- three timer ticks to a step and five steps
   to a cycle, both divisions unsigned like the leg cycle's.

   EACH RAMP IS NINE BYTES AND NOT FIVE.  It is a five-long cycle with its
   first four values repeated behind it, so the five-entry window at any phase
   0..4 reads inside the array and the original needs no modulus in the inner
   loop: MOV AL,byte ptr [EAX + EBP*0x1 + -0x48] at 00031ea8 indexes with
   phase + entry, whose maximum is 8. */
#define SIGNBOARD_DAC_FIRST_ENTRY 0xf0
#define SIGNBOARD_DAC_ENTRIES 5
#define SIGNBOARD_RAMP_TICKS 3
#define SIGNBOARD_RAMP_PHASES 5
#define SIGNBOARD_RAMP_BYTES 9

/* 00031bc0.  The modal signboard menu the village phase runs.  See village.h
   for what the arguments are and what a caller has to know; what follows is
   how the assembly maps onto the C.

   THE FRAME COUNTER IS NEVER SEEDED, and it drives three separate things.  The
   local at [EBP-0x24] is read at 00031d5a for the marker's leg cell, at
   00031e64 for the palette phase and at 00031f22 for the frame wait, and its
   only write is at 00031f32 at the tail of the loop body.  The loop is entered
   by falling through, so the first pass reads whatever the stack held: the
   first frame starts on an arbitrary leg cell and an arbitrary palette phase
   and its wait falls straight through.  The effect is bounded rather than a
   crash -- the value is reduced by (v / 5) & 3 and by (v / 3) % 5 -- and
   seeding it from data_fdps_timer_tick_counter before the loop, which is the
   intuitive C, would diverge (rebuild_info/pitfalls.md).

   THE CONFIRMING PASS STILL DRAWS ITS FRAME.  Enter and Space set the flag and
   then fall into the rest of the body at 00031d31, and the flag is only tested
   back at the top of the loop, so the menu's last act is a full frame: a
   marker, both plates, a palette step and a page copy to the adapter.  Ending
   the loop where the key is read would leave the previous frame on screen.

   THE KEY CHAIN STARTS WITH THE UNLOCK CODE, and every accepted make code goes
   into it: CALL 0x000357a0 at 00031c51 comes before any of the compares, so
   the arrows and Tab feed the matcher on their way past.  A completed code
   takes the whole chain -- the arrow tests are the ELSE of it -- so the
   keystroke that finishes the code does not also move the cursor.

   THE CURSOR'S OLD POSITION IS TAKEN BEFORE THE KEY IS ACTED ON, at 00031c3b,
   so the walk animation is handed where the marker was standing and where it
   is going.  It is read on every pass whether or not anything moves.

   THE ROSTER INDEX IS RANGE-CHECKED ONCE, on entry at 00031bf1 and with a
   SIGNED compare, and never again inside the loop.  Tab's own step is a signed
   modulus by data_fdps_roster_member_count, which the entry check has just
   made non-zero for any index that survived it -- but only for that index: a
   party of zero members reaches the modulus at 00031d4b and divides by zero,
   exactly as the original does.

   THE PALETTE STEP AND THE PAGE COPY STRADDLE THE RETRACE FROM THE SAME SIDE.
   The first spin at 00031e4c waits for the retrace to BEGIN, the DAC writes go
   out inside it, and the second spin at 00031efb waits for it to END before
   the page copy starts.  So the palette changes where it cannot tear and the
   copy runs into the visible frame, which is the opposite way round from the
   walk animation's own present (see below).

   THE SHEET IS LOADED ONCE AND THE PAGE EVERY FRAME.  fdps_vfs_load_entry at
   00031c14 and its free at 00031f4a are outside the loop; malloc at 00031d9e
   and free at 00031f39 are inside it, so a menu that ran for n frames made n
   allocations and one load.  malloc's answer is not tested, the same as the
   original.

   The frames are paced by the retrace and by the timer tick, so how many
   instructions stand between them is not observable (contract D). */
void fdps_village_signboard_menu(unsigned char *background, int *selection)
{
    /* The three six-bit component ramps that cycle DAC entries 0xf0..0xf4.
       They are copied onto the frame on every call -- LEA EDI,[EBP-0x48] / MOV
       ESI,0x31064 / MOVSD MOVSD MOVSB at 00031bd0, and the same at 00031bdb
       and 00031be6 -- which is what an automatic array with an initializer
       compiles to, and not what a static const table compiles to. */
    unsigned char ramp_red[SIGNBOARD_RAMP_BYTES] = {
        0x3c, 0x37, 0x31, 0x33, 0x37, 0x3c, 0x37, 0x31, 0x33
    };
    unsigned char ramp_green[SIGNBOARD_RAMP_BYTES] = {
        0x3c, 0x39, 0x33, 0x36, 0x39, 0x3c, 0x39, 0x33, 0x36
    };
    unsigned char ramp_blue[SIGNBOARD_RAMP_BYTES] = {
        0x3c, 0x3c, 0x3a, 0x3b, 0x3c, 0x3c, 0x3c, 0x3a, 0x3b
    };
    /* The "CanBan.cel" sheet, held for the whole run of the menu.  A byte and
       not a struct pointer: nothing here reads the header. */
    unsigned char *signboard_cel;
    /* Set by Enter or Space and tested at the top of the loop, so it is the
       only way out.  A byte in the original's frame. */
    char done;
    /* The make code this pass took out of the ring, widened from the byte
       fdps_read_keyboard_queue sets in AL -- the AND EAX,0xff at 00031c33.
       0xff means the ring was empty and is dropped by the threshold. */
    int scancode;
    /* Which destination the cursor was on when this pass started, so the walk
       animation knows where the marker has to set off from. */
    int previous_selection;
    /* Which of the marker's three leg cells this frame shows. */
    int marker_cell;
    /* That cell's packed stream, out of the roster member's own cache slot. */
    unsigned char *marker_stream;
    /* The private page this frame is composed on, taken and released inside
       the loop.  malloc's answer is not tested, the same as the original. */
    unsigned char *page;
    /* Where the sliding five-entry window into the three ramps starts. */
    int ramp_phase;
    /* Which of the five animated DAC entries is being written. */
    int dac_entry;
    /* The tick the previous frame ended on, and the clock both animations are
       read off.  Deliberately not initialised -- see the note above. */
    unsigned int last_tick;

    done = 0;
    if (data_fdps_village_marker_roster_idx >= data_fdps_roster_member_count) {
        data_fdps_village_marker_roster_idx = 0;
    }
    signboard_cel = (unsigned char *)
        fdps_vfs_load_entry(SIGNBOARD_ARCHIVE, SIGNBOARD_SHEET);

    while (done == 0) {
        fdps_cd_music_repeat_poll();
        scancode = fdps_read_keyboard_queue();
        previous_selection = *selection;

        if (scancode < SIGNBOARD_SCANCODE_IGNORED_FROM) {
            if (fdps_check_secret_code_key(scancode) != 0) {
                *selection = SIGNBOARD_SECRET_SHOP;
                fdps_village_animate_walk_to_destination(background,
                                                         previous_selection,
                                                         *selection,
                                                         signboard_cel);
            } else if (scancode == SIGNBOARD_KEY_LEFT
                       || scancode == SIGNBOARD_KEY_UP) {
                fdps_play_sfx(SIGNBOARD_MOVE_SFX);
                *selection = (*selection + SIGNBOARD_STEP_BACK)
                             % SIGNBOARD_ENTRIES;
                fdps_village_animate_walk_to_destination(background,
                                                         previous_selection,
                                                         *selection,
                                                         signboard_cel);
            } else if (scancode == SIGNBOARD_KEY_RIGHT
                       || scancode == SIGNBOARD_KEY_DOWN) {
                fdps_play_sfx(SIGNBOARD_MOVE_SFX);
                *selection = (*selection + 1) % SIGNBOARD_ENTRIES;
                fdps_village_animate_walk_to_destination(background,
                                                         previous_selection,
                                                         *selection,
                                                         signboard_cel);
            } else if (scancode == SIGNBOARD_KEY_ENTER
                       || scancode == SIGNBOARD_KEY_SPACE) {
                done = 1;
            } else if (scancode == SIGNBOARD_KEY_TAB) {
                data_fdps_village_marker_roster_idx =
                    (data_fdps_village_marker_roster_idx + 1)
                    % data_fdps_roster_member_count;
            }
        }

        marker_cell = (int) ((last_tick / MARKER_CELL_TICKS)
                             & MARKER_CELL_MASK);
        if (marker_cell == MARKER_CELL_ABSENT) {
            marker_cell = MARKER_CELL_FOLDED_TO;
        }
        /* A stored offset is measured from the base of the cache block, not
           from the slot it was read out of.  The cell number is the whole of
           the index, with no facing term ahead of it, so the three cells the
           menu can show are the first facing's three (WALK_CELLS_PER_FACING)
           and the other nine offsets of the slot are never reached. */
        marker_stream = data_fdps_cel_sprite_cache_ptr
            + ((struct fdps_cel_cache_slot *) data_fdps_cel_sprite_cache_ptr)
                  [data_fdps_village_marker_roster_idx]
                      .sprite_offset[marker_cell];

        page = (unsigned char *) malloc((size_t) VGA_SCREEN_BYTES);
        memmove(page, background, (size_t) VGA_SCREEN_BYTES);

        fdps_blit_dispatch(marker_stream,
                           page
                           + data_fdps_village_destination_marker_y_table
                                 [*selection] * VGA_SCREEN_PITCH
                           + data_fdps_village_signboard_destination_x_table
                                 [*selection],
                           MARKER_W, MARKER_H, VGA_SCREEN_PITCH, 0,
                           BLIT_MODE_OPAQUE);
        fdps_cel_blit_sprite(signboard_cel, PLATE_FRAME_SPRITE, page,
                             VGA_SCREEN_PITCH, PLATE_BOX_X, PLATE_BOX_Y, 0,
                             BLIT_MODE_OPAQUE);
        fdps_cel_blit_sprite(signboard_cel, *selection, page,
                             VGA_SCREEN_PITCH, PLATE_BOX_X, PLATE_BOX_Y, 0,
                             BLIT_MODE_OPAQUE);

        while ((inp(VGA_INPUT_STATUS_1) & VGA_STATUS_VERTICAL_RETRACE) == 0) {
            /* Spin until the retrace begins, so the DAC writes below land
               inside it. */
        }
        ramp_phase = (int) ((last_tick / SIGNBOARD_RAMP_TICKS)
                            % SIGNBOARD_RAMP_PHASES);
        for (dac_entry = 0; dac_entry < SIGNBOARD_DAC_ENTRIES; dac_entry++) {
            outp(VGA_DAC_WRITE_INDEX, dac_entry + SIGNBOARD_DAC_FIRST_ENTRY);
            outp(VGA_DAC_DATA, ramp_red[ramp_phase + dac_entry]);
            outp(VGA_DAC_DATA, ramp_green[ramp_phase + dac_entry]);
            outp(VGA_DAC_DATA, ramp_blue[ramp_phase + dac_entry]);
        }
        while ((inp(VGA_INPUT_STATUS_1) & VGA_STATUS_VERTICAL_RETRACE) != 0) {
            /* And until it ends, before the page goes out. */
        }
        memmove((void *) VGA_SCREEN_BASE, page, (size_t) VGA_SCREEN_BYTES);

        while (last_tick == data_fdps_timer_tick_counter) {
            /* Hold the frame until the timer interrupt moves the counter. */
        }
        last_tick = data_fdps_timer_tick_counter;

        free(page);
    }

    free(signboard_cel);
}

/* The three slots of a mode-9 blend descriptor, named as src/rleblend.c names
   them.  The address of the first is what reaches the kernel, so all three
   have to be one adjacent block in this order. */
#define BLEND_DESC_SHADE_RAMP 0
#define BLEND_DESC_LEVEL 1
#define BLEND_DESC_CUBE 2
#define BLEND_DESC_SLOTS 3

/* The game's direction codes, the ones fdps_animate_move_path dispatches on:
   0 down, 1 left, 2 up, 3 right.  MOV dword ptr [EBP-0x24] of 0x3, 0x1, 0x0
   and 0x2 at 00031ffc, 00032005, 00032014 and 0003201d. */
#define FACING_DOWN 0
#define FACING_LEFT 1
#define FACING_UP 2
#define FACING_RIGHT 3

/* The marker is drawn from cell 0 of its facing on every one of the six frames
   -- IMUL EDX,[EBP-0x24],0xc at 0003203e with no cell displacement added -- so
   it slides across without cycling its legs. */
#define WALK_CELL_STANDING 0

/* Six frames, i = 0..5, and the interpolation divisor is the LAST frame index
   rather than the count: CMP dword ptr [EBP-0x38],0x6 / JL at 0003202b and MOV
   EBX,0x5 / IDIV at 0003208e.  So the last frame lands the marker exactly on
   the destination. */
#define WALK_FRAMES 6
#define WALK_LAST_FRAME 5

/* The name plate's box size, 0x48 by 0x18, which is what the two centring
   terms subtract from at 0003215c and 00032145.  The plate is drawn at a
   multiple of a twelfth of that width by a quarter of its height, so it steps
   72x24, 60x20, 48x16 and back. */
#define PLATE_BOX_W 0x48
#define PLATE_BOX_H 0x18
#define PLATE_WIDTH_STEP 0xc
#define PLATE_HEIGHT_STEP 4

/* Which frame the plate being drawn changes over on.  The two tests are
   written against different constants -- CMP [EBP-0x38],0x3 / JGE at 00032139
   skips the outgoing plate and CMP [EBP-0x38],0x2 / JLE at 000321b2 skips the
   incoming one -- and they meet exactly here, so every frame draws exactly one
   plate and no frame draws both. */
#define PLATE_SWAP_FRAME 3

/* The two afterimages' blend levels, MOV dword ptr [EBP-0x48],0x8 at 00032209
   and 0xc at 0003228b.  A higher level is fainter (src/rleblend.h), so the
   older trail step is the dimmer one. */
#define GHOST_NEAR_LEVEL 8
#define GHOST_FAR_LEVEL 0xc

/* 00031f60.  Four stack arguments, caller-cleaned: all three call sites in
   fdps_village_signboard_menu push four dwords right to left and follow the
   CALL with ADD ESP,0x10, at 00031c7c, 00031ccd and 00031d1c, the body reads
   them at [EBP+0x14] through [EBP+0x20] behind PUSH EBX/ESI/EDI/EBP and the
   return address, and RET carries no immediate.  EAX is never set before the
   epilogue and no call site looks at it.

   The facing is the dominant axis of the whole journey, taken once before the
   loop and held for all six frames: abs(dx) against abs(dy) with CMP EBX,EAX /
   JLE at 00031ff2, so a tie goes to the vertical branch, and each branch then
   tests its own delta with JLE, so a delta of exactly zero reads as the
   negative direction.  Two destinations at the same point therefore face the
   marker up.

   THE TICK LATCH IS NEVER SEEDED.  The frame wait at 0003233b compares an
   uninitialised local against data_fdps_timer_tick_counter and only then
   latches it, so the first of the six frames ends its wait immediately unless
   the stack happens to hold the live tick.  Seeding it from the counter before
   the loop, which is the intuitive C, adds a tick to the first frame.

   THE SECOND AFTERIMAGE IS GUARDED AT BOTH ENDS.  CMP [EBP-0x38],0x1 / JLE
   with CMP [EBP-0x38],0x5 / JL at 0003227d is 1 < i && i < 5 and not i > 1, so
   the last frame deliberately carries a single ghost while the middle ones
   carry two.

   Every frame allocates its own composing page and frees it before the next
   one starts, so the animation holds one 64,000-byte block at a time and none
   at all across the return. */
void fdps_village_animate_walk_to_destination(unsigned char *background,
                                              int from_destination,
                                              int to_destination,
                                              unsigned char *signboard_cel)
{
    /* The descriptor the two afterimages are drawn through.  Only the level
       changes between them; the two table addresses are written once. */
    int blend_desc[BLEND_DESC_SLOTS];
    /* Where the marker starts and where it is walking to, in screen pixels. */
    int from_x;
    int from_y;
    int to_x;
    int to_y;
    /* The whole journey, read only to choose which way the marker faces. */
    int delta_x;
    int delta_y;
    /* That choice, one of the four direction codes. */
    int facing;
    /* Which of the six frames of the walk is being drawn. */
    int frame;
    /* Cell 0 of the chosen facing, out of the marker's own cache slot. */
    unsigned char *marker_stream;
    /* The 64,000-byte page this frame is composed in. */
    unsigned char *page;
    /* Where the marker's top left corner goes; reused for each afterimage's
       corner in turn, exactly as the original reuses the two slots. */
    int marker_x;
    int marker_y;
    /* The name plate's drawn size and the packed pair mode 4 reads it as. */
    int plate_width;
    int plate_height;
    int plate_size;
    /* The tick the previous frame ended on.  Deliberately not initialised --
       see the note above. */
    unsigned int last_tick;

    blend_desc[BLEND_DESC_SHADE_RAMP] =
        (int) data_fdps_palette_shade_ramp_table;
    blend_desc[BLEND_DESC_CUBE] = (int) data_fdps_inverse_palette_cube;

    from_x = data_fdps_village_signboard_destination_x_table[from_destination];
    from_y = data_fdps_village_destination_marker_y_table[from_destination];
    to_x = data_fdps_village_signboard_destination_x_table[to_destination];
    to_y = data_fdps_village_destination_marker_y_table[to_destination];
    delta_x = to_x - from_x;
    delta_y = to_y - from_y;
    if (abs(delta_x) > abs(delta_y)) {
        if (delta_x > 0) {
            facing = FACING_RIGHT;
        } else {
            facing = FACING_LEFT;
        }
    } else {
        if (delta_y > 0) {
            facing = FACING_DOWN;
        } else {
            facing = FACING_UP;
        }
    }

    for (frame = 0; frame < WALK_FRAMES; frame++) {
        /* A stored offset is measured from the base of the cache block, not
           from the slot it was read out of.  It is resolved afresh every frame
           because that is where the original resolves it. */
        marker_stream = data_fdps_cel_sprite_cache_ptr
            + ((struct fdps_cel_cache_slot *) data_fdps_cel_sprite_cache_ptr)
                  [data_fdps_village_marker_roster_idx].sprite_offset[
                      facing * WALK_CELLS_PER_FACING + WALK_CELL_STANDING];
        page = (unsigned char *) malloc((size_t) VGA_SCREEN_BYTES);
        memmove(page, background, (size_t) VGA_SCREEN_BYTES);

        marker_x = from_x - (from_x - to_x) * frame / WALK_LAST_FRAME;
        marker_y = from_y - (from_y - to_y) * frame / WALK_LAST_FRAME;
        fdps_blit_dispatch(marker_stream,
                           page + marker_y * VGA_SCREEN_PITCH + marker_x,
                           MARKER_W, MARKER_H, VGA_SCREEN_PITCH, 0,
                           BLIT_MODE_OPAQUE);
        fdps_cel_blit_sprite(signboard_cel, PLATE_FRAME_SPRITE, page,
                             VGA_SCREEN_PITCH, PLATE_BOX_X, PLATE_BOX_Y, 0,
                             BLIT_MODE_OPAQUE);

        /* The outgoing plate, full size on the first frame and two thirds of
           it by the third.  Both sizes are worked out whether or not the frame
           draws them, which is what the original does. */
        plate_height = (WALK_FRAMES - frame) * PLATE_HEIGHT_STEP;
        plate_width = (WALK_FRAMES - frame) * PLATE_WIDTH_STEP;
        plate_size = (plate_height << 16) + plate_width;
        if (frame < PLATE_SWAP_FRAME) {
            fdps_cel_blit_sprite(signboard_cel, from_destination, page,
                                 VGA_SCREEN_PITCH,
                                 PLATE_BOX_X + (PLATE_BOX_W - plate_width) / 2,
                                 PLATE_BOX_Y
                                     + (PLATE_BOX_H - plate_height) / 2,
                                 (unsigned int) plate_size,
                                 BLIT_MODE_SCALED);
        }

        /* The incoming plate, the same three sizes back the other way. */
        plate_height = (frame + 1) * PLATE_HEIGHT_STEP;
        plate_width = (frame + 1) * PLATE_WIDTH_STEP;
        plate_size = (plate_height << 16) + plate_width;
        if (frame >= PLATE_SWAP_FRAME) {
            fdps_cel_blit_sprite(signboard_cel, to_destination, page,
                                 VGA_SCREEN_PITCH,
                                 PLATE_BOX_X + (PLATE_BOX_W - plate_width) / 2,
                                 PLATE_BOX_Y
                                     + (PLATE_BOX_H - plate_height) / 2,
                                 (unsigned int) plate_size,
                                 BLIT_MODE_SCALED);
        }

        /* The trail: where the marker stood one frame ago, and where it stood
           two frames ago, both drawn translucent over the frame just
           composed. */
        if (frame > 0) {
            blend_desc[BLEND_DESC_LEVEL] = GHOST_NEAR_LEVEL;
            marker_x = from_x
                       - (from_x - to_x) * (frame - 1) / WALK_LAST_FRAME;
            marker_y = from_y
                       - (from_y - to_y) * (frame - 1) / WALK_LAST_FRAME;
            fdps_blit_dispatch(marker_stream,
                               page + marker_y * VGA_SCREEN_PITCH + marker_x,
                               MARKER_W, MARKER_H, VGA_SCREEN_PITCH,
                               (unsigned int) blend_desc,
                               BLIT_MODE_TRANSLUCENT);
        }
        if (frame > 1 && frame < WALK_LAST_FRAME) {
            blend_desc[BLEND_DESC_LEVEL] = GHOST_FAR_LEVEL;
            marker_x = from_x
                       - (from_x - to_x) * (frame - 2) / WALK_LAST_FRAME;
            marker_y = from_y
                       - (from_y - to_y) * (frame - 2) / WALK_LAST_FRAME;
            fdps_blit_dispatch(marker_stream,
                               page + marker_y * VGA_SCREEN_PITCH + marker_x,
                               MARKER_W, MARKER_H, VGA_SCREEN_PITCH,
                               (unsigned int) blend_desc,
                               BLIT_MODE_TRANSLUCENT);
        }

        while ((inp(VGA_INPUT_STATUS_1) & VGA_STATUS_VERTICAL_RETRACE) != 0) {
            /* Spin until the retrace that may already be running has ended. */
        }
        while ((inp(VGA_INPUT_STATUS_1) & VGA_STATUS_VERTICAL_RETRACE) == 0) {
            /* And until the next one begins, so the whole page reaches the
               adapter inside the blanking interval. */
        }
        memmove((void *) VGA_SCREEN_BASE, page, (size_t) VGA_SCREEN_BYTES);
        while (last_tick == data_fdps_timer_tick_counter) {
        }
        last_tick = data_fdps_timer_tick_counter;
        free(page);
    }
}

/* The gold readout's plate is sprites 0x34, 0x35 and 0x36 of the Command.cel
   sheet laid side by side: MOV EAX,[EBP-0x4] / ADD EAX,0x34 at 00032393 picks
   the sprite and IMUL EAX,dword ptr [EBP + -0x4],0x19 at 0003239e steps the
   destination by one sprite width, the same 0x19 fdps_blit_command_sprite
   publishes as its width (sprite.h).  Three of them -- CMP dword ptr [EBP +
   -0x4],0x3 / JL at 00032383, a signed compare over a signed counter. */
#define GOLD_CAPTION_FIRST_SPRITE 0x34
#define GOLD_CAPTION_SPRITES 3
#define GOLD_CAPTION_SPRITE_WIDTH 0x19

/* Where the figure sits relative to the caption's top-left corner, and how wide
   its field is: LEA EAX,[EAX*0x4 + 0x0] over the pitch at 000323c2 puts it four
   scanlines down, ADD EAX,0x13 at 000323cc nineteen pixels right, and PUSH 0x8
   at 000323b3 gives fdps_draw_number an eight-digit field.  Eight digits is a
   purse of up to 99,999,999; a larger one is drawn as eight '?' glyphs instead,
   which is fdps_draw_number's own overflow behaviour (text.h) and not something
   this function tests for. */
#define GOLD_FIGURE_ROWS_DOWN 4
#define GOLD_FIGURE_COLUMNS_RIGHT 0x13
#define GOLD_FIGURE_DIGITS 8

/* 00032370.  Draws the party's gold readout: the three-sprite plate first, then
   the purse as an eight-digit figure painted over it.  The figure's corner is
   inside the plate -- 4 rows down and 19 columns right of a 75 by 22 plate, and
   48 by 8 itself -- so the order the two are drawn in is what makes the digits
   visible.

   The purse is not an argument -- PUSH dword ptr [0x000643a4] at 000323b5 reads
   the shared global directly -- so every caller draws whatever the global holds
   at that moment, and a shop that has just charged the player redraws by
   calling here again.  It is signed and can be negative (gamedata.h), in which
   case the field's minus sign eats one of the eight digit positions.

   XOR EAX,EAX / PUSH EAX at 000323b0 is the show_plus argument: the figure
   never carries a leading '+'.

   `dst` is the readout's top-left corner in an 8bpp destination surface and
   `pitch` its bytes per row.  All seventeen call sites pass the literal 0xa8208
   and 0x140, i.e. column 8, row 104 of the mode-13h screen, but neither is
   hard-coded here and the routine draws wherever it is pointed. */
void fdps_draw_party_gold(unsigned char *dst, int pitch)
{
    int caption_index;

    for (caption_index = 0; caption_index < GOLD_CAPTION_SPRITES;
         caption_index++) {
        fdps_blit_command_sprite(dst + caption_index * GOLD_CAPTION_SPRITE_WIDTH,
                                 pitch,
                                 GOLD_CAPTION_FIRST_SPRITE + caption_index);
    }

    fdps_draw_number(dst + pitch * GOLD_FIGURE_ROWS_DOWN
                         + GOLD_FIGURE_COLUMNS_RIGHT,
                     pitch, data_fdps_shared_party_total_gold,
                     GOLD_FIGURE_DIGITS, 0);
}

/* The window sweep's two extremes.  The corner it collapses to is held in the
   original's frame -- MOV dword ptr [EBP + -0x8],0x8c and MOV dword ptr [EBP +
   -0x14],0x50 at 00032583 and 0003258a, both of them read back as the base of
   an interpolation and as its far end -- while the opened window's own corner
   is an immediate loaded into the arithmetic, MOV EDX,0x4 at 00032653 and MOV
   EDX,0x79 at 0003266f.  The opened size is the pair of IMUL immediates, 0x138
   at 0003260a and 0x4c at 00032629: 312 by 76 at column 4, row 121, which is
   the shop and church window laid across the bottom of the screen. */
#define ZOOM_COLLAPSED_X 0x8c
#define ZOOM_COLLAPSED_Y 0x50
#define ZOOM_OPEN_X 4
#define ZOOM_OPEN_Y 0x79
#define ZOOM_OPEN_W 0x138
#define ZOOM_OPEN_H 0x4c

/* What a step that works out to nothing is drawn as instead, MOV dword ptr
   [EBP + -0x18],0x4 at 00032622 and MOV dword ptr [EBP + -0x1c],0x2 at
   0003263e.  Only the fully collapsed step reaches either: with a divisor of 8
   the next step up is already 39 by 9. */
#define ZOOM_MIN_W 4
#define ZOOM_MIN_H 2

/* How many steps the sweep is cut into.  The counter runs 0..8 INCLUSIVE --
   CMP EAX,dword ptr [EBP + -0x20] / JLE at 000325cc -- so nine frames are
   drawn and the last one lands exactly on the far extreme. */
#define ZOOM_STEPS 8

/* The window frame is sprite 0 of the sheet, PUSH 0x0 at 000326a2. */
#define ZOOM_WINDOW_SPRITE 0

/* 00032570.  Plays the shop and church window frame opening out of, or closing
   back into, a sliver in the middle of the screen: nine frames, one per
   vertical retrace, each composed on a private page over a fresh copy of the
   caller's screen.  See village.h for what a caller has to have in place.

   Two stack arguments, caller-cleaned.  All seventy-odd call sites push two
   dwords and follow the CALL with ADD ESP,0x8 -- 00033b9d, 00033bb6, 00035d5e
   and 00033fa0 are typical -- the body reads them at [EBP+0x14] and [EBP+0x18]
   behind PUSH EBX/ESI/EDI/EBP and the return address, and RET carries no
   immediate.  EAX is never set before the epilogue and no call site looks at
   it.

   THE PORTRAIT BUFFER IS RELEASED HERE AND THAT IS NOT INCIDENTAL.  CMP dword
   ptr [0x00060120],0x0 / free / MOV dword ptr [0x00060120],0x0 at 00032591 is
   the same shape fdps_load_and_draw_portrait and fdps_message_window_wait_key
   use, and the readers of that pointer draw the portrait whenever it is
   non-null (gamedata.h).  Writing this as a pure window animation and dropping
   the release leaks the block and leaves the previous speaker's portrait drawn
   over the town prompts.

   THE STEP COUNT, THE COLLAPSED CORNER AND THE SWEEP ARE SIGNED THROUGHOUT.
   All four interpolations are CDQ/IDIV over the same [EBP-0x20] divisor and
   the loop's own test is JLE, so nothing here is an unsigned division that a
   shift could stand in for.

   THE MINIMUM SIZE IS A SUBSTITUTION AND NOT A CLAMP.  Each of the two sizes
   is computed first and only then tested against zero, so a step whose size
   comes out zero is drawn as 4 by 2 at whatever corner that step's own
   interpolation gave it -- which on a close is the collapsed corner itself.

   THE CALLER'S PAGE IS NEVER WRITTEN.  Every frame copies it over the private
   page and draws into that, so a caller can call again for the closing half
   and get the same screen back underneath.

   The frames are paced by the retrace, and the page copy deliberately runs
   AFTER the retrace has ended rather than inside it: the first spin waits for
   it to begin and the second for it to end, the same way round as
   fdps_village_signboard_menu's own present.  How many instructions stand
   between the two spins is therefore not observable (contract D).

   malloc's answer is not tested, the same as the original. */
void fdps_village_animate_window_zoom(unsigned char *screen_page,
                                      unsigned char closing)
{
    /* The sweep's divisor and the corner the window collapses to.  All three
       are locals in the original's frame and not immediates folded into the
       arithmetic: the divisor is the memory operand of all four IDIVs and the
       two coordinates are each read twice, once as the far end of their own
       interpolation and once as its base. */
    int zoom_steps = ZOOM_STEPS;
    int collapsed_x = ZOOM_COLLAPSED_X;
    int collapsed_y = ZOOM_COLLAPSED_Y;
    /* The 64,000-byte page the whole animation is composed on, taken once and
       released on the way out -- unlike the two other village animations,
       which take one per frame. */
    unsigned char *compose_page;
    /* Which of the nine frames is being drawn, 0..zoom_steps. */
    int step;
    /* How far open this frame's window is, 0 for the sliver and zoom_steps for
       the full window.  It counts up with the frames on an open and down on a
       close, which is the whole of what `closing` does. */
    int open_amount;
    /* That amount turned into the window's drawn size, its top-left corner,
       and the size packed the way mode 4 reads it. */
    int window_width;
    int window_height;
    int window_x;
    int window_y;
    int window_size;

    if (data_fdps_portrait_sprite_buf_ptr != NULL) {
        free(data_fdps_portrait_sprite_buf_ptr);
    }
    data_fdps_portrait_sprite_buf_ptr = NULL;

    compose_page = (unsigned char *) malloc((size_t) VGA_SCREEN_BYTES);

    for (step = 0; step <= zoom_steps; step++) {
        if (closing != 0) {
            open_amount = zoom_steps - step;
        } else {
            open_amount = step;
        }
        memmove(compose_page, screen_page, (size_t) VGA_SCREEN_BYTES);

        window_width = open_amount * ZOOM_OPEN_W / zoom_steps;
        if (window_width == 0) {
            window_width = ZOOM_MIN_W;
        }
        window_height = open_amount * ZOOM_OPEN_H / zoom_steps;
        if (window_height == 0) {
            window_height = ZOOM_MIN_H;
        }
        window_size = (window_height << 16) + window_width;
        window_x = collapsed_x
                   + (ZOOM_OPEN_X - collapsed_x) * open_amount / zoom_steps;
        window_y = collapsed_y
                   + (ZOOM_OPEN_Y - collapsed_y) * open_amount / zoom_steps;
        fdps_cel_blit_sprite(data_fdps_village_window_sheet_ptr,
                             ZOOM_WINDOW_SPRITE, compose_page,
                             VGA_SCREEN_PITCH, window_x, window_y,
                             (unsigned int) window_size, BLIT_MODE_SCALED);

        while ((inp(VGA_INPUT_STATUS_1) & VGA_STATUS_VERTICAL_RETRACE) == 0) {
            /* Spin until the retrace begins. */
        }
        while ((inp(VGA_INPUT_STATUS_1) & VGA_STATUS_VERTICAL_RETRACE) != 0) {
            /* And until it ends, before the page goes out. */
        }
        memmove((void *) VGA_SCREEN_BASE, compose_page,
                (size_t) VGA_SCREEN_BYTES);
    }

    free(compose_page);
}

/* 000357a0.  One keystroke of the chapter's secret-shop unlock code.

   The table is a 24-row, eight-byte-per-row block of keyboard make codes that
   the original copies into a stack buffer on every call -- MOV ECX,0x30 /
   LEA EDI,[EBP-0xc4] / MOV ESI,0x310c0 / REP MOVSD -- so it is an ordinary
   automatic array with an initializer here and NOT a static const table.  The
   difference is visible: with chapter id 0 the row selector below reads the
   eight bytes underneath the buffer, which are whatever the previous frame
   left on the stack, whereas a static table would read the eight zero bytes
   that happen to sit in front of the block at 0x000310b8.

   The row selector is off by one.  The effective address the original builds
   is [EAX*8 + EBP - 0xcc] with EAX the chapter id, and the buffer starts at
   EBP - 0xc4, so the base of the indexing is the buffer MINUS one row: the row
   used is chapter_codes[chapter_id - 1].  The chapter id is 0-based, so row 0
   belongs to chapter id 1, the chapter the player sees as chapter 2.  The
   strategy guide's dump of this same table agrees independently -- "each
   chapter 8 bytes, from chapter 2 through chapter 25, beginning 4D 50 4D 1E"
   (docs/guide, fdps/modify2) -- and 4D 50 4D 1E is exactly row 0.  Writing the
   obvious chapter_codes[chapter_id] would give every chapter the next one's
   code.

   The byte read is compared as an unsigned value against the parameter: MOV AL
   / AND EAX,0xff / CMP EAX,[EBP+0x14], so the table's codes above 0x7f -- none
   in the shipped block, but the compare is what decides -- would still match a
   positive scancode rather than arriving negative.

   Four rows are all zero (rows 15, 16, 20 and 21, the chapters the player sees
   as 17, 18, 22 and 23), and the caller never passes scancode 0, so in those
   chapters the first compare always fails and the match position can never
   leave 0: those chapters have no code at all.

   The table does not cover every chapter.  The game has thirty (chapters/),
   so the chapter id runs 0..29, while the table's rows answer for ids 1..24
   only.  The original does not check that, and the six ids outside the table
   read the frame itself rather than the buffer, at EBP - 0xcc + id * 8: id 0
   lands eight bytes BELOW the buffer, on whatever the previous frame left
   there, and ids 25..29 land above it -- on the return-value slot, then the
   saved EBP, EDI, ESI and EBX, then the return address, and at id 28 on the
   incoming scancode itself.  That last one matches whatever key was pressed
   and has a zero byte behind it, so it reports the code complete on the first
   keystroke.  None of that is emulated here or special-cased: the array is
   indexed exactly as the original indexes it and lands on the same slots of
   the same frame, which is the only way the out-of-range chapters behave the
   same way at all.  A bounds check, a clamp, or a static const table would
   each change what those six chapters do.

   The row is scanned by the two branches below and never by a loop: a match
   advances the position and reports completion when the NEXT byte is the row's
   0 terminator or the position has reached 8, and a mismatch throws the
   attempt away but immediately re-opens it if the offending keystroke happens
   to be the code's own first byte.  The position-reached-8 half is unreachable
   with the shipped table, whose longest code is seven keys followed by a
   terminator, but it is in the original and the compare order is the
   original's: the terminator is tested first. */
int fdps_check_secret_code_key(int scancode)
{
    /* Row n is the code for chapter id n + 1, the player's chapter n + 2. */
    unsigned char chapter_codes[24][8] = {
        { 0x4d, 0x50, 0x4d, 0x1e },                          /* right down right A */
        { 0x4d, 0x4b, 0x50, 0x4d, 0x1e },                    /* right left down right A */
        { 0x48, 0x4b, 0x50, 0x4d, 0x48 },                    /* up left down right up */
        { 0x1f, 0x18, 0x1e, 0x13 },                          /* S O A R */
        { 0x1f, 0x12, 0x2e, 0x13, 0x12, 0x14 },              /* S E C R E T */
        { 0x02, 0x03, 0x03, 0x02, 0x02 },                    /* 1 2 2 1 1 */
        { 0x34, 0x34, 0x34, 0x34 },                          /* . . . . */
        { 0x30, 0x1e, 0x13, 0x31, 0x1e, 0x20, 0x18 },        /* B A R N A D O */
        { 0x12, 0x31, 0x14, 0x12, 0x13 },                    /* E N T E R */
        { 0x48, 0x4b, 0x50, 0x4d },                          /* up left down right */
        { 0x4b, 0x50, 0x4d },                                /* left down right */
        { 0x03, 0x0b, 0x0c, 0x05, 0x0b },                    /* 2 0 - 4 0 */
        { 0x31, 0x17, 0x22, 0x23, 0x14 },                    /* N I G H T */
        { 0x02, 0x06 },                                      /* 1 5 */
        { 0x26, 0x18, 0x14, 0x18, 0x13, 0x17, 0x1e },        /* L O T O R I A */
        { 0 },
        { 0 },
        { 0x02, 0x03 },                                      /* 1 2 */
        { 0x02, 0x04, 0x03, 0x05 },                          /* 1 3 2 4 */
        { 0x23, 0x12, 0x13, 0x18 },                          /* H E R O */
        { 0 },
        { 0 },
        { 0x50, 0x50, 0x50, 0x50 },                          /* down down down down */
        { 0x11, 0x12, 0x1f, 0x14 }                           /* W E S T */
    };

    if (chapter_codes[data_fdps_chapter_current_chapter_id - 1]
                     [data_fdps_secret_code_match_pos] == scancode) {
        data_fdps_secret_code_match_pos++;
        if (chapter_codes[data_fdps_chapter_current_chapter_id - 1]
                         [data_fdps_secret_code_match_pos] == 0 ||
            data_fdps_secret_code_match_pos == 8) {
            return 1;
        }
    } else {
        data_fdps_secret_code_match_pos = 0;
        /* The original reads the row's byte 0 as a literal index here and does
           not re-read the position it has just zeroed: MOV EAX,[0x69cf4] /
           LEA EAX,[EAX*8] with no ADD of [0x601c0]. */
        if (chapter_codes[data_fdps_chapter_current_chapter_id - 1][0] == scancode) {
            data_fdps_secret_code_match_pos = 1;
        }
    }
    return 0;
}
