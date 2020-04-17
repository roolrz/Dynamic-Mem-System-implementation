#include "port.h"
#include "debug.h"

/*
 * To simplify the implementation, brk() is perfered rather than
 * mmap(), for mmap would not guarantee providing a contiguous page
 * address space, which means that you must store the data cross
 * different memory address region, page assign infomation must be 
 * recorded to make sure there's no unlawful memory access. 
 */

static int init_status = 0;
static void * current_heap_start = NULL;
static void * current_heap_end = NULL;

/*
 * Return the start of heap (accessable from the return address)
 * 
 */
void * port_get_mem_pool_start(void) {
    return current_heap_start;
}

/*
 * Return the end of heap (not accessable from the return address)
 * 
 */
void * port_get_mem_pool_end(void) {
    return current_heap_end;
}

/*
 * Extend heap n pages (mmap version, but not finished due to address contiguous problem)
 */
/*
int port_extend_page(int count) {
    if(init_status == 0) {
        current_heap_start = mmap(NULL, count*PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if(MAP_FAILED == current_heap_start) {
            error("mmap failed!");
            return -1;
        }
        current_heap_end = current_heap_start + count*PAGE_SIZE;
        debug("Heap: start=%p, end=%p", current_heap_start, current_heap_end);
        init_status = 1;
        return 0;
    }

    debug("requesting %d pages from %p", count, current_heap_end);

    void * addr = mmap(current_heap_end, count*PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if(MAP_FAILED == addr) {
        error("mmap failed!");
        return -1;
    }
    
    current_heap_end += count*PAGE_SIZE;

    if(addr != current_heap_end-count*PAGE_SIZE) {
        error("mmap returned %p, but expect %p", addr, current_heap_end);
        return -1;
    }

    debug("Heap: start=%p, end=%p", current_heap_start, current_heap_end);
    return 0;
}
*/

/*
 * Extend heap n pages from current_heap_end (brk version)
 */
int port_extend_page(int count) {
    if(init_status == 0) {
        current_heap_start = sbrk(count*PAGE_SIZE);
        current_heap_end = current_heap_start + count*PAGE_SIZE;
        debug("Heap: start=%p, end=%p", current_heap_start, current_heap_end);
        init_status = 1;
        return 0;
    }

    debug("requesting %d pages from %p", count, current_heap_end);

    current_heap_end += count*PAGE_SIZE;

    if(0 != brk(current_heap_end)) {
        error("sbrk failed!");
        return -1;
    }
    
    debug("Heap: start=%p, end=%p", current_heap_start, current_heap_end);
    return 0;
}