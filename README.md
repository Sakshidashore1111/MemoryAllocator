# MemoryAllocator

A custom dynamic memory allocator in C — a from-scratch implementation of
`malloc`, `free`, `calloc`, and `realloc`, built to understand how memory
management really works under the hood.

Instead of relying on the C standard library, this allocator requests raw
memory directly from the Linux kernel using the `mmap` system call and
manages every byte itself.

## Features

- **`my_malloc` / `my_free` / `my_calloc` / `my_realloc`** — drop-in style API
- **First-fit allocation** over a linked list of memory blocks
- **Block splitting** — large free blocks are split to minimize wasted memory
- **Block coalescing** — adjacent free blocks are merged on `free` to fight fragmentation
- **16-byte aligned** returned pointers (matching glibc malloc's guarantee)
- **Double-free detection** — freeing the same pointer twice is caught and reported, not fatal
- **Corruption detection** — every block carries a magic number; invalid or foreign pointers are rejected
- **Overflow-safe `calloc`** — multiplication overflow is detected before allocating
- **Allocator statistics** — live report of blocks, used bytes, and free bytes
- **Test suite** — 10 automated tests covering alignment, reuse, overlap, edge cases

## Build and run

Requires Linux (or WSL) and gcc.

```bash
make test
```

Expected output:

```text
[ 1] malloc returns non-NULL                      PASS
[ 2] returned pointer is 16-byte aligned          PASS
[ 3] memory is actually writable and readable     PASS
[ 4] two allocations don't overlap                PASS
[ 5] free then malloc reuses memory (first-fit)   PASS
[ 6] free(NULL) does not crash                    PASS
myalloc: double free detected at 0x7703ebd00020
[ 7] double free is detected, not fatal           PASS
[ 8] calloc zeroes memory                         PASS
[ 9] realloc preserves contents                   PASS
[10] large allocation (2 MB) works                PASS

All 10 tests passed.
```

(The "double free detected" warning is intentional — test 7 deliberately
triggers it to prove the safety check works.)

## How it works

The allocator requests memory from the OS in large chunks (1 MB) via `mmap`,
then carves them up itself. Every allocation is preceded by a hidden metadata
header:

```text
+-----------------------------+----------------------------+
| header: size | magic | free |  payload (returned to user)|
| aligned to 16 bytes         |  ptr points here --^       |
+-----------------------------+----------------------------+
```

- `my_malloc` walks the block list (first-fit), splits oversized blocks,
  and returns a pointer just past the header.
- `my_free` steps back to the header, validates the magic number and
  free-status, marks the block free, and merges it with any physically
  adjacent free neighbors.
- Requests that don't fit any existing block trigger a new `mmap` chunk.

## Project structure

```text
├── include/myalloc.h     public API
├── src/allocator.c       allocator implementation
├── tests/test_basic.c    automated test suite
└── Makefile              build + test in one command
```

## Roadmap

- [x] First-fit allocation, splitting, coalescing
- [x] Double-free and corruption detection
- [x] Automated test suite
- [ ] Thread safety (mutex, then per-thread caches)
- [ ] Segregated size-class memory pools for O(1) small allocations
- [ ] Benchmarks vs. glibc malloc
- [ ] `LD_PRELOAD` support to run real programs on this allocator