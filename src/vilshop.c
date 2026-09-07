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
#include <conio.h>
#include <dos.h>
#include <stdlib.h>
#include <string.h>
#include "gamedata.h"
#include "audio.h"
#include "blit.h"
#include "church.h"
#include "keybd.h"
#include "menu.h"
#include "roster.h"
#include "saf.h"
#include "shop.h"
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

/* ------------------------------------------------------------------
 * fdps_run_weapon_shop @ 00035fd0
 * ------------------------------------------------------------------ */

/* The backdrop and where it is taken from, PUSH 0x61fc8 and PUSH 0x60128 at
   00035ff2.  Both are plain writable literals and have to stay that way:
   fdps_vfs_load_entry upper-cases the caller's own storage in place (vfs.h),
   so the original's two strings read "MISC.VFS" and "WEAPON.CEL" from the
   first call onward and a copy placed in read-only storage would fault instead
   (rebuild_info/pitfalls.md). */
#define WEAPON_SHOP_ARCHIVE "MISC.VFS"
#define WEAPON_SHOP_BACKDROP "Weapon.cel"

/* The backdrop is sprite 0 of that sheet, drawn at the page's origin through
   the opaque pass-through kernel: PUSH 0x0 five times and PUSH 0x140 for the
   pitch at 00036021. */
#define WEAPON_BACKDROP_SPRITE 0
#define WEAPON_BACKDROP_X 0
#define WEAPON_BACKDROP_Y 0
#define WEAPON_BACKDROP_BLIT_OPERAND 0
#define WEAPON_BACKDROP_BLIT_MODE 0

/* The point the opening and closing transitions are magnified about, PUSH 0x9f
   and PUSH 0x63 at 00036052 and 000361e4: the screen centre, so this screen
   gets a straight pull-back and not the swing the signboard menu gets
   (transit.h). */
#define WEAPON_TRANSITION_CENTER_X 0x9f
#define WEAPON_TRANSITION_CENTER_Y 0x63

/* Where this screen's two messages are written: 0xaa3d4, column 20 and row 131
   of the mode 13h screen, inside the window frame the zoom has just opened;
   and where the gold readout goes: 0xa8208, column 8 and row 104.  The colour
   trio is the standard one, PUSH 0xd0 / PUSH 0x0 / PUSH 0x6d in front of each
   message. */
#define WEAPON_MESSAGE_SCREEN_AT 0xa3d4
#define WEAPON_GOLD_READOUT_SCREEN_AT 0x8208
#define WEAPON_TEXT_FG_COLOR 0xd0
#define WEAPON_TEXT_BG_COLOR 0
#define WEAPON_TEXT_OUTLINE_COLOR 0x6d

/* The two entries of the resident text block this screen draws, PUSH 0x1fe at
   00036099 and PUSH 0x1ff at 000361b7.  The first goes up once when the screen
   opens; the second replaces it after every command that was not the cancel. */
#define WEAPON_OPENING_TEXT_ID 0x1fe
#define WEAPON_AFTER_COMMAND_TEXT_ID 0x1ff

/* And the entry of the LOADED CHAPTER's own text block that the first command
   reprints, PUSH 0x5 at 0003612d in front of data_fdps_current_chapter_text_ptr
   rather than the resident table.  The five village screens take one entry each
   out of that block and they are consecutive: 4 in fdps_village_item_menu
   (vilmenu.c), 5 here, 6 in fdps_run_bar_shop, 7 in fdps_run_church_screen and
   8 in fdps_run_secret_menu. */
#define WEAPON_SHOP_CHAPTER_TEXT_ID 5

/* The row of commands: how many entries it has, PUSH 0x5 at 000360d0, and the
   Command.cel sub-image id of each, which is the five-dword template the
   original copies onto the frame with REP MOVSD at 00035ff0 -- an initialised
   automatic array, not a global, and with no other reader.  The order is the
   order the row draws them, left to right. */
#define WEAPON_COMMAND_COUNT 5
#define WEAPON_ICON_TALK 0x24
#define WEAPON_ICON_BUY 0x14
#define WEAPON_ICON_SELL 0x15
#define WEAPON_ICON_TRANSFER 0x04
#define WEAPON_ICON_EQUIP 0x06

/* The answer the row hands back, which is an index into that table.  The five
   arms are the five entries of the CS-relative jump table at 000360b8 --
   000360f9, 0003613f, 00036153, 00036161 and 0003616f -- in this order. */
#define WEAPON_COMMAND_TALK 0
#define WEAPON_COMMAND_BUY 1
#define WEAPON_COMMAND_SELL 2
#define WEAPON_COMMAND_TRANSFER 3
#define WEAPON_COMMAND_EQUIP 4

/* And what the row writes into it when the player backs out, which is the
   loop's only exit (menu.h). */
#define WEAPON_COMMAND_CANCELLED (-1)

/* Which shop table the buy counter is opened against, MOV EAX,0x1 / PUSH EAX
   at 0003613f.  It is a byte argument and the weapon shop is shop 1, so the
   stock offered is row 1 of SHOP%02d.DAT and not the item screen's row 0
   (shop.h). */
#define WEAPON_SHOP_INDEX 1

/* 00035fd0.  No arguments and no answer: the one call site, in
   fdps_run_village_phase at 0003147a, pushes nothing and follows the CALL with
   a JMP rather than a stack adjustment, nothing above EBP is read, EAX is not
   set before the epilogue and the RET carries no immediate.

   The control flow is one loop with a five-way switch and one two-armed test in
   it.  The entry test CMP dword ptr [EBP-0x8],-0x1 / JNZ at 000360ac is the top
   of the loop and the JMP at 000361dc is its back edge, so this is a while
   whose condition is checked before the first pass -- which is why the answer
   slot is seeded with zero at 00035fdc rather than with a command: zero is
   simply a value that is not -1.  The switch is CMP dword ptr [EBP-0x8],0x4 /
   JA at 000360de in front of JMP dword ptr CS:[EAX*4 + 0x360b8], and the bound
   is UNSIGNED, which is how the cancel's -1 falls past all five arms and lands
   on the same address a taken arm falls to.  Every arm reaches the two-armed
   test at 0003617b and that test's arms both reach the back edge, so backing
   out of the icon row is the only way to the epilogue.

   THE TWO OPENING DRAWS ARE IN THE OTHER ORDER FROM THE TWO CLOSING ONES.  On
   the way in the gold readout goes down first and the message second (0003607e
   then 000360a4); after a command it is the message first and the readout
   second (000361c2 then 000361d4).  They do not overlap -- row 104 against row
   131 -- so the order is not load-bearing, but it is what the assembly does.

   THE REPRINT ARM CLOSES THE WINDOW BEFORE IT REOPENS IT.  MOV EAX,0x1 at
   000360f9 then XOR EAX,EAX at 0003610b: the frame is swept shut and open
   again, which is what clears the icon row's own painting off the frame before
   the line is written into it.  The arm then falls into the same test every
   other arm does, so its answer is still 0 and the window is opened a second
   time and the resident message drawn over the chapter line it just put up.

   The one value used after a CALL is fdps_vfs_load_entry's EAX, stored to
   [EBP-0x4] at 00036006 and handed to the blit and then to free.  malloc's EAX
   is stored to [EBP-0xc] at 00036016 and copied to the global from there --
   MOV EAX,[EBP-0xc] / MOV [0x00063fb4],EAX at 00036019, so every later use is
   the local's value and not a second read of the global.  Nothing else is
   read: fdps_menu_command_icon_select_loop's answer comes back through the
   pointer and its EAX is dropped (ADD ESP,0xc at 000360db with no use of it),
   and the blit, the two transitions, the window sweeps, the readout,
   fdps_draw_text and the four submenus are each followed straight by their
   stack cleanup.

   NOTHING IS CHECKED.  The load is not tested -- a container or a member that
   cannot be found ends the process inside fdps_vfs_load_entry (vfs.h) -- and
   neither is malloc's answer, which is written to the global and then blitted
   into. */
