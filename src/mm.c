#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <errno.h>
#include <string.h>

#include "debug.h"
#include "mm.h"
#include "port.h"

/*
 * Memory Pool Map
 * 
 * -------------------------------------------------------------------- <- Heap start (aligned)
 * |                    padding (Length = 1 word)                     |
 * --------------------------------------------------------------------
 * |   Block size (highest bit - bit 3) | Allocate Flag (Must be 1)   |     Prologue Block Header
 * -------------------------------------------------------------------- <- aligned
 * |                          Unused Space                            |
 * --------------------------------------------------------------------
 * |                          Unused Space                            |
 * -------------------------------------------------------------------- <- aligned
 * |                  Value = Header XOR Magic Byte                   |     Prologue Block Footer
 * --------------------------------------------------------------------
 * |                                                                  |
 * |                        Memory Pool Blocks                        |
 * |                                                                  |
 * --------------------------------------------------------------------
 * |                               0x1                                |     Epilogue Block Footer
 * -------------------------------------------------------------------- <- Heap end (aligned)
 */

/*
 * Allocated Memory Map
 * 
 * --------------------------------------------------------------------
 * | Block size (highest bit - bit 3) | Allocate Flag (bit 2 - bit 0) |     Block Header
 * -------------------------------------------------------------------- <- aligned
 * |                                                                  |
 * |                            Contents                              |
 * |                                                                  |
 * -------------------------------------------------------------------- <- aligned
 * |                  Value = Header XOR Magic Byte                   |     Block Footer
 * --------------------------------------------------------------------
 * 
 */

/*
 * Unalloced Memory Map (actually should be freed memory map)
 * 
 * --------------------------------------------------------------------
 * | Block size (highest bit - bit 3) | Allocate Flag (bit 2 - bit 0) |     Block Header
 * -------------------------------------------------------------------- <- aligned
 * |  Previous Unalloced Block in FREE_LIST (Pointer to Block Header) |
 * --------------------------------------------------------------------
 * |    Next Unalloced Block in FREE_LIST (Pointer to Block Header)   |
 * -------------------------------------------------------------------- <- aligned
 * |                                                                  |
 * |                         Unused space                             |
 * |                                                                  |
 * -------------------------------------------------------------------- <- aligned
 * |                  Value = Header XOR Magic Byte                   |     Block Footer
 * --------------------------------------------------------------------
 * 
 */

#define WORD_SIZE           (sizeof(size_t))
#define SIZE_HorF           (sizeof(size_t))
#define alignMask           (WORD_SIZE-1)
#define actualBlkSize(size) (size + 2*SIZE_HorF)
#define alignedSize(size)   ((size & ((alignMask<<1) + 1))?((size & ~((alignMask<<1) + 1)) + 2*WORD_SIZE):(size))
#define nextBlock(ptr)      ((void *)*((size_t)(ptr+WORD_SIZE))
#define requiredPage(size)  ((size%PAGE_SIZE)?(size/PAGE_SIZE + 1):(size/PAGE_SIZE))
#define getBlkSize(ptr)     (ptr->header & ~alignMask)

#define findBlkInList(ptr) \
            while(ptr != NULL) { \
                if(ptr->header >= size) { \
                    info("%lx", ptr->header); \
                    if((ptr->header & alignMask) != 0) { \
                        error("Free List corrupted!"); \
                        return NULL; \
                    } \
                    return ptr; \
                } \
                else { \
                    ptr = ptr->next; \
                } \
            }

/*
 * It's really tricky to define the struct like that, the reason is that the struct stored
 * in Unalloced Memory Map (as shown), the unused space is not fixed, so unable to define
 * in struct.
 * 
 * Define the list struct like this is the only solution.
 */
typedef struct mem_list{
    size_t prev_footer;
    size_t header;
    struct mem_list * prev;
    struct mem_list * next;
}mem_list_t;

static uint8_t flag_inited = 0;
static mem_list_t * FREE_LIST[10] = {NULL};

size_t magic_byte(void) {
    return (size_t)0x1122334455667788;
}

