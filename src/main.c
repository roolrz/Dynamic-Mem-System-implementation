/*
 * Simple Dynamic Memory System Implementation
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "debug.h"
#include "mm.h"

int main(int argc, char * argv[]) {
    // Claim space
    char * something = my_malloc(500);

    // Doing something
    strcpy(something, "Hello!");
    printf("%s\n", something);

    // Free space
    my_free(something);

    void * test_addr[1000];

    for(int i = 0; i < 1000; i++) {
        test_addr[i] = my_malloc(i*1000);
        if(test_addr[i] == NULL)
            return EXIT_FAILURE;
        sprintf(test_addr[i], "%d", i);
        printf("%s\n", (char *)test_addr[i]);
        my_free(test_addr[i]);
    }

    for(int i = 0; i < 1000; i++) {
        
    }

    return EXIT_SUCCESS;
}