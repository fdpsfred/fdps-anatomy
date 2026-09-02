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
 * The ISR is not installed while these run, so nothing but the cases themselves
 * writes the byte, and each case puts back what it found.  No case asserts what
 * the byte holds to begin with: data_fdps_input_last_scancode is ticket 23's to
 * define and is zero-filled until then.
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
}
