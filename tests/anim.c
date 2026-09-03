/* tests/anim.c -- cover for src/anim.c.
 *
 * Expected values come from the assembly at 0002a240 -- PUSH dword ptr
 * [0x000643a8] at 0002a254 for the archive base, the store of EAX into
 * [0x000643ec] at 0002a262, the CMP/JZ at 0002a267 and the load of that same
 * global at 0002a270 for the value returned -- and from the directory of the
 * shipped FIELD2.VFS, whose member starts are the numbers tests/vfs.c asserts
 * against the same container.  None of them is read off the emitted C.
 *
 * WHAT STANDS IN FOR BaseAni.vfs.  The function looks its member up in
 * whatever resident container image the archive global points at; nothing in
 * it is particular to BaseAni.vfs, which is not a loose file on disk but a
 * member of MISC.VFS.  FIELD2.VFS is a real container the game ships, it is
 * the smallest at 112,350 bytes, and it is already staged for tests/vfs.c
 * (tests/gamefile.lst), so it is read whole here exactly as the loader holds
 * the animation pack and the lookup is exercised against a directory the game
 * itself wrote.  A fabricated image would only be a copy of this one with
 * different numbers in it.
 *
 * WHAT IS NOT COVERED.  The miss path prints "File not found: %s" and calls
 * exit(1), so it ends the process rather than returning: no assertion in a
 * test executable can reach it and no cover here tries.  What the message says
 * about the query -- that it is the upper-cased spelling, not the caller's --
 * is asserted through the found path instead, which folds the buffer the same
 * way.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fdpstype.h"
#include "gamedata.h"
#include "sprite.h"
#include "anim.h"
#include "testharn.h"

/* The staged container, and its own length asserted rather than assumed so a
   staged file that is not this container fails here instead of failing as
   arithmetic (resource_info/vfs.md). */
#define VFS_NAME "FIELD2.VFS"
#define VFS_IMAGE_BYTES 112350L

/* Query buffers are written into, because the lookup upper-cases the caller's
   own storage. */
#define QUERY_MAX 16

/* Two members of FIELD2.VFS, taken from its directory: entry 0 and entry 130,
   the last.  Entry 0 begins where the member data begins so it is found under
   any stride, and entry 130's 36 bytes end exactly at the container's 112,350.
   They are far enough apart that a stale answer cannot pass for a fresh one. */
#define ATTR000_START 3441L
#define DSC64_START 112314L

/* Reads a container into one block with plain library calls, exactly as the
   startup loader holds the animation pack.  Returns NULL rather than
   asserting, and every caller checks; the block is the caller's to free. */
static char *read_whole_container(char *archive_name, long *out_bytes)
{
    FILE *fp;
    char *image;
    long bytes;

    fp = fopen(archive_name, "rb");
    if (fp == NULL) {
        return NULL;
    }
    fseek(fp, 0L, SEEK_END);
    bytes = ftell(fp);
    fseek(fp, 0L, SEEK_SET);
    image = (char *)malloc((size_t)bytes);
    if (image == NULL) {
        fclose(fp);
        return NULL;
    }
    if (fread(image, (size_t)bytes, 1, fp) != 1) {
        free(image);
        fclose(fp);
        return NULL;
    }
    fclose(fp);
    *out_bytes = bytes;
    return image;
}

/* The answer is checked as a distance from the image base, because that is
   what the lookup computes and what makes this an assertion about a member
   found where it lies rather than about a copy.  The global is checked against
   the same pointer: the store at 0002a262 and the load at 0002a270 mean the
   returned value IS the global's contents, so a caller and a later reader of
   the global cannot disagree. */
static void baseani_lookup_returns_a_pointer_into_the_image(void)
{
    char *image;
    char *member;
    char query[QUERY_MAX];
    long bytes;

    bytes = 0;
    image = read_whole_container(VFS_NAME, &bytes);
    CHECK_EQ(image != NULL, 1);
    if (image == NULL) {
        return;
    }
    CHECK_EQ(bytes, VFS_IMAGE_BYTES);

    data_fdps_animation_baseani_archive_ptr = (unsigned char *)image;
    strcpy(query, "ATTR000.DAT");
    member = (char *)fdps_baseani_get_entry_or_exit(query);
    CHECK_EQ(member != NULL, 1);
    if (member != NULL) {
        CHECK_EQ(member - image, ATTR000_START);
    }
    CHECK_EQ((char *)data_fdps_animation_baseani_entry_ptr == member, 1);

    data_fdps_animation_baseani_archive_ptr = NULL;
    free(image);
}

