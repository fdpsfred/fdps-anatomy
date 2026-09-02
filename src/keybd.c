/* keybd.c -- keyboard input: the game's INT 09h handler, the scancode queue it
 * fills, and the readers that drain it.
 *
 * The game replaces the BIOS keyboard interrupt with a handler of its own and
 * keeps two pieces of state behind it: a ten-entry ring of make codes, and a
 * single byte holding the last raw scancode the hardware produced.  This file
 * holds the accessors for both; the state itself is defined here once ticket 23
 * emits it.
 *
 * On top of those accessors sits the auto-repeat filter the modal input loops
 * poll, which lives at 000178f0 -- far from the rest of this file in the image,
 * but it is this file's three private globals it runs on.  It is the only user
 * of the timer tick counter here, which is why gamedata.h is included.
 */
#include "gamedata.h"
#include "keybd.h"

/* The latch's no-key value, and equally the filter's "nothing to report this
   poll" answer.  The ISR never produces it as a scancode: 0xff is a break code
   for a make code of 0x7f, which no key on the keyboard has. */
#define SCANCODE_NONE 0xff

/* The auto-repeat schedule, in timer ticks of the key being held.  Nothing is
   reported until the hold has lasted REPEAT_DELAY_TICKS, and from there a
   report goes out on every tick whose count divides by REPEAT_PERIOD_TICKS --
   so the first repeat lands on tick 6, then 9, 12 and on. */
#define REPEAT_DELAY_TICKS 5
#define REPEAT_PERIOD_TICKS 3

unsigned int fdps_read_scancode_auto_repeat(void)
{
    /* The scancode this poll will report, which starts as the raw latched byte
       and is replaced by SCANCODE_NONE on every path that decides the caller
       should hear nothing.  It is a full unsigned int holding a zero-extended
       byte, exactly as the assembly widens it: XOR EAX,EAX / MOV AL,byte ptr
       [EDX] at 00017903, so a break code arrives as 156 rather than -100. */
    unsigned int scancode;

    scancode = *fdps_keyboard_scancode_ptr();

    if (scancode != data_fdps_input_key_repeat_prev_scancode) {
        /* A different code from last time: a key went down, or the one that
           was down came up (the latch goes to 0xff, which is a change like any
           other).  Report it as it stands and start the hold count over.

           data_fdps_input_key_repeat_last_tick is deliberately NOT touched
           here.  Recording the current tick on this path is the obvious tidy-up
           and it moves the start of the repeat delay by a tick, because the
           first held poll of a new key is meant to compare against the tick
           some earlier key left behind (rebuild_info/pitfalls.md). */
        data_fdps_input_key_repeat_counter = 0;
        data_fdps_input_key_repeat_prev_scancode = scancode;
    } else if (data_fdps_input_key_repeat_last_tick ==
               data_fdps_timer_tick_counter) {
        /* Same key, and the timer has not moved since the last poll that acted:
           say nothing and change nothing, so polling faster than the timer
           cannot make the key repeat faster. */
        scancode = SCANCODE_NONE;
    } else {
        /* Same key and a new tick: the hold is one tick longer.  The report
           goes out only on the ticks the schedule allows; the rest are
           silenced, but the count and the serviced tick advance either way. */
        data_fdps_input_key_repeat_counter = data_fdps_input_key_repeat_counter + 1;
        if (data_fdps_input_key_repeat_counter < REPEAT_DELAY_TICKS ||
            data_fdps_input_key_repeat_counter % REPEAT_PERIOD_TICKS != 0) {
            scancode = SCANCODE_NONE;
        }
        data_fdps_input_key_repeat_last_tick = data_fdps_timer_tick_counter;
    }

    /* Every path ends here.  Whatever the ISR queued since the last poll is
       thrown away, so this reader answers from the latch alone; draining the
       queue instead would hand callers presses the original never reports. */
    fdps_flush_keyboard_queue();
    return scancode;
}

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

void fdps_flush_keyboard_queue(void)
{
    /* 000567b3: MOV EAX,[0x70019] / MOV [0x7001d],EAX / RET.  Eleven bytes,
       three instructions, no prologue and no frame: the whole function is the
       rewind fdps_wait_any_key above performs after its spin, without the
       spin.  There is no local to name -- EAX is the load's landing place and
       nothing else, and no caller reads it (all 32 call sites push nothing,
       adjust nothing afterwards and either overwrite EAX from memory or call
       something else before touching it).

       Both operand addresses are dword references to the two ring indices, so
       they are written as the symbols; 0x70019 and 0x7001d are where the
       original linker put them and mean nothing in the rebuild
       (rebuild_info/pitfalls.md).

       Making the two indices equal is the queue-empty condition
       fdps_read_keyboard_queue tests (CMP EBX,dword ptr [0x0007001d] / JZ to
       its 0xff exit), so every scancode still in the ring becomes unreachable.
       Nothing else is disturbed: the ring bytes stay as the handler left them,
       and so does data_fdps_input_last_scancode -- callers that want that byte
       cleared store 0xff through fdps_keyboard_scancode_ptr themselves. */
    data_fdps_input_scancode_queue_write_index =
        data_fdps_input_scancode_queue_head;
}

unsigned char fdps_read_keyboard_queue(void)
{
    /* The code this call hands back.  AL carries it through the whole of the
       assembly: MOV AL,0xff at 000567bf puts the marker there before anything
       is tested, and the single path that finds an entry overwrites AL with it
       at 000567cf.  A byte and not a widened int, because only AL is ever set
       -- the rest of EAX keeps whatever the caller left in it, which is why
       every call site widens the answer itself with AND EAX,0xff. */
    unsigned char scancode;

    scancode = SCANCODE_NONE;

    /* Equal indices mean the queue is empty, and that is the only emptiness
       test the ring has: CMP EBX,dword ptr [0x0007001d] / JZ at 000567c7 to the
       exit.  There is no count and no fullness test anywhere -- the handler
       advances the write index unconditionally, so ten unread codes bring the
       write index back round to the read index and this reads as empty with
       the ring full.  Adding the pending count that would fix it changes which
       keystrokes the game sees (rebuild_info/pitfalls.md). */
    if (data_fdps_input_scancode_queue_head !=
        data_fdps_input_scancode_queue_write_index) {
        scancode = data_fdps_input_scancode_queue[
                       data_fdps_input_scancode_queue_head];

        /* Only the read index is written, and that is the contract with the
           INT 09h handler: it owns the write index and the ring's bytes, this
           owns the read index, and neither ever writes the other's state, so
           an interrupt landing anywhere inside here cannot corrupt either
           side.  The entry just taken is deliberately left in the ring -- the
           handler overwrites it when the write index comes round again, and
           the read index alone decides what is readable.

           The wrap is an equality against the ring's length, not a modulus and
           not a >= test, and it sits INSIDE this branch: the empty case at the
           top returns without touching the index at all. */
        data_fdps_input_scancode_queue_head =
            data_fdps_input_scancode_queue_head + 1;
        if (data_fdps_input_scancode_queue_head == SCANCODE_QUEUE_LEN) {
            data_fdps_input_scancode_queue_head = 0;
        }
    }

    return scancode;
}
