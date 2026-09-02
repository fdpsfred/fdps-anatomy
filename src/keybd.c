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
