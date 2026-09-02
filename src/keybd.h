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

/* 00063fc4.  The scancode fdps_read_scancode_auto_repeat saw on its previous
 * poll, and the only thing that tells a new key press from a key still held
 * down: the poll compares the latched byte with this and treats a difference
 * as a fresh press.  0xff -- no key -- is stored here like any other value, so
 * releasing a key is itself a change and rearms the filter.
 *
 * A full dword holding a zero-extended byte: the poll widens the latch with
 * XOR EAX,EAX / MOV AL,byte ptr [EDX] at 00017903 and stores the whole
 * register (MOV [0x00063fc4],EAX at 00017922), so 0x9c arrives here as 156 and
 * never as -100.  Unsigned for that reason; nothing compares it for order --
 * the one test on it is CMP EAX,dword ptr [0x00063fc4] / JZ at 0001790d.
 *
 * A sweep of the whole image for 0x00063fc4 finds two instructions, both in
 * fdps_read_scancode_auto_repeat: this is that filter's private state and no
 * other function can see it.
 *
 * Defined by ticket 23 along with the rest of keybd.c's data; until then the
 * build supplies it zero-filled. */
extern unsigned int data_fdps_input_key_repeat_prev_scancode;

/* 00063fc8.  The value of data_fdps_timer_tick_counter at the last poll that
 * actually advanced the repeat counter.  fdps_read_scancode_auto_repeat
 * compares the current tick against it and does nothing when they are equal,
 * which is what makes the auto-repeat run on the timer rather than on however
 * often the caller polls.
 *
 * Written on one path only -- the held-key poll that found the tick had moved
 * (MOV EAX,[0x00069d64] / MOV [0x00063fc8],EAX at 0001796e) -- and deliberately
 * not on the new-key path, which is a divergence trap rather than an oversight
 * (rebuild_info/pitfalls.md).
 *
 * Unsigned, to match the tick counter it holds a copy of, and only ever tested
 * for equality against it (CMP EAX,dword ptr [0x00069d64] / JNZ at 0001792e),
 * so the counter's wrap costs it nothing.
 *
 * A sweep of the whole image for 0x00063fc8 finds two instructions, both in
 * fdps_read_scancode_auto_repeat.
 *
 * Defined by ticket 23 along with the rest of keybd.c's data; until then the
 * build supplies it zero-filled. */
extern unsigned int data_fdps_input_key_repeat_last_tick;

/* 00060018.  How many timer ticks the key now held down has been held for,
 * counted by fdps_read_scancode_auto_repeat: zeroed when a different scancode
 * appears and incremented by at most one per tick while the same one stays.
 * The repeat schedule is read off it -- a poll reports the key again when the
 * count has reached 5 and is a multiple of 3.
 *
 * SIGNED, and that is a branch rather than a spelling: the delay test is
 * CMP dword ptr [0x00060018],0x5 / JL at 00017945, and the multiple-of-three
 * test divides with CDQ-style sign extension into IDIV (SAR EDX,0x1f / IDIV
 * EBX at 0001795e).  Nothing can drive it negative in practice, but int is
 * what the arithmetic in the image is.
 *
 * A sweep of the whole image for 0x00060018 finds five instructions, all in
 * fdps_read_scancode_auto_repeat.
 *
 * Defined by ticket 23 along with the rest of keybd.c's data; until then the
 * build supplies it zero-filled. */
extern int data_fdps_input_key_repeat_counter;

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

/* Polls the latched scancode through the game's auto-repeat filter: one report
   per key press, then a slow repeat while the key stays down.

   Reads the byte behind fdps_keyboard_scancode_ptr and answers with it when it
   differs from the previous poll's, which is what makes a press report exactly
   once.  While the same code keeps coming back the answer is 0xff until the
   key has been held for six timer ticks, then again every third tick after
   that; the count advances at most once per tick however often a caller polls,
   because the filter records the tick it last acted on.

   The result is the raw scancode the caller should act on, or 0xff for
   "nothing this poll" -- which covers both no key down and a held key still
   inside its repeat delay.  Callers do not tell those apart: each tests the
   result against the few codes it cares about (CMP dword ptr [EBP-0xc],0x1 for
   Escape at 0003240a, 0x53 at 00032410, 0x1c for Enter at 0003277d) and
   ignores everything else.  Two of them -- fdps_unit_status_window_wait_input
   at 00017117 and fdps_spell_list_window_wait_input at 00027907 -- instead
   reject the whole 0x80..0xff range with CMP ...,0x7f / JLE before looking.

   No arguments: all seven call sites push nothing before the CALL and adjust
   nothing after it, and take the result straight out of EAX.

   Every call flushes the scancode queue as it leaves, so keystrokes that
   arrived between polls are discarded rather than delivered -- this reader
   works off the latch alone. */
extern unsigned int fdps_read_scancode_auto_repeat(void);
#pragma aux fdps_read_scancode_auto_repeat "*" parm caller [];

#endif
