/* vilmenu.c -- the village's party-member picker and the item loops built on
 * it.
 *
 * See vilmenu.h for what the picker is asked and what it answers.  Nothing
 * here owns a resource: the window frame, the selection bar, the command
 * sprite sheet, the walking-icon cache, the roster array and the message
 * table are all globals the chapter loader filled (gamedata.h), and the only
 * storage this file allocates is the one offscreen page a pass draws into and
 * frees again before it ends.
 */
#include <stdlib.h>
#include <conio.h>
#include "fdpstype.h"
#include "gamedata.h"
#include "blit.h"
#include "cdaudio.h"
#include "keybd.h"
#include "palcycle.h"
#include "sprite.h"
#include "table.h"
#include "text.h"
#include "audio.h"
#include "vilmenu.h"

/* The grid's offscreen page: 312 bytes to a row, 76 rows, and the 0x5ca0 a
   pass mallocs is exactly that product.  It is rebuilt from nothing every pass
   and freed at the end of it, so no state carries in the pixels. */
#define GRID_PAGE_PITCH 0x138
#define GRID_PAGE_BYTES 0x5ca0

/* The window as it reaches the visible screen: 304 by 67 read from page offset
   0x3ad, which is page (5, 3), and written at 0xa9c48, which is screen
   (8, 125) on the mode 13h framebuffer.  0xa0000 stays a literal because it is
   where the adapter answers and not the address of anything the linker places,
   and the page offsets are literals for the same reason -- they are positions
   inside a block this function allocated. */
#define VGA_SCREEN_BASE 0x000a0000
#define VGA_SCREEN_PITCH 0x140
#define GRID_WINDOW_PAGE_AT 0x3ad
#define GRID_WINDOW_SCREEN_AT 0x9c48
#define GRID_WINDOW_W 0x130
#define GRID_WINDOW_H 0x43

#define VGA_INPUT_STATUS_1 0x3da
#define VGA_STATUS_VERTICAL_RETRACE 0x08

/* The six cells: three columns 0x65 apart starting at x 0x0b, two rows 0x1b
   apart starting at y 0x09, filled left to right and top to bottom.  Six is
   also the distance the window top keeps from the cursor, and three -- one
   row -- is the step both the window and the vertical cursor moves take.

   The column and the row come out of ONE signed division of the cell number by
   three, remainder for the column and quotient for the row (MOV EBX,0x3 / SAR
   EDX,0x1f / IDIV EBX twice, at 000338b4 and 000338cd). */
#define GRID_VISIBLE_CELLS 6
#define GRID_COLUMNS 3
#define GRID_ROW_STEP 3
#define GRID_CELL_X_BASE 0x0b
#define GRID_CELL_X_STRIDE 0x65
#define GRID_CELL_Y_BASE 0x09
#define GRID_CELL_Y_STRIDE 0x1b

/* Sprite 0 of ShopWin.Cel is the window frame and sprite 2 of SelBar.Cel is
   the highlight behind the entry the cursor stands on, hung six pixels left of
   the cell and five below its top so the icon sits inside it.  Both go through
   fdps_cel_blit_sprite in its plain opaque mode with no operand. */
#define GRID_WINDOW_SPRITE 0
#define GRID_HIGHLIGHT_SPRITE 2
#define HIGHLIGHT_X_LIFT 6
#define HIGHLIGHT_Y_DROP 5
#define GRID_BLIT_OPERAND 0
#define GRID_BLIT_MODE 0

/* Command.cel sprites 0x2a and 0x2b are the left and right halves of the stand
   the walking icon is posed on, laid 0x10 scanlines below the cell top with
   their halves 0x17 apart and the pair starting two pixels left of the cell.
   fdps_blit_command_sprite puts a fixed 25 by 22 block down with no clipping
   (sprite.h). */
#define STAND_LEFT_SPRITE 0x2a
#define STAND_RIGHT_SPRITE 0x2b
#define STAND_X_LIFT 2
#define STAND_RIGHT_X_OFFSET 0x17
#define STAND_Y_DROP 0x10

