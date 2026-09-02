/* main.c -- entry point and global resource lifecycle.
 *
 * See main.h.  Nothing here owns state of its own: the nine data-table
 * pointers it fills at startup and releases at shutdown are gamedata.c's, and
 * the container they are read out of belongs to whoever opened it.
 */
#include <stdlib.h>
#include "gamedata.h"
#include "vfs.h"
#include "main.h"

/* 00018930.  Nine identical groups, each MOV EAX,<destination global> / PUSH /
   MOV EAX,<filename> / PUSH / MOV EAX,[EBP + 0x14] / PUSH / CALL 00029400 /
   ADD ESP,0xc, and nothing else: one basic block, no compare, no jump and no
   local.  The frame is the canonical Watcom one with SUB ESP,0x0, and the
   argument is read fresh from [EBP + 0x14] before each of the nine calls
   rather than being kept in a register, which is what -od does with a
   parameter and not a sign that anything reassigns it.

   The PUSH triples and the ADD ESP,0xc belong to the callee's stack
   convention; this function's own is the same one, RET with no immediate.

   Nothing is done with what the loader leaves behind.  fdps_vfs_load_file_or_exit
   returns void and writes its answer through the third argument, so the
   destination global is written by the callee and read by nobody here; and its
   failure arm calls fdps_wait_any_key and exit(1) instead of coming back, which
   is why nine unguarded loads in a row need no test between them.

   The order of the nine calls is the order of the assembly, which is neither
   the order of the globals in memory nor the order fdps_free_global_resource_buffers
   frees them in.  Nothing observable depends on it -- the nine loads are
   independent -- so it is reproduced because it is what the original does.

   The names are passed as literals, exactly as the original passes pointers
   into its own literal pool at 0x6160c..0x61680.  That matters here rather
   than being a style choice: the loader upper-cases the name it is given IN
   PLACE, so each of these nine literals is folded to upper case by the first
   call and stays that way for the life of the process (see vfs.h). */
void fdps_load_data_tables(void *vfs)
{
    fdps_vfs_load_file_or_exit(vfs, "Friaprda.dat",
        (void **)&data_fdps_battle_character_base_table_ptr);
    fdps_vfs_load_file_or_exit(vfs, "FriLevUp.dat",
        (void **)&data_fdps_battle_character_growth_table_ptr);
    fdps_vfs_load_file_or_exit(vfs, "Item.dat",
        (void **)&data_fdps_item_effect_table_ptr);
    fdps_vfs_load_file_or_exit(vfs, "EnemyDat.dat",
        (void **)&data_fdps_battle_enemy_data_table_ptr);
    fdps_vfs_load_file_or_exit(vfs, "ProMap.dat",
        (void **)&data_fdps_class_table_ptr);
    fdps_vfs_load_file_or_exit(vfs, "ProEqu.dat",
        (void **)&data_fdps_class_equip_table_ptr);
    fdps_vfs_load_file_or_exit(vfs, "MagicDat.dat",
        (void **)&data_fdps_battle_spell_effect_table_ptr);
    fdps_vfs_load_file_or_exit(vfs, "GetMgTab.dat",
        (void **)&data_fdps_spell_learning_table_ptr);
    fdps_vfs_load_file_or_exit(vfs, "RankUp.dat",
        (void **)&data_fdps_promotion_table_ptr);
}

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
