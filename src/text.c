/* text.c -- drawing text and numbers, and the 1bpp glyph blit underneath them.
 *
 * See text.h for the glyph bitmap layout and the destination surface contract.
 * Nothing here owns state: the cell size, the sheet's glyph stride, the
 * decoration style and its offsets, the pen advances and the text blocks
 * themselves all come from globals gamedata.c owns, and everything else arrives
 * as an argument.
 *
 * sprintf comes from <stdio.h>, strlen strcpy and strcat from <string.h>,
 * malloc and free from <stdlib.h> and inp from <conio.h>, which is where Watcom
 * 10.0a declares each of them; all of them are ordinary library calls in the
 * original (CALL 00042d41, CALL 00042dd2, CALL 0003d375, CALL 0003d478 and
 * CALL 0003d4e4) and none is expanded inline.
 */
#include <conio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "blit.h"
#include "fdpstype.h"
#include "gamedata.h"
#include "keybd.h"
#include "mapdraw.h"
#include "msgwin.h"
#include "sprite.h"
#include "text.h"
#include "unit.h"

/* 0001fd80.  Seven stack arguments, caller-cleaned: both call sites in
   fdps_draw_text (00020222 and 0002038e) push seven dwords right to left and
   follow the CALL with ADD ESP,0x1c, and the function itself reads them at
   [EBP+0x14] through [EBP+0x2c] behind PUSH EBX/ESI/EDI/EBP and the return
   address.  RET carries no immediate.  Nothing reads EAX afterwards.

   The three colour arguments are palette indices, and each one's zero means
   something different -- which is the whole shape of the function and the one
   place a tidier rewrite goes wrong:

     bg_color == 0     JZ at 0001fd9d skips the cell fill.
     fg_color == 0     JZ at 0001fea0 skips the glyph body.
     outline_color == 0  JZ at 0001fe6b skips the DROP SHADOW only.  The four
                       outline blits at 0001fdfd-0001fe62 sit on the other side
                       of that branch and are not guarded by anything, so with
                       the outline style selected a zero here paints four
                       palette-index-0 copies of the glyph rather than nothing.

   The decoration style is chosen by CMP byte ptr [0x0006404a],0x0 / JZ, so the
   outline and the shadow are alternatives and never both.  The outline's four
   blits go down, left, up and right in that order; each one recomputes its
   address from dst, so they are 4-connected neighbours of the cell and not a
   walk.

   The glyph's bitmap address is computed once, into the font_base argument's
   own slot (ADD dword ptr [EBP+0x1c],EAX at 0001fd96), and all five blits are
   handed that same pointer.  That is safe because fdps_blit_glyph_1bpp steps
   only its own copy -- see its note below -- and it is what makes the outline
   four copies of one glyph rather than four consecutive glyphs.

   The cell dimensions the fill walks come from the font globals and not from
   the caller or the glyph, and they are read as zero-extended bytes (XOR
   EAX,EAX / MOV AL): a signed char would make a 200-row cell negative and the
   fill would never run.  The fill stores MOV AL,byte ptr [EBP+0x28] -- the low
   byte of bg_color and nothing wider. */
void fdps_draw_glyph(unsigned char *dst, int pitch, unsigned char *font_base,
                     int glyph_index, int fg_color, int bg_color,
                     int outline_color)
{
    unsigned char *glyph_bits;
    unsigned char *cell_row;
    unsigned char *shadow_dst;
    int row;
    int column;

    glyph_bits = font_base + glyph_index * data_fdps_font_glyph_stride_bytes;

    if (bg_color != 0) {
        cell_row = dst;
        for (row = 0; row < (int) data_fdps_glyph_cell_height; row++) {
            for (column = 0; column < (int) data_fdps_font_glyph_width;
                 column++) {
                cell_row[column] = (unsigned char) bg_color;
            }
            cell_row += pitch;
        }
    }

    if (data_fdps_font_outline_enabled_flag != 0) {
        fdps_blit_glyph_1bpp(dst + pitch, pitch, glyph_bits, outline_color);
        fdps_blit_glyph_1bpp(dst - 1, pitch, glyph_bits, outline_color);
        fdps_blit_glyph_1bpp(dst - pitch, pitch, glyph_bits, outline_color);
        fdps_blit_glyph_1bpp(dst + 1, pitch, glyph_bits, outline_color);
    } else if (outline_color != 0) {
        shadow_dst = dst + data_fdps_glyph_shadow_row_offset * pitch
                     + data_fdps_font_shadow_offset_x;
        fdps_blit_glyph_1bpp(shadow_dst, pitch, glyph_bits, outline_color);
    }

    if (fg_color != 0) {
        fdps_blit_glyph_1bpp(dst, pitch, glyph_bits, fg_color);
    }
}

