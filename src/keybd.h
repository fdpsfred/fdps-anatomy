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

/* 00070019.  The scancode ring's read index: the entry fdps_read_keyboard_queue
 * takes the next make code from, counted in entries rather than bytes and
 * always in 0..9.  Its only writer is fdps_read_keyboard_queue, which bumps it
 * past the entry it just took and wraps it at ten (INC dword ptr [0x00070019] /
 * CMP dword ptr [0x00070019],0xa / MOV dword ptr [0x00070019],0x0 at 000567d5,
 * 000567db and 000567e4).  The queue is empty when this equals the write index.
 *
 * NOT volatile, and that is a measurement rather than an omission: a sweep of
 * the whole image for 0x00070019 finds six instructions, all of them in
 * fdps_wait_any_key, fdps_flush_keyboard_queue and fdps_read_keyboard_queue.
 * The INT 09h handler never touches it, so nothing changes it asynchronously
 * and no reader has to re-read it.
 *
 * Signedness is not decided by anything in the image: every test on either
 * index is an equality -- against the other index, or against 0xa -- so no
 * signed-versus-unsigned branch exists to read.  int is what the layout carries.
 *
 * Defined by ticket 23 along with the rest of keybd.c's data; until then the
 * build supplies it zero-filled. */
extern int data_fdps_input_scancode_queue_head;

/* 0007001d.  The scancode ring's write index: the entry the INT 09h handler
 * will store the next make code into, in 0..9 like the read index, wrapped the
 * same way (INC dword ptr [0x0007001d] / CMP dword ptr [0x0007001d],0xa /
 * MOV dword ptr [0x0007001d],0x0 at 00056875, 0005687b and 00056884).
 *
 * volatile is load-bearing here.  fdps_keyboard_isr writes this one from the
 * interrupt, and fdps_wait_any_key spins on it with nothing inside the loop
 * writing anything -- an optimiser is entitled to hoist the load out and the
 * game then waits for a key forever.  It is qualified at the one declaration
 * rather than at the spinning reader so that no reader has to remember;
 * ticket 23's definition has to carry the same qualifier.  Same reasoning as
 * data_fdps_timer_tick_counter in gamedata.h (rebuild_info/pitfalls.md).
 *
 * Storing the read index into it is how both waiters throw the queue away:
 * that single store, and not any clearing of the ring itself, is what "flush"
 * means here.
 *
 * Defined by ticket 23 along with the rest of keybd.c's data; until then the
 * build supplies it zero-filled. */
extern volatile int data_fdps_input_scancode_queue_write_index;

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

/* Blocks until a key is pressed, then throws the whole queue away.

   Spins while the two ring indices are equal -- the queue-empty condition --
   and falls through the moment fdps_keyboard_isr advances the write index for
   a make code.  It then stores the read index into the write index, which
   discards the key that ended the wait along with anything else pending, so
   the screen that follows does not inherit a keypress meant for this one.
   That rewind is the same single store fdps_flush_keyboard_queue makes.

   No arguments and no result: all 23 call sites push nothing before the CALL,
   adjust nothing after it, and read EAX only after loading it again from
   memory.  EAX is left holding the read index as a by-product of the compare
   and is not a return value. */
extern void fdps_wait_any_key(void);
#pragma aux fdps_wait_any_key "*" parm caller [];

/* Throws every queued scancode away without waiting for one.

   Stores the read index into the write index and returns -- the same single
   store fdps_wait_any_key ends with, minus the spin, so this one returns at
   once whether or not anything was pending.  Screens call it before they start
   reading keys, to be sure the first key they see was pressed for them.

   Only the write index changes.  The ring's bytes, the read index and the
   latched byte data_fdps_input_last_scancode all keep their values; a caller
   that also wants that byte cleared stores 0xff through
   fdps_keyboard_scancode_ptr, which several of them do.

   No arguments and no result: all 32 call sites push nothing before the CALL
   and adjust nothing after it, and none reads EAX -- it is left holding the
   read index only because that is where the load put it. */
extern void fdps_flush_keyboard_queue(void);
#pragma aux fdps_flush_keyboard_queue "*" parm caller [];

#endif
