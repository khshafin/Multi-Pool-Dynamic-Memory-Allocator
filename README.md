# Multi-Pool Dynamic Memory Allocator

Complete implementation of malloc/free/calloc/realloc using a multi-pool allocation strategy.

## Architecture

**Pool Allocation (1-4088 bytes):**
- 8 free lists for block sizes: 32, 64, 128, 256, 512, 1024, 2048, 4096 bytes
- Requests 4KB chunks from OS via `sbrk()`, splits into fixed-size blocks
- Explicit free lists using space within free blocks

**Bulk Allocation (>4088 bytes):**
- Direct memory mapping via `bulk_alloc()`/`bulk_free()`
- No pool management overhead

## Block Structure

Each block has an 8-byte header:
- **Size**: Total block size (including header)
- **Bit 0**: Allocated flag (1 = allocated, 0 = free)
- **Bit 1**: Bulk allocation marker (1 = bulk, 0 = pool)

Free blocks additionally store a `next` pointer for the free list.

## Key Design Decisions

**Two flag bits:** Using bit 1 to mark bulk allocations makes `free()` O(1) and reliable—no need to guess allocation type from size alone.

**8-byte overhead:** Minimal per-allocation overhead; all allocations are 8-byte aligned.

**O(1) operations:** malloc/free just pop/push from free lists; realloc is O(1) when staying in same pool.

## Building

```bash
gcc -c mm.c bulk.c -g -Wall -Werror -std=c99 -fPIC -D_DEFAULT_SOURCE
gcc -shared -fPIC -o libcsemalloc.so mm.o bulk.o

# Test with Unix commands
LD_PRELOAD=./libcsemalloc.so ls
```

## Testing

- ✅ Passes `test_simple_malloc.c` and `test_bulk.c`
- ✅ Works with Unix utilities: ls, echo, pwd, whoami, w
- ✅ Handles all edge cases: NULL pointers, size 0, realloc scenarios
