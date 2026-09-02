/* tests/keybd.c -- cover for src/keybd.c.
 *
 * Covers fdps_keyboard_scancode_ptr at 00056799, fdps_wait_any_key at
 * 000567a0 and fdps_flush_keyboard_queue at 000567b3.  Expected values come
 * from its
 * assembly -- LEA EAX,[0x70006] / RET, which is the whole function -- from the
 * two writers of that byte, fdps_keyboard_isr at 00056851 (MOV byte ptr
 * [0x00070006],BL, one byte, unconditional) and fdps_uninstall_keyboard_isr at
 * 0005682f, and from the fifteen call sites, every one of which uses EAX as a
 * byte pointer straight away: MOV AL,byte ptr [EAX] / AND EAX,0xff at 0002d3f8,
 * 0002d501, 00017905 and the rest, and MOV byte ptr [EAX],0xff at 0002bb13 and
 * 0002b5c8.  None of them is read off the emitted C.
 *
 * Also covers fdps_install_keyboard_isr at 000567f0 and
 * fdps_uninstall_keyboard_isr at 00056818, whose cases are at the end of the
 * file behind their own explanation: they are the only functions here with an
 * effect outside the program's memory, so their cases move interrupt vector 09h
 * for real and put it back with IRQ1 masked throughout.
 *
 * The game's handler never actually runs during any of this -- vector 09h holds
 * it only inside the fenced window in keybd_probe_install, and IRQ1 is masked
 * for the whole of that window -- so nothing but the cases themselves writes
 * the latched byte or the ring, and each case puts back what it found.  No case
 * asserts what the byte holds to begin with: data_fdps_input_last_scancode is
 * ticket 23's to define and is zero-filled until then.
 */
#include "testharn.h"
#include "gamedata.h"
#include "keybd.h"

/* The returned pointer must name the byte the ISR writes and no other object.
   This is the whole content of the function: if it named a different byte, the
   input loops would poll storage the interrupt handler never touches and the
   game would answer no key forever, with nothing failing to say so. */
static void keybd_scancode_ptr_names_the_isr_slot(void)
{
    unsigned char *scancode_slot;

    scancode_slot = fdps_keyboard_scancode_ptr();

    CHECK_EQ(scancode_slot == &data_fdps_input_last_scancode, 1);
    CHECK_EQ(scancode_slot != 0, 1);
}

/* LEA of a link-time constant: the address cannot vary, so two calls have to
   answer with the same pointer.  A body that handed back a pointer into
   anything allocated or copied per call would fail here -- and it would fail
   the game silently, because a caller that stores 0xff through one pointer to
   discard a stale key and then reads through another would never see its own
   clear. */
static void keybd_scancode_ptr_is_the_same_slot_every_call(void)
{
    CHECK_EQ(fdps_keyboard_scancode_ptr() == fdps_keyboard_scancode_ptr(), 1);
}

/* A store through the returned pointer must land in the byte the ISR writes,
   and reading it back through a fresh call must produce it.  This is the round
   trip fdps_battle_player_phase_loop and fdps_map_cursor_select_loop depend on:
   both do MOV byte ptr [EAX],0xff through this pointer before entering their
   loops, then poll the same byte for the next key. */
static void keybd_scancode_ptr_reaches_the_byte_the_isr_writes(void)
{
    unsigned char *scancode_slot;
    unsigned char saved_scancode;

    scancode_slot = fdps_keyboard_scancode_ptr();
    saved_scancode = data_fdps_input_last_scancode;

    *scancode_slot = 0x1c;   /* Enter's make code, as the ISR would latch it */
    CHECK_EQ(data_fdps_input_last_scancode, 0x1c);

    data_fdps_input_last_scancode = 0x39;   /* Space, written the other way */
    CHECK_EQ(*fdps_keyboard_scancode_ptr(), 0x39);

    data_fdps_input_last_scancode = saved_scancode;
}

/* The pointer is one byte wide, so the writers reach exactly the byte at
   0x70006 and nothing beside it.  0x70021 -- data_fdps_input_isr_prev_scancode,
   the byte the ISR compares against next -- is a separate object, and a wider
   store through this pointer would be writing into whatever the rebuild's
   linker put after the slot. */
static void keybd_scancode_ptr_is_one_byte_wide(void)
{
    unsigned char *scancode_slot;

    scancode_slot = fdps_keyboard_scancode_ptr();

    CHECK_EQ((int) sizeof(*scancode_slot), 1);
    CHECK_EQ((int) sizeof(data_fdps_input_last_scancode), 1);
}

/* The byte is unsigned, and that decides branches rather than spelling.  The
   ISR latches the raw port 0x60 byte before it filters anything, so break codes
   -- 0x80 and above -- pass through this slot, and 0xff is the value both the
   uninstall path and the input loops store to mean "no key pending".  Read
   through a signed char pointer those come out as -1 and -128; the readers'
   AND EAX,0xff says they must come out as 255 and 128, and the ISR's own
   CMP BL,0x80 / JNC is an unsigned compare.  A `char *` return type still
   compiles, still links and still passes every case above. */
static void keybd_scancode_ptr_reads_high_codes_unsigned(void)
{
    unsigned char *scancode_slot;
    unsigned char saved_scancode;

    scancode_slot = fdps_keyboard_scancode_ptr();
    saved_scancode = data_fdps_input_last_scancode;

    *scancode_slot = 0xff;   /* the no-key-pending value, not a scancode */
    CHECK_EQ(*scancode_slot, 255);

    *scancode_slot = 0x80;   /* the lowest break code the ISR can latch */
    CHECK_EQ(*scancode_slot, 128);

    *scancode_slot = 0x9c;   /* Enter's break code, make code | 0x80 */
    CHECK_EQ(*scancode_slot, 156);

    data_fdps_input_last_scancode = saved_scancode;
}

/* fdps_wait_any_key at 000567a0.  Expected values come from its five
   instructions -- MOV EAX,[0x70019] / CMP EAX,[0x7001d] / JZ to the top /
   MOV [0x7001d],EAX / RET -- and from the ring's other two users:
   fdps_keyboard_isr, which stores at the write index and bumps it with wrap at
   ten (00056875..00056884), and fdps_read_keyboard_queue, which takes from the
   read index and bumps that the same way (000567d5..000567e4).

   Every case below sets up a NON-EMPTY queue, because the empty queue is the
   blocking case: with the indices equal the function spins until an interrupt
   moves one, and no unit test can supply that.  What the cases do pin down is
   everything the function does once the wait is over, which is where its whole
   effect lives.  One consequence of testing a blocking routine is that a wrong
   emptiness test would hang here rather than report a failure -- an implementation
   spelt with an order comparison instead of an equality would sit in the loop
   forever on the case its comparison gets backwards.

   Neither index is asserted before a case sets it: they are ticket 23's to
   define and are zero-filled until then.  Every case puts both back to zero. */

/* A queued code ends the wait, and the wait's price is the code itself: the
   write index comes back to the read index, which is the queue-empty
   condition, so nothing survives for the next screen to read.  The read index
   must not move -- the function discards, it does not dequeue.  Had it
   dequeued, the read index would read 1 here. */
static void keybd_wait_any_key_empties_the_queue_it_waited_for(void)
{
    data_fdps_input_scancode_queue_head = 0;
    data_fdps_input_scancode_queue_write_index = 1;   /* one code pending */

    fdps_wait_any_key();

    CHECK_EQ(data_fdps_input_scancode_queue_write_index, 0);
    CHECK_EQ(data_fdps_input_scancode_queue_head, 0);
    CHECK_EQ(data_fdps_input_scancode_queue_head ==
             data_fdps_input_scancode_queue_write_index, 1);

    data_fdps_input_scancode_queue_head = 0;
    data_fdps_input_scancode_queue_write_index = 0;
}

/* Four codes queued while the previous screen was busy, and all four go.  The
   single store at 000567ad rewinds the write index however far ahead it is, so
   the count of discarded entries never enters into it. */
static void keybd_wait_any_key_discards_every_pending_code(void)
{
    data_fdps_input_scancode_queue_head = 3;
    data_fdps_input_scancode_queue_write_index = 7;   /* entries 3,4,5,6 */

    fdps_wait_any_key();

    CHECK_EQ(data_fdps_input_scancode_queue_write_index, 3);
    CHECK_EQ(data_fdps_input_scancode_queue_head, 3);

    data_fdps_input_scancode_queue_head = 0;
    data_fdps_input_scancode_queue_write_index = 0;
}

/* The ring wraps, so the write index is routinely BELOW the read index and the
   queue is still not empty: with the read index at 8 and the write index
   wrapped round to 2, four entries are pending.  The assembly asks only
   whether the two are equal (CMP / JZ), never which is larger, so this case
   behaves exactly like the one above. */
