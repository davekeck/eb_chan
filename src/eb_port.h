#pragma once
#include <stddef.h>
#include <stdbool.h>
#include "eb_nsec.h"

typedef struct eb_port *eb_port;

eb_port eb_port_create();
eb_port eb_port_retain(eb_port p);
void eb_port_release(eb_port p);

void eb_port_signal(eb_port p);
bool eb_port_wait(eb_port p, eb_nsec timeout);
