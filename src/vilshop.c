/* vilshop.c -- the village's church, bar, weapon shop, secret menu and
 * lottery screens.
 *
 * See vilshop.h for what each screen is and what it leaves behind.  The window
 * frame, the command sprite sheet, the number sheet, the roster array and the
 * two message tables are all globals the chapter loader filled (gamedata.h)
 * and none of them is owned here.  What each screen does own is its own
 * 320x200 backdrop page: it is taken from the heap on the way in, published in
 * data_fdps_village_backdrop_page_ptr for the window and status code, and
 * freed on the way out.
 */
#include <stdlib.h>
#include "gamedata.h"
#include "church.h"
#include "menu.h"
#include "sprite.h"
#include "text.h"
#include "transit.h"
#include "vfs.h"
#include "village.h"
#include "vilmenu.h"
#include "vilshop.h"

/* The mode 13h aperture and its row stride, PUSH 0xa8208 at 00035b46 and
   00035c83, PUSH 0xaa3d4 at 00035b61 and 00035c66, PUSH 0x140 beside each of
   them and beside the backdrop blit at 00035af6.  0xa0000 is written as a
   literal because it is where the display adapter answers and not the address
   of anything the linker places, and the two offsets on it are literals for
   the same reason -- they are positions on the adapter. */
#define VGA_SCREEN_BASE 0x000a0000
#define VGA_SCREEN_PITCH 0x140

/* The whole mode 13h frame, PUSH 0xfa00 as malloc's count at 00035ad6.  A
   320x200 sheet blitted at pitch 0x140 fills it exactly. */
#define VGA_SCREEN_BYTES 0xfa00

/* ------------------------------------------------------------------
 * fdps_run_church_screen @ 00035aa0
 * ------------------------------------------------------------------ */

/* The backdrop and where it is taken from, PUSH 0x61fb0 and PUSH 0x60128 at
   00035abf.  Both are plain writable literals and have to stay that way:
   fdps_vfs_load_entry upper-cases the caller's own storage in place (vfs.h),
   so the original's two strings read "MISC.VFS" and "CHURCH.CEL" from the
   first call onward and a copy placed in read-only storage would fault instead
   (rebuild_info/pitfalls.md). */
#define CHURCH_SCREEN_ARCHIVE "MISC.VFS"
#define CHURCH_SCREEN_BACKDROP "Church.cel"

/* The backdrop is sprite 0 of that sheet, drawn at the page's origin through
   the opaque pass-through kernel: PUSH 0x0 five times and PUSH 0x140 for the
   pitch at 00035aee. */
#define CHURCH_BACKDROP_SPRITE 0
#define CHURCH_BACKDROP_X 0
#define CHURCH_BACKDROP_Y 0
#define CHURCH_BACKDROP_BLIT_OPERAND 0
#define CHURCH_BACKDROP_BLIT_MODE 0

/* The point the opening and closing transitions are magnified about, PUSH 0x9f
   and PUSH 0x63 at 00035b1f and 00035c98: the screen centre, so this screen
   gets a straight pull-back and not the swing the signboard menu gets
   (transit.h). */
#define CHURCH_TRANSITION_CENTER_X 0x9f
#define CHURCH_TRANSITION_CENTER_Y 0x63

/* fdps_transition_zoom's direction byte, which is read as "non-zero pulls the
   picture back" -- MOV EAX,0x1 at 00035b19 on the way in, XOR EAX,EAX at
   00035c95 on the way out.  The closing one leaves the aperture cleared. */
#define TRANSITION_ZOOM_OUT 1
#define TRANSITION_ZOOM_IN 0

/* fdps_village_animate_window_zoom's second argument, which is only tested
   against zero (village.h): XOR EAX,EAX at 00035b32, 00035bd3 and 00035c49
   sweeps the frame open, MOV EAX,0x1 at 00035bc1 and 00035c35 sweeps it
   shut. */
#define WINDOW_ZOOM_OPEN 0
#define WINDOW_ZOOM_CLOSE 1

/* Where this screen's two messages are written: 0xaa3d4, column 20 and row 131
   of the mode 13h screen, inside the window frame the zoom has just opened;
   and where the gold readout goes: 0xa8208, column 8 and row 104.  The colour
   trio is the standard one, PUSH 0xd0 / PUSH 0x0 / PUSH 0x6d in front of each
   message. */
#define CHURCH_MESSAGE_SCREEN_AT 0xa3d4
#define CHURCH_GOLD_READOUT_SCREEN_AT 0x8208
#define CHURCH_TEXT_FG_COLOR 0xd0
#define CHURCH_TEXT_BG_COLOR 0
#define CHURCH_TEXT_OUTLINE_COLOR 0x6d

/* The two entries of the resident text block this screen draws, PUSH 0x200 at
   00035b66 and PUSH 0x201 at 00035c6b.  The first goes up once when the screen
   opens; the second replaces it after every command that was not the cancel. */