/* The walking icon.  The sprite cache holds twelve stream offsets per member,
   four facings of three walk frames, and ITS offset table starts at the base
   rather than at a .CEL file's +0x0f -- it is a table
   fdps_cache_cel_sprite_group builds, not a loaded sheet (mapdraw.c).  The
   slot is the ROSTER INDEX here -- IMUL EAX,dword ptr [EBP-0xc],0xc at
   0003398b -- and NOT the record's own sprite_cache_slot, which is what the
   battle map indexes by.  It lines up because
   fdps_load_field_chapter_resources clears the cache and then caches one group
   per roster member in roster order (00031540), so slot i is member i.

   The frame is (tick / 6) & 3 with the value 3 folded back onto 1, which rocks
   the icon 0, 1, 2, 1 instead of jumping from 2 to 0.  The divide is DIV and
   not IDIV (MOV EBX,0x6 / XOR EDX,EDX / DIV EBX at 0003396a), which is why
   data_fdps_timer_tick_counter is unsigned at its declaration. */
#define MEMBER_SPRITES_PER_CACHE_SLOT 0x0c
#define CEL_SUB_IMAGE_ENTRY_BYTES 4
#define WALK_FRAME_TICKS 6
#define WALK_FRAME_MASK 3
#define WALK_FRAME_FOLD_FROM 3
#define WALK_FRAME_FOLD_TO 1
#define MEMBER_ICON_W 0x18
#define MEMBER_ICON_H 0x18

/* The three dwords of a mode-9 blend descriptor (rleblend.h): the shade ramp
   base, the blend level and the inverse colour cube base.  They are one block
   because the address of the first is what is handed to the kernel, so they
   have to be adjacent and in this order. */
#define BLEND_DESCRIPTOR_RAMP 0
#define BLEND_DESCRIPTOR_LEVEL 1
#define BLEND_DESCRIPTOR_CUBE 2
#define BLEND_DESCRIPTOR_DWORDS 3
#define GHOST_BLEND_LEVEL 10
#define BLIT_MODE_TRANSLUCENT 9

/* The member's name: message entry char_id + 1 of the resident Fdetxt00.txt
   table, drawn 0x1c pixels right of the cell and six scanlines down, in the
   standard glyph colour 0xd0 over no background with outline 0x6d. */
#define NAME_X_OFFSET 0x1c
#define NAME_Y_OFFSET 6
#define NAME_TEXT_ID_BIAS 1
#define NAME_FG_COLOR 0xd0
#define NAME_BG_COLOR 0
#define NAME_OUTLINE_COLOR 0x6d

/* The two scroll arrows: Command.cel sprites 0x44/0x45 for up and 0x46/0x47
   for down, the second of each pair being the lit frame.  Which one is drawn
   is (tick / 5) & 1, so each frame of the blink lasts five timer ticks.

   THE DOWN ARROW IS DRAWN OFF THE END OF THE PAGE.  fdps_blit_command_sprite
   puts a fixed 25 by 22 block down with no clipping (sprite.h) and page offset
   0x4e96 is row 64 of the 76 the page has, so its last ten rows run past the
   0x5ca0 block.  It stays where it is: the screen window only ever shows its
   top six rows, so moving it to fit the page changes the picture
   (rebuild_info/pitfalls.md). */
#define ARROW_BLINK_TICKS 5
#define ARROW_BLINK_MASK 1
#define ARROW_UP_SPRITE 0x44
#define ARROW_DOWN_SPRITE 0x46
#define ARROW_UP_PAGE_AT 0x6ae
#define ARROW_DOWN_PAGE_AT 0x4e96

/* The make codes the grid acts on.  Everything else, the filter's 0xff "no key
   this poll" included, falls through the chain and only costs a frame. */
