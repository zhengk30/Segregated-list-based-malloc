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

#define NUM_SIZE_CLASSES (10)
#define HASH_DIFF (7)

#define INVALID_ADDR ("[ERROR] mm_check() fails: free chunk has invalid address\n")
#define NONFREE_IN_SEGLIST ("[ERROR] mm_check() fails: non-free chunk appears in free list\n")
#define FREE_NOT_IN_SEGLIST ("[ERROR] mm_check() fails: free chunk not found in free list\n")

static size_t heap_size = 0;
void* heap_listp = NULL;

typedef struct block_t {
    struct block_t* prev;
    struct block_t* next;
} block_s;

block_s* segfit_lists[NUM_SIZE_CLASSES];
/*
0: <= 128 (2^7)
1: 129-256 (2^8)
2: 257-512 (2^9)
3: 513-1024 (2^10)
4: 1025-2048 (2^11)
5: 2049-4096 (2^12)
6: 4097-8192 (2^13)
7: 8193-16384 (2^14)
8: 16385-32768 (2^15)
9: >= 32769
*/

static int mm_alloc_correct(void);
static int mm_free_in_seglist(void);
static int mm_valid_free_address(void);
static int segfit_asize2index(size_t);
static void segfit_insert(block_s*);
static void segfit_remove(block_s*);

/**********************************************************
 * mm_init
 * Initialize the heap, including "allocation" of the
 * prologue and epilogue
 **********************************************************/
int mm_init(void) {
    if ((heap_listp = mem_sbrk(4 * WSIZE)) == (void *)-1)
        return -1;
    heap_size += 4 * WSIZE;                        // for mm_check()
    PUT(heap_listp, 0);                            // alignment padding
    PUT(heap_listp + (1 * WSIZE), PACK(DSIZE, 1)); // prologue header
    PUT(heap_listp + (2 * WSIZE), PACK(DSIZE, 1)); // prologue footer
    PUT(heap_listp + (3 * WSIZE), PACK(0, 1));     // epilogue header
    heap_listp += DSIZE;
    /* cannot initialize globally, otherwise segfault */
    for (int i = 0; i < NUM_SIZE_CLASSES; ++i) {
        segfit_lists[i] = NULL;
    }
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
void* coalesce(void *bp) {
    size_t prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(bp)));
    size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));
    size_t size = GET_SIZE(HDRP(bp));
    if (prev_alloc && next_alloc) { /* Case 1 */
        return bp;
    } else if (prev_alloc && !next_alloc) { /* Case 2 */
        size += GET_SIZE(HDRP(NEXT_BLKP(bp)));
        segfit_remove((block_s *)NEXT_BLKP(bp));
        PUT(HDRP(bp), PACK(size, 0));
        PUT(FTRP(bp), PACK(size, 0));
        return bp;
    } else if (!prev_alloc && next_alloc) { /* Case 3 */
        size += GET_SIZE(HDRP(PREV_BLKP(bp)));
        segfit_remove((block_s *)PREV_BLKP(bp));
        PUT(FTRP(bp), PACK(size, 0));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
        return PREV_BLKP(bp);
    } else { /* Case 4 */
        size += GET_SIZE(HDRP(PREV_BLKP(bp))) + GET_SIZE(FTRP(NEXT_BLKP(bp)));
        segfit_remove((block_s *)PREV_BLKP(bp));
        segfit_remove((block_s *)NEXT_BLKP(bp));
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
void* extend_heap(size_t words) {
    char *bp;
    size_t size;

    /* Allocate an even number of words to maintain alignments */
    size = (words % 2) ? (words + 1) * WSIZE : words * WSIZE;
    // printf("[extend_heap] looking for %ld bytes\n", size);
    if ((bp = mem_sbrk(size)) == (void *)-1)
        return NULL;
    heap_size += size;                    // for mm_check()
    /* Initialize free block header/footer and the epilogue header */
    PUT(HDRP(bp), PACK(size, 0));         // free block header
    PUT(FTRP(bp), PACK(size, 0));         // free block footer
    PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1)); // new epilogue header
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
    int i, start = segfit_asize2index(asize);
    for (i = start; i < NUM_SIZE_CLASSES; ++i) {
        block_s* head = segfit_lists[i];
        if (!head) {
            continue;
        }
        block_s* curr = head;
        //while (1) {  do-while is faster (might be because of fewer branch predictions?)
        do {
            size_t csize = GET_SIZE(HDRP((void *)curr));
            if (asize <= csize) {
                segfit_remove(curr);
                size_t diff = csize - asize;
                if (diff >= asize /*2 * DSIZE*/) {
                    PUT(HDRP(curr), PACK(asize, 0));
                    PUT(HDRP(curr), PACK(asize, 0));
                    void* rp = (void *)curr + asize;
                    PUT(HDRP(rp), PACK(diff, 0));
                    PUT(FTRP(rp), PACK(diff, 0));
                    segfit_insert((block_s *)rp);
                }
                return (void *)curr;
            }
            curr = curr->next;
            // if (curr == head) {
            //     break;
            // }
        } while (curr != head);
    }
    return NULL;
}

