/*- -*- mode: c; c-basic-offset: 4; -*-
 *
 * The public interface to the students' memory allocator.
 */

int mm_init(void);
void *mm_malloc(size_t size);
void mm_free(void *ptr);
void *mm_realloc(void *ptr, size_t size);
int mm_check(void);
