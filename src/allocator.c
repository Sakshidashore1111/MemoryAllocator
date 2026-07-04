#include "../include/myalloc.h"
#include <sys/mman.h>   /* mmap, munmap */
#include <stdio.h>      /* fprintf for errors/stats */
#include <string.h>     /* memset, memcpy */
#include <stdint.h>     /* uintptr_t */
#include <pthread.h>    /* mutex + threads */

/* ---------- configuration ---------- */

#define CHUNK_SIZE   (1024 * 1024)          /* ask OS for 1 MB at a time  */
#define MAGIC        0xC0FFEE42u            /* stamp proving a valid block */
#define ALIGN16(x)   (((x) + 15) & ~(size_t)15)

/* ---------- the block header (the parcel label) ---------- */

typedef struct block_header {
    size_t               size;       /* usable bytes in this block   */
    unsigned int         magic;      /* corruption / bad-free detect */
    int                  is_free;    /* 1 = free, 0 = in use         */
    int                  pool_class; /* tray index, or -1 = not from a pool */
    struct block_header *next;       /* next block in the list       */
} __attribute__((aligned(16))) block_header_t;

#define HDR_SIZE   (sizeof(block_header_t))
#define MIN_SPLIT  (HDR_SIZE + 32)   /* don't create crumbs smaller than this */

static block_header_t *head = NULL;  /* start of the large-block list */
static pthread_mutex_t alloc_lock = PTHREAD_MUTEX_INITIALIZER;

/* ---------- memory pools: segregated size classes ---------- */

#define NUM_CLASSES 10
static const size_t class_sizes[NUM_CLASSES] =
    { 16, 32, 48, 64, 96, 128, 192, 256, 384, 512 };

#define POOL_BYTES (64 * 1024)          /* one tray = 64 KB slab */

static block_header_t *bins[NUM_CLASSES];   /* shared free-slot list per class */

/* ---------- per-thread caches: each thread's private pocket ---------- */

#define TCACHE_BATCH 32     /* slots moved per refill/flush trip */
#define TCACHE_MAX   96     /* pocket too full above this -> give some back */

static __thread block_header_t *tcache[NUM_CLASSES];
static __thread size_t          tcache_count[NUM_CLASSES];

/* ---------- helpers ---------- */

static int size_to_class(size_t size)
{
    for (int i = 0; i < NUM_CLASSES; i++)
        if (size <= class_sizes[i])
            return i;
    return -1;                          /* too big: use the large path */
}