static void keybd_wait_any_key_handles_a_wrapped_write_index(void)
{
    data_fdps_input_scancode_queue_head = 8;
    data_fdps_input_scancode_queue_write_index = 2;   /* 8,9,0,1 pending */

    fdps_wait_any_key();

    CHECK_EQ(data_fdps_input_scancode_queue_write_index, 8);
    CHECK_EQ(data_fdps_input_scancode_queue_head, 8);

    data_fdps_input_scancode_queue_head = 0;
    data_fdps_input_scancode_queue_write_index = 0;
}

/* The rewind leaves both indices inside the ring, so the pair it hands back is
   one fdps_read_keyboard_queue and fdps_keyboard_isr can go on using: both
   index the ten-entry buffer with the value as it stands, and neither range
   checks before it does.  A rewind that stored anything but the read index --
   a zero, say -- would still leave the queue empty and still pass the two
   cases above. */
static void keybd_wait_any_key_leaves_indices_inside_the_ring(void)
{
    data_fdps_input_scancode_queue_head = 9;   /* the last entry */
    data_fdps_input_scancode_queue_write_index = 0;   /* wrapped past it */

    fdps_wait_any_key();

    CHECK_EQ(data_fdps_input_scancode_queue_write_index, 9);
    CHECK_EQ(data_fdps_input_scancode_queue_write_index >= 0 &&
             data_fdps_input_scancode_queue_write_index < 10, 1);
    CHECK_EQ(data_fdps_input_scancode_queue_head, 9);

    data_fdps_input_scancode_queue_head = 0;
    data_fdps_input_scancode_queue_write_index = 0;
}

/* fdps_flush_keyboard_queue at 000567b3.  Expected values come from its three
   instructions -- MOV EAX,[0x70019] / MOV [0x7001d],EAX / RET -- and from the
   ring's other users: fdps_read_keyboard_queue, whose CMP EBX,dword ptr
   [0x0007001d] / JZ makes equal indices mean "empty" (000567c7), and
   fdps_keyboard_isr, which advances the write index as it queues make codes
   (00056875..00056884).

   The cases below overlap the fdps_wait_any_key cases on purpose: the two
   functions perform the identical store and differ only in what precedes it,
   so the flush cases have to include the one setup its sibling cannot survive
   -- an already-empty queue, which fdps_wait_any_key spins on and this one
   returns from.  That case is what separates the two bodies; the rest pin down
   the store they share.

   Neither index is asserted before a case sets it: they are ticket 23's to
   define and are zero-filled until then.  Every case puts both back to zero. */

/* The pending codes go, and the read index does not move: this discards, it
   never dequeues.  A body that dequeued would leave the read index at 1. */
static void keybd_flush_discards_the_pending_codes(void)
{
    data_fdps_input_scancode_queue_head = 0;
    data_fdps_input_scancode_queue_write_index = 4;   /* entries 0,1,2,3 */

    fdps_flush_keyboard_queue();

    CHECK_EQ(data_fdps_input_scancode_queue_write_index, 0);
    CHECK_EQ(data_fdps_input_scancode_queue_head, 0);
    CHECK_EQ(data_fdps_input_scancode_queue_head ==
             data_fdps_input_scancode_queue_write_index, 1);

    data_fdps_input_scancode_queue_head = 0;
    data_fdps_input_scancode_queue_write_index = 0;
}

/* An empty queue -- the two indices already equal -- and the function returns
   anyway, leaving them where they were.  This is the entire difference from
   fdps_wait_any_key at 000567a0, whose JZ 0x000567a0 sits on exactly this
   condition until an interrupt breaks it.  If this body were spelt as the
   blocking sibling, this case would hang rather than fail, and the run would
   never reach the ones below. */
static void keybd_flush_returns_at_once_on_an_empty_queue(void)
{
    data_fdps_input_scancode_queue_head = 5;
    data_fdps_input_scancode_queue_write_index = 5;   /* nothing pending */

    fdps_flush_keyboard_queue();

    CHECK_EQ(data_fdps_input_scancode_queue_write_index, 5);
    CHECK_EQ(data_fdps_input_scancode_queue_head, 5);

    data_fdps_input_scancode_queue_head = 0;
    data_fdps_input_scancode_queue_write_index = 0;
}

/* The ring wraps, so a non-empty queue routinely has the write index BELOW the
   read index: read at 8, write wrapped to 2, four codes pending.  The store is
   unconditional -- nothing in the three instructions compares the two -- so
   the direction of the rewind never enters into it. */
static void keybd_flush_handles_a_wrapped_write_index(void)
{
    data_fdps_input_scancode_queue_head = 8;
    data_fdps_input_scancode_queue_write_index = 2;   /* 8,9,0,1 pending */

    fdps_flush_keyboard_queue();

    CHECK_EQ(data_fdps_input_scancode_queue_write_index, 8);
    CHECK_EQ(data_fdps_input_scancode_queue_head, 8);

    data_fdps_input_scancode_queue_head = 0;
    data_fdps_input_scancode_queue_write_index = 0;
}

/* The value stored is the read index itself, which leaves the write index
   inside the ten-entry ring for fdps_keyboard_isr to index with as it stands
   -- it stores at the write index without range checking first.  A rewind that
   stored some other empty-making value, a zero say, would still empty the
   queue and still pass the two cases above. */
static void keybd_flush_leaves_indices_inside_the_ring(void)
{
    data_fdps_input_scancode_queue_head = 9;   /* the last entry */
    data_fdps_input_scancode_queue_write_index = 0;   /* wrapped past it */

    fdps_flush_keyboard_queue();

    CHECK_EQ(data_fdps_input_scancode_queue_write_index, 9);
    CHECK_EQ(data_fdps_input_scancode_queue_write_index >= 0 &&
             data_fdps_input_scancode_queue_write_index < 10, 1);
    CHECK_EQ(data_fdps_input_scancode_queue_head, 9);

    data_fdps_input_scancode_queue_head = 0;
    data_fdps_input_scancode_queue_write_index = 0;
}

/* The ring and the latched byte are two independent pieces of state, and this
   function touches only the first.  data_fdps_input_last_scancode is what the
   polling loops read directly; clearing it here as well would look like
   tidying up and would swallow a keypress those loops are waiting for.  The
   assembly writes one dword, to 0x7001d, and nothing else. */
static void keybd_flush_leaves_the_latched_scancode_alone(void)
{
    unsigned char saved_scancode;

    saved_scancode = data_fdps_input_last_scancode;
    data_fdps_input_last_scancode = 0x1c;   /* Enter, as the ISR latched it */
    data_fdps_input_scancode_queue_head = 2;
    data_fdps_input_scancode_queue_write_index = 6;

    fdps_flush_keyboard_queue();

    CHECK_EQ(data_fdps_input_last_scancode, 0x1c);
    CHECK_EQ(data_fdps_input_scancode_queue_write_index, 2);
    CHECK_EQ(data_fdps_input_scancode_queue_head, 2);

    data_fdps_input_last_scancode = saved_scancode;
    data_fdps_input_scancode_queue_head = 0;
    data_fdps_input_scancode_queue_write_index = 0;
}

/* fdps_read_scancode_auto_repeat at 000178f0.  Expected values come from its
   assembly: the widening XOR EAX,EAX / MOV AL,byte ptr [EDX] at 00017903, the
   CMP EAX,dword ptr [0x00063fc4] / JZ that separates a new key from a held one
   at 0001790d, the CMP EAX,dword ptr [0x00069d64] / JNZ tick guard at 0001792e,
   the INC dword ptr [0x00060018] at 0001793f, the CMP dword ptr
   [0x00060018],0x5 / JL and the IDIV by 3 with TEST EDX,EDX / JZ that follow it
   at 00017945..00017965, the MOV [0x00063fc8],EAX at 00017973 that is reached
   from that one path only, and the CALL 0x000567b3 at 00017978 that every path
   arrives at.  None of them is read off the emitted C.

   THE FILTER'S STATE IS DRIVEN, NOT OBSERVED COLD.  All four globals involved
   -- the latched scancode, the previous scancode, the hold counter and the
   serviced tick -- are ticket 23's to define and are zero-filled until then, so
   no case below reads one it has not written first, and every case restores
   them.  The timer tick counter is written directly for the same reason: no
   timer interrupt runs under the test harness, so the tick is whatever a case
   sets, which is exactly the control the schedule cases need.

   0x48 is the Up arrow's make code and 0x50 the Down arrow's -- the two keys
   these menus repeat on -- and 0x9c is Enter's break code.  Nothing here
   depends on which keys they are; they are distinct byte values, one of them
   above 0x7f. */

/* A scancode different from last poll's is a fresh press and comes back
   unchanged, with the hold count started over and the new code remembered.
   The tick is set EQUAL to the serviced tick here on purpose: on this path the
   tick guard is not consulted at all, so a body that tested it first would
   answer 0xff and fail.  The queue is flushed on the way out like every other
   path. */