/* Nothing clears the global and nothing else writes it, so the only thing that
   can change it is the next lookup: two calls in a row have to leave it
   holding the second member, not the first.  The two starts are 108,873 bytes
   apart, so a global that was written once and not again is not a near miss. */
static void baseani_lookup_republishes_on_every_call(void)
{
    char *image;
    char query[QUERY_MAX];
    long bytes;

    bytes = 0;
    image = read_whole_container(VFS_NAME, &bytes);
    CHECK_EQ(image != NULL, 1);
    if (image == NULL) {
        return;
    }

    data_fdps_animation_baseani_archive_ptr = (unsigned char *)image;
    strcpy(query, "ATTR000.DAT");
    fdps_baseani_get_entry_or_exit(query);
    CHECK_EQ((char *)data_fdps_animation_baseani_entry_ptr - image,
             ATTR000_START);

    strcpy(query, "DSC64.DAT");
    fdps_baseani_get_entry_or_exit(query);
    CHECK_EQ((char *)data_fdps_animation_baseani_entry_ptr - image,
             DSC64_START);

    data_fdps_animation_baseani_archive_ptr = NULL;
    free(image);
}

/* The archive base is loaded from the global at the moment of the call, PUSH
   dword ptr [0x000643a8] at 0002a254, and is not cached anywhere: the same
   query against two images has to answer inside whichever one the global names
   now.  Two separate blocks are used rather than one, so the two answers are
   different addresses and not the same address asserted twice. */
static void baseani_lookup_reads_the_archive_base_each_call(void)
{
    char *first_image;
    char *second_image;
    char *member;
    char query[QUERY_MAX];
    long bytes;

    bytes = 0;
    first_image = read_whole_container(VFS_NAME, &bytes);
    second_image = read_whole_container(VFS_NAME, &bytes);
    CHECK_EQ(first_image != NULL, 1);
    CHECK_EQ(second_image != NULL, 1);
    if (first_image == NULL || second_image == NULL) {
        free(first_image);
        free(second_image);
        return;
    }
    CHECK_EQ(first_image != second_image, 1);

    data_fdps_animation_baseani_archive_ptr = (unsigned char *)first_image;
    strcpy(query, "ATTR000.DAT");
    member = (char *)fdps_baseani_get_entry_or_exit(query);
    CHECK_EQ(member - first_image, ATTR000_START);

    data_fdps_animation_baseani_archive_ptr = (unsigned char *)second_image;
    strcpy(query, "ATTR000.DAT");
    member = (char *)fdps_baseani_get_entry_or_exit(query);
    CHECK_EQ(member - second_image, ATTR000_START);

    data_fdps_animation_baseani_archive_ptr = NULL;
    free(first_image);
    free(second_image);
}

/* strupr runs on the caller's buffer inside the lookup (vfs.h), so a
   mixed-case query finds the upper-case member and the caller's string is
   upper-case afterwards.  Both halves are asserted: the second is not a side
   effect nobody depends on -- it is the spelling this function would print in
   its "File not found: %s" message, which is the one thing about the miss path
   a test can establish without ending the process. */
static void baseani_lookup_folds_the_query_in_place(void)
{
    char *image;
    char *member;
    char query[QUERY_MAX];
    long bytes;

    bytes = 0;
    image = read_whole_container(VFS_NAME, &bytes);
    CHECK_EQ(image != NULL, 1);
    if (image == NULL) {
        return;
    }

    data_fdps_animation_baseani_archive_ptr = (unsigned char *)image;
    strcpy(query, "attr000.dat");
    member = (char *)fdps_baseani_get_entry_or_exit(query);
    CHECK_EQ(member != NULL, 1);
    if (member != NULL) {
        CHECK_EQ(member - image, ATTR000_START);
    }
    CHECK_EQ(strcmp(query, "ATTR000.DAT"), 0);

    data_fdps_animation_baseani_archive_ptr = NULL;
    free(image);
}

