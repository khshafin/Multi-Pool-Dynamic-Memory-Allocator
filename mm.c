#include <string.h>
#include <stdio.h>
#include <unistd.h>

/* The standard allocator interface from stdlib.h.  These are the
 * functions you must implement, more information on each function is
 * found below. They are declared here in case you want to use one
 * function in the implementation of another. */
void *malloc(size_t size);
void free(void *ptr);
void *calloc(size_t nmemb, size_t size);
void *realloc(void *ptr, size_t size);

/* When requesting memory from the OS using sbrk(), request it in
 * increments of CHUNK_SIZE. */
#define CHUNK_SIZE (1<<12)

/*
 * This function, defined in bulk.c, allocates a contiguous memory
 * region of at least size bytes.  It MAY NOT BE USED as the allocator
 * for pool-allocated regions.  Memory allocated using bulk_alloc()
 * must be freed by bulk_free().
 *
 * This function will return NULL on failure.
 */
extern void *bulk_alloc(size_t size);

/*
 * This function is also defined in bulk.c, and it frees an allocation
 * created with bulk_alloc().  Note that the pointer passed to this
 * function MUST have been returned by bulk_alloc(), and the size MUST
 * be the same as the size passed to bulk_alloc() when that memory was
 * allocated.  Any other usage is likely to fail, and may crash your
 * program.
 *
 * Passing incorrect arguments to this function will result in an
 * error message notifying you of this mistake.
 */
extern void bulk_free(void *ptr, size_t size);

/*
 * This function computes the log base 2 of the allocation block size
 * for a given allocation.  To find the allocation block size from the
 * result of this function, use 1 << block_index(x).
 *
 * This function ALREADY ACCOUNTS FOR both padding and the size of the
 * header.
 *
 * Note that its results are NOT meaningful for any
 * size > 4088!
 *
 * You do NOT need to understand how this function works.  If you are
 * curious, see the gcc info page and search for __builtin_clz; it
 * basically counts the number of leading binary zeroes in the value
 * passed as its argument.
 */
static inline __attribute__((unused)) int block_index(size_t x) {
    if (x <= 8) {
        return 5;
    } else {
        return 32 - __builtin_clz((unsigned int)x + 7);
    }
}

/* Free block structure for managing the free list */
typedef struct free_block {
    size_t header;              /* Size and flags */
    struct free_block *next;    /* Next free block in list */
} free_block_t;

/* Free list table - array of pointers to free lists */
static free_block_t **free_lists = NULL;

/* Number of free lists (for block sizes 2^5 to 2^12) */
#define NUM_LISTS 8

/* Allocated bit flag */
#define ALLOCATED_BIT 1

/* Bulk allocation marker bit (bit 1) - pool sizes are multiples of 32 */
#define BULK_ALLOC_BIT 2

/* Helper functions */
static void init_free_lists(void);
static void *get_more_memory(int index);
static free_block_t *get_header(void *ptr);
static void *get_payload(free_block_t *block);
static size_t get_block_size(free_block_t *block);
static void set_allocated(free_block_t *block, size_t size);
static void set_free(free_block_t *block, size_t size);
static void add_to_free_list(free_block_t *block, int index);
static free_block_t *remove_from_free_list(int index);

/*
 * Initialize the free list table
 */
static void init_free_lists(void) {
    /* Allocate space for the free list table using sbrk */
    free_lists = (free_block_t **)sbrk(NUM_LISTS * sizeof(free_block_t *));
    
    if (free_lists == (void *)-1) {
        free_lists = NULL;
        return;
    }
    
    /* Initialize all lists to NULL */
    for (int i = 0; i < NUM_LISTS; i++) {
        free_lists[i] = NULL;
    }
}

/*
 * Get the header from a user pointer
 */
static free_block_t *get_header(void *ptr) {
    return (free_block_t *)((char *)ptr - sizeof(size_t));
}

/*
 * Get the payload pointer from a header
 */
static void *get_payload(free_block_t *block) {
    return (void *)((char *)block + sizeof(size_t));
}

/*
 * Get the block size (without allocated bit)
 */
static size_t get_block_size(free_block_t *block) {
    return block->header & ~(ALLOCATED_BIT | BULK_ALLOC_BIT);
}

/*
 * Mark block as allocated with given size
 */
static void set_allocated(free_block_t *block, size_t size) {
    block->header = size | ALLOCATED_BIT;
}

/*
 * Mark block as free with given size
 */
static void set_free(free_block_t *block, size_t size) {
    block->header = size;
}

/*
 * Add a block to the appropriate free list
 */
static void add_to_free_list(free_block_t *block, int index) {
    block->next = free_lists[index];
    free_lists[index] = block;
}

/*
 * Remove and return a block from the free list
 */
static free_block_t *remove_from_free_list(int index) {
    if (free_lists[index] == NULL) {
        return NULL;
    }
    
    free_block_t *block = free_lists[index];
    free_lists[index] = block->next;
    return block;
}

/*
 * Request more memory from OS and break it into blocks
 */
static void *get_more_memory(int index) {
    /* Request CHUNK_SIZE bytes */
    void *mem = sbrk(CHUNK_SIZE);
    if (mem == (void *)-1) {
        return NULL;
    }
    
    /* Calculate block size for this index */
    size_t block_size = 1 << (index + 5);
    
    /* Break the chunk into blocks and add to free list */
    char *current = (char *)mem;
    char *end = current + CHUNK_SIZE;
    
    while (current + block_size <= end) {
        free_block_t *block = (free_block_t *)current;
        set_free(block, block_size);
        add_to_free_list(block, index);
        current += block_size;
    }
    
    return mem;
}