static int determine_free_list_idx(size_t size) {
    if(size <= 512) {
        return 0;
    }
    else if(size <= 1*1024*1024) {
        return 1;
    }
    else if(size <= 2*1024*1024) {
        return 2;
    }
    else if(size <= 4*1024*1024) {
        return 3;
    }
    else if(size <= 8*1024*1024) {
        return 4;
    }
    else if(size <= 16*1024*1024) {
        return 5;
    }
    else if(size <= 32*1024*1024) {
        return 6;
    }
    else if(size <= 64*1024*1024) {
        return 7;
    }
    else if(size <= 128*1024*1024) {
        return 8;
    }
    else {
        return 9;
    }
}

static int determine_free_list(size_t size) {
    if(size <= 512 && (FREE_LIST[0] != NULL)) {
        return 0;
    }
    else if(size <= 1*1024*1024 && (FREE_LIST[1] != NULL)) {
        return 1;
    }
    else if(size <= 2*1024*1024 && (FREE_LIST[2] != NULL)) {
        return 2;
    }
    else if(size <= 4*1024*1024 && (FREE_LIST[3] != NULL)) {
        return 3;
    }
    else if(size <= 8*1024*1024 && (FREE_LIST[4] != NULL)) {
        return 4;
    }
    else if(size <= 16*1024*1024 && (FREE_LIST[5] != NULL)) {
        return 5;
    }
    else if(size <= 32*1024*1024 && (FREE_LIST[6] != NULL)) {
        return 6;
    }
    else if(size <= 64*1024*1024 && (FREE_LIST[7] != NULL)) {
        return 7;
    }
    else if(size <= 128*1024*1024 && (FREE_LIST[8] != NULL)) {
        return 8;
    }
    else if((FREE_LIST[9] != NULL)){
        return 9;
    }
    else {
        return 10;
    }
}

/*
 * 
 */
static void list_operation_insert_node(mem_list_t * list_node, mem_list_t * target) {
    if(list_node->prev == NULL) {
        // Insert to header
        target->next = list_node;
        target->prev = NULL;
        list_node->prev = target;
    }
    else {
        target->next = list_node;
        target->prev = list_node->prev;
        list_node->prev->next = target;
        list_node->prev = target;
    }
}

/*
 * 
 */
static void list_operation_delete_node(mem_list_t * list_node) {
    if(list_node->next == NULL) {
        // Delete Footer
        list_node->prev->next = NULL;
        list_node->prev = NULL;
        list_node->next = NULL;
    }
    else if(list_node->prev == NULL) {
        list_node->next->prev = NULL;
        list_node->prev = NULL;
        list_node->next = NULL;
    }
    else {
        list_node->prev->next = list_node->next;
        list_node->next->prev = list_node->prev;
        list_node->prev = NULL;
        list_node->next = NULL;
    }
}

/*
 * Check the head and footer
 */
static int check_blk(mem_list_t * blk) {
    size_t size = getBlkSize(blk);
    size_t header = blk->header;
    size_t footer = *(size_t *)((void *)blk + size + 2*SIZE_HorF);

    if(header == (footer ^ magic_byte())) {
        // Pass
        return 0;
    }
    else {
        // Error
        return -1;
    }
}

/*
 * Insert block into free list
 */
static int insert_blk(mem_list_t * blk) {
    size_t size = blk->header & ~alignMask;
    mem_list_t * list = FREE_LIST[determine_free_list_idx(size)];

    debug("Inserting blk_addr=%p, size=%lu", blk, size);

    if(check_blk(blk) != 0) {
        error("Block corrupted, header=%lx@%p, footer=header=%lx@%p", blk->header, &blk->header, *(size_t *)((void *)blk + getBlkSize(blk) + 2*SIZE_HorF), (void *)blk + getBlkSize(blk) + 2*SIZE_HorF);
        return -1;
    }

    if(determine_free_list_idx(size) == 0) {
        // FIFO policy
        FREE_LIST[0] = blk;
        blk->next = list;
        if(list)
            list->prev = blk;
    }
    else {
        // Best-Fit policy
        if(list == NULL) {
            FREE_LIST[determine_free_list_idx(size)] = blk;
            blk->prev = NULL;
            blk->next = NULL;

            return 0;
        }

        while(list->next != NULL) {
            if(getBlkSize(blk) >= getBlkSize(list)) {
                list_operation_insert_node(list->next, blk);
                return 0;
            }
            list = list->next;
        }

        if(getBlkSize(blk) >= getBlkSize(list)) {
            list->next = blk;
            blk->prev = list;
            blk->next = NULL;
        }
        else {
            list_operation_insert_node(list, blk);
        }
    }

    return 0;
}

