/* keybd.c -- keyboard input: the game's INT 09h handler, the scancode queue it
 * fills, and the readers that drain it.
 *
 * The game replaces the BIOS keyboard interrupt with a handler of its own and
 * keeps two pieces of state behind it: a ten-entry ring of make codes, and a
 * single byte holding the last raw scancode the hardware produced.  This file
 * holds the accessors for both; the state itself is defined here once ticket 23
 * emits it.
 */
#include "keybd.h"

unsigned char *fdps_keyboard_scancode_ptr(void)
{
    /* 00056799: LEA EAX,[0x70006] / RET.  Two instructions, seven bytes, no
       prologue and no frame -- this is `return &g;` and nothing else.

       The literal 0x70006 in the image is the address the original linker gave
       the byte; the rebuild does not place anything where the original placed
       it, so the address has to come from the symbol.  Writing the constant
       back would point at whatever happens to live there instead, and neither
       the compiler nor the build gate would say a word.  See
       rebuild_info/pitfalls.md. */
    return &data_fdps_input_last_scancode;
}

void fdps_wait_any_key(void)
{
    /* 000567a0: MOV EAX,[0x70019] / CMP EAX,[0x7001d] / JZ back to the top /
       MOV [0x7001d],EAX / RET.  Five instructions, no prologue and no frame,
       so there is no local here either -- the value the store writes is the
       one the compare loaded, and the read index cannot have moved between the
       two: fdps_read_keyboard_queue is its only writer and it is not running.

       The loop condition is the queue-empty test the whole ring is built on:
       equal indices mean nothing is pending.  The wait ends because the INT
       09h handler advances the write index for every make code it queues, and
       that is why the write index is declared volatile at its extern -- with
       nothing inside this loop writing anything, an unqualified read may be
       hoisted out of it and the wait never ends. */
    while (data_fdps_input_scancode_queue_head ==
           data_fdps_input_scancode_queue_write_index) {
        /* spin: only the keyboard interrupt can end this */
    }

    /* Rewind the queue to empty rather than dequeue: the key that ended the
       wait and everything queued behind it are discarded, so whatever screen
       follows starts with no keypress of this one's left in the ring.  The
       ring's bytes are deliberately left alone -- the handler overwrites an
       entry when it reaches it, and the read index alone decides what is
       readable. */
    data_fdps_input_scancode_queue_write_index =
        data_fdps_input_scancode_queue_head;
}