static void keybd_repeat_reports_a_new_key_unchanged(void)
{
    unsigned char saved_scancode;
    unsigned int reported_scancode;

    saved_scancode = data_fdps_input_last_scancode;
    data_fdps_input_last_scancode = 0x48;            /* Up pressed now */
    data_fdps_input_key_repeat_prev_scancode = 0x50; /* Down was down before */
    data_fdps_input_key_repeat_counter = 7;          /* mid-repeat on Down */
    data_fdps_input_key_repeat_last_tick = 99;
    data_fdps_timer_tick_counter = 99;               /* same tick, on purpose */
    data_fdps_input_scancode_queue_head = 0;
    data_fdps_input_scancode_queue_write_index = 3;  /* three codes pending */

    reported_scancode = fdps_read_scancode_auto_repeat();

    CHECK_EQ(reported_scancode, 0x48);
    CHECK_EQ(data_fdps_input_key_repeat_counter, 0);
    CHECK_EQ(data_fdps_input_key_repeat_prev_scancode, 0x48);
    CHECK_EQ(data_fdps_input_scancode_queue_write_index, 0);

    data_fdps_input_last_scancode = saved_scancode;
    data_fdps_input_key_repeat_prev_scancode = 0;
    data_fdps_input_key_repeat_counter = 0;
    data_fdps_input_key_repeat_last_tick = 0;
    data_fdps_timer_tick_counter = 0;
    data_fdps_input_scancode_queue_head = 0;
    data_fdps_input_scancode_queue_write_index = 0;
}

/* The new-key path must NOT record the current tick.  The store at 00017973 is
   reached only from the held-key branch that found the tick had moved, and
   hoisting it out -- the obvious tidy-up -- shifts the whole repeat delay by a
   tick.  The second poll below is what makes the difference visible: with the
   serviced tick still the stale 99, that poll sees a moved tick and spends the
   first of the six ticks of delay immediately.  Had the press recorded tick
   200, the poll would have found nothing moved, answered without counting, and
   the key would have repeated one tick later than the original's. */
static void keybd_repeat_new_key_does_not_record_the_tick(void)
{
    unsigned char saved_scancode;
    unsigned int reported_scancode;

    saved_scancode = data_fdps_input_last_scancode;
    data_fdps_input_last_scancode = 0x48;
    data_fdps_input_key_repeat_prev_scancode = 0x50;
    data_fdps_input_key_repeat_counter = 7;
    data_fdps_input_key_repeat_last_tick = 99;       /* left by an older key */
    data_fdps_timer_tick_counter = 200;

    reported_scancode = fdps_read_scancode_auto_repeat();

    CHECK_EQ(reported_scancode, 0x48);
    CHECK_EQ(data_fdps_input_key_repeat_last_tick, 99);
    CHECK_EQ(data_fdps_input_key_repeat_counter, 0);

    /* Same key, same tick, and the hold count moves anyway. */
    reported_scancode = fdps_read_scancode_auto_repeat();

    CHECK_EQ(reported_scancode, 0xff);
    CHECK_EQ(data_fdps_input_key_repeat_counter, 1);
    CHECK_EQ(data_fdps_input_key_repeat_last_tick, 200);

    data_fdps_input_last_scancode = saved_scancode;
    data_fdps_input_key_repeat_prev_scancode = 0;
    data_fdps_input_key_repeat_counter = 0;
    data_fdps_input_key_repeat_last_tick = 0;
    data_fdps_timer_tick_counter = 0;
}

/* A held key inside a tick reports nothing and changes nothing: the count does
   not move, the serviced tick does not move, and the remembered code does not
   move.  This is what stops a caller that polls twenty times a frame from
   repeating twenty times faster than one that polls once. */
static void keybd_repeat_stays_silent_within_one_tick(void)
{
    unsigned char saved_scancode;
    unsigned int reported_scancode;

    saved_scancode = data_fdps_input_last_scancode;
    data_fdps_input_last_scancode = 0x48;
    data_fdps_input_key_repeat_prev_scancode = 0x48; /* still held */
    data_fdps_input_key_repeat_counter = 2;
    data_fdps_input_key_repeat_last_tick = 77;
    data_fdps_timer_tick_counter = 77;               /* timer has not moved */
    data_fdps_input_scancode_queue_head = 1;
    data_fdps_input_scancode_queue_write_index = 5;

    reported_scancode = fdps_read_scancode_auto_repeat();

    CHECK_EQ(reported_scancode, 0xff);
    CHECK_EQ(data_fdps_input_key_repeat_counter, 2);
    CHECK_EQ(data_fdps_input_key_repeat_last_tick, 77);
    CHECK_EQ(data_fdps_input_key_repeat_prev_scancode, 0x48);
    CHECK_EQ(data_fdps_input_scancode_queue_write_index, 1);

    data_fdps_input_last_scancode = saved_scancode;
    data_fdps_input_key_repeat_prev_scancode = 0;
    data_fdps_input_key_repeat_counter = 0;
    data_fdps_input_key_repeat_last_tick = 0;
    data_fdps_timer_tick_counter = 0;
    data_fdps_input_scancode_queue_head = 0;
    data_fdps_input_scancode_queue_write_index = 0;
}

/* One tick, one count, however many polls fall inside it.  The first poll of a
   new tick counts and records the tick; every further poll of that tick is the
   silent case above.  Both polls answer 0xff -- the count is one, far short of
   the delay. */
static void keybd_repeat_counts_one_tick_at_a_time(void)
{
    unsigned char saved_scancode;
    unsigned int reported_scancode;

    saved_scancode = data_fdps_input_last_scancode;
    data_fdps_input_last_scancode = 0x48;
    data_fdps_input_key_repeat_prev_scancode = 0x48;
    data_fdps_input_key_repeat_counter = 0;
    data_fdps_input_key_repeat_last_tick = 0;
    data_fdps_timer_tick_counter = 1;

    reported_scancode = fdps_read_scancode_auto_repeat();
    CHECK_EQ(reported_scancode, 0xff);
    CHECK_EQ(data_fdps_input_key_repeat_counter, 1);
    CHECK_EQ(data_fdps_input_key_repeat_last_tick, 1);

    reported_scancode = fdps_read_scancode_auto_repeat();
    CHECK_EQ(reported_scancode, 0xff);
    CHECK_EQ(data_fdps_input_key_repeat_counter, 1);

    data_fdps_input_last_scancode = saved_scancode;
    data_fdps_input_key_repeat_prev_scancode = 0;
    data_fdps_input_key_repeat_counter = 0;
    data_fdps_input_key_repeat_last_tick = 0;
    data_fdps_timer_tick_counter = 0;
}

/* The whole schedule, driven one tick at a time from a fresh press: the key is
   reported again on the sixth tick of the hold and then every third tick.  The
   expected pattern is read straight off CMP dword ptr [0x00060018],0x5 / JL
   (silence below five) and the IDIV by three whose zero remainder is the only
   thing that lets a report through -- so 3 is silenced by the delay although it
   divides, 5 is silenced by the period although it clears the delay, and 6 and
   9 are the first two repeats. */
static void keybd_repeat_first_repeat_lands_on_the_sixth_tick(void)
{
    unsigned char saved_scancode;
    unsigned int reported_scancode;
    int tick;

    saved_scancode = data_fdps_input_last_scancode;
    data_fdps_input_last_scancode = 0x48;
    data_fdps_input_key_repeat_prev_scancode = 0x48; /* already held */
    data_fdps_input_key_repeat_counter = 0;          /* as the press left it */
    data_fdps_input_key_repeat_last_tick = 0;

    for (tick = 1; tick <= 9; tick++) {
        data_fdps_timer_tick_counter = (unsigned int) tick;
        reported_scancode = fdps_read_scancode_auto_repeat();

        if (tick == 6 || tick == 9) {
            CHECK_EQ(reported_scancode, 0x48);
        } else {
            CHECK_EQ(reported_scancode, 0xff);
        }
        CHECK_EQ(data_fdps_input_key_repeat_counter, tick);
    }

    data_fdps_input_last_scancode = saved_scancode;
    data_fdps_input_key_repeat_prev_scancode = 0;
    data_fdps_input_key_repeat_counter = 0;
    data_fdps_input_key_repeat_last_tick = 0;
    data_fdps_timer_tick_counter = 0;
}

/* The two halves of the gate are independent, and each case here would pass if
   the other half were missing.  Count 3 divides by three and is still silenced,
   because the delay has not elapsed; count 5 has reached the delay and is still
   silenced, because it is not on the period; count 6 satisfies both and is the
   one that speaks. */
