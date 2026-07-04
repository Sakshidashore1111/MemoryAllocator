#include "../include/myalloc.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

static int tests_run = 0;
#define TEST(name) do { tests_run++; printf("[%2d] %-45s", tests_run, name); } while (0)
#define PASS()     printf("PASS\n")

int main(void)
{
    TEST("malloc returns non-NULL");
    char *a = my_malloc(100);
    assert(a != NULL);
    PASS();

    TEST("returned pointer is 16-byte aligned");
    assert(((uintptr_t)a % 16) == 0);
    PASS();

    TEST("memory is actually writable and readable");
    memset(a, 'A', 100);
    assert(a[0] == 'A' && a[99] == 'A');
    PASS();

    TEST("two allocations don't overlap");
    char *b = my_malloc(200);
    memset(b, 'B', 200);
    assert(a[99] == 'A' && b[0] == 'B');
    PASS();

    TEST("free then malloc reuses memory (first-fit)");
    my_free(a);
    char *c = my_malloc(50);
    assert(c == a);                 /* the freed 100-byte slot fits 50 */
    PASS();

    TEST("free(NULL) does not crash");
    my_free(NULL);
    PASS();

    TEST("double free is detected, not fatal");
    my_free(c);
    my_free(c);                     /* should print a warning, keep running */
    PASS();

    TEST("calloc zeroes memory");
    int *nums = my_calloc(64, sizeof(int));
    assert(nums != NULL);
    for (int i = 0; i < 64; i++) assert(nums[i] == 0);
    PASS();

    TEST("realloc preserves contents");
    strcpy(b, "hello allocator");
    b = my_realloc(b, 5000);
    assert(strcmp(b, "hello allocator") == 0);
    PASS();

    TEST("large allocation (2 MB) works");
    char *big = my_malloc(2 * 1024 * 1024);
    assert(big != NULL);
    big[2 * 1024 * 1024 - 1] = 'Z';
    PASS();

    my_free(nums);
    my_free(b);
    my_free(big);

    printf("\nAll %d tests passed.\n", tests_run);
    my_alloc_stats();
    return 0;
}