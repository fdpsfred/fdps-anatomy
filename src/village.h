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

#endif