void fdps_run_weapon_shop(void)
{
    /* The unpacked "Weapon.cel" sheet, alive only long enough to be drawn into
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
    /* The row's five Command.cel sub-image ids, left to right. */
    int command_icon_ids[WEAPON_COMMAND_COUNT] = {
        WEAPON_ICON_TALK, WEAPON_ICON_BUY, WEAPON_ICON_SELL,
        WEAPON_ICON_TRANSFER, WEAPON_ICON_EQUIP
    };

    backdrop_cel = (unsigned char *)
        fdps_vfs_load_entry(WEAPON_SHOP_ARCHIVE, WEAPON_SHOP_BACKDROP);
    screen_page = (unsigned char *) malloc((size_t) VGA_SCREEN_BYTES);
    data_fdps_village_backdrop_page_ptr = screen_page;
    fdps_cel_blit_sprite(backdrop_cel, WEAPON_BACKDROP_SPRITE, screen_page,
                         VGA_SCREEN_PITCH, WEAPON_BACKDROP_X,
                         WEAPON_BACKDROP_Y, WEAPON_BACKDROP_BLIT_OPERAND,
                         WEAPON_BACKDROP_BLIT_MODE);
    free(backdrop_cel);

    fdps_transition_zoom(screen_page, WEAPON_TRANSITION_CENTER_X,
                         WEAPON_TRANSITION_CENTER_Y, TRANSITION_ZOOM_OUT);
    fdps_village_animate_window_zoom(screen_page, WINDOW_ZOOM_OPEN);
    fdps_draw_party_gold((unsigned char *) (VGA_SCREEN_BASE
                                            + WEAPON_GOLD_READOUT_SCREEN_AT),
                         VGA_SCREEN_PITCH);
    fdps_draw_text(data_fdps_all_game_text_ptr, WEAPON_OPENING_TEXT_ID,
                   (unsigned char *) (VGA_SCREEN_BASE
                                      + WEAPON_MESSAGE_SCREEN_AT),
                   VGA_SCREEN_PITCH, WEAPON_TEXT_FG_COLOR,
                   WEAPON_TEXT_BG_COLOR, WEAPON_TEXT_OUTLINE_COLOR);

    while (selected_command != WEAPON_COMMAND_CANCELLED) {
        fdps_menu_command_icon_select_loop(command_icon_ids,
                                           WEAPON_COMMAND_COUNT,
                                           &selected_command);

        switch (selected_command) {
        case WEAPON_COMMAND_TALK:
            fdps_village_animate_window_zoom(screen_page, WINDOW_ZOOM_CLOSE);
            fdps_village_animate_window_zoom(screen_page, WINDOW_ZOOM_OPEN);
            fdps_draw_text(data_fdps_current_chapter_text_ptr,
                           WEAPON_SHOP_CHAPTER_TEXT_ID,
                           (unsigned char *) (VGA_SCREEN_BASE
                                              + WEAPON_MESSAGE_SCREEN_AT),
                           VGA_SCREEN_PITCH, WEAPON_TEXT_FG_COLOR,
                           WEAPON_TEXT_BG_COLOR, WEAPON_TEXT_OUTLINE_COLOR);
            break;
        case WEAPON_COMMAND_BUY:
            fdps_shop_buy_loop(screen_page, WEAPON_SHOP_INDEX);
            break;
        case WEAPON_COMMAND_SELL:
            fdps_village_item_sell_loop(screen_page);
            break;
        case WEAPON_COMMAND_TRANSFER:
            fdps_village_item_transfer_loop(screen_page);
            break;
        case WEAPON_COMMAND_EQUIP:
            fdps_village_member_equip_loop(screen_page);
            break;
        }

        if (selected_command == WEAPON_COMMAND_CANCELLED) {
            fdps_village_animate_window_zoom(screen_page, WINDOW_ZOOM_CLOSE);
        } else {
            fdps_village_animate_window_zoom(screen_page, WINDOW_ZOOM_OPEN);
            fdps_draw_text(data_fdps_all_game_text_ptr,
                           WEAPON_AFTER_COMMAND_TEXT_ID,
                           (unsigned char *) (VGA_SCREEN_BASE
                                              + WEAPON_MESSAGE_SCREEN_AT),
                           VGA_SCREEN_PITCH, WEAPON_TEXT_FG_COLOR,
                           WEAPON_TEXT_BG_COLOR, WEAPON_TEXT_OUTLINE_COLOR);
            fdps_draw_party_gold((unsigned char *)
                                     (VGA_SCREEN_BASE
                                      + WEAPON_GOLD_READOUT_SCREEN_AT),
                                 VGA_SCREEN_PITCH);
        }
    }

    fdps_transition_zoom(screen_page, WEAPON_TRANSITION_CENTER_X,
                         WEAPON_TRANSITION_CENTER_Y, TRANSITION_ZOOM_IN);
    free(screen_page);
}

/* ------------------------------------------------------------------
 * fdps_run_secret_menu @ 00036210
 * ------------------------------------------------------------------ */

/* The backdrop and where it is taken from, PUSH 0x61fd4 and PUSH 0x60128 at
   00036232.  Both are plain writable literals and have to stay that way:
   fdps_vfs_load_entry upper-cases the caller's own storage in place (vfs.h),
   so the original's two strings read "MISC.VFS" and "Secret.cel" from the
   first call onward and a copy placed in read-only storage would fault instead
   (rebuild_info/pitfalls.md). */