#define CHURCH_OPENING_TEXT_ID 0x200
#define CHURCH_AFTER_COMMAND_TEXT_ID 0x201

/* And the entry of the LOADED CHAPTER's own text block that the first command
   reprints, PUSH 0x7 at 00035bf5 in front of data_fdps_current_chapter_text_ptr
   rather than the resident table.  The five village screens take one entry each
   out of that block and they are consecutive: 4 in fdps_village_item_menu
   (vilmenu.c), 5 in fdps_run_weapon_shop, 6 in fdps_run_bar_shop, 7 here and 8
   in fdps_run_secret_menu. */
#define CHURCH_SCREEN_CHAPTER_TEXT_ID 7

/* The row of commands: how many entries it has, PUSH 0x4 at 00035b98, and the
   Command.cel sub-image id of each, which is the four-dword template the
   original copies onto the frame with four MOVSD at 00035abb -- an initialised
   automatic array, not a global, and with no other reader.  The order is the
   order the row draws them, left to right. */
#define CHURCH_COMMAND_COUNT 4
#define CHURCH_ICON_TALK 0x24
#define CHURCH_ICON_PROMOTE 0x12
#define CHURCH_ICON_TRANSFER 0x04
#define CHURCH_ICON_STATUS 0x10

/* The answer the row hands back, which is an index into that table.  The four
   arms are the four entries of the CS-relative jump table at 00035b84 --
   00035bc1, 00035c07, 00035c15 and 00035c23 -- in this order. */
#define CHURCH_COMMAND_TALK 0
#define CHURCH_COMMAND_PROMOTE 1
#define CHURCH_COMMAND_TRANSFER 2
#define CHURCH_COMMAND_STATUS 3

/* And what the row writes into it when the player backs out, which is the
   loop's only exit (menu.h). */
#define CHURCH_COMMAND_CANCELLED (-1)

/* 00035aa0.  No arguments and no answer: the one call site, in
   fdps_run_village_phase at 00031210, pushes nothing and follows the CALL with
   a stack cleanup of its own, nothing above EBP is read, EAX is not set before
   the epilogue and the RET carries no immediate.

   The control flow is one loop with a four-way switch and one two-armed test
   in it.  The entry test CMP dword ptr [EBP-0x8],-0x1 / JNZ at 00035b79 is the
   top of the loop and the JMP at 00035c90 is its back edge, so this is a while
   whose condition is checked before the first pass -- which is why the answer
   slot is seeded with zero at 00035aac rather than with a command: zero is
   simply a value that is not -1.  The switch is CMP dword ptr [EBP-0x8],0x3 /
   JA at 00035ba6 in front of JMP dword ptr CS:[EAX*4 + 0x35b84], and the bound
   is UNSIGNED, which is how the cancel's -1 falls past all four arms and lands
   on the same address a taken arm falls to.  Every arm reaches the two-armed
   test at 00035c2f and that test's arms both reach the back edge, so backing
   out of the icon row is the only way to the epilogue.

   THE TWO OPENING DRAWS ARE IN THE OTHER ORDER FROM THE TWO CLOSING ONES.  On
   the way in the gold readout goes down first and the message second (00035b41
   then 00035b53); after a command it is the message first and the readout
   second (00035c58 then 00035c7e).  They do not overlap -- row 104 against row
   131 -- so the order is not load-bearing, but it is what the assembly does.

   THE TALK ARM CLOSES THE WINDOW BEFORE IT REOPENS IT.  MOV EAX,0x1 at
   00035bc1 then XOR EAX,EAX at 00035bd3: the frame is swept shut and open
   again, which is what clears the icon row's own painting off the frame before
   the line is written into it.  The arm then falls into the same test every
   other arm does, so its answer is still 0 and the window is opened a second
   time and the resident message drawn over the chapter line it just put up.

   The one value used after a CALL is fdps_vfs_load_entry's EAX, stored to
   [EBP-0x4] at 00035ad3 and handed to the blit and then to free.  malloc's EAX
   is stored to [EBP-0xc] at 00035ae3 and copied to the global from there --
   MOV EAX,[EBP-0xc] / MOV [0x00063fb4],EAX at 00035ae6, so every later use is
   the local's value and not a second read of the global.  Nothing else is
   read: fdps_menu_command_icon_select_loop's answer comes back through the
   pointer and its EAX is dropped (ADD ESP,0xc at 00035ba3 with no use of it),
   and the blit, the two transitions, the window sweeps, the readout,
   fdps_draw_text and the three submenus are each followed straight by their
   stack cleanup.

   NOTHING IS CHECKED.  The load is not tested -- a container or a member that
   cannot be found ends the process inside fdps_vfs_load_entry (vfs.h) -- and
   neither is malloc's answer, which is written to the global and then blitted
   into. */
