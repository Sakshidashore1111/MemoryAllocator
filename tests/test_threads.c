#define _GNU_SOURCE
#include "../include/myalloc.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <time.h>

#define NUM_THREADS 4
#define ITERATIONS  20000
#define SLOTS       64

static double now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

static void *worker(void *arg)
{
    int id = (int)(long)arg;
    unsigned char pattern = (unsigned char)(0xA0 + id); /* my signature byte */
    char  *slots[SLOTS] = {0};
    size_t sizes[SLOTS] = {0};
    unsigned int seed = 1234u + (unsigned int)id;

    for (int i = 0; i < ITERATIONS; i++) {
        int s = rand_r(&seed) % SLOTS;

        if (slots[s] == NULL) {
            /* allocate a random size and sign every byte with my pattern */
            size_t size = 1 + (size_t)(rand_r(&seed) % 256);
            slots[s] = my_malloc(size);
            assert(slots[s] != NULL);
            for (size_t j = 0; j < size; j++)
                slots[s][j] = (char)pattern;
            sizes[s] = size;
        } else {
            /* verify MY signature survived, then free */
            for (size_t j = 0; j < sizes[s]; j++)
                assert((unsigned char)slots[s][j] == pattern);
            my_free(slots[s]);
            slots[s] = NULL;
        }
    }

    for (int s = 0; s < SLOTS; s++)
        if (slots[s]) my_free(slots[s]);
    return NULL;
}

int main(void)
{
    pthread_t threads[NUM_THREADS];

    printf("Stress test: %d threads x %d operations each...\n",
           NUM_THREADS, ITERATIONS);

    double t0 = now_ms();

    for (long i = 0; i < NUM_THREADS; i++)
        pthread_create(&threads[i], NULL, worker, (void *)i);
    for (int i = 0; i < NUM_THREADS; i++)
        pthread_join(threads[i], NULL);

    double t1 = now_ms();

    printf("PASS: no corruption across %d total operations.\n",
           NUM_THREADS * ITERATIONS);
    printf("elapsed: %.1f ms\n", t1 - t0);
    my_alloc_stats();
    return 0;
}