#define SECRET_MENU_ARCHIVE "MISC.VFS"
#define SECRET_MENU_BACKDROP "Secret.cel"

/* The backdrop is sprite 0 of that sheet, drawn at the page's origin through
   the opaque pass-through kernel: PUSH 0x0 five times and PUSH 0x140 for the
   pitch at 00036261. */
#define SECRET_BACKDROP_SPRITE 0
#define SECRET_BACKDROP_X 0
#define SECRET_BACKDROP_Y 0
#define SECRET_BACKDROP_BLIT_OPERAND 0
#define SECRET_BACKDROP_BLIT_MODE 0

/* The point the opening and closing transitions are magnified about, PUSH 0x9f
   and PUSH 0x63 at 00036292 and 00036436: the screen centre, so this screen
   gets a straight pull-back and not the swing the signboard menu gets
   (transit.h). */
#define SECRET_TRANSITION_CENTER_X 0x9f
#define SECRET_TRANSITION_CENTER_Y 0x63

/* Where this screen's two messages are written: 0xaa3d4, column 20 and row 131
   of the mode 13h screen, inside the window frame the zoom has just opened;
   and where the gold readout goes: 0xa8208, column 8 and row 104.  The colour
   trio is the standard one, PUSH 0xd0 / PUSH 0x0 / PUSH 0x6d in front of each
   message. */
#define SECRET_MESSAGE_SCREEN_AT 0xa3d4
#define SECRET_GOLD_READOUT_SCREEN_AT 0x8208
#define SECRET_TEXT_FG_COLOR 0xd0
#define SECRET_TEXT_BG_COLOR 0
#define SECRET_TEXT_OUTLINE_COLOR 0x6d

/* The two entries of the resident text block this screen draws, PUSH 0x202 at
   000362d9 and PUSH 0x1f5 at 00036409.  The first goes up once when the screen
   opens; the second replaces it after every command that was not the cancel.
   THE SECOND ONE IS NOT THIS SCREEN'S OWN: 0x1f5 is also what
   fdps_village_item_menu reprints after a command (vilmenu.c), and those two
   sites are the only readers of it in the image.  0x202 is read here and
   nowhere else. */
#define SECRET_OPENING_TEXT_ID 0x202
#define SECRET_AFTER_COMMAND_TEXT_ID 0x1f5

/* And the entry of the LOADED CHAPTER's own text block that the first command
   reprints, PUSH 0x8 at 00036371 in front of data_fdps_current_chapter_text_ptr
   rather than the resident table.  The five village screens take one entry each
   out of that block and they are consecutive: 4 in fdps_village_item_menu
   (vilmenu.c), 5 in fdps_run_weapon_shop, 6 in fdps_run_bar_shop, 7 in
   fdps_run_church_screen and 8 here. */
#define SECRET_MENU_CHAPTER_TEXT_ID 8

/* The row of commands: how many entries it has, PUSH 0x6 at 00036314, and the
   Command.cel sub-image id of each, which is the six-dword template the
   original copies onto the frame with REP MOVSD at 00036230 -- an initialised
   automatic array, not a global, and with no other reader.  The order is the
   order the row draws them, left to right.  It is the weapon shop's five with
   the church's status icon added on the end. */
#define SECRET_COMMAND_COUNT 6
#define SECRET_ICON_TALK 0x24
#define SECRET_ICON_BUY 0x14
#define SECRET_ICON_SELL 0x15
#define SECRET_ICON_TRANSFER 0x04
#define SECRET_ICON_EQUIP 0x06
#define SECRET_ICON_STATUS 0x10

/* The answer the row hands back, which is an index into that table.  The six
   arms are the six entries of the CS-relative jump table at 000362f8 --
   0003633d, 00036383, 00036397, 000363a5, 000363b3 and 000363c1 -- in this
   order. */
#define SECRET_COMMAND_TALK 0
#define SECRET_COMMAND_BUY 1
#define SECRET_COMMAND_SELL 2
#define SECRET_COMMAND_TRANSFER 3
#define SECRET_COMMAND_EQUIP 4
#define SECRET_COMMAND_STATUS 5

/* And what the row writes into it when the player backs out, which is the
   loop's only exit (menu.h). */
#define SECRET_COMMAND_CANCELLED (-1)

/* The sweep code the reprint arm shuts the window with, MOV EAX,0x2 at
   0003633d, where the church and the weapon shop both use 1.  The argument is
   only tested against zero (village.h), so 2 plays the same closing sweep 1
   does; it is written as the 2 the original has rather than folded into
   WINDOW_ZOOM_CLOSE because it is a different literal in a different
   function. */
#define SECRET_REPRINT_WINDOW_CLOSE 2

/* Which shop table the buy counter is opened against, MOV EAX,0x2 / PUSH EAX
   at 00036383.  It is a byte argument and this screen is shop 2, so the stock
   offered is row 2 of SHOP%02d.DAT and neither the item screen's row 0 nor the
   weapon shop's row 1 (shop.h). */
#define SECRET_SHOP_INDEX 2

/* 00036210.  No arguments and no answer: the one call site, in
   fdps_run_village_phase at 00031481, pushes nothing and follows the CALL with
   a JMP rather than a stack adjustment, nothing above EBP is read, EAX is not
   set before the epilogue and the RET carries no immediate.

   The control flow is one loop with a six-way switch and one two-armed test in
   it.  The entry test CMP dword ptr [EBP-0x8],-0x1 / JNZ at 000362ec is the
   top of the loop and the JMP at 0003642e is its back edge, so this is a while
   whose condition is checked before the first pass -- which is why the answer
   slot is seeded with zero at 0003621c rather than with a command: zero is
   simply a value that is not -1.  The switch is CMP dword ptr [EBP-0x8],0x5 /
   JA at 00036322 in front of JMP dword ptr CS:[EAX*4 + 0x362f8], and the bound
   is UNSIGNED, which is how the cancel's -1 falls past all six arms and lands
   on the same address a taken arm falls to.  Every arm reaches the two-armed
   test at 000363cd and that test's arms both reach the back edge, so backing
   out of the icon row is the only way to the epilogue.

   THE TWO OPENING DRAWS ARE IN THE OTHER ORDER FROM THE TWO CLOSING ONES.  On
   the way in the gold readout goes down first and the message second (000362be
   then 000362e4); after a command it is the message first and the readout
   second (00036414 then 00036426).  They do not overlap -- row 104 against row
   131 -- so the order is not load-bearing, but it is what the assembly does.

   THE REPRINT ARM CLOSES THE WINDOW BEFORE IT REOPENS IT.  MOV EAX,0x2 at
   0003633d then XOR EAX,EAX at 0003634f: the frame is swept shut and open
   again, which is what clears the icon row's own painting off the frame before
   the line is written into it.  The arm then falls into the same test every
   other arm does, so its answer is still 0 and the window is opened a second
   time and the resident message drawn over the chapter line it just put up.

   The one value used after a CALL is fdps_vfs_load_entry's EAX, stored to
   [EBP-0x4] at 00036246 and handed to the blit and then to free.  malloc's EAX
   is stored to [EBP-0xc] at 00036256 and copied to the global from there --
   MOV EAX,[EBP-0xc] / MOV [0x00063fb4],EAX at 00036259, so every later use is
   the local's value and not a second read of the global.  Nothing else is
   read: fdps_menu_command_icon_select_loop's answer comes back through the
   pointer and its EAX is dropped (ADD ESP,0xc at 0003631f with no use of it),
   and the blit, the two transitions, the window sweeps, the readout,
   fdps_draw_text and the five submenus are each followed straight by their
   stack cleanup.

   NOTHING IS CHECKED.  The load is not tested -- a container or a member that
   cannot be found ends the process inside fdps_vfs_load_entry (vfs.h) -- and
   neither is malloc's answer, which is written to the global and then blitted
   into. */
