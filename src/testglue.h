#pragma once
#include "eb_chan.h"
#include "eb_atomic.h"
#include <assert.h>
#include <stdlib.h>
#include <pthread.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <strings.h>
#include <Block.h>

#if __MACH__
    #define DARWIN 1
    #include <mach/mach.h>
    #include <mach/message.h>
    #include <mach/kern_return.h>
    #include <mach/task_info.h>
#elif __linux__
    #define LINUX 1
#endif

typedef void (^VoidBlock)();
void spawnThread(VoidBlock b);
#define go(a) spawnThread(^{ a ;});
