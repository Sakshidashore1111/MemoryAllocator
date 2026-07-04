#include "../include/myalloc.h"
#include <sys/mman.h>   /* mmap, munmap */
#include <stdio.h>      /* fprintf for errors/stats */
#include <string.h>     /* memset, memcpy */
#include <stdint.h>     /* uintptr_t */

/* ---------- configuration ---------- */

#define CHUNK_SIZE   (1024 * 1024)          /* ask OS for 1 MB at a time  */
#define MAGIC        0xC0FFEE42u            /* stamp proving a valid block */
#define ALIGN16(x)   (((x) + 15) & ~(size_t)15)

/* ---------- the block header (the parcel label) ---------- */

typedef struct block_header {
    size_t               size;     /* usable bytes in this block   */
    unsigned int         magic;    /* corruption / bad-free detect */
    int                  is_free;  /* 1 = free, 0 = in use         */
    struct block_header *next;     /* next block in the list       */
} __attribute__((aligned(16))) block_header_t;

#define HDR_SIZE   (sizeof(block_header_t))
#define MIN_SPLIT  (HDR_SIZE + 32)   /* don't create crumbs smaller than this */

static block_header_t *head = NULL;  /* start of the block list */

/* ---------- ask the OS for a fresh slab ---------- */

static block_header_t *request_chunk(size_t min_bytes)
{
    size_t total = CHUNK_SIZE;
    if (min_bytes + HDR_SIZE > total)
        total = min_bytes + HDR_SIZE;

    void *mem = mmap(NULL, total, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (mem == MAP_FAILED)
        return NULL;                      /* OS refused: out of memory */

    block_header_t *hdr = (block_header_t *)mem;
    hdr->size    = total - HDR_SIZE;
    hdr->magic   = MAGIC;
    hdr->is_free = 1;
    hdr->next    = NULL;

    /* append to the end of the list */
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

/* ---------- split a big free block in two ---------- */

static void split_block(block_header_t *hdr, size_t size)
{
    if (hdr->size >= size + MIN_SPLIT) {
        block_header_t *nb =
            (block_header_t *)((char *)(hdr + 1) + size);
        nb->size    = hdr->size - size - HDR_SIZE;
        nb->magic   = MAGIC;
        nb->is_free = 1;
        nb->next    = hdr->next;

        hdr->size = size;
        hdr->next = nb;
    }
}

/* ---------- are two blocks physically touching? ---------- */

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

    /* first-fit: walk the list for a free block big enough */
    block_header_t *cur = head;
    while (cur != NULL) {
        if (cur->is_free && cur->size >= size) {
            split_block(cur, size);
            cur->is_free = 0;
            return (void *)(cur + 1);     /* payload starts after label */
        }
        cur = cur->next;
    }

    /* nothing fit: get a new slab from the OS and retry on it */
    block_header_t *fresh = request_chunk(size);
    if (fresh == NULL)
        return NULL;

    split_block(fresh, size);
    fresh->is_free = 0;
    return (void *)(fresh + 1);
}

void my_free(void *ptr)
{
    if (ptr == NULL)
        return;                            /* free(NULL) is legal: do nothing */

    block_header_t *hdr = (block_header_t *)ptr - 1;   /* step back to label */

    if (hdr->magic != MAGIC) {
        fprintf(stderr, "myalloc: bad or corrupted pointer %p\n", ptr);
        return;
    }
    if (hdr->is_free) {
        fprintf(stderr, "myalloc: double free detected at %p\n", ptr);
        return;
    }

    hdr->is_free = 1;

    /* coalesce with the NEXT block if free and physically adjacent */
    if (hdr->next && hdr->next->is_free && adjacent(hdr, hdr->next)) {
        hdr->size += HDR_SIZE + hdr->next->size;
        hdr->next  = hdr->next->next;
    }

    /* coalesce with the PREVIOUS block: find it by walking */
    block_header_t *prev = NULL, *cur = head;
    while (cur != NULL && cur != hdr) {
        prev = cur;
        cur = cur->next;
    }
    if (prev && prev->is_free && adjacent(prev, hdr)) {
        prev->size += HDR_SIZE + hdr->size;
        prev->next  = hdr->next;
    }
}

void *my_calloc(size_t count, size_t size)
{
    if (count != 0 && size > (size_t)-1 / count)
        return NULL;                       /* multiplication would overflow */

    size_t total = count * size;
    void *ptr = my_malloc(total);
    if (ptr != NULL)
        memset(ptr, 0, total);             /* calloc promises zeroed memory */
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
        return ptr;                        /* current block already big enough */

    void *fresh = my_malloc(size);
    if (fresh == NULL)
        return NULL;

    memcpy(fresh, ptr, hdr->size);         /* copy old contents */
    my_free(ptr);
    return fresh;
}

void my_alloc_stats(void)
{
    size_t blocks = 0, free_blocks = 0;
    size_t used_bytes = 0, free_bytes = 0;

    for (block_header_t *cur = head; cur != NULL; cur = cur->next) {
        blocks++;
        if (cur->is_free) { free_blocks++; free_bytes += cur->size; }
        else              { used_bytes += cur->size; }
    }
    fprintf(stderr,
        "---- myalloc stats ----\n"
        "blocks: %zu (free: %zu)\n"
        "bytes in use: %zu | bytes free: %zu\n",
        blocks, free_blocks, used_bytes, free_bytes);
}