static void keybd_repeat_needs_the_delay_and_the_period_together(void)
{
    unsigned char saved_scancode;
    unsigned int reported_scancode;

    saved_scancode = data_fdps_input_last_scancode;
    data_fdps_input_last_scancode = 0x48;
    data_fdps_input_key_repeat_prev_scancode = 0x48;
    data_fdps_input_key_repeat_last_tick = 0;

    data_fdps_input_key_repeat_counter = 2;          /* about to become 3 */
    data_fdps_timer_tick_counter = 1;
    reported_scancode = fdps_read_scancode_auto_repeat();
    CHECK_EQ(reported_scancode, 0xff);
    CHECK_EQ(data_fdps_input_key_repeat_counter, 3);

    data_fdps_input_key_repeat_counter = 4;          /* about to become 5 */
    data_fdps_timer_tick_counter = 2;
    reported_scancode = fdps_read_scancode_auto_repeat();
    CHECK_EQ(reported_scancode, 0xff);
    CHECK_EQ(data_fdps_input_key_repeat_counter, 5);

    data_fdps_timer_tick_counter = 3;                /* 5 becomes 6 */
    reported_scancode = fdps_read_scancode_auto_repeat();
    CHECK_EQ(reported_scancode, 0x48);
    CHECK_EQ(data_fdps_input_key_repeat_counter, 6);

    data_fdps_input_last_scancode = saved_scancode;
    data_fdps_input_key_repeat_prev_scancode = 0;
    data_fdps_input_key_repeat_counter = 0;
    data_fdps_input_key_repeat_last_tick = 0;
    data_fdps_timer_tick_counter = 0;
}

/* The latched byte is widened without sign, so a break code comes back as the
   byte it is and is remembered as that.  Read through a signed char the 0x9c
   below would be -100, would never equal the 156 the previous poll stored, and
   every poll of a held key would look like a fresh press -- the repeat would
   never start and the key would report on every single frame.  0xff, the
   no-key value, is the same story at the top of the range. */
static void keybd_repeat_widens_the_latch_unsigned(void)
{
    unsigned char saved_scancode;
    unsigned int reported_scancode;

    saved_scancode = data_fdps_input_last_scancode;
    data_fdps_input_key_repeat_last_tick = 0;
    data_fdps_timer_tick_counter = 0;

    data_fdps_input_last_scancode = 0x9c;            /* Enter's break code */
    data_fdps_input_key_repeat_prev_scancode = 0x48;
    reported_scancode = fdps_read_scancode_auto_repeat();
    CHECK_EQ(reported_scancode, 156);
    CHECK_EQ(data_fdps_input_key_repeat_prev_scancode, 156);

    data_fdps_input_last_scancode = 0xff;            /* the key came up */
    reported_scancode = fdps_read_scancode_auto_repeat();
    CHECK_EQ(reported_scancode, 255);
    CHECK_EQ(data_fdps_input_key_repeat_prev_scancode, 255);
    CHECK_EQ(data_fdps_input_key_repeat_counter, 0);

    data_fdps_input_last_scancode = saved_scancode;
    data_fdps_input_key_repeat_prev_scancode = 0;
    data_fdps_input_key_repeat_counter = 0;
    data_fdps_input_key_repeat_last_tick = 0;
    data_fdps_timer_tick_counter = 0;
}

/* No key down is treated as a key held down -- 0xff goes through the counter
   and the schedule like any scancode -- and that costs nothing, because the
   value the schedule eventually lets through is 0xff itself.  So an idle input
   loop can never be handed a keypress it did not get, and the function needs no
   special case for the idle state.  The count below is set to 5 so the next
   tick reaches 6, the first tick the gate opens on. */
static void keybd_repeat_never_reports_the_no_key_value_as_a_key(void)
{
    unsigned char saved_scancode;
    unsigned int reported_scancode;

    saved_scancode = data_fdps_input_last_scancode;
    data_fdps_input_last_scancode = 0xff;            /* nothing is down */
    data_fdps_input_key_repeat_prev_scancode = 0xff;
    data_fdps_input_key_repeat_counter = 5;
    data_fdps_input_key_repeat_last_tick = 0;
    data_fdps_timer_tick_counter = 1;

    reported_scancode = fdps_read_scancode_auto_repeat();

    CHECK_EQ(reported_scancode, 0xff);
    CHECK_EQ(data_fdps_input_key_repeat_counter, 6);

    data_fdps_input_last_scancode = saved_scancode;
    data_fdps_input_key_repeat_prev_scancode = 0;
    data_fdps_input_key_repeat_counter = 0;
    data_fdps_input_key_repeat_last_tick = 0;
    data_fdps_timer_tick_counter = 0;
}

/* The queue is flushed on all three paths, not just the one that reports.  The
   CALL at 00017978 sits after the branches have joined, so a press, a silenced
   poll inside a tick and a counted poll all leave the ring empty; keystrokes
   the ISR buffered between polls are discarded by design.  A body that flushed
   only where it reports would deliver those stale codes to whichever screen
   read the queue next. */
static void keybd_repeat_flushes_the_queue_on_every_path(void)
{
    unsigned char saved_scancode;

    saved_scancode = data_fdps_input_last_scancode;
    data_fdps_input_last_scancode = 0x48;

    data_fdps_input_key_repeat_prev_scancode = 0x50; /* a fresh press */
    data_fdps_input_key_repeat_last_tick = 4;
    data_fdps_timer_tick_counter = 4;
    data_fdps_input_scancode_queue_head = 2;
    data_fdps_input_scancode_queue_write_index = 6;
    fdps_read_scancode_auto_repeat();
    CHECK_EQ(data_fdps_input_scancode_queue_write_index, 2);

    data_fdps_input_key_repeat_last_tick = 4;        /* held, tick unmoved */
    data_fdps_input_scancode_queue_head = 3;
    data_fdps_input_scancode_queue_write_index = 9;
    fdps_read_scancode_auto_repeat();
    CHECK_EQ(data_fdps_input_scancode_queue_write_index, 3);

    data_fdps_timer_tick_counter = 5;                /* held, tick moved */
    data_fdps_input_scancode_queue_head = 7;
    data_fdps_input_scancode_queue_write_index = 1;
    fdps_read_scancode_auto_repeat();
    CHECK_EQ(data_fdps_input_scancode_queue_write_index, 7);

    data_fdps_input_last_scancode = saved_scancode;
    data_fdps_input_key_repeat_prev_scancode = 0;
    data_fdps_input_key_repeat_counter = 0;
    data_fdps_input_key_repeat_last_tick = 0;
    data_fdps_timer_tick_counter = 0;
    data_fdps_input_scancode_queue_head = 0;
    data_fdps_input_scancode_queue_write_index = 0;
}

/* The latched byte is read and never written: the assembly loads through the
   pointer at 00017905 and stores nothing back.  Clearing it here would look
   like good housekeeping and would end the auto-repeat outright -- the next
   poll would find 0xff, call it a new key, and no key could ever be held.  The
   callers that do want it cleared write 0xff through
   fdps_keyboard_scancode_ptr themselves before their loops start. */
static void keybd_repeat_leaves_the_latched_scancode_alone(void)
{
    unsigned char saved_scancode;

    saved_scancode = data_fdps_input_last_scancode;
    data_fdps_input_last_scancode = 0x48;
    data_fdps_input_key_repeat_prev_scancode = 0x50;
    data_fdps_input_key_repeat_last_tick = 0;
    data_fdps_timer_tick_counter = 0;

    fdps_read_scancode_auto_repeat();                /* the press */
    CHECK_EQ(data_fdps_input_last_scancode, 0x48);

    data_fdps_timer_tick_counter = 1;
    fdps_read_scancode_auto_repeat();                /* a counted poll */
    CHECK_EQ(data_fdps_input_last_scancode, 0x48);

    data_fdps_input_last_scancode = saved_scancode;
    data_fdps_input_key_repeat_prev_scancode = 0;
    data_fdps_input_key_repeat_counter = 0;
    data_fdps_input_key_repeat_last_tick = 0;
    data_fdps_timer_tick_counter = 0;
}

/* fdps_read_keyboard_queue at 000567be.  Expected values come from its eleven
   instructions -- MOV AL,0xff / MOV EBX,[0x70019] / CMP EBX,[0x7001d] / JZ to
   the exit / MOV AL,byte ptr [EBX + 0x7000f] / INC dword ptr [0x70019] /
   CMP dword ptr [0x70019],0xa / JNZ to the exit / MOV dword ptr [0x70019],0x0
   -- from the handler that fills the ring, fdps_keyboard_isr, which stores at
   the write index and wraps it at ten the same way (0005686f..00056884) and
   drops everything with bit 7 set (CMP BL,0x80 / JNC at 00056865), and from the
   thirteen call sites, every one of which widens the answer with AND EAX,0xff
   and ten of which then compare it against 0x7f or 0x80.  None of them is read
   off the emitted C.

   The ring's bytes are ticket 23's to define and are zero-filled until then, so
   no case reads an entry it has not written; each saves the whole ring and puts
   it back, and puts both indices back to zero.  The ISR is not installed while
   these run, so nothing but the cases themselves moves the write index. */

static void keybd_ring_save(unsigned char *saved_ring)
{
    int entry;

    for (entry = 0; entry < SCANCODE_QUEUE_LEN; entry++) {
        saved_ring[entry] = data_fdps_input_scancode_queue[entry];
    }
}

