/* table.c -- record accessors for the game's static data tables.
 *
 * See table.h.  This file owns no state of its own: the table base pointers it
 * reads are gamedata.c's, filled once at startup by fdps_load_data_tables and
 * released by fdps_free_global_resource_buffers.
 */
#include "fdpstype.h"
#include "gamedata.h"
#include "table.h"

/* The stride of one FRIAPRDA.DAT record, as the original writes it: IMUL
   EAX,dword ptr [EBP+0x14],0x18.  It is a literal and not sizeof(struct
   fdps_character_base_record) on purpose -- the two agree only for as long as
   the struct stays byte-packed, and the file's stride is the fact that has to
   survive (rebuild_info/pitfalls.md). */
#define CHARACTER_BASE_RECORD_STRIDE 0x18

/* 00018ab0.  The whole body is IMUL EAX,dword ptr [EBP+0x14],0x18 / MOV
   EDX,dword ptr [0x00063fd8] / ADD EDX,EAX, spilled to a stack local and
   reloaded into EAX to be returned.  No compare, no branch, no CALL: the
   function has exactly one basic block.

   The multiply is IMUL, the signed form, so a negative char_id walks backwards
   off the front of the table rather than wrapping to a huge offset.  Nothing
   here can tell -- the low 32 bits of a signed and an unsigned multiply are
   the same bits -- but it is what decides the answer once the product is added
   to the base, and int is the type the parameter is read at.

   The table base is read fresh from the global on every call; it is not
   cached, and it is not tested for null, so a call before
   fdps_load_data_tables has run returns the offset alone as though it were an
   address.  Both callers run well after the load. */
struct fdps_character_base_record *fdps_get_character_base_record(int char_id)
{
    struct fdps_character_base_record *record;

    record = (struct fdps_character_base_record *)
        (data_fdps_battle_character_base_table_ptr
         + char_id * CHARACTER_BASE_RECORD_STRIDE);
    return record;
}
