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

/* The scancode ring's length in entries.  It is one constant rather than two
 * because the array's size and the value both users wrap their index at have to
 * agree: fdps_read_keyboard_queue wraps with CMP dword ptr [0x00070019],0xa at
 * 000567db and fdps_keyboard_isr with CMP dword ptr [0x0007001d],0xa at
 * 0005687b, so nine is the highest index either can reach and neither ever
 * leaves the array. */
#define SCANCODE_QUEUE_LEN 10

/* 0007000f.  The ring of make codes itself: fdps_keyboard_isr stores into it at
 * the write index and fdps_read_keyboard_queue takes from it at the read index,
 * both indexing in entries.  Only make codes reach it -- the handler drops
 * anything with bit 7 set (CMP BL,0x80 / JNC at 00056865) -- so an entry is
 * always 0x01..0x7f and 0xff can serve as the reader's no-key marker without
 * colliding with one.
 *
 * ONE ARRAY OF EXACTLY TEN, and both halves of that matter.  A sweep of the
 * whole image for 0x7000f finds two instructions, the reader's MOV AL,byte ptr
 * [EBX + 0x7000f] at 000567cf and the handler's MOV byte ptr [EAX + 0x7000f],BL
 * at 0005686f; both name this base explicitly and both bound their index by the
 * wrap above, so nothing indexes from here into the two indices that follow at
 * 0x00070019 and 0x0007001d.  The eight bytes between the latched scancode at
 * 0x00070006 and this base are referenced by nothing in the image.
 *
 * NOT volatile, and that is a measurement rather than an omission: the ring's
 * bytes are written asynchronously by the INT 09h handler, but no function
 * reads one twice -- fdps_read_keyboard_queue's single load is the only read in
 * the image -- so no reader can cache a byte across the change.  The write
 * index is the one that needed the qualifier, because fdps_wait_any_key spins
 * on it.
 *
 * Defined by ticket 23 along with the rest of keybd.c's data; until then the
 * build supplies it zero-filled. */
extern unsigned char data_fdps_input_scancode_queue[SCANCODE_QUEUE_LEN];

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

/* 00070021.  The scancode fdps_keyboard_isr saw on the previous key event, and
 * the whole of its repeat filter: the handler queues a make code only when it
 * differs from this byte, which is what stops the keyboard's own typematic
 * repeat from filling the ring while a key is held down.
 *
 * Written on every event whose code differs from it, break codes included
 * (MOV byte ptr [0x00070021],BL at 0005685f sits BEFORE the CMP BL,0x80 that
 * drops break codes), so a press and its release each rearm the filter for the
 * other.  Holding a key produces one queue entry however long the typematic
 * runs; tapping the same key twice produces two, because the break code in
 * between changes this byte.
 *
 * UNSIGNED and one byte, because the comparison it exists for is against BL --
 * CMP BL,byte ptr [0x00070021] at 00056857, a byte compare of the raw port 0x60
 * value, which the same routine then sorts against 0x80 with an unsigned JNC.
 *
 * A sweep of the whole image for 0x00070021 finds exactly those two
 * instructions, both in fdps_keyboard_isr: this is the handler's private state
 * and nothing else in the game can see it.  In particular it is NOT the byte
 * fdps_read_scancode_auto_repeat filters on -- that one is
 * data_fdps_input_key_repeat_prev_scancode at 0x00063fc4, a dword, and the two
 * filters run independently.
 *
 * Defined by ticket 23 along with the rest of keybd.c's data; until then the
 * build supplies it zero-filled. */
extern unsigned char data_fdps_input_isr_prev_scancode;

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

/* Takes the next make code out of the scancode ring, or reports it empty.

   The queue is empty exactly when the two indices are equal, and then the
   answer is the marker 0xff and nothing moves.  Otherwise the entry at the read
   index is taken, the read index is advanced past it and wrapped at ten, and
   nothing else in the ring's state is touched -- the write index and the ring's
   bytes stay as fdps_keyboard_isr left them, so this reader never contends with
   the interrupt that fills it.

   The result is a make code, 0x01..0x7f, or 0xff when nothing was queued.  It
   is a byte and not a widened int: the assembly sets AL alone (MOV AL,0xff at
   000567bf, MOV AL,byte ptr [EBX + 0x7000f] at 000567cf) and leaves the rest of
   EAX holding whatever the caller left there, which is why all thirteen call
   sites widen the answer themselves with AND EAX,0xff before testing it.

   Unsigned, and that decides a branch rather than a spelling.  Every call site
   widens the byte with AND EAX,0xff -- an unsigned widening -- and ten of them
   then sort a real key from the marker against that threshold: nine with
   CMP EAX,0x7f (00020443, 00024691, 0002adda, 00017c1e, 0001e1b2, 00018079,
   0002e37c, 0003678e, 00039fde) and fdps_title_screen with CMP EAX,0x80 at
   0002a4b1.  Declared signed, the marker would sign-extend to -1 in the rebuilt
   callers instead of widening to 255 and every one of those tests would take
   the other arm on the value that means "no key".  Queued entries never reach
   0x80, so the marker is the whole of the difference.

   No arguments: all thirteen call sites push nothing before the CALL and adjust
   nothing after it. */
extern unsigned char fdps_read_keyboard_queue(void);
#pragma aux fdps_read_keyboard_queue "*" parm caller [];

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