void fdps_run_secret_menu(void)
{
    /* The unpacked "Secret.cel" sheet, alive only long enough to be drawn into
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
    /* The row's six Command.cel sub-image ids, left to right. */
    int command_icon_ids[SECRET_COMMAND_COUNT] = {
        SECRET_ICON_TALK, SECRET_ICON_BUY, SECRET_ICON_SELL,
        SECRET_ICON_TRANSFER, SECRET_ICON_EQUIP, SECRET_ICON_STATUS
    };

    backdrop_cel = (unsigned char *)
        fdps_vfs_load_entry(SECRET_MENU_ARCHIVE, SECRET_MENU_BACKDROP);
    screen_page = (unsigned char *) malloc((size_t) VGA_SCREEN_BYTES);
    data_fdps_village_backdrop_page_ptr = screen_page;
    fdps_cel_blit_sprite(backdrop_cel, SECRET_BACKDROP_SPRITE, screen_page,
                         VGA_SCREEN_PITCH, SECRET_BACKDROP_X,
                         SECRET_BACKDROP_Y, SECRET_BACKDROP_BLIT_OPERAND,
                         SECRET_BACKDROP_BLIT_MODE);
    free(backdrop_cel);

    fdps_transition_zoom(screen_page, SECRET_TRANSITION_CENTER_X,
                         SECRET_TRANSITION_CENTER_Y, TRANSITION_ZOOM_OUT);
    fdps_village_animate_window_zoom(screen_page, WINDOW_ZOOM_OPEN);
    fdps_draw_party_gold((unsigned char *) (VGA_SCREEN_BASE
                                            + SECRET_GOLD_READOUT_SCREEN_AT),
                         VGA_SCREEN_PITCH);
    fdps_draw_text(data_fdps_all_game_text_ptr, SECRET_OPENING_TEXT_ID,
                   (unsigned char *) (VGA_SCREEN_BASE
                                      + SECRET_MESSAGE_SCREEN_AT),
                   VGA_SCREEN_PITCH, SECRET_TEXT_FG_COLOR,
                   SECRET_TEXT_BG_COLOR, SECRET_TEXT_OUTLINE_COLOR);

    while (selected_command != SECRET_COMMAND_CANCELLED) {
        fdps_menu_command_icon_select_loop(command_icon_ids,
                                           SECRET_COMMAND_COUNT,
                                           &selected_command);

        switch (selected_command) {
        case SECRET_COMMAND_TALK:
            fdps_village_animate_window_zoom(screen_page,
                                             SECRET_REPRINT_WINDOW_CLOSE);
            fdps_village_animate_window_zoom(screen_page, WINDOW_ZOOM_OPEN);
            fdps_draw_text(data_fdps_current_chapter_text_ptr,
                           SECRET_MENU_CHAPTER_TEXT_ID,
                           (unsigned char *) (VGA_SCREEN_BASE
                                              + SECRET_MESSAGE_SCREEN_AT),
                           VGA_SCREEN_PITCH, SECRET_TEXT_FG_COLOR,
                           SECRET_TEXT_BG_COLOR, SECRET_TEXT_OUTLINE_COLOR);
            break;
        case SECRET_COMMAND_BUY:
            fdps_shop_buy_loop(screen_page, SECRET_SHOP_INDEX);
            break;
        case SECRET_COMMAND_SELL:
            fdps_village_item_sell_loop(screen_page);
            break;
        case SECRET_COMMAND_TRANSFER:
            fdps_village_item_transfer_loop(screen_page);
            break;
        case SECRET_COMMAND_EQUIP:
            fdps_village_member_equip_loop(screen_page);
            break;
        case SECRET_COMMAND_STATUS:
            fdps_village_member_status_loop(screen_page);
            break;
        }

        if (selected_command == SECRET_COMMAND_CANCELLED) {
            fdps_village_animate_window_zoom(screen_page, WINDOW_ZOOM_CLOSE);
        } else {
            fdps_village_animate_window_zoom(screen_page, WINDOW_ZOOM_OPEN);
            fdps_draw_text(data_fdps_all_game_text_ptr,
                           SECRET_AFTER_COMMAND_TEXT_ID,
                           (unsigned char *) (VGA_SCREEN_BASE
                                              + SECRET_MESSAGE_SCREEN_AT),
                           VGA_SCREEN_PITCH, SECRET_TEXT_FG_COLOR,
                           SECRET_TEXT_BG_COLOR, SECRET_TEXT_OUTLINE_COLOR);
            fdps_draw_party_gold((unsigned char *)
                                     (VGA_SCREEN_BASE
                                      + SECRET_GOLD_READOUT_SCREEN_AT),
                                 VGA_SCREEN_PITCH);
        }
    }

    fdps_transition_zoom(screen_page, SECRET_TRANSITION_CENTER_X,
                         SECRET_TRANSITION_CENTER_Y, TRANSITION_ZOOM_IN);
    free(screen_page);
}

/* ------------------------------------------------------------------
 * fdps_run_bonus_lottery @ 00036460
 * ------------------------------------------------------------------ */

/* The one day the draw happens, and nothing else opens it: CMP EAX,0x7ce at
   00036499 against the u16 year of the struct dosdate_t _dos_getdate filled,
   CMP EAX,0x1 at 000364a7 against its month byte and CMP EAX,0x1c at 000364b3
   against its day byte.  Each is compared after a zero-extension, which is
   what an unsigned short and an unsigned char promote to. */
#define BONUS_DRAW_YEAR 0x7ce
#define BONUS_DRAW_MONTH 1
#define BONUS_DRAW_DAY 0x1c