/**********************************************************
 * place
 * Mark the block as allocated
 **********************************************************/
void place(void *bp, size_t asize) {
    size_t bsize = GET_SIZE(HDRP(bp));
    size_t rsize = bsize - asize;
    if (rsize >= asize) {
        /* split if free chunk is too large */
        void* rp = bp + asize;
        PUT(HDRP(rp), PACK(rsize, 0));
        PUT(FTRP(rp), PACK(rsize, 0));
        segfit_insert((block_s *)rp);
        PUT(HDRP(bp), PACK(asize, 1));
        PUT(FTRP(bp), PACK(asize, 1));
    } else {
        PUT(HDRP(bp), PACK(bsize, 1));
        PUT(FTRP(bp), PACK(bsize, 1));
    }
    // assert(mm_check());

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
    block_s* coal_bp = (block_s *)coalesce(bp);
    segfit_insert(coal_bp);
    // assert(mm_check());
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
        return NULL;
    }
    /* Adjust block size to include overhead and alignment reqs. */
    asize = (size <= DSIZE) ?
            2 * DSIZE : DSIZE * ((size + (DSIZE) + (DSIZE - 1)) / DSIZE);

    /* Search the free list for a fit */
    if ((bp = find_fit(asize)) != NULL) {
        place(bp, asize);
        // assert(mm_check());
        return bp;
    }
    /* No fit found. Get more memory and place the block */
    extendsize = MAX(asize, CHUNKSIZE);
    if ((bp = extend_heap(extendsize / WSIZE)) == NULL) {
        // assert(mm_check());
        return NULL;
    }
    place(bp, asize);
    // assert(mm_check());
    return bp;
}

/**********************************************************
 * mm_realloc
 * Implemented simply in terms of mm_malloc and mm_free
 *********************************************************/
void *mm_realloc(void *ptr, size_t size) {
    /* If size == 0 then this is just free, and we return NULL. */
    if (size == 0) {
        mm_free(ptr);
        // assert(mm_check());
        return NULL;
    }
    /* If oldptr is NULL, then this is just malloc. */
    if (ptr == NULL) {
        return mm_malloc(size);
    }
    /* need to compute size of chunk to include overhead and alignment */
    size_t new_asize = (size <= DSIZE) ?
            2 * DSIZE : DSIZE * ((size + (DSIZE) + (DSIZE - 1)) / DSIZE);
    size_t old_asize = GET_SIZE(HDRP(ptr));
    if (new_asize == old_asize) {
        /* no need to malloc and copy, just return the old one */
        return ptr;
    } else if (new_asize < old_asize) {
        /* no need to malloc a new chunk, just chop the old one */
        size_t rsize = old_asize - new_asize;
        if (rsize >= new_asize) {
            PUT(HDRP(ptr), PACK(new_asize, 1));
            PUT(FTRP(ptr), PACK(new_asize, 1));
            void* rp = ptr + new_asize;
            PUT(HDRP(rp), PACK(rsize, 0));
            PUT(FTRP(rp), PACK(rsize, 0));
            segfit_insert(rp);
            // assert(mm_check());
        }
        return ptr;
    } else {
        void* oldptr = ptr;
        void* newptr = mm_malloc(size);
        if (!newptr) {
            return NULL;
        }
        memcpy(newptr, oldptr, size);
        mm_free(oldptr);
        // assert(mm_check());
        return newptr;
    }
}

/**********************************************************
 * mm_check
 * Check the consistency of the memory heap
 * Return nonzero if the heap is consistant.
 *********************************************************/
