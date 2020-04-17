/*
 * This file defines the finction port to provide portability
 */

#ifndef _PORT_H_
#define _PORT_H_

#include <unistd.h>

#define PAGE_SIZE (sysconf(_SC_PAGE_SIZE)) // 4K page size in Linux x64

/*
 * Return the start of heap (accessable from the return address)
 * 
 */
void * port_get_mem_pool_start(void);

/*
 * Return the end of heap (not accessable from the return address)
 * 
 */
void * port_get_mem_pool_end(void);

/*
 * Extend heap n pages from footer
 */
int port_extend_page(int count);

#endif