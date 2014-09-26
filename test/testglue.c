#include "testglue.h"

static void *runBlock(VoidBlock b) {
    b();
    Block_release(b);
    return NULL;
}

void spawnThread(VoidBlock b) {
    b = (VoidBlock)Block_copy((void *)b);
    pthread_t t;
    assert(!pthread_create(&t, NULL, (void*)runBlock, b));
    assert(!pthread_detach(t));
}
