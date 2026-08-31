/* tests/shop.c -- cover for src/shop.c.
 *
 * Expected values come from the assembly at 00031700 and from the SHOP%02d.DAT
 * members the shipped FIELD.VFS actually holds; none of them is read off the
 * emitted C.
 *
 * The stock table is not a file the reader opens -- it is a VFS member that
 * fdps_load_field_chapter_resources parks in data_fdps_shop_stock_table_ptr --
 * so a test installs the member's own 36 bytes behind that pointer rather than
 * staging a file.  The byte rows below are transcribed from the unpacked
 * FIELD.VFS members and are the real ones, not invented stand-ins.
 */
#include "testharn.h"
#include "gamedata.h"
#include "shop.h"

/* SHOP01.DAT: row 0 the item shop, row 1 the weapon shop, row 2 the secret
   shop.  The weapon row is the canonical "hole between two stocked entries"
   case and the item row is the canonical "id above 0x7f" case. */
static unsigned char shop01_table[36] = {
    0xb4, 0xde, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0x02, 0x71, 0xff, 0xff, 0x72, 0x73, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xb5, 0xb7, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff
};

/* SHOP03.DAT: its weapon row opens ON an empty slot, so a scan that treated
   0xff as the terminator would report this shop as stocking nothing at all. */
static unsigned char shop03_table[36] = {
    0xb4, 0xb5, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0x03, 0xff, 0x1e, 0x1f, 0x30, 0x64, 0x65, 0x74, 0x75, 0x82, 0x83,
    0xb5, 0xb7, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff
};

/* SHOP00.DAT: the item row runs to nine ids before its holes, and both the
   weapon and secret rows are twelve bytes of 0x00 -- a live item id, not a
   terminator and not an empty slot. */
static unsigned char shop00_table[36] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x64, 0xc8, 0xff, 0xff, 0xff,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

/* Every slot empty: the loop still runs its twelve iterations and writes
   nothing. */
static unsigned char empty_table[36] = {
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff
};

/* -2 is not a byte the row can produce, so an untouched element is
   distinguishable from anything the function could have written. */
static void fill_unwritten(int *out_item_ids)
{
    int slot;

    for (slot = 0; slot < 12; slot++) {
        out_item_ids[slot] = -2;
    }
}

/* The weapon row 02 71 FF FF 72 73 FF FF FF FF FF FF: the pair of holes at
   slots 2 and 3 is stepped over, not stopped on, and the two ids behind them
   land at destination indices 2 and 3.  The destination index is the write
   counter and not the slot, so the answer is packed. */
static void shop_skips_holes_between_stock(void)
{
    int stock[12];

    data_fdps_shop_stock_table_ptr = shop01_table;
    fill_unwritten(stock);
    CHECK_EQ(fdps_shop_collect_stock_items(1, stock), 4);
    CHECK_EQ(stock[0], 0x02);
    CHECK_EQ(stock[1], 0x71);
    CHECK_EQ(stock[2], 0x72);
    CHECK_EQ(stock[3], 0x73);
    /* Nothing is written past the count: the store at 0003175b is inside the
       taken branch and the counter never reached 4 for a fifth slot. */
    CHECK_EQ(stock[4], -2);
}

/* SHOP03.DAT's weapon row FF 03 FF 1E 1F 30 64 65 74 75 82 83: a leading hole
   and a second one at slot 2, then ten stocked slots.  Treating 0xff as the
   row terminator answers 0 here. */
static void shop_leading_hole_does_not_end_row(void)
{
    int stock[12];

    data_fdps_shop_stock_table_ptr = shop03_table;
    fill_unwritten(stock);
    CHECK_EQ(fdps_shop_collect_stock_items(1, stock), 10);
    CHECK_EQ(stock[0], 0x03);
    CHECK_EQ(stock[1], 0x1e);
    CHECK_EQ(stock[9], 0x83);
}