/* The reel clip and the fanfare, and the container both come out of, PUSH
   0x61fe0 with PUSH 0x60128 at 0003652a and PUSH 0x61fec with the same
   container at 00036920.  All three are plain writable literals and have to
   stay that way: fdps_vfs_load_entry upper-cases the caller's own storage in
   place (vfs.h), so a copy placed in read-only storage would fault instead
   (rebuild_info/pitfalls.md). */
#define BONUS_ARCHIVE "MISC.VFS"
#define BONUS_REEL_CLIP "Bonus-1.saf"
#define BONUS_FANFARE_WAV "Bonus.wav"

/* The private page every reel frame is composed on: 368 by 248 with a 24-pixel
   margin on each side, PUSH 0x16480 at 00036567 for the allocation with PUSH
   0x170 and PUSH 0xf8 into the request at 0003657d, and 0x18 for both origins
   at 00036598.  0x2298 is 24 * 0x170 + 24, the pixel at (24,24), so the page's
   inner 320x200 window is what the snapshot is copied into and what is put
   back on the adapter. */
#define REEL_SURFACE_PITCH 0x170
#define REEL_SURFACE_ROWS 0xf8
#define REEL_SURFACE_BYTES 0x16480
#define REEL_SURFACE_MARGIN 0x18
#define REEL_VISIBLE_ORIGIN_OFFSET 0x2298

/* The row count of every transfer between the aperture and that window, PUSH
   0xc8 at 000365dd and at each of the four presents.  The 0x140 beside it is
   both the aperture's pitch and the transfer's bytes per row, which is
   VGA_SCREEN_PITCH above. */
#define BONUS_SCREEN_ROWS 0xc8

/* Input status register 1 and its vertical retrace bit, PUSH 0x3da with TEST
   AL,0x8 at 00036615 and at the three other presents. */
#define VGA_INPUT_STATUS_1 0x3da
#define VGA_STATUS_VERTICAL_RETRACE 0x08

/* The three dwords of a mode-9 blend descriptor (rleblend.h): the shade ramp
   base, the blend level and the inverse colour cube base.  They are one block
   because the address of the first is what goes into the request -- LEA
   EAX,[EBP-0x3c] at 0003658b over the two table stores at 00036559 and
   00036560 -- so they have to be adjacent and in this order. */
#define BLEND_DESCRIPTOR_RAMP 0
#define BLEND_DESCRIPTOR_LEVEL 1
#define BLEND_DESCRIPTOR_CUBE 2
#define BLEND_DESCRIPTOR_DWORDS 3

/* The two blit modes the reel is drawn through: mode 9, the translucent kernel
   the fade-in runs on with the descriptor above as its operand (MOV
   [EBP-0x4c],0x9 at 00036591), and mode 0 with a null operand, the opaque
   pass-through the spin itself uses (MOV [EBP-0x50],0x0 / MOV [EBP-0x4c],0x0
   at 00036679). */
#define FADE_BLIT_MODE 9
#define OPAQUE_BLIT_MODE 0
#define OPAQUE_BLIT_OPERAND 0

/* fdps_draw_composite_sprite's sound flag (sprite.h): the fade-in draws its
   frames silently and every frame of the spin plays the sound its own record
   names. */
#define FRAME_SOUND_OFF 0
#define FRAME_SOUND_ON 1

/* fdps_saf_advance_tick's mode byte (saf.h): 1 rewinds the cursor and 0 runs
   the clip, restarting it at frame 0 at the end -- which is what makes the
   reel an endless loop of ten frames. */
#define SAF_CURSOR_RESET 1
#define SAF_ADVANCE_LOOPING 0

/* The fade-in: six steps of the blend level from 15 down to 0, MOV
   [EBP-0x1c],0xf at 000365ac with ADD dword ptr [EBP-0x1c],-0x3 at 000365be
   and the bound JGE at 000365b7.  Level 0 is fully opaque (rleblend.h), so the
   reel arrives out of the picture underneath it. */
#define BONUS_FADE_FIRST_LEVEL 0xf
#define BONUS_FADE_LEVEL_STEP 3

/* The spin-up and the spin-down.  Each is a walk of the per-frame hold in
   ticks -- 8 down to 1 at 00036687, then 1 up to 8 at 00036822 -- with a
   random number of frames stepped at each hold, rand() % 10 + 1 on the way up
   (MOV EBX,0xa / IDIV EBX / INC EDX at 000366a8) and rand() % 3 + 1 on the way
   down (MOV EBX,0x3 at 00036843).  A shrinking hold is an accelerating reel
   and a growing one a decelerating one. */
#define BONUS_SPIN_UP_FIRST_HOLD 8
#define BONUS_SPIN_UP_LAST_HOLD 1
#define BONUS_SPIN_UP_FRAME_SPREAD 10
#define BONUS_SPIN_DOWN_FIRST_HOLD 1
#define BONUS_SPIN_DOWN_LAST_HOLD 8
#define BONUS_SPIN_DOWN_FRAME_SPREAD 3

/* What the reel is stopped by: AND EAX,0xff / CMP EAX,0x7f / JLE at 00036789
   on what fdps_read_keyboard_queue answered, so the reel runs while the answer
   is above 0x7f and stops on the first make code at or below it.  The compare
   is signed on a value that was zero-extended out of a byte, which is exactly
   what an unsigned char promoted to int gives (keybd.h). */
#define BONUS_REEL_STOP_MAX_MAKE_CODE 0x7f

/* The ten prize classes the reel's frames stand for, one per frame of
   Bonus-1.saf, copied onto the frame by the REP MOVSD at 0003647c out of the
   initialiser image at 000311e0 -- an initialised automatic array, not a
   global, and with no other reader.  The clip really does hold ten frames and
   fdps_saf_advance_tick wraps at the tenth, so the index can never leave the
   table. */
#define BONUS_REEL_FRAMES 10

/* The four arms of the prize test at 0003691a, 00036981 and 000369af, and what
   each hands out.  The sword's item id is 0xbf plus the chapter's decade (MOV
   EBX,0xa / IDIV EBX / ADD EAX,0xbf at 0003694c), which selects 0xbf, 0xc0 or
   0xc1 -- the three power tiers of the 斬鐵劍 (assets/items.md).  0xb9 is the
   水晶粒 and 0xb4 the 藥草. */
#define BONUS_PRIZE_SWORD 0
#define BONUS_PRIZE_CRYSTALS 1
#define BONUS_PRIZE_GOLD 2
#define BONUS_SWORD_FIRST_ITEM 0xbf
#define BONUS_SWORD_TIER_CHAPTERS 10
#define BONUS_CRYSTAL_ITEM 0xb9
#define BONUS_CRYSTAL_COUNT 10
#define BONUS_GOLD_PRIZE 0x4e20
#define BONUS_CONSOLATION_ITEM 0xb4

