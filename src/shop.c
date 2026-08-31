/* shop.c -- the village shop's stock lookup, item picker and buying flow.
 *
 * See shop.h for what each entry point is asked and what it answers.  The
 * stock itself lives in the chapter's SHOP%02d.DAT image, which
 * fdps_load_field_chapter_resources parks in data_fdps_shop_stock_table_ptr
 * (gamedata.h); nothing here allocates or frees it.
 */
#include "fdpstype.h"
#include "gamedata.h"
#include "shop.h"

/* 00031700.  Walks one twelve-byte row of the shop stock table and packs the
   stocked ids down into the caller's array.

   The loop bound is a literal twelve, CMP dword ptr [EBP-0x10],0xc / JL, and
   there is no second exit: 0xff jumps to the increment at 00031763, not out of
   the loop.  Writing the row's empty slot as a terminator would truncate most
   shipped shops and empty SHOP03.DAT's weapon row entirely
   (rebuild_info/pitfalls.md).

   The row byte is read zero-extended -- XOR EAX,EAX / MOV AL,byte ptr [EDX] at
   00031739 -- which is why data_fdps_shop_stock_table_ptr is an unsigned char
   pointer.  Ids above 0x7f are ordinary stock, so the sign matters twice over:
   for the 0xff test and for the id that lands in the array.

   The two indices are separate.  The row is stepped by the loop counter and
   the destination by the write counter, which only advances on a stocked slot
   (INC dword ptr [EBP-0xc] at 00031760 sits inside the taken branch), so the
   holes are squeezed out and the returned count is the number of stocked
   slots, not the position of the last one. */
int fdps_shop_collect_stock_items(int shop_index, int *out_item_ids)
{
    int write_count;
    int slot;
    int item_id;

    write_count = 0;
    for (slot = 0; slot < 12; slot++) {
        item_id = data_fdps_shop_stock_table_ptr[shop_index * 12 + slot];
        if (item_id != 0xff) {
            out_item_ids[write_count] = item_id;
            write_count++;
        }
    }
    return write_count;
}