/*
 * Delect block in free list
 */
static int delete_block(mem_list_t * blk) {
    size_t size = blk->header & ~alignMask;
    mem_list_t * list = FREE_LIST[determine_free_list_idx(size)];

    if(check_blk(blk) != 0) {
        error("List corrupted");
        return -1;
    }

    if(list == blk) {
        FREE_LIST[determine_free_list_idx(size)] = blk->next;
        return 0;
    }

    list_operation_delete_node(blk);

    return 0;
}

/*
 * Check if the remaining space greater than minimal block size
 * 
 * If so, split the block and insert the remaining block into free list.
 * 
 * If not, do nothing.
 */
static int split_blk_if_necessary(mem_list_t * blk, size_t requested_size) {
    size_t size = getBlkSize(blk);
    
    if(check_blk(blk) != 0) {
        error("Context corrupted");
        return -1;
    }

    size_t new_blk_size = size - requested_size;

    if(new_blk_size >= 16+2*SIZE_HorF) {
        new_blk_size = new_blk_size - 2*SIZE_HorF;
        debug("Spliting %ld into %ld and %ld", size+2*SIZE_HorF, requested_size+2*SIZE_HorF, new_blk_size+2*SIZE_HorF);
        if(new_blk_size%(2*WORD_SIZE) != 0) {
            error("**Unaligned, %ld mod %ld = %ld", new_blk_size, 2*WORD_SIZE, new_blk_size%(2*WORD_SIZE));
            return -1;
        }

        size_t align_flag = blk->header & alignMask;
        blk->header = align_flag | requested_size;
        mem_list_t * new_block = (void *)blk + requested_size + 2*SIZE_HorF;
        new_block->prev_footer = blk->header ^ magic_byte();
        new_block->header = new_blk_size;

        size_t * new_footer = (size_t *)((void *)new_block + new_blk_size + 2*SIZE_HorF);
        *new_footer = new_block->header ^ magic_byte();

        insert_blk(new_block);

        debug("Required block header=0x%lx@%p, footer=0x%lx@%p, size=%ld", blk->header, &blk->header, new_block->prev_footer, &new_block->prev_footer, requested_size);
        debug("New block header=0x%lx@%p, footer=0x%lx@%p, size=%ld", new_block->header, &new_block->header, *new_footer, new_footer, new_blk_size);
    }

    return 0;
}

/*
 * Check if the previous blocks and next blocks are free.
 * 
 * Coalesce them if possible.
 */