static int refill_pool(int cls)
{
    size_t slot = HDR_SIZE + class_sizes[cls];
    void *mem = mmap(NULL, POOL_BYTES, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (mem == MAP_FAILED)
        return -1;

    size_t count = POOL_BYTES / slot;
    char *p = (char *)mem;
    for (size_t i = 0; i < count; i++) {
        block_header_t *hdr = (block_header_t *)(p + i * slot);
        hdr->size       = class_sizes[cls];
        hdr->magic      = MAGIC;
        hdr->is_free    = 1;
        hdr->pool_class = cls;
        hdr->next       = bins[cls];
        bins[cls]       = hdr;
    }
    return 0;
}

static block_header_t *request_chunk(size_t min_bytes)
{
    size_t total = CHUNK_SIZE;
    if (min_bytes + HDR_SIZE > total)
        total = min_bytes + HDR_SIZE;

    void *mem = mmap(NULL, total, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (mem == MAP_FAILED)
        return NULL;

    block_header_t *hdr = (block_header_t *)mem;
    hdr->size       = total - HDR_SIZE;
    hdr->magic      = MAGIC;
    hdr->is_free    = 1;
    hdr->pool_class = -1;
    hdr->next       = NULL;

    if (head == NULL) {
        head = hdr;
    } else {
        block_header_t *cur = head;
        while (cur->next != NULL)
            cur = cur->next;
        cur->next = hdr;
    }
    return hdr;
}

static void split_block(block_header_t *hdr, size_t size)
{
    if (hdr->size >= size + MIN_SPLIT) {
        block_header_t *nb =
            (block_header_t *)((char *)(hdr + 1) + size);
        nb->size       = hdr->size - size - HDR_SIZE;
        nb->magic      = MAGIC;
        nb->is_free    = 1;
        nb->pool_class = -1;
        nb->next       = hdr->next;

        hdr->size = size;
        hdr->next = nb;
    }
}

static int adjacent(block_header_t *a, block_header_t *b)
{
    return (char *)(a + 1) + a->size == (char *)b;
}

/* ================== PUBLIC API ================== */

void *my_malloc(size_t size)
{
    if (size == 0)
        return NULL;

    size = ALIGN16(size);

    int cls = size_to_class(size);
    if (cls >= 0) {
        /* FASTEST PATH: my private pocket — no lock, no waiting */
        if (tcache[cls] != NULL) {
            block_header_t *hdr = tcache[cls];
            tcache[cls] = hdr->next;
            tcache_count[cls]--;
            hdr->next    = NULL;
            hdr->is_free = 0;
            return (void *)(hdr + 1);
        }

        /* pocket empty: take the key ONCE, grab a whole batch */
        pthread_mutex_lock(&alloc_lock);
        if (bins[cls] == NULL && refill_pool(cls) != 0) {
            pthread_mutex_unlock(&alloc_lock);
            return NULL;
        }
        for (int i = 0; i < TCACHE_BATCH && bins[cls] != NULL; i++) {
            block_header_t *hdr = bins[cls];
            bins[cls]   = hdr->next;
            hdr->next   = tcache[cls];
            tcache[cls] = hdr;
            tcache_count[cls]++;
        }
        pthread_mutex_unlock(&alloc_lock);

        block_header_t *hdr = tcache[cls];
        tcache[cls] = hdr->next;
        tcache_count[cls]--;
        hdr->next    = NULL;
        hdr->is_free = 0;
        return (void *)(hdr + 1);
    }

    /* LARGE PATH: first-fit under the lock */
    pthread_mutex_lock(&alloc_lock);

    block_header_t *cur = head;
    while (cur != NULL) {
        if (cur->is_free && cur->size >= size) {
            split_block(cur, size);
            cur->is_free = 0;
            pthread_mutex_unlock(&alloc_lock);
            return (void *)(cur + 1);
        }
        cur = cur->next;
    }

    block_header_t *fresh = request_chunk(size);
    if (fresh == NULL) {
        pthread_mutex_unlock(&alloc_lock);
        return NULL;
    }

    split_block(fresh, size);
    fresh->is_free = 0;
    pthread_mutex_unlock(&alloc_lock);
    return (void *)(fresh + 1);
}

void my_free(void *ptr)
{
    if (ptr == NULL)
        return;

    block_header_t *hdr = (block_header_t *)ptr - 1;

    /* safety checks: this block belongs to the caller */
    if (hdr->magic != MAGIC) {
        fprintf(stderr, "myalloc: bad or corrupted pointer %p\n", ptr);
        return;
    }
    if (hdr->is_free) {
        fprintf(stderr, "myalloc: double free detected at %p\n", ptr);
        return;
    }

    /* POOL PATH: drop into my private pocket — no lock */
    if (hdr->pool_class >= 0) {
        int cls = hdr->pool_class;
        hdr->is_free = 1;
        hdr->next    = tcache[cls];
        tcache[cls]  = hdr;
        tcache_count[cls]++;

        /* pocket overflowing? return a batch to the shared bin */
        if (tcache_count[cls] > TCACHE_MAX) {
            pthread_mutex_lock(&alloc_lock);
            for (int i = 0; i < TCACHE_BATCH; i++) {
                block_header_t *h = tcache[cls];
                tcache[cls] = h->next;
                h->next     = bins[cls];
                bins[cls]   = h;
                tcache_count[cls]--;
            }
            pthread_mutex_unlock(&alloc_lock);
        }
        return;
    }

    /* LARGE PATH: mark free and coalesce neighbors, under the lock */
    pthread_mutex_lock(&alloc_lock);

    hdr->is_free = 1;

    if (hdr->next && hdr->next->is_free && adjacent(hdr, hdr->next)) {
        hdr->size += HDR_SIZE + hdr->next->size;
        hdr->next  = hdr->next->next;
    }

    block_header_t *prev = NULL, *cur = head;
    while (cur != NULL && cur != hdr) {
        prev = cur;
        cur  = cur->next;
    }
    if (prev && prev->is_free && adjacent(prev, hdr)) {
        prev->size += HDR_SIZE + hdr->size;
        prev->next  = hdr->next;
    }

    pthread_mutex_unlock(&alloc_lock);
}

void *my_calloc(size_t count, size_t size)
{
    if (count != 0 && size > (size_t)-1 / count)
        return NULL;                       /* multiplication would overflow */

    size_t total = count * size;
    void *ptr = my_malloc(total);
    if (ptr != NULL)
        memset(ptr, 0, total);
    return ptr;
}

void *my_realloc(void *ptr, size_t size)
{
    if (ptr == NULL)
        return my_malloc(size);
    if (size == 0) {
        my_free(ptr);
        return NULL;
    }

    block_header_t *hdr = (block_header_t *)ptr - 1;
    if (hdr->size >= ALIGN16(size))
        return ptr;

    void *fresh = my_malloc(size);
    if (fresh == NULL)
        return NULL;

    memcpy(fresh, ptr, hdr->size);
    my_free(ptr);
    return fresh;
}

void my_alloc_stats(void)
{
    size_t blocks = 0, free_blocks = 0;
    size_t used_bytes = 0, free_bytes = 0;
    size_t pool_free_slots = 0;

    for (block_header_t *cur = head; cur != NULL; cur = cur->next) {
        blocks++;
        if (cur->is_free) { free_blocks++; free_bytes += cur->size; }
        else              { used_bytes += cur->size; }
    }
    for (int i = 0; i < NUM_CLASSES; i++)
        for (block_header_t *c = bins[i]; c != NULL; c = c->next)
            pool_free_slots++;

    fprintf(stderr,
        "---- myalloc stats ----\n"
        "large blocks: %zu (free: %zu)\n"
        "large bytes in use: %zu | free: %zu\n"
        "pool slots free: %zu\n",
        blocks, free_blocks, used_bytes, free_bytes, pool_free_slots);
}