static void keybd_ring_restore(unsigned char *saved_ring)
{
    int entry;

    for (entry = 0; entry < SCANCODE_QUEUE_LEN; entry++) {
        data_fdps_input_scancode_queue[entry] = saved_ring[entry];
    }
}

/* Equal indices are the empty queue, and then the answer is the marker the
   function started AL with and nothing at all moves -- not the read index, not
   the write index, and not the stale byte still sitting at the read index.
   Reading that stale byte is what a body without the emptiness test would do,
   and it would hand the game a keypress that was already consumed.

   The first check calls the function inside CHECK_EQ rather than through a
   local, and that is the point of it: the harness widens what the call returns,
   so a signed return type would arrive as -1 here instead of 255 -- and would
   then sign-extend in the callers, whose CMP EAX,0x7f tests sort the marker
   from a real key. */
static void keybd_read_queue_reports_the_marker_when_empty(void)
{
    unsigned char saved_ring[SCANCODE_QUEUE_LEN];
    unsigned char taken_scancode;

    keybd_ring_save(saved_ring);
    data_fdps_input_scancode_queue[3] = 0x1c;   /* consumed on an earlier read */
    data_fdps_input_scancode_queue_head = 3;
    data_fdps_input_scancode_queue_write_index = 3;   /* nothing pending */

    CHECK_EQ(fdps_read_keyboard_queue(), 255);

    taken_scancode = fdps_read_keyboard_queue();

    CHECK_EQ(taken_scancode, 0xff);
    CHECK_EQ(data_fdps_input_scancode_queue_head, 3);
    CHECK_EQ(data_fdps_input_scancode_queue_write_index, 3);
    CHECK_EQ(data_fdps_input_scancode_queue[3], 0x1c);

    keybd_ring_restore(saved_ring);
    data_fdps_input_scancode_queue_head = 0;
    data_fdps_input_scancode_queue_write_index = 0;
}

/* The advance and its wrap live inside the non-empty branch -- the JZ at
   000567cd jumps past all three instructions.  With the read index at the last
   entry and the queue empty, a body that advanced or wrapped unconditionally
   would leave it at 0 and the next code the handler queued at 9 would be
   skipped, with nothing to show for it but a lost keystroke. */
static void keybd_read_queue_empty_does_not_advance_or_wrap_the_index(void)
{
    unsigned char taken_scancode;

    data_fdps_input_scancode_queue_head = 9;   /* the last entry */
    data_fdps_input_scancode_queue_write_index = 9;   /* nothing pending */

    taken_scancode = fdps_read_keyboard_queue();

    CHECK_EQ(taken_scancode, 0xff);
    CHECK_EQ(data_fdps_input_scancode_queue_head, 9);
    CHECK_EQ(data_fdps_input_scancode_queue_write_index, 9);

    data_fdps_input_scancode_queue_head = 0;
    data_fdps_input_scancode_queue_write_index = 0;
}

/* The entry taken is the one AT the read index, and the read index moves on by
   exactly one.  The write index must not move: this dequeues one code, where
   fdps_flush_keyboard_queue at 000567b3 discards them all, and the two would be
   indistinguishable on a queue holding a single code. */
static void keybd_read_queue_takes_the_entry_at_the_read_index(void)
{
    unsigned char saved_ring[SCANCODE_QUEUE_LEN];
    unsigned char taken_scancode;

    keybd_ring_save(saved_ring);
    data_fdps_input_scancode_queue[2] = 0x39;   /* Space */
    data_fdps_input_scancode_queue[3] = 0x1c;   /* Enter, still pending */
    data_fdps_input_scancode_queue_head = 2;
    data_fdps_input_scancode_queue_write_index = 5;   /* entries 2,3,4 */

    taken_scancode = fdps_read_keyboard_queue();

    CHECK_EQ(taken_scancode, 0x39);
    CHECK_EQ(data_fdps_input_scancode_queue_head, 3);
    CHECK_EQ(data_fdps_input_scancode_queue_write_index, 5);
    CHECK_EQ(data_fdps_input_scancode_queue[2], 0x39);   /* not erased */

    keybd_ring_restore(saved_ring);
    data_fdps_input_scancode_queue_head = 0;
    data_fdps_input_scancode_queue_write_index = 0;
}

/* Three codes queued by the handler come back in the order they were queued,
   and the queue then reports itself empty -- the read index has caught the
   write index up, which is the same condition the function started from in the
   empty case above.  A body that read at the write index, or that took the
   newest entry, would answer 0x1c first here. */
static void keybd_read_queue_returns_the_codes_in_order(void)
{
    unsigned char saved_ring[SCANCODE_QUEUE_LEN];

    keybd_ring_save(saved_ring);
    data_fdps_input_scancode_queue[0] = 0x48;   /* Up, queued first */
    data_fdps_input_scancode_queue[1] = 0x50;   /* Down */
    data_fdps_input_scancode_queue[2] = 0x1c;   /* Enter, queued last */
    data_fdps_input_scancode_queue_head = 0;
    data_fdps_input_scancode_queue_write_index = 3;

    CHECK_EQ(fdps_read_keyboard_queue(), 0x48);
    CHECK_EQ(fdps_read_keyboard_queue(), 0x50);
    CHECK_EQ(fdps_read_keyboard_queue(), 0x1c);
    CHECK_EQ(data_fdps_input_scancode_queue_head, 3);

    CHECK_EQ(fdps_read_keyboard_queue(), 255);
    CHECK_EQ(data_fdps_input_scancode_queue_head, 3);

    keybd_ring_restore(saved_ring);
    data_fdps_input_scancode_queue_head = 0;
    data_fdps_input_scancode_queue_write_index = 0;
}

/* The read index wraps to zero after the tenth entry, which is what keeps it a
   legal index into a ten-entry array: nothing range checks it, here or in the
   handler.  The wrap is to 0 and not to anything else, so the code the handler
   writes at 0 next is the one the following read takes. */
static void keybd_read_queue_wraps_the_read_index_at_ten(void)
{
    unsigned char saved_ring[SCANCODE_QUEUE_LEN];
    unsigned char taken_scancode;

    keybd_ring_save(saved_ring);
    data_fdps_input_scancode_queue[9] = 0x4b;   /* Left, in the last entry */
    data_fdps_input_scancode_queue_head = 9;
    data_fdps_input_scancode_queue_write_index = 0;   /* just entry 9 pending */

    taken_scancode = fdps_read_keyboard_queue();

    CHECK_EQ(taken_scancode, 0x4b);
    CHECK_EQ(data_fdps_input_scancode_queue_head, 0);
    CHECK_EQ(data_fdps_input_scancode_queue_write_index, 0);
    CHECK_EQ(fdps_read_keyboard_queue(), 255);   /* and now empty */

    keybd_ring_restore(saved_ring);
    data_fdps_input_scancode_queue_head = 0;
    data_fdps_input_scancode_queue_write_index = 0;
}

/* The wrap is CMP ...,0xa / JNZ -- an equality against the length -- so the
   entry before the last does not wrap.  Spelt as a >= test against nine, or as
   a wrap of the pre-increment value, this case would answer 0 and the ninth
   entry of every lap round the ring would be skipped. */
static void keybd_read_queue_does_not_wrap_before_ten(void)
{
    unsigned char saved_ring[SCANCODE_QUEUE_LEN];
    unsigned char taken_scancode;

    keybd_ring_save(saved_ring);
    data_fdps_input_scancode_queue[8] = 0x4d;   /* Right */
    data_fdps_input_scancode_queue_head = 8;
    data_fdps_input_scancode_queue_write_index = 0;   /* entries 8 and 9 */

    taken_scancode = fdps_read_keyboard_queue();

    CHECK_EQ(taken_scancode, 0x4d);
    CHECK_EQ(data_fdps_input_scancode_queue_head, 9);

    keybd_ring_restore(saved_ring);
    data_fdps_input_scancode_queue_head = 0;
    data_fdps_input_scancode_queue_write_index = 0;
}

/* The write index is routinely BELOW the read index, because it wraps too, and
   the queue is not empty then: read at 8 and write at 2 is four codes pending
   across the seam.  The only test in the assembly is an equality (CMP EBX,
   dword ptr [0x0007001d] / JZ), never an order comparison, so all four come
   back and the fifth read is the empty one. */