static mem_list_t * coalesce_blk_if_possible(mem_list_t * blk) {
    void * real_header = &(blk->header);
    void * real_footer = (blk->header & ~alignMask) + real_header + SIZE_HorF;

    if(check_blk(blk) != 0) {
        error("Context corrupted");
        return NULL;
    }

    // Checking previous block
    while((size_t)(real_header - SIZE_HorF) > (size_t)port_get_mem_pool_start()) {
        void * prev_footer = real_header - SIZE_HorF;
        if(((*(size_t *)prev_footer ^ magic_byte()) & alignMask) != 0) {
            // Block probably already aligned, or undefined
            break;
        }
        size_t prev_size = (*(size_t *)prev_footer ^ magic_byte()) & ~alignMask;

        void * prev_header = prev_footer - prev_size - SIZE_HorF;
        if((size_t)prev_header <= (size_t)port_get_mem_pool_start()) {
            // Probably undefined block hit
            break;
        }

        if(*(size_t *)prev_header != (magic_byte() ^ *(size_t *)prev_footer)) {
            // Check mismatch
            break;
        }

        // Update size info
        size_t * old_head = (size_t *)real_header;
        old_head = old_head;
        size_t old_size = *(size_t *)real_header & ~alignMask;
        size_t new_size = old_size + prev_size + 2*SIZE_HorF;
        if((new_size & alignMask) != 0 ) {
            // Block alignment broken
            break;
        }
        if(delete_block((mem_list_t *)(prev_header-SIZE_HorF)) != 0) {
            // Block delete error
            break;
        }
        real_header = prev_header;
        *(size_t *)real_header = new_size;
        *(size_t *)real_footer = *(size_t *)real_header ^ magic_byte();

        debug("Coalescing %ld@%p and %ld@%p into %ld@%p", prev_size, prev_header, old_size, old_head, new_size, real_header);
    }

    // Checking next block
    while((size_t)(real_footer + SIZE_HorF) < (size_t)port_get_mem_pool_end()) {
        void * next_header = real_footer + SIZE_HorF;
        if((*(size_t *)next_header & alignMask) != 0) {
            // Block probably already aligned, or undefined
            break;
        }
        size_t next_size = *(size_t *)next_header & ~alignMask;

        void * next_footer = next_header + next_size + SIZE_HorF;
        if((size_t)next_footer >= (size_t)port_get_mem_pool_end()) {
            // Probably undefined block hit
            break;
        }

        if(*(size_t *)next_header != (magic_byte() ^ *(size_t *)next_footer)) {
            // Check mismatch
            break;
        }

        // Update size info
        size_t * old_head = real_header;
        old_head = old_head;
        size_t old_size = *(size_t *)real_header & ~alignMask;
        size_t new_size = old_size + next_size + 2*SIZE_HorF;
        if(delete_block(next_header-SIZE_HorF) != 0) {
            // Block delete error
            break;
        }
        real_footer = next_footer;
        *(size_t *)real_header = new_size;
        *(size_t *)real_footer = *(size_t *)real_header ^ magic_byte();

        debug("Coalescing %ld@%p and %ld@%p into %ld@%p", old_size, old_head, next_size, next_header, new_size, real_header);
    }

    return (mem_list_t *)(real_header-SIZE_HorF);
}

/*
 * Find free block, extend page if necessary 
 *
 * FREE_LIST[0] is a FIFO list, which stores blocks smaller than 512 byte
 * 
 * But the other lists do not use FIFO policy, the block must be linked 
 * as sorted to find the best-fit block.
 * 
 * FREE_LIST[1] stores blocks up to 1 MB which would not fit in previous list
 * FREE_LIST[2] stores blocks up to 2 MB which would not fit in previous list
 * FREE_LIST[3] stores blocks up to 4 MB which would not fit in previous list
 * FREE_LIST[4] stores blocks up to 8 MB which would not fit in previous list
 * FREE_LIST[5] stores blocks up to 16 MB which would not fit in previous list
 * FREE_LIST[6] stores blocks up to 32 MB which would not fit in previous list
 * FREE_LIST[7] stores blocks up to 64 MB which would not fit in previous list
 * FREE_LIST[8] stores blocks up to 128 MB which would not fit in previous list
 * FREE_LIST[9] stores blocks which would not fit in previous list
 * 
 */