/* 0001fed0.  Four stack arguments -- PUSH EAX x4 at the call sites in
   fdps_draw_glyph, then ADD ESP,0x10 after each CALL -- read at [EBP+0x14]
   through [EBP+0x20] behind the four pushed registers and the return address.
   Nothing reads EAX afterwards; the function returns nothing.

   Both loop bounds are re-read from their globals on every iteration (XOR
   EAX,EAX / MOV AL,[0x0006403d] inside the loop head at 0001fee3, and the same
   shape for the width at 0001ff00), so the C spells them in the condition
   rather than latching them into locals.  The compare is CMP EAX,[EBP-0x8] /
   JG, the signed one, on a byte that was zero-extended: 0..255 either way, so
   the signedness costs nothing here, but the read has to stay a widening of an
   unsigned char and not a sign-extension of a char, or a cell height of 200
   becomes -56 and nothing is drawn at all.

   The bit window is a full dword slot (SHL dword ptr [EBP-0x4],0x1 and TEST
   dword ptr [EBP-0x4],0x80), not the byte the decompiler infers, and it is
   deliberately not initialised: column 0 of every row satisfies (column & 7)
   == 0, so the fetch branch always runs before the shift branch can.

   The fetch is MOV EDX,[EBP+0x1c] / INC dword ptr [EBP+0x1c] / MOV AL,[EDX]:
   the byte comes from the pointer's value before the step, and the pointer
   that steps is the callee's own copy of the argument.  fdps_draw_glyph hands
   the same glyph_bits to five calls in a row and depends on that.

   The fetch condition being true again at column 0 makes source rows
   byte-aligned: a 12-pixel-wide glyph consumes two bytes per row and the last
   four bits of the second byte are never drawn.

   A clear bit writes nothing at all -- the JZ at 0001ff38 skips the store
   rather than storing a background colour -- so the destination shows through
   between the strokes.  Filling the cell first would look equivalent on a
   cleared screen and is not: text here is drawn over the window artwork. */
void fdps_blit_glyph_1bpp(unsigned char *dst, int pitch,
                          unsigned char *glyph_bits, int color)
{
    unsigned int glyph_bit_window;
    int row;
    int column;

    for (row = 0; row < (int) data_fdps_glyph_cell_height; row++) {
        for (column = 0; column < (int) data_fdps_font_glyph_width; column++) {
            if ((column & 7) == 0) {
                glyph_bit_window = (unsigned int) *glyph_bits;
                glyph_bits++;
            } else {
                glyph_bit_window <<= 1;
            }
            if ((glyph_bit_window & 0x80) != 0) {
                dst[column] = (unsigned char) color;
            }
        }
        dst += pitch;
    }
}