static void keybd_read_queue_handles_a_wrapped_write_index(void)
{
    unsigned char saved_ring[SCANCODE_QUEUE_LEN];

    keybd_ring_save(saved_ring);
    data_fdps_input_scancode_queue[8] = 0x11;
    data_fdps_input_scancode_queue[9] = 0x22;
    data_fdps_input_scancode_queue[0] = 0x33;
    data_fdps_input_scancode_queue[1] = 0x44;
    data_fdps_input_scancode_queue_head = 8;
    data_fdps_input_scancode_queue_write_index = 2;   /* 8,9,0,1 pending */

    CHECK_EQ(fdps_read_keyboard_queue(), 0x11);
    CHECK_EQ(fdps_read_keyboard_queue(), 0x22);
    CHECK_EQ(fdps_read_keyboard_queue(), 0x33);
    CHECK_EQ(fdps_read_keyboard_queue(), 0x44);
    CHECK_EQ(data_fdps_input_scancode_queue_head, 2);
    CHECK_EQ(fdps_read_keyboard_queue(), 255);

    keybd_ring_restore(saved_ring);
    data_fdps_input_scancode_queue_head = 0;
    data_fdps_input_scancode_queue_write_index = 0;
}

/* The read index is the only thing this function writes, and that is its side
   of the arrangement with the INT 09h handler: the handler owns the ring's
   bytes and the write index, and if this touched either -- clearing the entry
   it took, say, or rewinding the write index the way its two siblings do -- an
   interrupt arriving mid-call would lose whatever the handler had just stored.
   The latched byte at 0x00070006 is separate state again and is not read here
   at all: the queue readers and the latch readers are different callers. */
static void keybd_read_queue_writes_only_the_read_index(void)
{
    unsigned char saved_ring[SCANCODE_QUEUE_LEN];
    unsigned char saved_scancode;

    keybd_ring_save(saved_ring);
    saved_scancode = data_fdps_input_last_scancode;
    data_fdps_input_last_scancode = 0x2a;   /* left Shift, latched */
    data_fdps_input_scancode_queue[4] = 0x1c;
    data_fdps_input_scancode_queue[5] = 0x39;
    data_fdps_input_scancode_queue_head = 4;
    data_fdps_input_scancode_queue_write_index = 6;

    fdps_read_keyboard_queue();

    CHECK_EQ(data_fdps_input_scancode_queue[4], 0x1c);
    CHECK_EQ(data_fdps_input_scancode_queue[5], 0x39);
    CHECK_EQ(data_fdps_input_scancode_queue_write_index, 6);
    CHECK_EQ(data_fdps_input_last_scancode, 0x2a);

    data_fdps_input_last_scancode = saved_scancode;
    keybd_ring_restore(saved_ring);
    data_fdps_input_scancode_queue_head = 0;
    data_fdps_input_scancode_queue_write_index = 0;
}

/* The ring is one object of exactly ten bytes.  Both users bound their index by
   the same ten (CMP ...,0xa at 000567db and 0005687b) and both name the base
   0x7000f in the instruction itself, so a shorter array would be indexed off
   its end and a longer one would put the two indices somewhere other than
   where the ring's users expect -- and neither the compiler nor the build gate
   can see either mistake. */
static void keybd_read_queue_ring_is_exactly_ten_entries(void)
{
    CHECK_EQ((int) sizeof(data_fdps_input_scancode_queue),
             SCANCODE_QUEUE_LEN);
    CHECK_EQ((int) sizeof(data_fdps_input_scancode_queue[0]), 1);
}

/* ---------------------------------------------------------------------------
 * fdps_install_keyboard_isr at 000567f0.
 *
 * This is the one function covered here with an effect outside the program's
 * own memory: it moves interrupt vector 09h.  Testing it means letting it move
 * the vector for real and then putting the vector back, and while it is moved
 * the vector points at whatever the build has for fdps_keyboard_isr -- which is
 * the stub, `return 0`, a RET where the CPU will have pushed an interrupt frame.
 * A key pressed inside that window would not return from it.
 *
 * So the window is fenced: IRQ1 is masked at the 8259 for the duration, which
 * stops the interrupt being delivered at all rather than merely deferring it
 * the way CLI would -- DOS re-enables interrupts inside the very INT 21h calls
 * being tested, so CLI would not hold.  The mask is put back exactly as it was
 * found, and so is the vector.
 *
 * The probes below are separate in-line assembly from the ones in src/keybd.c
 * on purpose: this file reads the vector back through its own INT 21h AH=35h
 * and compares that against what the function under test filed away, so the two
 * have to agree about a value neither of them chose.
 * ------------------------------------------------------------------------- */

/* INT 21h AH=35h for vector 09h, straight: offset as the result, selector
   through the pointer.  ESI is saved across the INT for the same reason the
   production copy saves it -- nothing in the image says a DOS call preserves
   it. */
extern unsigned int probe_read_int9_vector(unsigned short *selector_out);
#pragma aux probe_read_int9_vector =    \
    "push es"                           \
    "push esi"                          \
    "mov  eax,3509h"                    \
    "int  21h"                          \
    "mov  ax,es"                        \
    "pop  esi"                          \
    "mov  [esi],ax"                     \
    "pop  es"                           \
    parm [esi]                          \
    value [ebx]                         \
    modify [eax ebx ecx edx];

/* INT 21h AH=25h for vector 09h with an arbitrary selector:offset, which is
   what putting the original handler back needs and what the production pair
   deliberately cannot do: fdps_keybd_set_int9_vector_asm always installs into CS. */
extern void probe_write_int9_vector(unsigned short selector,
                                    unsigned int offset);
#pragma aux probe_write_int9_vector =   \
    "push ds"                           \
    "mov  eax,2509h"                    \
    "mov  ds,cx"                        \
    "int  21h"                          \
    "pop  ds"                           \
    parm [cx] [edx]                     \
    modify [eax ebx ecx edx];

extern unsigned short probe_current_cs(void);
#pragma aux probe_current_cs = "mov ax,cs" value [ax] modify [eax];

extern unsigned short probe_current_ds(void);
#pragma aux probe_current_ds = "mov ax,ds" value [ax] modify [eax];

extern unsigned short probe_current_es(void);
#pragma aux probe_current_es = "mov ax,es" value [ax] modify [eax];

/* Set bit 1 of the master 8259's interrupt mask register, so IRQ1 cannot be
   delivered, and hand back the mask as it was so it can be put back byte for
   byte.  Port 0x21 is the master PIC's IMR; the game's own handler talks to
   the same chip at port 0x20 (MOV AL,0x20 / OUT 0x20,AL at 0005688e), so the
   program is entitled to this port in exactly the way it is entitled to that
   one. */
extern unsigned char probe_mask_irq1(void);
#pragma aux probe_mask_irq1 =           \
    "in   al,21h"                       \
    "mov  ah,al"                        \
    "or   al,2"                         \
    "out  21h,al"                       \
    "mov  al,ah"                        \
    value [al]                          \
    modify [eax];

extern void probe_restore_irq_mask(unsigned char mask);
#pragma aux probe_restore_irq_mask = "out 21h,al" parm [al] modify [eax];

/* What one fenced install saw, filled in by keybd_probe_install below and read
   by the cases after it.  They are file-scope rather than passed around because
   each case asserts about a different part of the same single observation. */
static unsigned short probe_prev_selector;
static unsigned int probe_prev_offset;
static unsigned short probe_new_selector;
static unsigned int probe_new_offset;
static unsigned short probe_ds_before;
static unsigned short probe_ds_after;
static unsigned short probe_es_before;
static unsigned short probe_es_after;

/* Call fdps_install_keyboard_isr for real with IRQ1 masked, record the vector
   as it was and as the call left it, then put the vector and the mask back.
   Every case below runs this first, so each one starts from the same machine
   state and none of them leaves the stub handler on the vector. */
static void keybd_probe_install(void)
{
    unsigned char saved_irq_mask;

    saved_irq_mask = probe_mask_irq1();

    probe_prev_offset = probe_read_int9_vector(&probe_prev_selector);
    probe_ds_before = probe_current_ds();
    probe_es_before = probe_current_es();

    fdps_install_keyboard_isr();

    probe_ds_after = probe_current_ds();
    probe_es_after = probe_current_es();
    probe_new_offset = probe_read_int9_vector(&probe_new_selector);

    probe_write_int9_vector(probe_prev_selector, probe_prev_offset);
    probe_restore_irq_mask(saved_irq_mask);
}

/* The vector that was there has to survive in the two globals, because those
   two are the whole of what fdps_uninstall_keyboard_isr has to put back (MOV
   EDX,dword ptr [0x00070002] / MOV AX,[0x00070000] / MOV DS,AX at 00056819,
   0005681f and 00056825).  Expected values are not constants: they are whatever
   this machine's vector 09h held a moment earlier, read independently. */
static void keybd_install_saves_the_vector_it_replaced(void)
{
    keybd_probe_install();

    CHECK_EQ(data_fdps_prev_int9_handler_offset == probe_prev_offset, 1);
    CHECK_EQ(data_fdps_input_prev_int9_handler_selector == probe_prev_selector,
             1);
}

/* Whatever was saved must be the handler that was actually displaced, not the
   one just installed.  Reading the vector back after the call and finding it
   equal to what was filed would mean the function saved its own handler, and
   the uninstaller would then "restore" the game's ISR and leave it running
   after shutdown with nothing failing to say so. */