static mem_list_t * find_required_block(size_t size) {
    void * current_heap_end = port_get_mem_pool_end();
    void * assigned_block = NULL;
    mem_list_t * ptr_curr_list = NULL;

    switch(determine_free_list(size)) {
        case 0: // Find in block list[0]
            ptr_curr_list = FREE_LIST[0];
            // FIFO policy
            findBlkInList(ptr_curr_list);

        case 1: // Find in block list[1]
            ptr_curr_list = FREE_LIST[1];
            // Best-Fit policy (Because the list sorted the block size, it's the same operation)
            findBlkInList(ptr_curr_list);

        case 2: // Find in block list[2]
            ptr_curr_list = FREE_LIST[2];
            // Best-Fit policy (Because the list sorted the block size, it's the same operation)
            findBlkInList(ptr_curr_list);

        case 3: // Find in block list[3]
            ptr_curr_list = FREE_LIST[3];
            // Best-Fit policy (Because the list sorted the block size, it's the same operation)
            findBlkInList(ptr_curr_list);

        case 4: // Find in block list[4]
            ptr_curr_list = FREE_LIST[4];
            // Best-Fit policy (Because the list sorted the block size, it's the same operation)
            findBlkInList(ptr_curr_list);

        case 5: // Find in block list[5]
            ptr_curr_list = FREE_LIST[5];
            // Best-Fit policy (Because the list sorted the block size, it's the same operation)
            findBlkInList(ptr_curr_list);

        case 6: // Find in block list[6]
            ptr_curr_list = FREE_LIST[6];
            // Best-Fit policy (Because the list sorted the block size, it's the same operation)
            findBlkInList(ptr_curr_list);

        case 7: // Find in block list[7]
            ptr_curr_list = FREE_LIST[7];
            // Best-Fit policy (Because the list sorted the block size, it's the same operation)
            findBlkInList(ptr_curr_list);

        case 8: // Find in block list[8]
            ptr_curr_list = FREE_LIST[8];
            // Best-Fit policy (Because the list sorted the block size, it's the same operation)
            findBlkInList(ptr_curr_list);

        case 9: // Find in block list[9]
            ptr_curr_list = FREE_LIST[9];
            // Best-Fit policy (Because the list sorted the block size, it's the same operation)
            findBlkInList(ptr_curr_list);

        case 10: // None block satisfy the condition
            // Extend heap
            if(port_extend_page(requiredPage(size)) != 0) {
                return NULL;
            }
            debug("Successfully extended %ld page(s)", requiredPage(size));

            assigned_block = current_heap_end - 2*SIZE_HorF;

            // Init Heap Header & Epilogue Block Footer
            void * ptr_heap_header = port_get_mem_pool_start();
            current_heap_end = port_get_mem_pool_end();
            debug("Heap: start=%p, end=%p", ptr_heap_header, current_heap_end);
            *(size_t *)(ptr_heap_header + WORD_SIZE) = (current_heap_end-ptr_heap_header) | 0x1;
            debug("Writing to %p", (current_heap_end - SIZE_HorF));
            *(size_t *)(current_heap_end - SIZE_HorF) = 0x1;

            // Init new block
            ((mem_list_t *)assigned_block)->header = requiredPage(size)*PAGE_SIZE - 2*SIZE_HorF;
            *(size_t *)(current_heap_end - 2*SIZE_HorF) = ((mem_list_t *)assigned_block)->header ^ magic_byte();

            insert_blk((mem_list_t *)(assigned_block));

            return (mem_list_t *)(assigned_block);

    }
    // Just omit compiler warning, would never run to here
    return NULL;
}

/*
 * 
 */
static int mm_initialize(void) {    
    // If already inited, exit
    if(flag_inited)
        return 0;

    // Get the first page
    if(port_extend_page(1) != 0) {
        error("Page extend failed!");
        return -1;
    }

    if((PAGE_SIZE & alignMask) != 0) {
        error("Page size incompatible");
        return -1;
    }

    for(int i = 0; i < 10; i++) {
        FREE_LIST[i] = NULL;
    }

    void * heap_start = port_get_mem_pool_start();
    void * heap_end = port_get_mem_pool_end();
    
    // Init Heap header
    *(size_t *)(heap_start + WORD_SIZE) = (heap_end-heap_start) | 0x1;

    // Init epilogue footer
    void * epilogue_footer = heap_end - SIZE_HorF;
    *(size_t *)epilogue_footer = 0x1;

    // Init prologue block
    void * prologue_header = heap_start + WORD_SIZE;
    void * prologue_footer = prologue_header + 3*SIZE_HorF;
    *(size_t *)prologue_header = 2*WORD_SIZE | 0x1;
    *(size_t *)prologue_footer = *(size_t *)prologue_header ^ magic_byte();

    // Init new block
    mem_list_t * new_block = (mem_list_t *)prologue_footer;
    new_block->header = PAGE_SIZE - WORD_SIZE * 8;
    *(size_t *)(epilogue_footer - SIZE_HorF) = new_block->header ^ magic_byte();

    debug("Init: prologue_header=%lx, prologue_footer=%lx, epilogue_footer=%lx, initial_block_header=%lx, initial_block_footer=%lx", *(size_t *)(heap_start+WORD_SIZE), *(size_t *)(heap_start+4*WORD_SIZE), *(size_t *)(heap_end-WORD_SIZE), *(size_t *)(heap_start+5*WORD_SIZE), *(size_t *)(heap_end-2*WORD_SIZE));

    insert_blk(new_block);

    flag_inited = 1;

    
    return 0;
}