/* ---- fdps_draw_turn_number, 0001ea80 ------------------------------------
 *
 * Expected values come from the assembly: SUB EDX,0x2f at 0001ead2 for the
 * bank entry a formatted character becomes, MOV dword ptr [EAX+0x18],EDX at
 * 0001ead5 and ADD dword ptr [EAX+0xc],0x1c at 0001eaea for the two slots that
 * move and by how much, and the CALL at 0001eaaf sitting inside the loop's own
 * condition for how many times round it goes.  None of them is read off the
 * emitted C.
 *
 * HOW THE PAINTING IS KEPT OUT OF THE WAY.  fdps_draw_composite_sprite is real
 * code, not a stub, and it would walk a .SAF frame and blit it.  The request
 * here is aimed at a sprite bank that is 64 zero bytes, which makes the frame
 * count the drawer reads at +0x0c zero, so its one range test rejects every
 * entry index this function can produce -- the smallest is 1 -- and it returns
 * having drawn nothing, played nothing and written nothing (sprite.h).  That
 * is not a stand-in for a .SAF: it is the drawer's own documented do-nothing
 * path, chosen so that everything the request block holds afterwards was put
 * there by the function under test and by nothing else.
 *
 * WHAT IS NOT COVERED.  x is advanced AFTER each call, so the k-th digit is
 * painted at the caller's x plus 28(k-1); with the drawer inert there is
 * nothing that records where each digit landed, and no assertion here can tell
 * that order from the reverse.  It is settled by reading 0001ead5 through
 * 0001eaea, where the store of the entry index and the call both precede the
 * ADD, and not by a case below.
 */

/* Nine dwords, the block sprite.h describes, with the slots this function does
   not touch set to values that are recognisable if something writes them. */
#define REQ_DEST_BASE_MARK 0x11110000
#define REQ_DEST_PITCH_MARK 0x168
#define REQ_DEST_ROWS_MARK 0xf0
#define REQ_START_X 100
#define REQ_START_Y 0x22220000
#define REQ_BLIT_OPERAND_MARK 0x33330000
#define REQ_BLIT_MODE_MARK 0x44440000

/* The banner bank stood down to its do-nothing path: 64 bytes of zero, of
   which only the frame count at +0x0c is read. */
static unsigned char inert_sprite_bank[64];

static void build_turn_request(int *request)
{
    int slot;

    for (slot = 0; slot < (int) sizeof(inert_sprite_bank); slot++) {
        inert_sprite_bank[slot] = 0;
    }
    request[DRAW_REQUEST_DEST_BASE] = REQ_DEST_BASE_MARK;
    request[DRAW_REQUEST_DEST_PITCH] = REQ_DEST_PITCH_MARK;
    request[DRAW_REQUEST_DEST_ROWS] = REQ_DEST_ROWS_MARK;
    request[DRAW_REQUEST_X] = REQ_START_X;
    request[DRAW_REQUEST_Y] = REQ_START_Y;
    request[DRAW_REQUEST_IMAGE] = (int) inert_sprite_bank;
    request[DRAW_REQUEST_ITEM_INDEX] = 0;
    request[DRAW_REQUEST_BLIT_OPERAND] = REQ_BLIT_OPERAND_MARK;
    request[DRAW_REQUEST_BLIT_MODE] = REQ_BLIT_MODE_MARK;
}

/* One digit is one pass: x moves exactly one 28-pixel pitch and the entry
   index comes back as the digit plus one.  Turn 7 draws bank entry 8. */
static void turn_number_draws_one_pass_per_digit(void)
{
    int request[DRAW_REQUEST_DWORDS];

    build_turn_request(request);
    data_fdps_battle_turn_counter = 7;
    fdps_draw_turn_number(request);
    CHECK_EQ(request[DRAW_REQUEST_X], REQ_START_X + 28);
    CHECK_EQ(request[DRAW_REQUEST_ITEM_INDEX], 8);

    build_turn_request(request);
    data_fdps_battle_turn_counter = 123;
    fdps_draw_turn_number(request);
    CHECK_EQ(request[DRAW_REQUEST_X], REQ_START_X + 28 * 3);
    CHECK_EQ(request[DRAW_REQUEST_ITEM_INDEX], 4);

    build_turn_request(request);
    data_fdps_battle_turn_counter = 9999;
    fdps_draw_turn_number(request);
    CHECK_EQ(request[DRAW_REQUEST_X], REQ_START_X + 28 * 4);
    CHECK_EQ(request[DRAW_REQUEST_ITEM_INDEX], 10);
}

