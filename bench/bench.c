#define _GNU_SOURCE
#include "../include/myalloc.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define ROUNDS      1000000     /* alloc/free pairs per thread          */
#define SLOTS       128         /* live pointers held at any moment     */
#define NUM_THREADS 4

/* an "allocator" is just a pair of function pointers */
typedef void *(*alloc_fn)(size_t);
typedef void  (*free_fn)(void *);

typedef struct {
    alloc_fn my_alloc;
    free_fn  my_release;
} bench_arg_t;

static double now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

/* the workload: identical no matter which allocator is plugged in */
static void *workload(void *argp)
{
    bench_arg_t *arg = (bench_arg_t *)argp;
    void *slots[SLOTS] = {0};
    unsigned int seed = 42;

    for (int i = 0; i < ROUNDS; i++) {
        int s = rand_r(&seed) % SLOTS;
        if (slots[s] == NULL) {
            slots[s] = arg->my_alloc(1 + (size_t)(rand_r(&seed) % 256));
        } else {
            arg->my_release(slots[s]);
            slots[s] = NULL;
        }
    }
    for (int s = 0; s < SLOTS; s++)
        if (slots[s]) arg->my_release(slots[s]);
    return NULL;
}

static double run_single(bench_arg_t *arg)
{
    double t0 = now_ms();
    workload(arg);
    return now_ms() - t0;
}

static double run_threads(bench_arg_t *arg)
{
    pthread_t th[NUM_THREADS];
    double t0 = now_ms();
    for (int i = 0; i < NUM_THREADS; i++)
        pthread_create(&th[i], NULL, workload, arg);
    for (int i = 0; i < NUM_THREADS; i++)
        pthread_join(th[i], NULL);
    return now_ms() - t0;
}

int main(void)
{
    bench_arg_t mine = { my_malloc, my_free };
    bench_arg_t libc = { malloc,    free    };

    printf("Benchmark: %d alloc/free pairs per thread, sizes 1-256 bytes\n\n",
           ROUNDS);

    double m1 = run_single(&mine);
    double g1 = run_single(&libc);
    double m4 = run_threads(&mine);
    double g4 = run_threads(&libc);

    printf("%-22s %12s %12s\n", "", "myalloc", "glibc malloc");
    printf("%-22s %9.1f ms %9.1f ms\n", "1 thread",  m1, g1);
    printf("%-22s %9.1f ms %9.1f ms\n", "4 threads", m4, g4);

    printf("\nops/sec (1 thread):  myalloc %.1fM | glibc %.1fM\n",
           ROUNDS / m1 / 1000.0, ROUNDS / g1 / 1000.0);
    printf("ops/sec (4 threads): myalloc %.1fM | glibc %.1fM\n",
           NUM_THREADS * ROUNDS / m4 / 1000.0,
           NUM_THREADS * ROUNDS / g4 / 1000.0);
    return 0;
}