/* 00017530.  Five stack arguments, caller-cleaned: all forty-eight call sites
   push five dwords right to left and follow the CALL with ADD ESP,0x14, the
   body reads them at [EBP+0x14] through [EBP+0x24] behind PUSH EBX/ESI/EDI/EBP
   and the return address, and RET carries no immediate.  Nothing reads EAX
   afterwards, so it returns nothing.  show_plus is the one argument narrower
   than a dword: callers push a whole one but the body tests only its low byte,
   CMP byte ptr [EBP+0x24],0x0 at 000175e2.

   The formatting is two format strings and not one, which is the whole reason
   digit_count's zero is a separate branch (JZ at 00017551 to the "%d" call at
   000175cc).  "%.*d" with a precision of zero prints value 0 as nothing at all,
   and fdps_draw_cursor_info_panel calls with digit_count 0 -- so the tidy fold
   loses a digit on screen wherever a natural-width zero is drawn.

   The padded branch's format string is built per call: the template is a
   five-byte automatic array copied out of the literal at 00014854 (LEA EDI,
   [EBP-0x18] / MOV ESI,0x14854 / MOVSD / MOVSB, the inline expansion of the
   initialiser) and its '3' is then overwritten in place with '0' + digit_count,
   MOV AL,byte ptr [EBP+0x20] / ADD AL,0x30 / MOV [EBP-0x16],AL.  It has to be a
   copy: patching a string literal would rewrite the image's own constant.  Both
   character buffers are twenty bytes, which is what the widest thing the two
   formats can produce -- a nine-digit negative with a '+' that cannot reach it
   -- fits inside.

   The overflow guard exists only on the padded branch: limit is built as
   10^digit_count by repeated multiplication, and a value that reaches it is not
   formatted at all but replaced with digit_count '?' glyphs.  The compare is
   the signed JL at 0001757f, so a negative value never trips it however many
   digits it needs -- -12345 through a 3-digit field draws its first three
   characters and nothing guards that.

   The '+' is prepended by writing over the head of the buffer rather than by
   reformatting: the figure is copied aside, "+" is stored at the front, and the
   copy is appended back.  The store the compiler emits is a single word move
   out of the literal (MOV AX,[0x000615ac] / MOV [EBP-0x2c],AX at 00017600), an
   inline expansion of the two-byte copy; the copy-aside and the append are real
   CALLs to strcpy and strcat.  Which of the three the compiler chooses to
   expand is codegen and not behaviour (ADR-0001).

   strlen is called once per character rather than hoisted -- the CALL at
   00017625 is inside the loop's condition block, re-entered from the increment
   at 0001763d -- and the compare is JA, unsigned, which is why the index is
   unsigned here and the digit counters are not.

   The glyph mapping's default arm, anything that is not a digit or '+' or '-'
   or '?', draws sprite 0.  Nothing the two format strings can produce reaches
   it. */
void fdps_draw_number(unsigned char *dest, int pitch, int value,
                      int digit_count, char show_plus)
{
    char format_template[5] = "%.3d";
    char figure_text[20];
    char figure_text_copy[20];
    int overflow_limit;
    int digit_pos;
    unsigned int char_index;
    int glyph_index;
    unsigned char *glyph_stream;

    overflow_limit = 1;

    if (digit_count != 0) {
        for (digit_pos = 0; digit_pos < digit_count; digit_pos++) {
            overflow_limit = overflow_limit * 10;
        }

        if (value >= overflow_limit) {
            for (digit_pos = 0; digit_pos < digit_count; digit_pos++) {
                figure_text[digit_pos] = '?';
            }
            figure_text[digit_count] = '\0';
        } else {
            format_template[2] = (char) (digit_count + '0');
            sprintf(figure_text, format_template, value);
        }
    } else {
        sprintf(figure_text, "%d", value);
    }

    if (show_plus != 0 && value >= 0) {
        strcpy(figure_text_copy, figure_text);
        strcpy(figure_text, "+");
        strcat(figure_text, figure_text_copy);
    }

    for (char_index = 0; char_index < strlen(figure_text); char_index++) {
        glyph_index = (unsigned char) figure_text[char_index];

        if (glyph_index >= '0' && glyph_index <= '9') {
            glyph_index = glyph_index - '0';
        } else if (glyph_index == '-') {
            glyph_index = 11;
        } else if (glyph_index == '+') {
            glyph_index = 10;
        } else if (glyph_index == '?') {
            glyph_index = 12;
        } else {
            glyph_index = 0;
        }

        glyph_stream = data_fdps_number_glyph_sheet_ptr +
                       *(int *) (data_fdps_number_glyph_sheet_ptr +
                                 (data_fdps_number_glyph_color_row * 13 +
                                  glyph_index) * 4 + 0x0f);
        fdps_blit_dispatch(glyph_stream, dest + char_index * 6, 6, 8, pitch,
                           0, 0);
    }
}

/* The adapter's linear framebuffer and the visible mode 13h pitch.  The page
   break and both speaker codes put the pen at fixed addresses inside it -- PUSH
   0xa0504 at 00020059 and 000200e8, MOV dword ptr [EBP + 0x1c],0xaa44a at
   0002011f, 000202ca and 00020356, and the same store of 0xaa3d4 at 00020128 --
   and all of them stay literals here: 0xa0000 is where the display adapter
   answers, not the address of anything the linker places. */
#define VGA_SCREEN_BASE 0x000a0000
#define VGA_SCREEN_PITCH 0x140
#define VGA_INPUT_STATUS_1 0x3da
#define VGA_STATUS_VERTICAL_RETRACE 0x08

/* Where the message panel sits on the visible screen, and where the two text
   origins are: 0x504 is screen (4, 4), 0xa44a is (138, 131) and 0xa3d4 is
   (20, 131). */
