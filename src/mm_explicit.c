#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>
#include <stdint.h>

#include "mm.h"
#include "memlib.h"

/*************************************************************************
 * Basic Constants and Macros
 * You are not required to use these macros but may find them helpful.
 *************************************************************************/
#define WSIZE sizeof(void *) /* word size (bytes) */
#define DSIZE (2 * WSIZE)    /* doubleword size (bytes) */
#define CHUNKSIZE (1 << 7)   /* initial heap size (bytes) */

#define MAX(x, y) ((x) > (y) ? (x) : (y))
#define MIN(x, y) ((x) < (y) ? (x) : (y))
#define PW2(exp)  ((1) << (exp))

/* Pack a size and allocated bit into a word */
#define PACK(size, alloc) ((size) | (alloc))

/* Read and write a word at address p */
#define GET(p) (*(uintptr_t *)(p))
#define PUT(p, val) (*(uintptr_t *)(p) = (val))

/* Read the size and allocated fields from address p */
#define GET_SIZE(p) (GET(p) & ~(DSIZE - 1))
#define GET_ALLOC(p) (GET(p) & 0x1)

/* Given block ptr bp, compute address of its header and footer */
#define HDRP(bp) ((char *)(bp) - WSIZE)
#define FTRP(bp) ((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE)

/* Given block ptr bp, compute address of next and previous blocks */
#define NEXT_BLKP(bp) ((char *)(bp) + GET_SIZE(((char *)(bp) - WSIZE)))
#define PREV_BLKP(bp) ((char *)(bp) - GET_SIZE(((char *)(bp) - DSIZE)))

void* heap_listp = NULL;

typedef struct block_t {
    struct block_t* prev;
    struct block_t* next;
} block_s;

block_s* free_listp;

void freelist_remove(block_s* bp) {
    if (bp->next == bp) {
        free_listp = NULL;
    } else {
        bp->prev->next = bp->next;
        bp->next->prev = bp->prev;
        if (free_listp == bp) {
            free_listp = bp->next;
        }
    }
}

void freelist_insert(block_s* bp) {
    if (!free_listp) {
        bp->next = bp;
        bp->prev = bp;
        free_listp = bp;
    } else {
        bp->next = (block_s *)free_listp;
        bp->prev = ((block_s *)free_listp)->prev;
        bp->prev->next = (block_s *)bp;
        bp->next->prev = (block_s *)bp;
    }
}

/**********************************************************
 * mm_init
 * Initialize the heap, including "allocation" of the
 * prologue and epilogue
 **********************************************************/
int mm_init(void) {
    if ((heap_listp = mem_sbrk(4 * WSIZE)) == (void *)-1)
        return -1;
    PUT(heap_listp, 0);                            // alignment padding
    PUT(heap_listp + (1 * WSIZE), PACK(DSIZE, 1)); // prologue header
    PUT(heap_listp + (2 * WSIZE), PACK(DSIZE, 1)); // prologue footer
    PUT(heap_listp + (3 * WSIZE), PACK(0, 1));     // epilogue header
    heap_listp += DSIZE;
    free_listp = NULL;
    // free_listp = heap_listp;  // initially, all heap memory is free
    // ((block_s *)free_listp)->prev = (block_s *)free_listp;
    // ((block_s *)free_listp)->next = (block_s *)free_listp;
    return 0;
}

/**********************************************************
 * coalesce
 * Covers the 4 cases discussed in the text:
 * - both neighbours are allocated
 * - the next block is available for coalescing
 * - the previous block is available for coalescing
 * - both neighbours are available for coalescing
 **********************************************************/
void *coalesce(void *bp) {
    // printf("[coalesce] start\n");
    // printf("[coalesce] bp = %p\n", bp);
    // printf("[coalesce] prev = %p\n", PREV_BLKP(bp));
    // printf("[coalesce] next = %p\n", NEXT_BLKP(bp));
    size_t prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(bp)));
    size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));
    size_t size = GET_SIZE(HDRP(bp));
    // printf("[coalesce] alloc info retrived\n");
    if (prev_alloc && next_alloc) { /* Case 1 */
        return bp;
    } else if (prev_alloc && !next_alloc) { /* Case 2 */
        freelist_remove((block_s *)NEXT_BLKP(bp));
        size += GET_SIZE(HDRP(NEXT_BLKP(bp)));
        PUT(HDRP(bp), PACK(size, 0));
        PUT(FTRP(bp), PACK(size, 0));
        return bp;
    } else if (!prev_alloc && next_alloc) { /* Case 3 */
        freelist_remove((block_s *)PREV_BLKP(bp));
        size += GET_SIZE(HDRP(PREV_BLKP(bp)));
        PUT(FTRP(bp), PACK(size, 0));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
        return PREV_BLKP(bp);
    } else { /* Case 4 */
        freelist_remove((block_s *)PREV_BLKP(bp));
        freelist_remove((block_s *)NEXT_BLKP(bp));
        size += GET_SIZE(HDRP(PREV_BLKP(bp))) + GET_SIZE(FTRP(NEXT_BLKP(bp)));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
        PUT(FTRP(NEXT_BLKP(bp)), PACK(size, 0));
        return PREV_BLKP(bp);
    }
}

/**********************************************************
 * extend_heap
 * Extend the heap by "words" words, maintaining alignment
 * requirements of course. Free the former epilogue block
 * and reallocate its new header
 **********************************************************/
