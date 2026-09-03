/* text.c -- drawing text and numbers, and the 1bpp glyph blit underneath them.
 *
 * See text.h for the glyph bitmap layout and the destination surface contract.
 * Nothing here owns state: the cell size, the sheet's glyph stride and the
 * decoration style and its offsets all come from the font globals gamedata.c
 * owns, and everything else arrives as an argument.
 */
#include <stdio.h>
#include <string.h>

#include "blit.h"
#include "gamedata.h"
#include "text.h"

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