#define PANEL_WINDOW_AT 0x504
#define MAP_TEXT_ORIGIN 0xa44a
#define VILLAGE_TEXT_ORIGIN 0xa3d4

/* The scratch canvas the page break composes in: 360 by 240, and the two spots
   inside it the panel and the portrait go at -- 0x21d8 is canvas (24, 24) and
   0x9ad0 is canvas (32, 110). */
#define PANEL_CANVAS_PITCH 0x168
#define PANEL_CANVAS_BYTES 0x15180
#define PANEL_WINDOW_IN_CANVAS_AT 0x21d8
#define PANEL_PORTRAIT_IN_CANVAS_AT 0x9ad0
#define PANEL_WINDOW_WIDTH 0x138
#define PANEL_WINDOW_ROWS 0xc0
#define PORTRAIT_WIDTH 0x7d
#define PORTRAIT_ROWS 0x64

/* The control codes.  Everything else a stream holds is a glyph index, zero
   included. */
#define TEXT_END (-1)
#define TEXT_LINE_BREAK (-2)
#define TEXT_PAGE_BREAK (-3)
#define TEXT_SUBST_1 (-4)
#define TEXT_SUBST_2 (-5)
#define TEXT_NUMBER (-6)
#define TEXT_SPEAKER_BY_CHAR_ID (-0x11)
#define TEXT_SPEAKER_BY_UNIT (-0x12)

/* The colours a substitution and a repainted panel are always drawn in,
   whatever the caller asked for. */
#define MESSAGE_FG_COLOR 0xd0
#define MESSAGE_BG_COLOR 0
#define MESSAGE_OUTLINE_COLOR 0x6d

/* How long the modal waits stand: fdps_message_window_wait_key(1, 0x64), the
   indicator shown and a hundred ticks of timeout. */
#define MESSAGE_WAIT_INDICATOR 1
#define MESSAGE_WAIT_TICKS 0x64

/* How far the number code steps between digits.  It is a literal, ADD dword
   ptr [EBP + -0x14],0x10 at 0002022a, and NOT data_fdps_glyph_advance_x: a
   figure inside a sentence is spaced by 0x10 whatever the font's own advance
   is. */
#define NUMBER_DIGIT_ADVANCE 0x10

/* 0001ff60.  Seven stack arguments, caller-cleaned, EAX back.  Every call site
   in the image pushes seven dwords right to left and follows the CALL with ADD
   ESP,0x1c -- the two recursive ones at 00020161 and 00020198 and the outside
   ones at 00024a82, 0001da66 and the rest -- the body reads the arguments at
   [EBP+0x14] through [EBP+0x2c] behind PUSH EBX/ESI/EDI/EBP and the return
   address, and RET carries no immediate.  The return is EAX: the recursive call
   sites store it straight into the pen with MOV dword ptr [EBP + -0x14],EAX.

   dest IS A PARAMETER AND THE BODY WRITES TO IT.  The page break and both
   speaker codes store a fixed screen address into [EBP + 0x1c] and the line
   break then measures from that new value.  Keeping the caller's origin in a
   separate local and leaving the parameter alone compiles and looks tidier and
   drops every line after a page break or a speaker change back at the caller's
   origin instead of at the panel's.

   The entry lookup is byte arithmetic on a signed word: MOV EAX,[EBP+0x18] /
   ADD EAX,EAX / ADD EAX,[EBP+0x14] / MOVSX EAX,word ptr [EAX] / ADD
   [EBP+0x14],EAX.  The offset is added to the TABLE BASE and not to the address
   it was read from, and it is sign-extended, so an entry may sit before the
   table.

   The loop condition re-reads the word rather than testing the token slot (MOV
   EAX,[EBP+0x14] / MOVSX / CMP EAX,-0x1 at 0001ff96, then the same read again
   into [EBP-0x2c] at 0001ffa5), and every arm ends in the same JMP back to it.

   The two per-code operands are read at [text_base + 2] and the stream is then
   stepped by four rather than two, so a speaker code and its argument are one
   two-word instruction.

   The number code's digit map is a bare subtraction, AND EAX,0xff / SUB
   EAX,0x30, with no range check anywhere: sprintf's '-' for a negative value
   comes out as glyph index -3 and reads six bytes in front of the font sheet.
   That is the original's behaviour and not a mistake in the transcription.

   The four uses of a value after a CALL, all checked against the assembly:
   malloc's EAX into the canvas slot [EBP-0x8] at 0001fffe; strlen's AL -- the
   low byte only -- into the digit count [EBP-0x4] at 000201da; inp's AL tested
   against 8 at 000200c4 and 000200d5; and
   fdps_battle_find_unit_by_character_id's EAX into [EBP-0x1c] at 00020274,
   which is compared against -1 AFTER the record it wrote through the out
   pointer has already been read.  Neither malloc's result nor
   fdps_get_unit_record's is tested for null before it is used. */
