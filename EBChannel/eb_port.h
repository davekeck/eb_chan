#include <stddef.h>
#include <stdbool.h>

typedef struct eb_port *eb_port_t;

eb_port_t eb_port_create();
eb_port_t eb_port_retain(eb_port_t p);
void eb_port_release(eb_port_t p);

void eb_port_signal(eb_port_t p);
void eb_port_wait(eb_port_t p);
