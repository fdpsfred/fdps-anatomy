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

void run_anim_tests(void)
{
    RUN_TEST(baseani_lookup_returns_a_pointer_into_the_image);
    RUN_TEST(baseani_lookup_republishes_on_every_call);
    RUN_TEST(baseani_lookup_reads_the_archive_base_each_call);
    RUN_TEST(baseani_lookup_folds_the_query_in_place);
}
