/* keybd.h -- keyboard input: the game's own INT 09h handler, the scancode byte
 * it latches, and the readers that drain the queue behind it.
 *
 * The handler at 00056837 owns two independent pieces of state.  One is a ring
 * of make codes that fdps_read_keyboard_queue drains; the other is the single
 * byte declared below, which every key event overwrites -- break codes
 * included -- and which the input loops poll directly rather than through the
 * queue.  0xff in that byte is the no-key-pending value, not a scancode.
 */
#ifndef KEYBD_H
#define KEYBD_H

/* The one byte the game's INT 09h handler latches on every key event.
 *
 * fdps_keyboard_isr stores into it the raw byte it read from port 0x60
 * (MOV byte ptr [0x00070006],BL at 00056851), unconditionally and before any
 * filtering, so it holds break codes -- 0x80 and above -- as well as make
 * codes.  fdps_uninstall_keyboard_isr sets it to 0xff when it puts the
 * original INT 09h vector back, and callers store 0xff through it themselves
 * to throw a stale key away before entering an input loop.
 *
 * UNSIGNED, and that is behaviour rather than spelling: the ISR's own
 * make-code test is CMP BL,0x80 / JNC, an unsigned compare, and every reader
 * widens the byte with AND EAX,0xff.  Watcom's plain `char` is signed, which
 * would make 0xff and every break code negative and turn that widening into a
 * sign extension.  See rebuild_info/pitfalls.md.
 *
 * Defined by ticket 23 along with the rest of keybd.c's data; until then the
 * build supplies it zero-filled. */
extern unsigned char data_fdps_input_last_scancode;

/* Hands back the address of that latched scancode byte, so an input loop can
   both read the current key and clear it.

   The whole body is LEA EAX,[0x70006] / RET: no arguments, no state of its
   own, nothing read and nothing written.  The address is always the same one,
   so the returned pointer is never null and never varies between calls.

   *p is the last scancode the ISR saw, or 0xff when no key is pending.  All
   fifteen call sites use the result as a byte pointer immediately: seven read
   through it (MOV AL,byte ptr [EAX] / AND EAX,0xff, so the byte widens
   unsigned), and two -- fdps_battle_player_phase_loop at 0002bb13 and
   fdps_map_cursor_select_loop at 0002b5c8 -- write MOV byte ptr [EAX],0xff
   through it to discard a stale key before their loops begin. */
extern unsigned char *fdps_keyboard_scancode_ptr(void);
#pragma aux fdps_keyboard_scancode_ptr "*" parm caller [];

#endif