unsigned char *fdps_draw_text(unsigned char *text_base, int text_id,
                              unsigned char *dest, int pitch, int fg_color,
                              int bg_color, int outline_color)
{
    char number_text[12];
    int line_count;
    int token;
    int speaker_face_index;
    int digit_index;
    int line_height;
    int unit_index;
    int window_opened;
    unsigned char *cursor;
    struct fdps_unit_record *speaker_record;
    unsigned char *panel_canvas;
    unsigned char digit_count;

    line_count = 0;
    line_height = data_fdps_font_line_height;
    window_opened = 0;
    cursor = dest;
    text_base += *(short *) (text_base + text_id * 2);

    while (*(short *) text_base != TEXT_END) {
        token = *(short *) text_base;

        if (token == TEXT_LINE_BREAK) {
            /* The origin is dest and not cursor, and the step is the whole
               line count rather than one line, so a run of breaks steps a line
               each and a caller that chained onto a half-finished line still
               gets the next line at the left margin. */
            line_count++;
            cursor = dest + pitch * line_height * line_count;
            text_base += 2;
        } else if (token == TEXT_PAGE_BREAK) {
            fdps_flush_keyboard_queue();
            fdps_message_window_wait_key(MESSAGE_WAIT_INDICATOR,
                                         MESSAGE_WAIT_TICKS);
            panel_canvas = (unsigned char *) malloc(PANEL_CANVAS_BYTES);

            if (data_fdps_village_mode_flag == 0) {
                /* On the battle map the scene behind the panel is live and has
                   to be composited again; in the village the visible page
                   already holds a finished picture and is copied in. */
                fdps_draw_scene_layers(panel_canvas);
                fdps_cel_blit_sprite(data_fdps_message_window_sheet_ptr, 0,
                                     panel_canvas, PANEL_CANVAS_PITCH,
                                     0x1d, 0x8c, 0, 0);
            } else {
                fdps_blit_rect(
                    (unsigned int) (VGA_SCREEN_BASE + PANEL_WINDOW_AT),
                    VGA_SCREEN_PITCH,
                    panel_canvas + PANEL_WINDOW_IN_CANVAS_AT,
                    PANEL_CANVAS_PITCH, PANEL_WINDOW_WIDTH,
                    PANEL_WINDOW_ROWS);
                fdps_cel_blit_sprite(data_fdps_village_window_sheet_ptr, 0,
                                     panel_canvas, PANEL_CANVAS_PITCH,
                                     0x18, 0x8d, 0, 0);
            }

            if (data_fdps_portrait_sprite_buf_ptr != (unsigned char *) 0) {
                fdps_blit_dispatch(data_fdps_portrait_sprite_buf_ptr,
                                   panel_canvas + PANEL_PORTRAIT_IN_CANVAS_AT,
                                   PORTRAIT_WIDTH, PORTRAIT_ROWS,
                                   PANEL_CANVAS_PITCH, 0, 0);
            }

            while ((inp(VGA_INPUT_STATUS_1) & VGA_STATUS_VERTICAL_RETRACE)
                   == 0) {
                /* Spin until the retrace begins, so the present below starts
                   on a fresh one. */
            }
            while ((inp(VGA_INPUT_STATUS_1) & VGA_STATUS_VERTICAL_RETRACE)
                   != 0) {
                /* And until it ends, so the whole 312x192 window goes down
                   inside one frame. */
            }

            fdps_blit_rect(
                (unsigned int) (panel_canvas + PANEL_WINDOW_IN_CANVAS_AT),
                PANEL_CANVAS_PITCH,
                (void *) (VGA_SCREEN_BASE + PANEL_WINDOW_AT),
                VGA_SCREEN_PITCH, PANEL_WINDOW_WIDTH, PANEL_WINDOW_ROWS);
            free(panel_canvas);

            line_count = 0;
            if (data_fdps_village_mode_flag == 0) {
                dest = (unsigned char *) (VGA_SCREEN_BASE + MAP_TEXT_ORIGIN);
            } else {
                dest = (unsigned char *) (VGA_SCREEN_BASE
                                          + VILLAGE_TEXT_ORIGIN);
            }
            cursor = dest;
            text_base += 2;
        } else if (token == TEXT_SUBST_1) {
            /* The colours are the message colours and not the caller's, and
               the block is the global text and not the one being drawn. */
            cursor = fdps_draw_text(data_fdps_all_game_text_ptr,
                                    data_fdps_dialog_last_action_text_id_param,
                                    cursor, pitch, MESSAGE_FG_COLOR,
                                    MESSAGE_BG_COLOR, MESSAGE_OUTLINE_COLOR);
            text_base += 2;
        } else if (token == TEXT_SUBST_2) {
            cursor = fdps_draw_text(data_fdps_all_game_text_ptr,
                                    data_fdps_dialog_subst_text_id_2,
                                    cursor, pitch, MESSAGE_FG_COLOR,
                                    MESSAGE_BG_COLOR, MESSAGE_OUTLINE_COLOR);
            text_base += 2;
        } else if (token == TEXT_NUMBER) {
            sprintf(number_text, "%d",
                    data_fdps_dialog_last_action_value_param);
            digit_count = (unsigned char) strlen(number_text);
            for (digit_index = 0; digit_index < (int) digit_count;
                 digit_index++) {
                fdps_draw_glyph(cursor, pitch, data_fdps_font_sheet_ptr,
                                (int) (unsigned char)
                                    number_text[digit_index] - '0',
                                fg_color, bg_color, outline_color);
                cursor += NUMBER_DIGIT_ADVANCE;
            }
            text_base += 2;
        } else if (token == TEXT_SPEAKER_BY_CHAR_ID) {
            if (window_opened != 0) {
                fdps_message_window_wait_key(MESSAGE_WAIT_INDICATOR,
                                             MESSAGE_WAIT_TICKS);
                fdps_message_window_close();
            }
            /* The id out of the stream doubles as the fallback face index: it
               is only replaced when the lookup published a record, and it is
               what a character with no record on the map is drawn with. */
            speaker_face_index = (int) *(short *) (text_base + 2);
            unit_index = fdps_battle_find_unit_by_character_id(
                speaker_face_index, &speaker_record);
            if (speaker_record != (struct fdps_unit_record *) 0) {
                speaker_face_index = (int) speaker_record->portrait_id;
            }
            /* The record is read before the return value is looked at, which
               is why a retired match's record still counts: see
               fdps_battle_find_unit_by_character_id in unit.h. */
            if (unit_index == -1) {
                fdps_message_window_open_from_tile(-1, 0, speaker_face_index);
            } else {
                fdps_message_window_open_from_tile(
                    (int) speaker_record->pos_x, (int) speaker_record->pos_y,
                    speaker_face_index);
            }
            line_count = 0;
            dest = (unsigned char *) (VGA_SCREEN_BASE + MAP_TEXT_ORIGIN);
            cursor = dest;
            text_base += 4;
            window_opened = 1;
        } else if (token == TEXT_SPEAKER_BY_UNIT) {
            if (window_opened != 0) {
                fdps_message_window_wait_key(MESSAGE_WAIT_INDICATOR,
                                             MESSAGE_WAIT_TICKS);
                fdps_message_window_close();
            }
            /* The unit index goes straight to the array accessor, so this arm
               has no fallback and no null test: the portrait always comes out
               of the record. */
            unit_index = (int) *(short *) (text_base + 2);
            speaker_record = fdps_get_unit_record(unit_index);
            speaker_face_index = (int) speaker_record->portrait_id;
            fdps_message_window_open_from_tile(
                (int) speaker_record->pos_x, (int) speaker_record->pos_y,
                speaker_face_index);
            line_count = 0;
            dest = (unsigned char *) (VGA_SCREEN_BASE + MAP_TEXT_ORIGIN);
            cursor = dest;
            text_base += 4;
            window_opened = 1;
        } else {
            fdps_draw_glyph(cursor, pitch, data_fdps_font_sheet_ptr, token,
                            fg_color, bg_color, outline_color);
            cursor += data_fdps_glyph_advance_x;
            text_base += 2;
        }
    }

    if (window_opened != 0) {
        fdps_message_window_wait_key(MESSAGE_WAIT_INDICATOR,
                                     MESSAGE_WAIT_TICKS);
        fdps_message_window_close();
    }

    return cursor;
}