/*
 * You must implement malloc().  Your implementation of malloc() must be
 * the multi-pool allocator described in the project handout.
 */
void *malloc(size_t size) {
    /* Initialize free lists on first call */
    if (free_lists == NULL) {
        init_free_lists();
        if (free_lists == NULL) {
            return NULL;
        }
    }
    
    /* Handle size 0 */
    if (size == 0) {
        return NULL;
    }
    
    /* Check if we need bulk allocation (> 4088 bytes) */
    if (size > 4088) {
        /* Allocate with bulk allocator, adding 8 bytes for header */
        void *mem = bulk_alloc(size + 8);
        if (mem == NULL) {
            return NULL;
        }
        
        free_block_t *block = (free_block_t *)mem;
        /* Mark as allocated AND bulk */
        block->header = (size + 8) | ALLOCATED_BIT | BULK_ALLOC_BIT;
        return get_payload(block);
    }
    
    /* Pool allocation for sizes <= 4088 */
    int index = block_index(size);
    size_t block_size = 1 << (index + 5);
    
    /* Adjust index to be 0-based for our array */
    index -= 5;
    
    /* Check if we have a free block */
    free_block_t *block = remove_from_free_list(index);
    
    /* If no free block, get more memory */
    if (block == NULL) {
        if (get_more_memory(index) == NULL) {
            return NULL;
        }
        block = remove_from_free_list(index);
    }
    
    /* Mark block as allocated */
    set_allocated(block, block_size);
    
    return get_payload(block);
}

/*
 * You must also implement calloc().  It should create allocations
 * compatible with those created by malloc().  In particular, any
 * allocations of a total size <= 4088 bytes must be pool allocated,
 * while larger allocations must use the bulk allocator.
 *
 * calloc() (see man 3 calloc) returns a cleared allocation large enough
 * to hold nmemb elements of size size.  It is cleared by setting every
 * byte of the allocation to 0.  You should use the function memset()
 * for this (see man 3 memset).
 */
void *calloc(size_t nmemb, size_t size) {
    /* Calculate total size */
    size_t total = nmemb * size;
    
    /* Check for overflow */
    if (nmemb != 0 && total / nmemb != size) {
        return NULL;
    }
    
    /* Allocate memory */
    void *ptr = malloc(total);
    if (ptr == NULL) {
        return NULL;
    }
    
    /* Clear the memory */
    memset(ptr, 0, total);
    
    return ptr;
}

/*
 * You must also implement realloc().  It should create allocations
 * compatible with those created by malloc(), honoring the pool
 * alocation and bulk allocation rules.  It must move data from the
 * previously-allocated block to the newly-allocated block if it cannot
 * resize the given block directly.  See man 3 realloc for more
 * information on what this means.
 *
 * It is not possible to implement realloc() using bulk_alloc() without
 * additional metadata, so the given code is NOT a working
 * implementation!
 */
void *realloc(void *ptr, size_t size) {
    /* If ptr is NULL, behave like malloc */
    if (ptr == NULL) {
        return malloc(size);
    }
    
    /* If size is 0, behave like free and return NULL */
    if (size == 0) {
        free(ptr);
        return NULL;
    }
    
    /* Get the header of the current block */
    free_block_t *block = get_header(ptr);
    size_t old_block_size = get_block_size(block);
    
    /* Determine the usable size (subtract header) */
    size_t old_usable = old_block_size - 8;
    
    /* Check if old block was bulk allocated */
    int old_is_bulk = (old_usable > 4088);
    
    /* Check if new size requires bulk allocation */
    int new_is_bulk = (size > 4088);
    
    /* Calculate new block size */
    size_t new_block_size;
    if (new_is_bulk) {
        new_block_size = size + 8;
    } else {
        int index = block_index(size);
        new_block_size = 1 << (index + 5);
    }
    
    /* If the new size fits in the current block, just return it */
    if (size <= old_usable && old_block_size == new_block_size) {
        return ptr;
    }
    
    /* If both are in the same pool, and new size fits in old block */
    if (!old_is_bulk && !new_is_bulk && size <= old_usable) {
        return ptr;
    }
    
    /* Otherwise, allocate new block and copy data */
    void *new_ptr = malloc(size);
    if (new_ptr == NULL) {
        return NULL;
    }
    
    /* Copy old data to new block */
    size_t copy_size = (size < old_usable) ? size : old_usable;
    memcpy(new_ptr, ptr, copy_size);
    
    /* Free old block */
    free(ptr);
    
    return new_ptr;
}

/*
 * You should implement a free() that can successfully free a region of
 * memory allocated by any of the above allocation routines, whether it
 * is a pool- or bulk-allocated region.
 *
 * The given implementation does nothing.
 */
void free(void *ptr) {
    /* Handle NULL pointer */
    if (ptr == NULL) {
        return;
    }
    
    /* Get the header */
    free_block_t *block = get_header(ptr);
    
    /* Check if this is a bulk allocation using the bulk bit */
    if (block->header & BULK_ALLOC_BIT) {
        size_t block_size = get_block_size(block);
        bulk_free((void *)block, block_size);
        return;
    }
    
    /* Pool allocation - get block size and add back to free list */
    size_t block_size = get_block_size(block);
    
    /* Calculate the index based on block size */
    int index;
    switch (block_size) {
        case 32:   index = 0; break;
        case 64:   index = 1; break;
        case 128:  index = 2; break;
        case 256:  index = 3; break;
        case 512:  index = 4; break;
        case 1024: index = 5; break;
        case 2048: index = 6; break;
        case 4096: index = 7; break;
        default:   return;  /* Invalid block size */
    }
    
    /* Mark block as free */
    set_free(block, block_size);
    
    /* Add to free list */
    add_to_free_list(block, index);
}
