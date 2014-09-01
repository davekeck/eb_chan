#include <stddef.h>
#include <stdbool.h>
#include "eb_time.h"

typedef struct eb_sem *eb_sem;

eb_sem eb_sem_create();
eb_sem eb_sem_retain(eb_sem p);
void eb_sem_release(eb_sem p);

void eb_sem_signal(eb_sem p);
bool eb_sem_wait(eb_sem p, eb_nsecs timeout);
