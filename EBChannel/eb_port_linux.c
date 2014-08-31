#if __linux__

#include "eb_port.h"
#include <stdlib.h>
#include <strings.h>
#include <assert.h>
#include <semaphore.h>
#include "eb_assert.h"
#include "eb_atomic.h"

struct eb_port {
    eb_atomic_int retain_count;
    sem_t sem;
    bool sem_valid;
};

static void eb_port_free(eb_port p) {
    /* Intentionally allowing p==NULL so that this function can be called from eb_port_create() */
    if (!p) {
        return;
    }
    
    if (p->sem_valid) {
        int r = sem_destroy(&p->sem);
            eb_assert_or_recover(!r, eb_no_op);
        
        p->sem_valid = false;
    }
    
    free(p);
    p = NULL;
}

eb_port eb_port_create() {
    eb_port p = malloc(sizeof(*p));
        eb_assert_or_recover(p, goto failed);
    bzero(p, sizeof(*p));
    
    int r = sem_init(&p->sem, 0, 0);
        eb_assert_or_recover(!r,  goto failed);
    
    p->sem_valid = true;
    p->retain_count = 1;
    return p;
    
    failed: {
        eb_port_free(p);
        return NULL;
    }
}

eb_port eb_port_retain(eb_port p) {
    assert(p);
    eb_atomic_int_add(&p->retain_count, 1);
    return p;
}

void eb_port_release(eb_port p) {
    assert(p);
    if (eb_atomic_int_add(&p->retain_count, -1) == 1) {
        eb_port_free(p);
    }
}

void eb_port_signal(eb_port p) {
    int r = sem_post(&p->sem);
        eb_assert_or_recover(!r, eb_no_op);
}

bool eb_port_wait(eb_port p, eb_timeout timeout) {
    // TODO make timeout work
    return !sem_timedwait(&p->sem, NULL);
}

#endif