/* fdps_audio_start_wav's loop count, PUSH 0x1 at 0003693b.  The rate and the
   volume beside it are the two sentinels audio.h names. */
#define BONUS_FANFARE_LOOP_COUNT 1

/* The three messages, PUSH 0x224 at 00036505, ADD EAX,0x225 onto the prize
   class at 00036a3b and PUSH 0x229 at 00036a83, all three out of the resident
   text table.  0x225 through 0x228 are therefore the four prize lines, one per
   class, and 0x229 is the closing line.  Where they are written and in what
   colours is the same trio the other screens in this file use: 0xaa3d4, column
   20 and row 131 of the mode 13h screen, inside the window frame the sweep has
   just opened. */
#define BONUS_OPENING_TEXT_ID 0x224
#define BONUS_PRIZE_FIRST_TEXT_ID 0x225
#define BONUS_CLOSING_TEXT_ID 0x229
#define BONUS_MESSAGE_SCREEN_AT 0xa3d4
#define BONUS_TEXT_FG_COLOR 0xd0
#define BONUS_TEXT_BG_COLOR 0
#define BONUS_TEXT_OUTLINE_COLOR 0x6d

/* 00036460.  No arguments and no answer: the one call site at 00035d52 in
   fdps_run_bar_shop pushes nothing, adjusts no stack after the CALL and reads
   no EAX, nothing above EBP is read here, and the RET carries no immediate.

   THE DATE IS READ BEFORE THE FLAG IS TESTED.  CALL _dos_getdate at 00036482
   sits in front of CMP dword ptr [0x00064110],0x0 at 0003648a, so the one
   thing this function does on every other day of the year is a DOS call.  The
   four tests after it are a short-circuit chain -- the flag, then the year,
   the month and the day -- whose every failure lands on the same JMP 0x36ae2
   at 000364b8 and so on the epilogue.

   THE PRIZE IS AWARDED FROM AN UNWRITTEN STACK SLOT.  The test at 0003691a
   reads [EBP-0xc] and the only store to that slot is the MOV at 00036a05,
   which runs after the test has already chosen an arm, so what the player
   receives is whatever the frame's storage held and the message printed
   afterwards is the class the reel really stopped on.  `prize_class` below is
   left uninitialised for that reason and the store that follows the second
   snapshot is kept: reading the reel first and awarding that prize is the
   obvious shape and it is a different game -- it would put the sword, the ten
   crystals and the 20,000 gold within reach, where the original hands out the
   consolation herb (rebuild_info/pitfalls.md).

   THE PACE LATCH IS DELIBERATELY NOT INITIALISED, the way every other paced
   loop in the game leaves it: each present ends by spinning until
   data_fdps_timer_tick_counter differs from `last_tick` and then re-latching
   it, and nothing seeds it, so the very first present ends against whatever
   the stack held (rebuild_info/pitfalls.md).

   THE PRESENT IS WRITTEN OUT FOUR TIMES AND NOT FACTORED OUT.  The retrace
   pair, the 64,000-byte blit and the tick wait appear inline at 00036615,
   00036714, 000367be and 000368af, and the reel's speed is the number of times
   that block runs; folding the four copies into a call of their own puts an
   instruction sequence into the innermost loop of an animation the player is
   watching that the original does not have.

   The values used after a CALL are malloc's EAX at 000364ca, 00036574 and
   000369db, each stored straight into the slot every later use reads;
   fdps_vfs_load_entry's EAX at 0003653e, copied into the cursor's image slot
   at 00036544 and read back from the local for the free at 00036ab4;
   fdps_audio_start_wav's EAX at 00036949, which is the voice the wait loop
   polls; fdps_read_keyboard_queue's AL, masked to a byte and compared;
   rand()'s EAX, divided; and fdps_saf_advance_tick's answer, which is dropped
   at every one of its four call sites (ADD ESP,0x8 with no use of EAX) --
   the reel is stopped by the keyboard and never by the end of the clip.
   fdps_draw_text answers a pointer that is likewise dropped.

   NOTHING IS CHECKED.  Neither malloc nor the two loads is tested, and the
   Bonus.wav image is never freed. */