static void keybd_install_saves_the_old_handler_not_the_new_one(void)
{
    keybd_probe_install();

    CHECK_EQ(data_fdps_prev_int9_handler_offset == probe_new_offset, 0);
}

/* MOV EDX,0x56837 / MOV AL,0x9 / MOV AH,0x25 / INT 0x21 at 0005680a..00056813:
   the offset installed on vector 09h is the address of fdps_keyboard_isr.  The
   literal in the image is where the original linker put the handler; here the
   only right answer is the symbol's own address, and a rebuild that wrote the
   constant back would install whatever happens to live at 0x56837. */
static void keybd_install_points_int9_at_the_game_handler(void)
{
    keybd_probe_install();

    CHECK_EQ(probe_new_offset == (unsigned int) fdps_keyboard_isr, 1);
    CHECK_EQ(probe_new_offset != probe_prev_offset, 1);
}

/* PUSH CS / POP DS at 00056808 before AH=25h: the selector half of the
   installed vector is the code selector, not the data selector the compiler
   keeps in DS.  Both address the same bytes under DOS/4GW and only one of them
   is executable, so this is the difference between a working hook and a fault
   on the first key pressed -- and nothing in a link or a unit test other than
   this one can see which was used. */
static void keybd_install_uses_the_code_selector(void)
{
    keybd_probe_install();

    CHECK_EQ(probe_new_selector == probe_current_cs(), 1);
}

/* PUSH DS / PUSH ES at 000567f0 and POP ES / POP DS at 00056815: the function
   hands both segment registers back exactly as it found them.  It has to --
   every memory reference the compiler generates after the call assumes DS and
   ES still address the flat data segment -- and DS in particular is left
   holding CS in the middle of the routine, so a missing restore would corrupt
   the caller rather than this function. */
static void keybd_install_leaves_the_segment_registers_alone(void)
{
    keybd_probe_install();

    CHECK_EQ(probe_ds_after == probe_ds_before, 1);
    CHECK_EQ(probe_es_after == probe_es_before, 1);
}

/* The installer writes the two vector slots and nothing else.  There is no
   store to 0x00070006 anywhere in 000567f0 -- that is the uninstaller's move
   (MOV byte ptr [0x00070006],0xff at 0005682f) -- and none to the ring or
   either index either.  Clearing the latch here would look tidy and would
   throw away a key the caller had not read yet. */
static void keybd_install_leaves_the_queue_state_alone(void)
{
    unsigned char saved_ring[SCANCODE_QUEUE_LEN];
    unsigned char saved_scancode;

    keybd_ring_save(saved_ring);
    saved_scancode = data_fdps_input_last_scancode;
    data_fdps_input_last_scancode = 0x39;   /* space, latched and unread */
    data_fdps_input_scancode_queue[2] = 0x1c;
    data_fdps_input_scancode_queue_head = 2;
    data_fdps_input_scancode_queue_write_index = 3;

    keybd_probe_install();

    CHECK_EQ(data_fdps_input_last_scancode, 0x39);
    CHECK_EQ(data_fdps_input_scancode_queue[2], 0x1c);
    CHECK_EQ(data_fdps_input_scancode_queue_head, 2);
    CHECK_EQ(data_fdps_input_scancode_queue_write_index, 3);

    data_fdps_input_last_scancode = saved_scancode;
    keybd_ring_restore(saved_ring);
    data_fdps_input_scancode_queue_head = 0;
    data_fdps_input_scancode_queue_write_index = 0;
}

/* The saved selector is two bytes and the saved offset is four, which is what
   the image's own instruction widths say: MOV [0x00070000],AX carries the
   operand-size prefix and moves sixteen bits, MOV dword ptr [0x00070002],EBX
   moves thirty-two.  A selector declared four bytes wide would swallow the
   first two bytes of the offset if ticket 23 laid them out adjacent. */
static void keybd_install_vector_slots_are_the_widths_the_image_uses(void)
{
    CHECK_EQ((int) sizeof(data_fdps_input_prev_int9_handler_selector), 2);
    CHECK_EQ((int) sizeof(data_fdps_prev_int9_handler_offset), 4);
}

/* --- fdps_uninstall_keyboard_isr at 00056818 -------------------------------
 *
 * The installer's mirror, fenced the same way and for the same reason.  A round
 * trip has to put the game's handler on the vector before it can take it off,
 * and that handler is the not-yet-emitted stub, so IRQ1 stays masked at the
 * 8259 across every window below; the vector and the mask are put back exactly
 * as they were found.
 *
 * The probes above are reused unchanged.  Their whole point applies here too:
 * probe_read_int9_vector asks DOS what is on the vector through its own INT 21h
 * AH=35h, so the function under test and the test have to agree about a value
 * neither of them chose -- this machine's own vector 09h.
 * -------------------------------------------------------------------------- */

/* What one fenced install-then-uninstall round trip saw.  File-scope for the
   same reason the installer's are: every case asserts about a different part of
   one observation. */
static unsigned short uninst_prev_selector;
static unsigned int uninst_prev_offset;
static unsigned short uninst_hooked_selector;
static unsigned int uninst_hooked_offset;
static unsigned short uninst_restored_selector;
static unsigned int uninst_restored_offset;
static unsigned short uninst_ds_before;
static unsigned short uninst_ds_after;
static unsigned short uninst_es_before;
static unsigned short uninst_es_after;
static unsigned char uninst_latch_after;
static unsigned int uninst_saved_offset_after;
static unsigned short uninst_saved_selector_after;

/* Install for real and then uninstall for real, with IRQ1 masked throughout,
   recording the vector as it was, as the install left it and as the uninstall
   left it.  The vector and the mask are put back afterwards even though a
   correct uninstall has already restored the vector itself -- if it has not,
   the machine must still be handed back intact for the cases that follow. */
static void keybd_probe_uninstall(void)
{
    unsigned char saved_irq_mask;
    unsigned char saved_scancode;

    saved_scancode = data_fdps_input_last_scancode;
    saved_irq_mask = probe_mask_irq1();

    uninst_prev_offset = probe_read_int9_vector(&uninst_prev_selector);
    fdps_install_keyboard_isr();
    uninst_hooked_offset = probe_read_int9_vector(&uninst_hooked_selector);

    /* A key the handler might have latched a moment before the uninstall, so
       that the 0xff the uninstall is meant to store cannot pass on a byte that
       already held it. */
    data_fdps_input_last_scancode = 0x39;   /* space */
    uninst_ds_before = probe_current_ds();
    uninst_es_before = probe_current_es();

    fdps_uninstall_keyboard_isr();

    uninst_ds_after = probe_current_ds();
    uninst_es_after = probe_current_es();
    uninst_latch_after = data_fdps_input_last_scancode;
    uninst_restored_offset = probe_read_int9_vector(&uninst_restored_selector);
    uninst_saved_offset_after = data_fdps_prev_int9_handler_offset;
    uninst_saved_selector_after = data_fdps_input_prev_int9_handler_selector;

    probe_write_int9_vector(uninst_prev_selector, uninst_prev_offset);
    probe_restore_irq_mask(saved_irq_mask);

    data_fdps_input_last_scancode = saved_scancode;
}

/* The pair the installer filed has to come back onto the vector in both halves:
   MOV EDX,dword ptr [0x00070002] / MOV AX,[0x00070000] / MOV DS,AX / MOV
   EAX,0x2509 / INT 0x21 at 00056819..0005682c is DS:EDX handed to AH=25h for
   vector 09h.  The expected values are not constants -- they are whatever this
   machine's vector 09h held before the install, read independently through the
   test's own AH=35h. */
static void keybd_uninstall_puts_the_saved_vector_back(void)
{
    keybd_probe_uninstall();

    CHECK_EQ(uninst_restored_offset == uninst_prev_offset, 1);
    CHECK_EQ(uninst_restored_selector == uninst_prev_selector, 1);
}

/* And it really does move the vector: the window it is undoing had the game's
   own handler on it, and the address that comes back afterwards is a different
   one.  Without this the case above would also pass on a function that did
   nothing at all, since the vector would still hold what it held before. */
static void keybd_uninstall_takes_the_game_handler_off_the_vector(void)
{
    keybd_probe_uninstall();

    CHECK_EQ(uninst_hooked_offset == (unsigned int) fdps_keyboard_isr, 1);
    CHECK_EQ(uninst_restored_offset != uninst_hooked_offset, 1);
}

/* MOV byte ptr [0x00070006],0xff at 0005682f: the latched scancode is parked at
   the no-key value on the way out.  It is the one byte of the keyboard state
   this function writes, and it has to be written -- with the game's handler off
   the vector nothing will overwrite it again, so a key still latched from
   before the uninstall would be reported by the next poll of it. */
static void keybd_uninstall_parks_the_latched_scancode(void)
{
    keybd_probe_uninstall();

    CHECK_EQ(uninst_latch_after, 0xff);
}

