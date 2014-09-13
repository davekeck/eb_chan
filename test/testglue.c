#include "testglue.h"

static void *runBlock(VoidBlock b) {
    b();
    return NULL;
}

void go(VoidBlock b) {
    b = (VoidBlock)Block_copy((void *)b);
    pthread_t t;
    pthread_create(&t, NULL, (void*)runBlock, b);
}