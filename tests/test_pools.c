#include "../include/myalloc.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

int main(void)
{
    /* 1: freeing and reallocating the same class reuses the same slot */
    void *a = my_malloc(24);            /* -> 32-byte class */
    assert(a != NULL);
    my_free(a);
    void *b = my_malloc(30);            /* same class -> same slot back */
    assert(a == b);
    my_free(b);
    printf("[1] pool reuse returns the same slot            PASS\n");

    /* 2: different classes are isolated */
    char *s = my_malloc(16);
    char *l = my_malloc(512);
    assert(s != NULL && l != NULL && (void *)s != (void *)l);
    memset(s, 1, 16);
    memset(l, 2, 512);
    assert(s[15] == 1 && l[0] == 2);
    my_free(s);
    my_free(l);
    printf("[2] size classes are isolated                   PASS\n");

    /* 3: hammer the fast path */
    enum { N = 100000 };
    clock_t t0 = clock();
    for (int i = 0; i < N; i++) {
        void *p = my_malloc((size_t)(1 + i % 500));
        assert(p != NULL);
        my_free(p);
    }
    double ms = 1000.0 * (double)(clock() - t0) / CLOCKS_PER_SEC;
    printf("[3] %d alloc/free pairs in %.1f ms          PASS\n", N, ms);

    /* 4: large allocations still work beside the pools */
    char *big = my_malloc(100000);
    assert(big != NULL);
    big[99999] = 'Z';
    my_free(big);
    printf("[4] large path unaffected                       PASS\n");

    printf("\nAll pool tests passed.\n");
    my_alloc_stats();
    return 0;
}