/* SHOP01.DAT's item row B4 DE: the byte is read zero-extended (XOR EAX,EAX /
   MOV AL,byte ptr [EDX] at 00031739), so the ids are 180 and 222.  Read
   through a signed char pointer they would be -76 and -34, and the 0xff slots
   after them would compare as -1 and be sold as stock. */
static void shop_ids_above_7f_are_unsigned(void)
{
    int stock[12];

    data_fdps_shop_stock_table_ptr = shop01_table;
    fill_unwritten(stock);
    CHECK_EQ(fdps_shop_collect_stock_items(0, stock), 2);
    CHECK_EQ(stock[0], 180);
    CHECK_EQ(stock[1], 222);
}

/* The row base is shop_index * 0xc (IMUL EAX,dword ptr [EBP+0x14],0xc at
   0003172a), so index 2 reads the third row and not the third byte. */
static void shop_index_selects_a_row_of_twelve(void)
{
    int stock[12];

    data_fdps_shop_stock_table_ptr = shop01_table;
    fill_unwritten(stock);
    CHECK_EQ(fdps_shop_collect_stock_items(2, stock), 2);
    CHECK_EQ(stock[0], 0xb5);
    CHECK_EQ(stock[1], 0xb7);
}

/* An all-0xff row writes nothing and returns 0 -- the count the picker at
   00032710 compares its cursor against. */
static void shop_empty_row_returns_zero(void)
{
    int stock[12];

    data_fdps_shop_stock_table_ptr = empty_table;
    fill_unwritten(stock);
    CHECK_EQ(fdps_shop_collect_stock_items(0, stock), 0);
    CHECK_EQ(stock[0], -2);
}

/* SHOP00.DAT's weapon row is twelve 0x00 bytes.  0x00 is a live item id and
   the only value the loop treats specially is 0xff, so all twelve are written
   and the caller's array needs room for twelve ints. */
static void shop_full_row_writes_twelve(void)
{
    int stock[12];

    data_fdps_shop_stock_table_ptr = shop00_table;
    fill_unwritten(stock);
    CHECK_EQ(fdps_shop_collect_stock_items(1, stock), 12);
    CHECK_EQ(stock[0], 0);
    CHECK_EQ(stock[11], 0);
}

/* SHOP00.DAT's item row 00 01 02 03 04 05 06 64 C8 FF FF FF: nine ids, the
   last two of them 100 and 200, then trailing holes that contribute nothing. */
static void shop_trailing_holes_contribute_nothing(void)
{
    int stock[12];

    data_fdps_shop_stock_table_ptr = shop00_table;
    fill_unwritten(stock);
    CHECK_EQ(fdps_shop_collect_stock_items(0, stock), 9);
    CHECK_EQ(stock[7], 100);
    CHECK_EQ(stock[8], 200);
    CHECK_EQ(stock[9], -2);
}

/* The table is only ever loaded from -- there is no store through the row
   pointer anywhere in the function -- so the chapter's stock image survives a
   collect and the three shops can be walked in any order. */
static void shop_does_not_modify_the_table(void)
{
    int stock[12];

    data_fdps_shop_stock_table_ptr = shop01_table;
    fill_unwritten(stock);
    CHECK_EQ(fdps_shop_collect_stock_items(1, stock), 4);
    CHECK_EQ(shop01_table[12], 0x02);
    CHECK_EQ(shop01_table[14], 0xff);
    CHECK_EQ(shop01_table[17], 0x73);
    /* And a second call over the same row answers the same thing. */
    CHECK_EQ(fdps_shop_collect_stock_items(1, stock), 4);
}

void run_shop_tests(void)
{
    RUN_TEST(shop_skips_holes_between_stock);
    RUN_TEST(shop_leading_hole_does_not_end_row);
    RUN_TEST(shop_ids_above_7f_are_unsigned);
    RUN_TEST(shop_index_selects_a_row_of_twelve);
    RUN_TEST(shop_empty_row_returns_zero);
    RUN_TEST(shop_full_row_writes_twelve);
    RUN_TEST(shop_trailing_holes_contribute_nothing);
    RUN_TEST(shop_does_not_modify_the_table);
}