void fdps_run_bonus_lottery(void)
{
    /* Which prize class each of the reel's ten frames stands for.  Frame 0 is
       the sword, frames 2 and 6 the crystals, frames 3, 5 and 8 the gold, and
       the remaining four the consolation herb. */
    int prize_class_by_reel_frame[BONUS_REEL_FRAMES] = {
        0, 3, 1, 2, 3, 2, 1, 3, 2, 3
    };
    /* The nine-slot draw request block sprite.h describes.  One block serves
       every frame of the draw: the page, its pitch, its height, the origin and
       the image are filled in once, and only the frame index and the blit mode
       move afterwards. */
    int request[DRAW_REQUEST_DWORDS];
    /* The mode-9 descriptor the fade-in draws through.  Its two table pointers
       are filled in once; only the level moves. */
    int fade_descriptor[BLEND_DESCRIPTOR_DWORDS];
    /* The reel clip's three-dword playback cursor (saf.h). */
    int reel_cursor[SAF_CURSOR_DWORDS];
    /* The system date, and the only thing this function reads on any other
       day. */
    struct dosdate_t today;
    /* The picture that was on the adapter when the draw opened, kept for as
       long as the draw runs because the fade-in composites the reel over it. */
    unsigned char *saved_screen;
    /* And the picture with the reel stopped on it, taken again once the prize
       has been handed out so the two announcement windows have something to
       sweep open over. */
    unsigned char *stopped_reel_screen;
    /* The loaded Bonus-1.saf, freed on the way out. */
    void *reel_clip;
    /* The loaded Bonus.wav, which is NOT freed. */
    void *fanfare_wav;
    /* The sample slot the fanfare was started on, or the no-slot answer, which
       the wait loop below is content to be handed (audio.h). */
    int fanfare_voice;
    /* The fade-in's blend level, 15 down to 0. */
    int fade_level;
    /* How many ticks one reel frame is held, which is what the two spin walks
       move: 8 down to 1 and then 1 back up to 8. */
    int frame_hold_ticks;
    /* How many frames this step of a spin walks the reel on, and how many of
       them have been drawn. */
    int frames_this_step;
    /* Which frame of this step is being drawn. */
    int stepped_frame;
    /* Which of this frame's held ticks is being presented. */
    int presented_tick;
    /* Which of the ten crystals is being handed out. */
    int crystal;
    /* Which prize is awarded.  Deliberately not initialised -- see the note
       above; the store that gives it the reel's real answer comes after every
       reader of it. */
    int prize_class;
    /* The tick the previous present ended on.  Deliberately not initialised --
       see the note above. */
    unsigned int last_tick;

    _dos_getdate(&today);
    if (data_fdps_bonus_lottery_drawn_flag != 0 || today.year != BONUS_DRAW_YEAR
        || today.month != BONUS_DRAW_MONTH || today.day != BONUS_DRAW_DAY) {
        return;
    }

    /* The opening message, over a snapshot of whatever the bar left showing. */
    saved_screen = (unsigned char *) malloc((size_t) VGA_SCREEN_BYTES);
    memmove(saved_screen, (void *) VGA_SCREEN_BASE, (size_t) VGA_SCREEN_BYTES);
    fdps_village_animate_window_zoom(saved_screen, WINDOW_ZOOM_OPEN);
    fdps_draw_text(data_fdps_all_game_text_ptr, BONUS_OPENING_TEXT_ID,
                   (unsigned char *) (VGA_SCREEN_BASE
                                      + BONUS_MESSAGE_SCREEN_AT),
                   VGA_SCREEN_PITCH, BONUS_TEXT_FG_COLOR, BONUS_TEXT_BG_COLOR,
                   BONUS_TEXT_OUTLINE_COLOR);
    fdps_village_animate_window_zoom(saved_screen, WINDOW_ZOOM_CLOSE);

    reel_clip = fdps_vfs_load_entry(BONUS_ARCHIVE, BONUS_REEL_CLIP);
    reel_cursor[SAF_CURSOR_IMAGE] = (int) reel_clip;
    fdps_saf_advance_tick(reel_cursor, SAF_CURSOR_RESET);

    fade_descriptor[BLEND_DESCRIPTOR_RAMP] =
        (int) data_fdps_palette_shade_ramp_table;
    fade_descriptor[BLEND_DESCRIPTOR_CUBE] =
        (int) data_fdps_inverse_palette_cube;

    request[DRAW_REQUEST_DEST_BASE] = (int) malloc((size_t) REEL_SURFACE_BYTES);
    request[DRAW_REQUEST_IMAGE] = (int) reel_clip;
    request[DRAW_REQUEST_DEST_PITCH] = REEL_SURFACE_PITCH;
    request[DRAW_REQUEST_DEST_ROWS] = REEL_SURFACE_ROWS;
    request[DRAW_REQUEST_BLIT_OPERAND] = (int) fade_descriptor;
    request[DRAW_REQUEST_BLIT_MODE] = FADE_BLIT_MODE;
    request[DRAW_REQUEST_X] = REEL_SURFACE_MARGIN;
    request[DRAW_REQUEST_Y] = REEL_SURFACE_MARGIN;
    request[DRAW_REQUEST_ITEM_INDEX] = reel_cursor[SAF_CURSOR_FRAME_INDEX];

    /* The fade-in.  The saved picture goes back into the page's window under
       every step, so the reel is blended into the bar and not into the step
       before it. */
    for (fade_level = BONUS_FADE_FIRST_LEVEL; fade_level >= 0;
         fade_level -= BONUS_FADE_LEVEL_STEP) {
        fade_descriptor[BLEND_DESCRIPTOR_LEVEL] = fade_level;
        memset((void *) request[DRAW_REQUEST_DEST_BASE], 0,
               (size_t) REEL_SURFACE_BYTES);
        fdps_blit_rect((unsigned int) saved_screen, VGA_SCREEN_PITCH,
                       (void *) (request[DRAW_REQUEST_DEST_BASE]
                                 + REEL_VISIBLE_ORIGIN_OFFSET),
                       REEL_SURFACE_PITCH, VGA_SCREEN_PITCH,
                       BONUS_SCREEN_ROWS);
        fdps_draw_composite_sprite(request, FRAME_SOUND_OFF);

        while ((inp(VGA_INPUT_STATUS_1) & VGA_STATUS_VERTICAL_RETRACE) == 0) {
            /* Spin until the retrace begins, so the frame just composed is
               the one the monitor shows whole. */
        }
        while ((inp(VGA_INPUT_STATUS_1) & VGA_STATUS_VERTICAL_RETRACE) != 0) {
            /* And until it ends, so the 64000-byte transfer starts clear. */
        }
        fdps_blit_rect((unsigned int) (request[DRAW_REQUEST_DEST_BASE]
                                       + REEL_VISIBLE_ORIGIN_OFFSET),
                       REEL_SURFACE_PITCH, (void *) VGA_SCREEN_BASE,
                       VGA_SCREEN_PITCH, VGA_SCREEN_PITCH, BONUS_SCREEN_ROWS);
        while (last_tick == data_fdps_timer_tick_counter) {
        }
        last_tick = data_fdps_timer_tick_counter;
    }

    /* From here the reel is opaque and the page is never cleared again, so
       every frame is drawn over the one before it. */
    request[DRAW_REQUEST_BLIT_OPERAND] = OPAQUE_BLIT_OPERAND;
    request[DRAW_REQUEST_BLIT_MODE] = OPAQUE_BLIT_MODE;

    /* The spin-up: the hold shrinks from eight ticks a frame to one. */
    for (frame_hold_ticks = BONUS_SPIN_UP_FIRST_HOLD;
         frame_hold_ticks >= BONUS_SPIN_UP_LAST_HOLD; frame_hold_ticks--) {
        frames_this_step = rand() % BONUS_SPIN_UP_FRAME_SPREAD + 1;
        for (stepped_frame = 0; stepped_frame < frames_this_step;
             stepped_frame++) {
            fdps_saf_advance_tick(reel_cursor, SAF_ADVANCE_LOOPING);
            request[DRAW_REQUEST_ITEM_INDEX] =
                reel_cursor[SAF_CURSOR_FRAME_INDEX];
            fdps_draw_composite_sprite(request, FRAME_SOUND_ON);
            for (presented_tick = 0; presented_tick < frame_hold_ticks;
                 presented_tick++) {
                while ((inp(VGA_INPUT_STATUS_1)
                        & VGA_STATUS_VERTICAL_RETRACE) == 0) {
                }
                while ((inp(VGA_INPUT_STATUS_1)
                        & VGA_STATUS_VERTICAL_RETRACE) != 0) {
                }
                fdps_blit_rect((unsigned int) (request[DRAW_REQUEST_DEST_BASE]
                                               + REEL_VISIBLE_ORIGIN_OFFSET),
                               REEL_SURFACE_PITCH, (void *) VGA_SCREEN_BASE,
                               VGA_SCREEN_PITCH, VGA_SCREEN_PITCH,
                               BONUS_SCREEN_ROWS);
                while (last_tick == data_fdps_timer_tick_counter) {
                }
                last_tick = data_fdps_timer_tick_counter;
            }
        }
    }

    /* The reel at full speed, one frame per tick, until the player presses a
       key.  The queue is flushed first, so a key pressed during the spin-up
       does not stop it immediately. */
    fdps_flush_keyboard_queue();
    while (fdps_read_keyboard_queue() > BONUS_REEL_STOP_MAX_MAKE_CODE) {
        fdps_saf_advance_tick(reel_cursor, SAF_ADVANCE_LOOPING);
        request[DRAW_REQUEST_ITEM_INDEX] = reel_cursor[SAF_CURSOR_FRAME_INDEX];
        fdps_draw_composite_sprite(request, FRAME_SOUND_ON);

        while ((inp(VGA_INPUT_STATUS_1) & VGA_STATUS_VERTICAL_RETRACE) == 0) {
        }
        while ((inp(VGA_INPUT_STATUS_1) & VGA_STATUS_VERTICAL_RETRACE) != 0) {
        }
        fdps_blit_rect((unsigned int) (request[DRAW_REQUEST_DEST_BASE]
                                       + REEL_VISIBLE_ORIGIN_OFFSET),
                       REEL_SURFACE_PITCH, (void *) VGA_SCREEN_BASE,
                       VGA_SCREEN_PITCH, VGA_SCREEN_PITCH, BONUS_SCREEN_ROWS);
        while (last_tick == data_fdps_timer_tick_counter) {
        }
        last_tick = data_fdps_timer_tick_counter;
    }

    /* The spin-down: the hold grows back from one tick a frame to eight, and
       fewer frames are stepped at each hold, so the reel coasts to a stop. */
    for (frame_hold_ticks = BONUS_SPIN_DOWN_FIRST_HOLD;
         frame_hold_ticks <= BONUS_SPIN_DOWN_LAST_HOLD; frame_hold_ticks++) {
        frames_this_step = rand() % BONUS_SPIN_DOWN_FRAME_SPREAD + 1;
        for (stepped_frame = 0; stepped_frame < frames_this_step;
             stepped_frame++) {
            fdps_saf_advance_tick(reel_cursor, SAF_ADVANCE_LOOPING);
            request[DRAW_REQUEST_ITEM_INDEX] =
                reel_cursor[SAF_CURSOR_FRAME_INDEX];
            fdps_draw_composite_sprite(request, FRAME_SOUND_ON);
            for (presented_tick = 0; presented_tick < frame_hold_ticks;
                 presented_tick++) {
                while ((inp(VGA_INPUT_STATUS_1)
                        & VGA_STATUS_VERTICAL_RETRACE) == 0) {
                }
                while ((inp(VGA_INPUT_STATUS_1)
                        & VGA_STATUS_VERTICAL_RETRACE) != 0) {
                }
                fdps_blit_rect((unsigned int) (request[DRAW_REQUEST_DEST_BASE]
                                               + REEL_VISIBLE_ORIGIN_OFFSET),
                               REEL_SURFACE_PITCH, (void *) VGA_SCREEN_BASE,
                               VGA_SCREEN_PITCH, VGA_SCREEN_PITCH,
                               BONUS_SCREEN_ROWS);
                while (last_tick == data_fdps_timer_tick_counter) {
                }
                last_tick = data_fdps_timer_tick_counter;
            }
        }
    }

    /* The prize, chosen by a slot nothing has written yet. */
    if (prize_class == BONUS_PRIZE_SWORD) {
        fanfare_wav = fdps_vfs_load_entry(BONUS_ARCHIVE, BONUS_FANFARE_WAV);
        fanfare_voice = fdps_audio_start_wav(fanfare_wav,
                                             BONUS_FANFARE_LOOP_COUNT,
                                             SFX_WAV_RATE_FROM_HEADER,
                                             SFX_WAV_VOLUME_FROM_DEFAULT);
        fdps_roster_add_item_to_all(data_fdps_chapter_current_chapter_id
                                        / BONUS_SWORD_TIER_CHAPTERS
                                    + BONUS_SWORD_FIRST_ITEM);
        while (fdps_audio_sample_is_playing(fanfare_voice) != 0) {
        }
    } else if (prize_class == BONUS_PRIZE_CRYSTALS) {
        for (crystal = 0; crystal < BONUS_CRYSTAL_COUNT; crystal++) {
            fdps_roster_add_item_to_all(BONUS_CRYSTAL_ITEM);
        }
    } else if (prize_class == BONUS_PRIZE_GOLD) {
        data_fdps_shared_party_total_gold += BONUS_GOLD_PRIZE;
    } else {
        fdps_roster_add_item_to_all(BONUS_CONSOLATION_ITEM);
    }

    /* The announcement.  The store into prize_class is the original's and is
       kept: it is where the reel's real answer goes, and every reader of the
       slot has already run.  The message id is formed from the table again,
       exactly as the assembly forms it at 00036a2a. */
    stopped_reel_screen = (unsigned char *) malloc((size_t) VGA_SCREEN_BYTES);
    memmove(stopped_reel_screen, (void *) VGA_SCREEN_BASE,
            (size_t) VGA_SCREEN_BYTES);
    prize_class =
        prize_class_by_reel_frame[reel_cursor[SAF_CURSOR_FRAME_INDEX]];

    fdps_village_animate_window_zoom(stopped_reel_screen, WINDOW_ZOOM_OPEN);
    fdps_draw_text(data_fdps_all_game_text_ptr,
                   prize_class_by_reel_frame[reel_cursor[
                       SAF_CURSOR_FRAME_INDEX]] + BONUS_PRIZE_FIRST_TEXT_ID,
                   (unsigned char *) (VGA_SCREEN_BASE
                                      + BONUS_MESSAGE_SCREEN_AT),
                   VGA_SCREEN_PITCH, BONUS_TEXT_FG_COLOR, BONUS_TEXT_BG_COLOR,
                   BONUS_TEXT_OUTLINE_COLOR);
    fdps_village_animate_window_zoom(stopped_reel_screen, WINDOW_ZOOM_CLOSE);

    fdps_village_animate_window_zoom(stopped_reel_screen, WINDOW_ZOOM_OPEN);
    fdps_draw_text(data_fdps_all_game_text_ptr, BONUS_CLOSING_TEXT_ID,
                   (unsigned char *) (VGA_SCREEN_BASE
                                      + BONUS_MESSAGE_SCREEN_AT),
                   VGA_SCREEN_PITCH, BONUS_TEXT_FG_COLOR, BONUS_TEXT_BG_COLOR,
                   BONUS_TEXT_OUTLINE_COLOR);
    fdps_village_animate_window_zoom(stopped_reel_screen, WINDOW_ZOOM_CLOSE);

    free(stopped_reel_screen);
    free(reel_clip);
    free((void *) request[DRAW_REQUEST_DEST_BASE]);
    free(saved_screen);
    data_fdps_bonus_lottery_drawn_flag = 1;
}