#define KEY_ESCAPE 0x01
#define KEY_ENTER 0x1c
#define KEY_SPACE 0x39
#define KEY_UP 0x48
#define KEY_LEFT 0x4b
#define KEY_RIGHT 0x4d
#define KEY_DELETE 0x53
#define KEY_DOWN 0x50

/* What the loop's own result slot holds.  The epilogue hands -1 back for
   anything that is not a confirmation, which is the same -1 a cancel put
   here. */
#define GRID_RUNNING 0
#define GRID_CANCELLED (-1)
#define GRID_CONFIRMED 1

/* 法蓮娜 leaves the party at chapter index 0x17, and from there on her entry
   can be stood on but not confirmed, and is drawn ghosted.  THE TWO ARE
   SEPARATE TESTS AND HAVE TO STAY SEPARATE: the refusal is on the CURSOR being
   roster slot 3 (CMP dword ptr [0x000601bc],0x3 at 00033727) and the ghosting
   is on the drawn record's CHARACTER ID being 1 (MOV AL,byte ptr [EAX + 0x8]
   at 000339bd).  Folding them into one condition ties the refusal to whoever
   happens to occupy slot 3 and the ghosting to whoever the cursor is on
   (rebuild_info/pitfalls.md). */
#define MEMBER_LOCKED_FROM_CHAPTER 0x17
#define MEMBER_LOCKED_ROSTER_SLOT 3
#define MEMBER_LOCKED_CHAR_ID 1

/* The sound every accepted cursor move plays.  This function pushes the copy
   of the string at 0x61f4c -- MOV EAX,0x61f4c ahead of all four calls -- which
   is the same literal village.c names. */
#define GRID_MOVE_SFX "Beep.wav"

/* 000336d0.  No arguments and caller-cleaned: all six call sites -- 00032c7f,
   00033fa8, 0003403e, 0003424e, 00034373 and 0003445e -- push nothing, adjust
   ESP by nothing afterwards and take the answer out of EAX.  The prologue is
   the ordinary PUSH EBX/ESI/EDI/EBP, MOV EBP,ESP, SUB ESP,0x3c and the body
   reads no slot above EBP.

   NEITHER THE CURSOR NOR THE WINDOW IS SEEDED HERE.  Both are the file globals
   in vilmenu.h and nothing in the image resets them, not even against the
   current member count, so the grid reopens exactly where the last visit left
   it.  Making them locals seeded to 0 loses that (rebuild_info/pitfalls.md).

   THE KEY CHAIN IS SHORT-CIRCUIT AND ITS ORDER IS THE ASSEMBLY'S: cancel,
   confirm, right, left, up, down.  Each arrow arm is one `if (code == k &&
   bound)` whose failure falls into the NEXT code's test -- JNZ and the failed
   bound both land on the same address.  No two codes can be equal so the
   ordering is not observable, but the guards are: a blocked move plays no
   sound and costs nothing but a frame.

   THE VERTICAL MOVES ARE NOT CLAMPED AGAINST THE ROW, ONLY AGAINST THE LIST.
   Up needs a cursor of 3 or more and Down needs three more entries to exist,
   so in a party whose size is not a multiple of three the last entries are
   reachable by Right from the one before them but not by Down from the row
   above.

   THE RECORD IS RESOLVED BEFORE THE CELL IS TESTED AGAINST THE MEMBER COUNT.
   fdps_get_roster_record is called for all six cells, including ones past the
   end of the party (CALL 00023950 at 0003389a sits ahead of the CMP at
   000338a8), so an out-of-range slot forms a pointer that is then never
   dereferenced.  It is address arithmetic on a global and reads nothing
   (table.h), so it is harmless; moving the call inside the test is a different
   program that happens to behave the same, and leaving it where the assembly
   has it costs nothing.

   THE FRAME IS DRAWN AFTER THE KEY IS HANDLED AND BEFORE THE LOOP TEST, so the
   pass that reads a cancel or a confirm still builds, presents and paces a
   whole frame before the function returns.

   THE PACE LATCH IS DELIBERATELY NOT INITIALISED.  Every pass ends by spinning
   until data_fdps_timer_tick_counter differs from `last_tick` and then
   re-latching it, and `last_tick` is never seeded, so the first pass compares
   stack garbage and normally falls straight through the wait.  Seeding it adds
   a tick to the opening of every grid (rebuild_info/pitfalls.md).
   data_fdps_timer_tick_counter is volatile at its declaration (gamedata.h)
   because of that wait: nothing inside the loop writes the counter, so a build
   allowed to hoist the load would spin here forever.

   The values used after a CALL are three.  fdps_read_scancode_auto_repeat's
   EAX is the scancode and goes to [EBP-0x14] at 000336f7.  malloc's EAX is the
   page, MOV dword ptr [EBP-0x24],EAX at 0003384d, used unchecked -- there is
   no test for NULL anywhere -- and freed at the end of the pass.
   fdps_get_roster_record's EAX is the member record, MOV dword ptr
   [EBP-0x2c],EAX at 000338a2, and only its char_id byte at +8 is ever read.
   fdps_draw_text answers a pen position that this caller drops (ADD ESP,0x1c
   at 00033a75 with no use of EAX), and the drawing, CD and palette calls
   return nothing this function looks at. */
