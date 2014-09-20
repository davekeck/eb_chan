#pragma once
#include "eb_chan.h"
#include "eb_atomic.h"
#include <assert.h>
#include <stdlib.h>
#include <pthread.h>
#include <stdio.h>
#include <unistd.h>
#include <Block.h>
#include <strings.h>
#include <mach/mach.h>
#include <mach/message.h>
#include <mach/kern_return.h>
#include <mach/task_info.h>

typedef void (^VoidBlock)();
void spawnThread(VoidBlock b);
#define go(a) spawnThread(^{ a ;});