void fdps_run_church_screen(void)
{
    /* The unpacked "Church.cel" sheet, alive only long enough to be drawn into
       the page. */
    unsigned char *backdrop_cel;
    /* Which command the icon row last came back on, and the screen's only
       exit: -1 is the row's cancel and nothing else stops this.  It is kept
       across passes, so the row reopens on the command it was last used
       from. */
    int selected_command = 0;
    /* This screen's own 320x200 page, published as the village backdrop for as
       long as the screen is up. */
    unsigned char *screen_page;
    /* The row's four Command.cel sub-image ids, left to right. */
    int command_icon_ids[CHURCH_COMMAND_COUNT] = {
        CHURCH_ICON_TALK, CHURCH_ICON_PROMOTE,
        CHURCH_ICON_TRANSFER, CHURCH_ICON_STATUS
    };

    backdrop_cel = (unsigned char *)
        fdps_vfs_load_entry(CHURCH_SCREEN_ARCHIVE, CHURCH_SCREEN_BACKDROP);
    screen_page = (unsigned char *) malloc((size_t) VGA_SCREEN_BYTES);
    data_fdps_village_backdrop_page_ptr = screen_page;
    fdps_cel_blit_sprite(backdrop_cel, CHURCH_BACKDROP_SPRITE, screen_page,
                         VGA_SCREEN_PITCH, CHURCH_BACKDROP_X,
                         CHURCH_BACKDROP_Y, CHURCH_BACKDROP_BLIT_OPERAND,
                         CHURCH_BACKDROP_BLIT_MODE);
    free(backdrop_cel);

    fdps_transition_zoom(screen_page, CHURCH_TRANSITION_CENTER_X,
                         CHURCH_TRANSITION_CENTER_Y, TRANSITION_ZOOM_OUT);
    fdps_village_animate_window_zoom(screen_page, WINDOW_ZOOM_OPEN);
    fdps_draw_party_gold((unsigned char *) (VGA_SCREEN_BASE
                                            + CHURCH_GOLD_READOUT_SCREEN_AT),
                         VGA_SCREEN_PITCH);
    fdps_draw_text(data_fdps_all_game_text_ptr, CHURCH_OPENING_TEXT_ID,
                   (unsigned char *) (VGA_SCREEN_BASE
                                      + CHURCH_MESSAGE_SCREEN_AT),
                   VGA_SCREEN_PITCH, CHURCH_TEXT_FG_COLOR,
                   CHURCH_TEXT_BG_COLOR, CHURCH_TEXT_OUTLINE_COLOR);

    while (selected_command != CHURCH_COMMAND_CANCELLED) {
        fdps_menu_command_icon_select_loop(command_icon_ids,
                                           CHURCH_COMMAND_COUNT,
                                           &selected_command);

        switch (selected_command) {
        case CHURCH_COMMAND_TALK:
            fdps_village_animate_window_zoom(screen_page, WINDOW_ZOOM_CLOSE);
            fdps_village_animate_window_zoom(screen_page, WINDOW_ZOOM_OPEN);
            fdps_draw_text(data_fdps_current_chapter_text_ptr,
                           CHURCH_SCREEN_CHAPTER_TEXT_ID,
                           (unsigned char *) (VGA_SCREEN_BASE
                                              + CHURCH_MESSAGE_SCREEN_AT),
                           VGA_SCREEN_PITCH, CHURCH_TEXT_FG_COLOR,
                           CHURCH_TEXT_BG_COLOR, CHURCH_TEXT_OUTLINE_COLOR);
            break;
        case CHURCH_COMMAND_PROMOTE:
            fdps_church_promote_loop(screen_page);
            break;
        case CHURCH_COMMAND_TRANSFER:
            fdps_village_item_transfer_loop(screen_page);
            break;
        case CHURCH_COMMAND_STATUS:
            fdps_village_member_status_loop(screen_page);
            break;
        }

        if (selected_command == CHURCH_COMMAND_CANCELLED) {
            fdps_village_animate_window_zoom(screen_page, WINDOW_ZOOM_CLOSE);
        } else {
            fdps_village_animate_window_zoom(screen_page, WINDOW_ZOOM_OPEN);
            fdps_draw_text(data_fdps_all_game_text_ptr,
                           CHURCH_AFTER_COMMAND_TEXT_ID,
                           (unsigned char *) (VGA_SCREEN_BASE
                                              + CHURCH_MESSAGE_SCREEN_AT),
                           VGA_SCREEN_PITCH, CHURCH_TEXT_FG_COLOR,
                           CHURCH_TEXT_BG_COLOR, CHURCH_TEXT_OUTLINE_COLOR);
            fdps_draw_party_gold((unsigned char *)
                                     (VGA_SCREEN_BASE
                                      + CHURCH_GOLD_READOUT_SCREEN_AT),
                                 VGA_SCREEN_PITCH);
        }
    }

    fdps_transition_zoom(screen_page, CHURCH_TRANSITION_CENTER_X,
                         CHURCH_TRANSITION_CENTER_Y, TRANSITION_ZOOM_IN);
    free(screen_page);
}
