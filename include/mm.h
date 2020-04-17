#ifndef _MM_H_
#define _MM_H_

#include <stdlib.h>

/*
 * Memory Alloc Policy:
 * 
 * 1. Find best-fit blocks in free list, goto 3 if found.
 * 2. Add extra pages to memory pool as required.
 * 3. If the reminder greater then the Minimal Block Size, Split 
 *    the block and put the reminder into proper Free List.
 * 4. Initialize the block and return the pointer to content.
 * 
 * To reduce memory fragmentation, the malloc request would return a 
 * space greater then 16 bytes. which means malloc(1) still
 * return a 16-byte space even you only use one byte.
 * 
 * Also, when split the memory blocks, the minimal block size would 
 * be 16 bytes.
 * 
 */
void * my_malloc(size_t size);

/*
 * Memory Free Procedure:
 * 
 * 1. Check the previous block and next block. If any is free,
 *    Delete it in Free List, combine them into one block, then
 *    re-insert into the Free List.
 * 2. Keep doing step 1 until no more block could be combined.
 * 
 */
int my_free(void * ptr);

/*
 * Just implementation of malloc(n_elements*element_size)
 * 
 */
void * my_calloc(size_t n_elements, size_t element_size);

/*
 * 1. Alloc a new block with new size
 * 2. Copy the content in old block into new block,
 *    abandon data which would cause overflow.
 * 3. Free old block.
 */
void * my_realloc(void * p, size_t size);

#endif