int fdps_village_select_member(void)
{
    /* The answer, filled in once the loop has stopped. */
    int selected_member_index;
    /* Whether the loop is still running, and if not, how it stopped. */
    int loop_result;
    /* One poll of the auto-repeat filter: a make code, or 0xff for nothing. */
    unsigned int scancode;
    /* The pass's offscreen page. */
    unsigned char *page;
    /* Which of the six visible cells is being drawn, 0 to 5. */
    int cell_index;
    /* Which roster entry that cell is showing. */
    int entry_index;
    /* That entry's record, resolved for every cell whether or not it is in
       the party. */
    struct fdps_unit_record *member;
    /* The cell's top left corner in the page. */
    int cell_x;
    int cell_y;
    /* 0, 1, 2 or 1: which frame of the member's walk cycle this pass shows. */
    int walk_frame;
    /* That frame's entry in the sprite cache, and the RLE stream it names. */
    int sprite_index;
    unsigned char *icon_stream;
    /* The mode-9 descriptor a departed member's ghosted icon is drawn
       through, rebuilt on every cell that needs it. */
    int ghost_descriptor[BLEND_DESCRIPTOR_DWORDS];
    /* 0 or 1: which frame of the two-frame scroll arrows this pass shows. */
    int arrow_blink_phase;
    /* The tick the previous pass finished on.  Deliberately not initialised --
       see the note above. */
    unsigned int last_tick;

    loop_result = GRID_RUNNING;

    while (loop_result == GRID_RUNNING) {
        fdps_cd_music_repeat_poll();
        scancode = fdps_read_scancode_auto_repeat();

        if (scancode == KEY_ESCAPE || scancode == KEY_DELETE) {
            loop_result = GRID_CANCELLED;
        } else if (scancode == KEY_ENTER || scancode == KEY_SPACE) {
            if (data_fdps_chapter_current_chapter_id
                    < MEMBER_LOCKED_FROM_CHAPTER
                || data_fdps_village_member_select_cursor_idx
                   != MEMBER_LOCKED_ROSTER_SLOT) {
                loop_result = GRID_CONFIRMED;
            }
        } else if (scancode == KEY_RIGHT
                   && data_fdps_village_member_select_cursor_idx
                      < data_fdps_roster_member_count - 1) {
            data_fdps_village_member_select_cursor_idx++;
            if (data_fdps_village_member_grid_scroll_offset
                + GRID_VISIBLE_CELLS
                <= data_fdps_village_member_select_cursor_idx) {
                data_fdps_village_member_grid_scroll_offset += GRID_ROW_STEP;
            }
            fdps_play_sfx(GRID_MOVE_SFX);
        } else if (scancode == KEY_LEFT
                   && data_fdps_village_member_select_cursor_idx > 0) {
            data_fdps_village_member_select_cursor_idx--;
            if (data_fdps_village_member_select_cursor_idx
                < data_fdps_village_member_grid_scroll_offset) {
                data_fdps_village_member_grid_scroll_offset -= GRID_ROW_STEP;
            }
            fdps_play_sfx(GRID_MOVE_SFX);
        } else if (scancode == KEY_UP
                   && data_fdps_village_member_select_cursor_idx
                      > GRID_ROW_STEP - 1) {
            data_fdps_village_member_select_cursor_idx -= GRID_ROW_STEP;
            if (data_fdps_village_member_select_cursor_idx
                < data_fdps_village_member_grid_scroll_offset) {
                data_fdps_village_member_grid_scroll_offset -= GRID_ROW_STEP;
            }
            fdps_play_sfx(GRID_MOVE_SFX);
        } else if (scancode == KEY_DOWN
                   && data_fdps_village_member_select_cursor_idx
                      < data_fdps_roster_member_count - GRID_ROW_STEP) {
            data_fdps_village_member_select_cursor_idx += GRID_ROW_STEP;
            if (data_fdps_village_member_grid_scroll_offset
                + GRID_VISIBLE_CELLS
                <= data_fdps_village_member_select_cursor_idx) {
                data_fdps_village_member_grid_scroll_offset += GRID_ROW_STEP;
            }
            fdps_play_sfx(GRID_MOVE_SFX);
        }

        page = (unsigned char *) malloc(GRID_PAGE_BYTES);
        fdps_cel_blit_sprite(data_fdps_village_window_sheet_ptr,
                             GRID_WINDOW_SPRITE, page, GRID_PAGE_PITCH,
                             0, 0, GRID_BLIT_OPERAND, GRID_BLIT_MODE);

        for (cell_index = 0; cell_index < GRID_VISIBLE_CELLS; cell_index++) {
            entry_index = data_fdps_village_member_grid_scroll_offset
                          + cell_index;
            member = fdps_get_roster_record(entry_index);

            if (entry_index < data_fdps_roster_member_count) {
                cell_x = (cell_index % GRID_COLUMNS) * GRID_CELL_X_STRIDE
                         + GRID_CELL_X_BASE;
                cell_y = (cell_index / GRID_COLUMNS) * GRID_CELL_Y_STRIDE
                         + GRID_CELL_Y_BASE;

                if (entry_index
                    == data_fdps_village_member_select_cursor_idx) {
                    fdps_cel_blit_sprite(data_fdps_selection_bar_sheet_ptr,
                                         GRID_HIGHLIGHT_SPRITE, page,
                                         GRID_PAGE_PITCH,
                                         cell_x - HIGHLIGHT_X_LIFT,
                                         cell_y + HIGHLIGHT_Y_DROP,
                                         GRID_BLIT_OPERAND, GRID_BLIT_MODE);
                }

                fdps_blit_command_sprite(page
                                             + (cell_y + STAND_Y_DROP)
                                               * GRID_PAGE_PITCH
                                             + cell_x - STAND_X_LIFT,
                                         GRID_PAGE_PITCH, STAND_LEFT_SPRITE);
                fdps_blit_command_sprite(page
                                             + (cell_y + STAND_Y_DROP)
                                               * GRID_PAGE_PITCH
                                             + cell_x + STAND_RIGHT_X_OFFSET,
                                         GRID_PAGE_PITCH, STAND_RIGHT_SPRITE);

                walk_frame = (int) ((data_fdps_timer_tick_counter
                                     / WALK_FRAME_TICKS) & WALK_FRAME_MASK);
                if (walk_frame == WALK_FRAME_FOLD_FROM) {
                    walk_frame = WALK_FRAME_FOLD_TO;
                }
                sprite_index = entry_index * MEMBER_SPRITES_PER_CACHE_SLOT
                               + walk_frame;
                icon_stream = data_fdps_cel_sprite_cache_ptr
                    + *(int *) (data_fdps_cel_sprite_cache_ptr
                                + sprite_index * CEL_SUB_IMAGE_ENTRY_BYTES);

                if (data_fdps_chapter_current_chapter_id
                        < MEMBER_LOCKED_FROM_CHAPTER
                    || member->char_id != MEMBER_LOCKED_CHAR_ID) {
                    fdps_blit_dispatch(icon_stream,
                                       page + cell_y * GRID_PAGE_PITCH
                                           + cell_x,
                                       MEMBER_ICON_W, MEMBER_ICON_H,
                                       GRID_PAGE_PITCH, GRID_BLIT_OPERAND,
                                       GRID_BLIT_MODE);
                } else {
                    ghost_descriptor[BLEND_DESCRIPTOR_RAMP] =
                        (int) data_fdps_palette_shade_ramp_table;
                    ghost_descriptor[BLEND_DESCRIPTOR_LEVEL] =
                        GHOST_BLEND_LEVEL;
                    ghost_descriptor[BLEND_DESCRIPTOR_CUBE] =
                        (int) data_fdps_inverse_palette_cube;
                    fdps_blit_dispatch(icon_stream,
                                       page + cell_y * GRID_PAGE_PITCH
                                           + cell_x,
                                       MEMBER_ICON_W, MEMBER_ICON_H,
                                       GRID_PAGE_PITCH,
                                       (unsigned int) ghost_descriptor,
                                       BLIT_MODE_TRANSLUCENT);
                }

                fdps_draw_text(data_fdps_all_game_text_ptr,
                               member->char_id + NAME_TEXT_ID_BIAS,
                               page + (cell_y + NAME_Y_OFFSET)
                                   * GRID_PAGE_PITCH
                                   + cell_x + NAME_X_OFFSET,
                               GRID_PAGE_PITCH, NAME_FG_COLOR, NAME_BG_COLOR,
                               NAME_OUTLINE_COLOR);
            }
        }

        arrow_blink_phase = (int) ((data_fdps_timer_tick_counter
                                    / ARROW_BLINK_TICKS) & ARROW_BLINK_MASK);

        if (data_fdps_village_member_grid_scroll_offset != 0) {
            fdps_blit_command_sprite(page + ARROW_UP_PAGE_AT, GRID_PAGE_PITCH,
                                     arrow_blink_phase + ARROW_UP_SPRITE);
        }

        if (data_fdps_village_member_grid_scroll_offset + GRID_VISIBLE_CELLS
            < data_fdps_roster_member_count) {
            fdps_blit_command_sprite(page + ARROW_DOWN_PAGE_AT,
                                     GRID_PAGE_PITCH,
                                     arrow_blink_phase + ARROW_DOWN_SPRITE);
        }

        fdps_cycle_ui_palette();
        while ((inp(VGA_INPUT_STATUS_1) & VGA_STATUS_VERTICAL_RETRACE) == 0) {
            /* Spin until the retrace begins, so the frame that has just been
               composed is the one the monitor shows whole. */
        }
        while ((inp(VGA_INPUT_STATUS_1) & VGA_STATUS_VERTICAL_RETRACE) != 0) {
            /* And until it ends, so the present starts clear of it. */
        }
        fdps_blit_rect((unsigned int) (page + GRID_WINDOW_PAGE_AT),
                       GRID_PAGE_PITCH,
                       (void *) (VGA_SCREEN_BASE + GRID_WINDOW_SCREEN_AT),
                       VGA_SCREEN_PITCH, GRID_WINDOW_W, GRID_WINDOW_H);
        free(page);

        while (last_tick == data_fdps_timer_tick_counter) {
        }
        last_tick = data_fdps_timer_tick_counter;
    }

    if (loop_result > GRID_RUNNING) {
        selected_member_index = data_fdps_village_member_select_cursor_idx;
    } else {
        selected_member_index = GRID_CANCELLED;
    }

    return selected_member_index;
}