void *extend_heap(size_t words) {
    // printf("[extend_heap] start\n");
    char *bp;
    size_t size;

    /* Allocate an even number of words to maintain alignments */
    size = (words % 2) ? (words + 1) * WSIZE : words * WSIZE;
    // printf("[extend_heap] looking for %ld bytes\n", size);
    if ((bp = mem_sbrk(size)) == (void *)-1)
        return NULL;
    // printf("[extend_heap] sbrk done, about to modify headers\n");
    /* Initialize free block header/footer and the epilogue header */
    PUT(HDRP(bp), PACK(size, 0));         // free block header
    PUT(FTRP(bp), PACK(size, 0));         // free block footer
    PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1)); // new epilogue header
    // printf("[extend_heap] header modification done, about to coalesce %p\n", bp);
    /* Coalesce if the previous block was free */
    return coalesce(bp);
}

/**********************************************************
 * find_fit
 * Traverse the heap searching for a block to fit asize
 * Return NULL if no free blocks can handle that size
 * Assumed that asize is aligned
 **********************************************************/
void* find_fit(size_t asize) {
    if (!free_listp) {
        return NULL;
    }
    block_s* curr = (block_s *)free_listp;
    do {
        // printf("[find_fit] free_listp = %p\n", free_listp);
        // printf("[find_fit] curr = %p\n", (void *)curr);
        // printf("[find_fit] curr->prev == NULL ? %d\n", curr->prev == NULL);
        // printf("[find_fit] curr->prev = %p\n", (void *)(curr->prev));
        // printf("[find_fit] curr->next = %p\n", (void *)(curr->next));
        size_t csize = GET_SIZE(HDRP((void *)curr));
        // printf("[find_fit] csize = %ld, asize = %ld\n", csize, asize);
        if (csize >= asize) {
            freelist_remove((block_s *)curr);
            // printf("[find_fit] fit found\n\n");
            return (void *)curr;
        }
        curr = curr->next;
    } while (curr != free_listp);
    return NULL;
    // void* bp;
    // void* nextbp;
    // for (bp = /*heap_listp*/last_fitp; GET_SIZE(HDRP(bp)) > 0; bp = NEXT_BLKP(bp)) {
    //     if (!GET_ALLOC(HDRP(bp))) {
    //         if (asize <= GET_SIZE(HDRP(bp))) {
    //             return bp;
    //         }
    //         // nextbp = NEXT_BLKP(bp);
    //         // if (GET_SIZE(HDRP(nextbp)) > 0 && !GET_ALLOC(HDRP(nextbp))) {
    //         //     bp = coalesce(bp);
    //         // }
    //     }
    // }
    // last_fitp = heap_listp;
    // return NULL;
}

/**********************************************************
 * place
 * Mark the block as allocated
 **********************************************************/
void place(void *bp, size_t asize) {
    /* Get the current block size */
    size_t bsize = GET_SIZE(HDRP(bp));
    PUT(HDRP(bp), PACK(bsize, 1));
    PUT(FTRP(bp), PACK(bsize, 1));

    // size_t bsize = GET_SIZE(HDRP(bp));
    // if (asize < bsize) {
    //     void* rp = bp + asize;
    //     size_t rsize = bsize - asize;
    //     PUT(HDRP(rp), PACK(rsize, 0));
    //     PUT(FTRP(rp), PACK(rsize, 0));
    // }
    // PUT(HDRP(bp), PACK(asize, 1));
    // PUT(FTRP(bp), PACK(asize, 1));
}

/**********************************************************
 * mm_free
 * Free the block and coalesce with neighbouring blocks
 **********************************************************/
void mm_free(void *bp) {
    if (bp == NULL) return;
    size_t size = GET_SIZE(HDRP(bp));
    PUT(HDRP(bp), PACK(size, 0));
    PUT(FTRP(bp), PACK(size, 0));
    freelist_insert((block_s *)coalesce(bp));
}

/**********************************************************
 * mm_malloc
 * Allocate a block of size bytes.
 * The type of search is determined by find_fit
 * The decision of splitting the block, or not is determined
 *   in place(..)
 * If no block satisfies the request, the heap is extended
 **********************************************************/
void* mm_malloc(size_t size) {
    size_t asize;      /* adjusted block size */
    size_t extendsize; /* amount to extend heap if no fit */
    char *bp;

    /* Ignore spurious requests */
    if (size == 0) {
        // printf("[mm_malloc] size = 0, return\n");
        return NULL;
    }
    /* Adjust block size to include overhead and alignment reqs. */
    asize = (size <= DSIZE) ?
            2 * DSIZE : DSIZE * ((size + (DSIZE) + (DSIZE - 1)) / DSIZE);
    /* Search the free list for a fit */
    if ((bp = find_fit(asize)) != NULL) {
        place(bp, asize);
        return bp;
    }
    /* No fit found. Get more memory and place the block */
    extendsize = MAX(asize, CHUNKSIZE);
    if ((bp = extend_heap(extendsize / WSIZE)) == NULL)
        return NULL;
    place(bp, asize);
    return bp;
}

/**********************************************************
 * mm_realloc
 * Implemented simply in terms of mm_malloc and mm_free
 *********************************************************/
void *mm_realloc(void *ptr, size_t size) {
    /* If size == 0 then this is just free, and we return NULL. */
    if (size == 0)
    {
        mm_free(ptr);
        return NULL;
    }
    /* If oldptr is NULL, then this is just malloc. */
    if (ptr == NULL)
        return (mm_malloc(size));

    void *oldptr = ptr;
    void *newptr;
    size_t copySize;

    newptr = mm_malloc(size);
    if (newptr == NULL)
        return NULL;

    /* Copy the old data. */
    copySize = GET_SIZE(HDRP(oldptr));
    if (size < copySize)
        copySize = size;
    memcpy(newptr, oldptr, copySize);
    mm_free(oldptr);
    return newptr;
}

/**********************************************************
 * mm_check
 * Check the consistency of the memory heap
 * Return nonzero if the heap is consistant.
 *********************************************************/
int mm_check(void) {
    return 1;
}
