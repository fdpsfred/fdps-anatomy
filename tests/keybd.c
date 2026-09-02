/* tests/keybd.c -- cover for src/keybd.c.
 *
 * Covers fdps_keyboard_scancode_ptr at 00056799 and fdps_wait_any_key at
 * 000567a0.  Expected values come from its
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
}