/* The bias is 0x2f and not '0', so a digit selects the entry one past its own
   number: '0' is entry 1 and never entry 0, which is the banner word graphic.
   Both numbers here end in a zero, so the naive bias would leave the entry
   index at 0 while x still came out right -- the entry index is the only slot
   that can tell the two spellings apart. */
static void turn_number_biases_digits_one_past_the_word_sprite(void)
{
    int request[DRAW_REQUEST_DWORDS];

    build_turn_request(request);
    data_fdps_battle_turn_counter = 0;
    fdps_draw_turn_number(request);
    CHECK_EQ(request[DRAW_REQUEST_X], REQ_START_X + 28);
    CHECK_EQ(request[DRAW_REQUEST_ITEM_INDEX], 1);

    build_turn_request(request);
    data_fdps_battle_turn_counter = 10;
    fdps_draw_turn_number(request);
    CHECK_EQ(request[DRAW_REQUEST_X], REQ_START_X + 28 * 2);
    CHECK_EQ(request[DRAW_REQUEST_ITEM_INDEX], 1);
}

/* The counter is read at the moment of the call and nothing is passed in, so
   the same block drawn twice with the counter moved on in between produces two
   different lengths, and the second run starts from wherever the first left x
   rather than from the block's original x.  This is what the caller sees when
   it forgets to reset the two slots. */
static void turn_number_reads_the_counter_at_the_call(void)
{
    int request[DRAW_REQUEST_DWORDS];

    build_turn_request(request);
    data_fdps_battle_turn_counter = 9;
    fdps_draw_turn_number(request);
    CHECK_EQ(request[DRAW_REQUEST_X], REQ_START_X + 28);
    CHECK_EQ(request[DRAW_REQUEST_ITEM_INDEX], 10);

    data_fdps_battle_turn_counter = 25;
    fdps_draw_turn_number(request);
    CHECK_EQ(request[DRAW_REQUEST_X], REQ_START_X + 28 * 3);
    CHECK_EQ(request[DRAW_REQUEST_ITEM_INDEX], 6);
}

/* Seven of the nine slots are neither read nor written: the assembly's only
   stores through the request pointer are [EAX+0x18] at 0001ead5 and
   [EAX+0xc] at 0001eaea.  The drawer is on its do-nothing path, so anything
   else that moved was moved here. */
static void turn_number_leaves_the_other_seven_slots_alone(void)
{
    int request[DRAW_REQUEST_DWORDS];

    build_turn_request(request);
    data_fdps_battle_turn_counter = 42;
    fdps_draw_turn_number(request);

    CHECK_EQ(request[DRAW_REQUEST_DEST_BASE], REQ_DEST_BASE_MARK);
    CHECK_EQ(request[DRAW_REQUEST_DEST_PITCH], REQ_DEST_PITCH_MARK);
    CHECK_EQ(request[DRAW_REQUEST_DEST_ROWS], REQ_DEST_ROWS_MARK);
    CHECK_EQ(request[DRAW_REQUEST_Y], REQ_START_Y);
    CHECK_EQ(request[DRAW_REQUEST_IMAGE] == (int) inert_sprite_bank, 1);
    CHECK_EQ(request[DRAW_REQUEST_BLIT_OPERAND], REQ_BLIT_OPERAND_MARK);
    CHECK_EQ(request[DRAW_REQUEST_BLIT_MODE], REQ_BLIT_MODE_MARK);
}

void run_anim_tests(void)
{
    RUN_TEST(baseani_lookup_returns_a_pointer_into_the_image);
    RUN_TEST(baseani_lookup_republishes_on_every_call);
    RUN_TEST(baseani_lookup_reads_the_archive_base_each_call);
    RUN_TEST(baseani_lookup_folds_the_query_in_place);
    RUN_TEST(turn_number_draws_one_pass_per_digit);
    RUN_TEST(turn_number_biases_digits_one_past_the_word_sprite);
    RUN_TEST(turn_number_reads_the_counter_at_the_call);
    RUN_TEST(turn_number_leaves_the_other_seven_slots_alone);
}