/* 00070000.  The selector half of whatever handler was on interrupt vector 09h
 * before the game hooked it -- the 16-bit protected-mode selector DOS hands
 * back in ES from INT 21h AH=35h.
 *
 * Sixteen bits and not a padded dword: both instructions that touch it carry
 * the 0x66 operand-size prefix and move AX, not EAX (MOV [0x00070000],AX at
 * 00056802, MOV AX,[0x00070000] at 0005681f).  A sweep of the whole image for
 * 0x00070000 finds exactly those two, so the installer writes it and the
 * uninstaller reads it and nothing else in the game can see it.
 *
 * Unsigned, and that is not a free choice: a selector is a bit pattern that is
 * loaded straight back into a segment register, and the DOS/4GW selectors this
 * holds have the top bit clear only by luck of the descriptor table.  Nothing
 * ever compares it for order, so the width is the whole of the contract.
 *
 * Its partner below holds the offset.  The two are NOT one far pointer as far
 * as the code is concerned: the uninstaller loads them with two separate
 * instructions (MOV EDX,dword ptr [0x00070002] then MOV AX,[0x00070000]) and
 * nothing in the image reads six bytes from 0x00070000, so they are two
 * globals and Watcom may lay them out in either order.
 *
 * Defined by ticket 23 along with the rest of keybd.c's data; until then the
 * build supplies it zero-filled. */
extern unsigned short data_fdps_input_prev_int9_handler_selector;

/* 00070002.  The offset half of that saved vector: the 32-bit EBX DOS returns
 * beside ES from INT 21h AH=35h, stored whole (MOV dword ptr
 * [0x00070002],EBX at 000567f9) and loaded whole by the uninstaller (MOV
 * EDX,dword ptr [0x00070002] at 00056819).  A sweep of the whole image for
 * 0x00070002 finds those two instructions and no others.
 *
 * Unsigned for the same reason as the selector: it is an address that goes
 * back into EDX for INT 21h AH=25h, never a quantity, and nothing compares it
 * for order.
 *
 * Defined by ticket 23 along with the rest of keybd.c's data; until then the
 * build supplies it zero-filled. */
extern unsigned int data_fdps_prev_int9_handler_offset;

/* 00056837.  The game's own INT 09h handler: reads the scancode out of port
   0x60, latches it into data_fdps_input_last_scancode, queues it when it is a
   make code the previous event did not already produce, acknowledges the
   keyboard on port 0x61 and the 8259 on port 0x20, and leaves through IRETD.

   __interrupt is the declaration, not a decoration.  The routine is entered
   through an interrupt gate and never by a CALL: the single reference to it
   anywhere in the image is MOV EDX,0x56837 at 0005680a, the address handed to
   INT 21h AH=25h, and a sweep of the whole image for 0x56837 finds that one
   instruction.  It saves the registers it uses and exits with IRETD rather than
   RET (PUSH EDX/ECX/EBX/EAX and PUSH DS at 00056838..0005683c against the POPs
   and the IRETD at 00056892..00056897), which is exactly what the keyword makes
   wcc386 generate -- and it is why no `parm caller []` appears below: there is
   no calling convention here to declare, only the undecorated symbol name that
   lets fdps_install_keyboard_isr hand the address to DOS.

   The handler is the only writer of data_fdps_input_isr_prev_scancode, the only
   writer of the ring's bytes and the only writer of the ring's write index; it
   never touches the read index.  That split is what lets
   fdps_read_keyboard_queue drain the ring with interrupts enabled. */
extern void __interrupt fdps_keyboard_isr(void);
#pragma aux fdps_keyboard_isr "*";

/* Hooks interrupt vector 09h, saving the handler it displaces.

   Asks DOS for the current vector 09h handler (INT 21h AH=35h), files the
   selector and offset it gets back in the two globals above, and then points
   the vector at fdps_keyboard_isr (INT 21h AH=25h with DS:EDX naming it).
   From the moment it returns, every key press and release runs the game's
   handler instead of the BIOS's, which is what fills the scancode ring and the
   latched byte the readers above work from.

   There is no is-it-already-installed test, and adding one would change what
   the uninstaller puts back: a second call with no uninstall in between files
   the game's own handler as the "previous" one.  The three call sites do not
   do that -- fdps_load_global_resources installs at startup against
   fdps_shutdown_free_resources, and fdps_cd_verify_disc_and_play_track and
   fdps_play_movie each uninstall before handing the machine to the CD or the
   movie player and install again afterwards.

   No arguments and no result: all three call sites push nothing before the
   CALL, adjust nothing after it, and read EAX only after loading it again. */
extern void fdps_install_keyboard_isr(void);
#pragma aux fdps_install_keyboard_isr "*" parm caller [];

/* Puts vector 09h back where fdps_install_keyboard_isr found it, and parks the
   latched scancode byte at its no-key value.

   Reads the selector and offset the installer filed in the two globals above
   and hands that pair straight to INT 21h AH=25h for vector 09h, so from the
   moment it returns the keyboard belongs to whoever owned it before -- the
   BIOS at startup, or the extender's own stub.  The selector comes out of the
   saved global and not out of CS: the vector being restored is somebody else's
   handler, which the game's code selector does not address.

   The scancode ring, its two indices and the auto-repeat filter's state are all
   left exactly as they were, so codes queued before the hook came down are
   still readable afterwards; only data_fdps_input_last_scancode is written, and
   it is set to 0xff, the same no-key value fdps_read_keyboard_queue reports for
   an empty ring.

   No arguments and no result: all three call sites -- in
   fdps_shutdown_free_resources, fdps_cd_verify_disc_and_play_track and
   fdps_play_movie -- push nothing before the CALL and adjust nothing after it,
   and none of them reads EAX before loading it again.  EAX does come back
   holding 0x2509, the DOS function number, but that is where the call left it
   rather than anything the function means to return. */
extern void fdps_uninstall_keyboard_isr(void);
#pragma aux fdps_uninstall_keyboard_isr "*" parm caller [];

#endif