int mm_check(void) {
    return mm_alloc_correct()       // checks all chunks in seg lists are free
        && mm_free_in_seglist()     // checks all free chunks in heap are added to seg list
        && mm_valid_free_address(); // checks all free chunks' addresses are within heap
}

/**********************************************************
 * HELPER FUNCTIONS
 * * segfit helpers
 * * memory check helpers
 *********************************************************/

static int segfit_asize2index(size_t asize) {
    if (asize <= PW2(HASH_DIFF)) {
        return 0;
    } else {
        int index = HASH_DIFF;
        while (PW2(index) < asize) {
            index++;
        }
        return MIN((index - HASH_DIFF), (NUM_SIZE_CLASSES - 1));
    }
}

static void segfit_remove(block_s* bp) {
    size_t bp_asize = GET_SIZE(HDRP((void *)bp));
    int bp_index = segfit_asize2index(bp_asize);
    if (bp->next == bp) {
        segfit_lists[bp_index] = NULL;
    } else {
        bp->prev->next = bp->next;
        bp->next->prev = bp->prev;
        if (segfit_lists[bp_index] == bp) {
            segfit_lists[bp_index] = bp->next;
        }
    }
}

static void segfit_insert(block_s* bp) {
    size_t bp_asize = GET_SIZE(HDRP((void *)bp));
    int bp_index = segfit_asize2index(bp_asize);
    if (!segfit_lists[bp_index]) {
        segfit_lists[bp_index] = bp;
        segfit_lists[bp_index]->next = segfit_lists[bp_index];
        segfit_lists[bp_index]->prev = segfit_lists[bp_index];
    } else {
        bp->next = (block_s *)segfit_lists[bp_index];
        bp->prev = ((block_s *)segfit_lists[bp_index])->prev;
        bp->prev->next = (block_s *)bp;
        bp->next->prev = (block_s *)bp;
    }
}

/**********************************************************
 * mm_alloc_correct
 * Checks whether the chunks in seg list are actually free
 *********************************************************/
static int mm_alloc_correct(void) {
    for (int i = 0; i < NUM_SIZE_CLASSES; i++) {
        block_s* head = segfit_lists[i];
        if (!head) {
            continue;
        }
        block_s* curr = head;
        while (1) {
            if (GET_ALLOC(HDRP((void *)curr))) {
                fprintf(stderr, NONFREE_IN_SEGLIST);
                return 0;
            }
            curr = curr->next;
            if (curr == head) {
                break;
            }
        }
    }
    return 1;
}

/**********************************************************
 * mm_free_in_seglist
 * Checks whether all free chunks in the heap are stored
 * in the corresponding segregated list
 * This procedure is extremly slow
 *********************************************************/
static int mm_free_in_seglist(void) {
    for (void* bp = heap_listp; GET_SIZE(HDRP(bp)) > 0; bp = NEXT_BLKP(bp)) {
        size_t asize = GET_SIZE(HDRP(bp));
        int index = segfit_asize2index(asize);
        int found_in_seglist = 0;
        if (GET_ALLOC(HDRP(bp))) {
            continue;
        }
        for (int i = index; i < NUM_SIZE_CLASSES; i++) {
            block_s* head = segfit_lists[i];
            if (!head) {
                continue;
            }
            block_s* curr = head;
            while (1) {
                if ((void *)curr == bp) {
                    found_in_seglist = 1;
                    break;
                }
                curr = curr->next;
                if (curr == head) {
                    break;
                }
            }
        }
        if (!found_in_seglist) {
            fprintf(stderr, FREE_NOT_IN_SEGLIST);
            return 0;
        }
    }
    return 1;
}

/**********************************************************
 * mm_valid_free_address
 * Checks whether each free chunk stored in the seg lists
 * are within the bounds of the currently allocated heap
 * (heap size might change over time, so check address validity
 * immeidately after seg lists are changed)
 *********************************************************/
static int mm_valid_free_address(void) {
    void* curr_heap_start = heap_listp;
    void* curr_heap_end = heap_listp + heap_size;
    for (int i = 0; i < NUM_SIZE_CLASSES; i++) {
        block_s* head = segfit_lists[i];
        if (!head) {
            continue;
        }
        block_s* curr = head;
        while (1) {
            if ((void *)curr < curr_heap_start || (void *)curr > curr_heap_end) {
                fprintf(stderr, INVALID_ADDR);
                return 0;
            }
            curr = curr->next;
            if (curr == head) {
                break;
            }
        }
    }
    return 1;
}
