/* village.c -- the village phase: the between-battle town screen.
 *
 * See village.h for what the phase covers.  This file holds the signboard
 * menu's hidden entry: the per-chapter secret-shop unlock code and the state
 * machine that matches one keystroke at a time against it.
 */
#include "gamedata.h"
#include "village.h"

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