/* There is no store to 0x00070000 or 0x00070002 in the body: the two saved
   halves are read and left as they are.  That is what lets
   fdps_cd_verify_disc_and_play_track and fdps_play_movie uninstall, hand the
   machine to the CD or the movie player and install again -- and it means a
   second uninstall with no install between restores the same vector rather than
   a cleared one. */
static void keybd_uninstall_leaves_the_saved_vector_slots_alone(void)
{
    keybd_probe_uninstall();

    CHECK_EQ(uninst_saved_offset_after == uninst_prev_offset, 1);
    CHECK_EQ(uninst_saved_selector_after == uninst_prev_selector, 1);
}

/* PUSH DS at 00056818 and POP DS at 0005682e: DS is handed back as it was
   found, and ES is never touched at all.  DS is left holding the restored
   handler's selector across the INT, which is not a data selector on any
   machine, so a missing restore would corrupt the caller rather than this
   function -- and would do it on the very last thing the game does before it
   exits, where it is hardest to attribute. */
static void keybd_uninstall_leaves_the_segment_registers_alone(void)
{
    keybd_probe_uninstall();

    CHECK_EQ(uninst_ds_after == uninst_ds_before, 1);
    CHECK_EQ(uninst_es_after == uninst_es_before, 1);
}

/* The ring and both its indices survive the uninstall untouched -- the body
   writes one byte and 0x0007000f, 0x00070019 and 0x0007001d are not it.  This
   is behaviour the callers depend on rather than an accident: the queue is
   still readable after the hook comes down, and clearing it here would be the
   tidy-looking change that throws away codes queued before the uninstall. */
static void keybd_uninstall_leaves_the_queue_alone(void)
{
    unsigned char saved_ring[SCANCODE_QUEUE_LEN];

    keybd_ring_save(saved_ring);
    data_fdps_input_scancode_queue[2] = 0x1c;   /* Return, queued and unread */
    data_fdps_input_scancode_queue_head = 2;
    data_fdps_input_scancode_queue_write_index = 3;

    keybd_probe_uninstall();

    CHECK_EQ(data_fdps_input_scancode_queue[2], 0x1c);
    CHECK_EQ(data_fdps_input_scancode_queue_head, 2);
    CHECK_EQ(data_fdps_input_scancode_queue_write_index, 3);

    keybd_ring_restore(saved_ring);
    data_fdps_input_scancode_queue_head = 0;
    data_fdps_input_scancode_queue_write_index = 0;
}

/* The selector half comes out of the saved global, not out of CS, and that is
   the one place this function must differ from the installer it mirrors.  The
   installer loads DS from CS because it is installing its own handler; copying
   that here would file somebody else's handler offset against the game's code
   selector, and nothing would fault until the first key pressed after the game
   let the keyboard go.

   So the globals are pointed at a vector that differs from the installer's in
   both halves -- the flat data selector, which is a valid selector this side of
   the fence and is never the code selector -- and the vector is expected to
   come back holding exactly that.  The handler on the vector is never entered:
   IRQ1 is masked for the whole window and the machine's own vector goes back
   before the mask does. */
static void keybd_uninstall_uses_the_saved_selector_not_cs(void)
{
    unsigned char saved_irq_mask;
    unsigned char saved_scancode;
    unsigned short saved_globals_selector;
    unsigned int saved_globals_offset;
    unsigned short machine_selector;    /* vector 09h as this machine has it */
    unsigned int machine_offset;
    unsigned short data_selector;       /* the selector asked for, in DS */
    unsigned short restored_selector;   /* what the vector ended up holding */
    unsigned int restored_offset;

    data_selector = probe_current_ds();
    saved_scancode = data_fdps_input_last_scancode;
    saved_globals_selector = data_fdps_input_prev_int9_handler_selector;
    saved_globals_offset = data_fdps_prev_int9_handler_offset;

    saved_irq_mask = probe_mask_irq1();
    machine_offset = probe_read_int9_vector(&machine_selector);

    probe_write_int9_vector(probe_current_cs(),
                            (unsigned int) fdps_keyboard_isr);
    data_fdps_input_prev_int9_handler_selector = data_selector;
    data_fdps_prev_int9_handler_offset = (unsigned int) fdps_wait_any_key;

    fdps_uninstall_keyboard_isr();

    restored_offset = probe_read_int9_vector(&restored_selector);

    probe_write_int9_vector(machine_selector, machine_offset);
    probe_restore_irq_mask(saved_irq_mask);

    data_fdps_input_prev_int9_handler_selector = saved_globals_selector;
    data_fdps_prev_int9_handler_offset = saved_globals_offset;
    data_fdps_input_last_scancode = saved_scancode;

    /* The discrimination only means anything while the two selectors differ,
       which under DOS/4GW they always do -- code and data are separate
       descriptors -- so it is asserted rather than assumed. */
    CHECK_EQ(data_selector != probe_current_cs(), 1);
    CHECK_EQ(restored_selector == data_selector, 1);
    CHECK_EQ(restored_offset == (unsigned int) fdps_wait_any_key, 1);
}

void run_keybd_tests(void)
{
    RUN_TEST(keybd_scancode_ptr_names_the_isr_slot);
    RUN_TEST(keybd_scancode_ptr_is_the_same_slot_every_call);
    RUN_TEST(keybd_scancode_ptr_reaches_the_byte_the_isr_writes);
    RUN_TEST(keybd_scancode_ptr_is_one_byte_wide);
    RUN_TEST(keybd_scancode_ptr_reads_high_codes_unsigned);
    RUN_TEST(keybd_wait_any_key_empties_the_queue_it_waited_for);
    RUN_TEST(keybd_wait_any_key_discards_every_pending_code);
    RUN_TEST(keybd_wait_any_key_handles_a_wrapped_write_index);
    RUN_TEST(keybd_wait_any_key_leaves_indices_inside_the_ring);
    RUN_TEST(keybd_flush_discards_the_pending_codes);
    RUN_TEST(keybd_flush_returns_at_once_on_an_empty_queue);
    RUN_TEST(keybd_flush_handles_a_wrapped_write_index);
    RUN_TEST(keybd_flush_leaves_indices_inside_the_ring);
    RUN_TEST(keybd_flush_leaves_the_latched_scancode_alone);
    RUN_TEST(keybd_repeat_reports_a_new_key_unchanged);
    RUN_TEST(keybd_repeat_new_key_does_not_record_the_tick);
    RUN_TEST(keybd_repeat_stays_silent_within_one_tick);
    RUN_TEST(keybd_repeat_counts_one_tick_at_a_time);
    RUN_TEST(keybd_repeat_first_repeat_lands_on_the_sixth_tick);
    RUN_TEST(keybd_repeat_needs_the_delay_and_the_period_together);
    RUN_TEST(keybd_repeat_widens_the_latch_unsigned);
    RUN_TEST(keybd_repeat_never_reports_the_no_key_value_as_a_key);
    RUN_TEST(keybd_repeat_flushes_the_queue_on_every_path);
    RUN_TEST(keybd_repeat_leaves_the_latched_scancode_alone);
    RUN_TEST(keybd_read_queue_reports_the_marker_when_empty);
    RUN_TEST(keybd_read_queue_empty_does_not_advance_or_wrap_the_index);
    RUN_TEST(keybd_read_queue_takes_the_entry_at_the_read_index);
    RUN_TEST(keybd_read_queue_returns_the_codes_in_order);
    RUN_TEST(keybd_read_queue_wraps_the_read_index_at_ten);
    RUN_TEST(keybd_read_queue_does_not_wrap_before_ten);
    RUN_TEST(keybd_read_queue_handles_a_wrapped_write_index);
    RUN_TEST(keybd_read_queue_writes_only_the_read_index);
    RUN_TEST(keybd_read_queue_ring_is_exactly_ten_entries);
    RUN_TEST(keybd_install_saves_the_vector_it_replaced);
    RUN_TEST(keybd_install_saves_the_old_handler_not_the_new_one);
    RUN_TEST(keybd_install_points_int9_at_the_game_handler);
    RUN_TEST(keybd_install_uses_the_code_selector);
    RUN_TEST(keybd_install_leaves_the_segment_registers_alone);
    RUN_TEST(keybd_install_leaves_the_queue_state_alone);
    RUN_TEST(keybd_install_vector_slots_are_the_widths_the_image_uses);
    RUN_TEST(keybd_uninstall_puts_the_saved_vector_back);
    RUN_TEST(keybd_uninstall_takes_the_game_handler_off_the_vector);
    RUN_TEST(keybd_uninstall_parks_the_latched_scancode);
    RUN_TEST(keybd_uninstall_leaves_the_saved_vector_slots_alone);
    RUN_TEST(keybd_uninstall_leaves_the_segment_registers_alone);
    RUN_TEST(keybd_uninstall_leaves_the_queue_alone);
    RUN_TEST(keybd_uninstall_uses_the_saved_selector_not_cs);
}
