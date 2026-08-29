/* main.c -- entry point and global resource lifecycle.
 *
 * See main.h.  Nothing here owns state of its own: the nine data-table
 * pointers it releases are gamedata.c's, filled by fdps_load_data_tables at
 * startup out of the VFS container.
 */
#include <stdlib.h>
#include "gamedata.h"
#include "main.h"

/* 00018a20.  Nine identical groups, PUSH dword ptr [global] / CALL free /
   ADD ESP,4, and nothing else: no branch, no compare, no local -- the frame is
   the canonical Watcom one with SUB ESP,0x0.  The PUSH and the ADD ESP,4
   belong to free()'s __cdecl convention, not to this function's, which takes
   nothing and returns nothing; the sole caller at 0002964f calls it with an
   empty stack and never looks at EAX.

   The order below is the order of the CALLs, which is not the order of the
   globals in memory (63fd8, 63fec, 63fd4, 63fd0, 63fe0, 63fe4, 63ff0, 63fe8,
   63fdc) and is not the order the loader at 00018930 fills them in either.
   Nothing observable depends on it -- free() has no ordering contract -- so it
   is reproduced because it is what the original does, not because anything is
   known to need it.

   No pointer is tested against null first, which is worth reading against the
   caller: fdps_shutdown_free_resources guards seven of its own pointers with
   CMP/JZ before freeing them and leaves nineteen unguarded, so the guard is a
   statement about which resources are optional, not a house style.  These nine
   are loaded unconditionally at startup, so an unguarded free matches; and in
   any case this CRT's free() returns at once on a null pointer (OR EAX,EAX /
   JZ inside the near-heap worker at 00043d9a), so a failed load costs nothing
   here.

   Nothing stores back into the nine globals -- there is no MOV [global],0
   anywhere in the body -- so they are left holding freed addresses.  That is
   what makes "runs exactly once" a contract rather than a preference: a second
   call is a double free, and the pointers give the caller no way to tell. */
void fdps_free_global_resource_buffers(void)
{
    free(data_fdps_battle_character_base_table_ptr);
    free(data_fdps_battle_character_growth_table_ptr);
    free(data_fdps_battle_enemy_data_table_ptr);
    free(data_fdps_class_table_ptr);
    free(data_fdps_item_effect_table_ptr);
    free(data_fdps_class_equip_table_ptr);
    free(data_fdps_battle_spell_effect_table_ptr);
    free(data_fdps_spell_learning_table_ptr);
    free(data_fdps_promotion_table_ptr);
}
