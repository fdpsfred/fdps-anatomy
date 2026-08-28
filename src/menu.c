/* menu.c -- the four-way command menus the battle screens put a cursor on.
 *
 * See menu.h for what a menu descriptor is.  Everything in this file works on
 * a caller-supplied descriptor; there is no menu state of its own here.
 */
#include "menu.h"

/* 000160e0.  The four-entry bound is hard-coded in the original -- CMP
   dword ptr [EBP-8],4 / JL -- and is not derived from anything the caller
   passes, so it stays a literal here.  JL is the signed compare, which is why
   the index is a plain int.

   The entry test is CMP dword ptr [EAX],0 / JNZ: any non-zero value means
   greyed out, negative included.  It is not a "> 0" test, and writing one
   would let a negative entry be picked. */
int fdps_menu_find_first_enabled_entry(int *cmd_disabled)
{
    int index;

    for (index = 0; index < 4; index++) {
        if (cmd_disabled[index] == 0) {
            return index;
        }
    }
    return -1;
}