/* ==================================================================================
 * |                   Functions below are public interfaces                        |
 * ==================================================================================
 */

/*
 * Memory Alloc Policy:
 * 
 * 1. Find best-fit blocks in free list (FIFO for block less than 512 byte),
 *    goto 3 if found.
 * 2. Add extra pages to memory pool as required.
 * 3. If the reminder greater then the Minimal Block Size, Split 
 *    the block and put the reminder into proper Free List.
 * 4. Initialize the block and return the pointer to content.
 * 
 * To ensure blocks can be inserted into list (reserved space for list node), 
 * also for fragmentation consideration, the minimal block size was set to 
 * 16 bytes. which means malloc(1) would return a block contain space of 16
 * bytes even if it's a kind of waste.
 * 
 * Also, when spliting the memory blocks, the minimal block size would 
 * be 16 bytes.
 * 
 */
void * my_malloc(size_t size) {
    if(mm_initialize() != 0) {
        error("Unable to initialize");
    }
    
    // Get the actual size which fit the alignment requirement
    debug("Request %ld, assign %ld", size, alignedSize(size));
    size = alignedSize(size);

    // Find block
    mem_list_t * assigned_block = find_required_block(size);

    if(assigned_block == NULL) {
        error("No Enough Mem!");
        return NULL;
    }

    // Check block
    if(check_blk(assigned_block) != 0) {
        error("Block corrupted");
        return NULL;
    }

    // Fetch from free list
    delete_block(assigned_block);

    // Set assign bit
    assigned_block->header = assigned_block->header | 0x1;
    size_t * footer = (void *)assigned_block + getBlkSize(assigned_block) + 2*SIZE_HorF;
    *footer = assigned_block->header ^ magic_byte();

    split_blk_if_necessary(assigned_block, size);

    // For security reasons, initialize the content to 0
    memset((void *)assigned_block + 2*SIZE_HorF, 0, size);

    return (void *)assigned_block + 2*SIZE_HorF;
}

/*
 * Memory Free Procedure:
 * 
 * 1. Check the previous block and next block. If any is free,
 *    Delete it in Free List, combine them into one block, then
 *    re-insert into the Free List.
 * 2. Keep doing step 1 until no more block could be combined.
 * 
 */
int my_free(void * ptr) {
    mem_list_t * blk = ptr - 2*SIZE_HorF;

    debug("Freeing %p blk: header=%lx@%p, footer=%lx@%p", blk, blk->header, &blk->header, *(size_t *)((void *)blk+getBlkSize(blk)+2*SIZE_HorF), (void *)blk+getBlkSize(blk)+2*SIZE_HorF);

    if((void *)blk > port_get_mem_pool_end() || (void *)blk < port_get_mem_pool_start()) {
        error("Invalid address!");
        return -1;
    }

    if((blk->header & alignMask) == 0) {
        error("Double free!");
        return -1;
    }

    if(check_blk(blk) != 0) {
        error("Block corrupted!");
        return -1;
    }

    // Clear assign bit
    blk->header = blk->header & ~alignMask;
    size_t * footer = (size_t *)((void *)blk + getBlkSize(blk) + 2*SIZE_HorF);
    *footer = blk->header ^ magic_byte();

    blk = coalesce_blk_if_possible(blk);
    insert_blk(blk);

    return 0;
}

/*
 * Just implementation of malloc(n_elements*element_size)
 * 
 */
void * my_calloc(size_t n_elements, size_t element_size) {
    if(element_size <= (SIZE_MAX/n_elements))
        return my_malloc(n_elements*element_size);
    else // Overflow
        return NULL;   
}

/*
 * 1. Alloc a new block with new size
 * 2. Copy the content in old block into new block,
 *    abandon data which would cause overflow.
 * 3. Free old block.
 */
void * my_realloc(void * p, size_t size) {
    mem_list_t * blk = p - 2*SIZE_HorF;
    size_t old_size = getBlkSize(blk);
    
    void * new_space = my_malloc(size);
    memcpy(new_space, p, (old_size < size)?(size):(old_size));

    if(my_free(p)) {
        // Error occurred
        my_free(new_space);
        return NULL;
    }
    
    return new_space;
}