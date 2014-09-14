//#include "testglue.h"
//
//void func(eb_chan a, eb_chan b) {
//    eb_chan_op send = eb_chan_send_op(a, "xxx");
//    eb_chan_op recv = eb_chan_recv_op(b);
//    for (;;) {
//        eb_chan_op *r = eb_chan_do(eb_nsec_forever, &send, &recv);
//        if (r == &send) {
//            printf("send\n");
//        } else if (r == &recv) {
//            printf("recv\n");
//        } else {
//            abort();
//        }
//    }
//}
//
//int main() {
//    eb_chan a = eb_chan_create(0);
//    eb_chan b = eb_chan_create(0);
//    
//    go(^{ func(a,b); });
//    go(^{ func(b,a); });
//    for (;;) {
//        sleep(-1);
//    }
//}
















//#include "testglue.h"
//
//int randx = 0;
//
//int nrand(int n) {
//	randx += 10007;
//	if (randx >= 1000000) {
//		randx -= 1000000;
//	}
//	return randx % n;
//}
//
//typedef struct {
//	eb_chan sc, rc; // send and recv chan
//	int sv, rv; // send and recv seq
//} Chan;
//
//int nproc = 0;
//eb_spinlock nprocLock = EB_SPINLOCK_INIT;
//int cval = 0;
//int end = 10000;
//int totr = 0;
//int tots = 0;
//eb_spinlock totLock = EB_SPINLOCK_INIT;
//Chan *nc = NULL;
//
//void init() {
//    nc = malloc(sizeof(*nc));
//    memset(nc, 0, sizeof(*nc));
//}
//
//int changeNproc(int adjust) {
//    eb_spinlock_lock(&nprocLock);
//	nproc += adjust;
//	int ret = nproc;
//    eb_spinlock_unlock(&nprocLock);
//	return ret;
//}
//
//Chan **mkchan(int c, int n) {
//	Chan **ca = malloc(n*sizeof(*ca));
//    memset(ca, 0, n*sizeof(*ca));
//    
//	for (size_t i = 0; i < n; i++) {
//		cval = cval + 100;
//		Chan *ch = malloc(sizeof(*ch));
//		ch->sc = eb_chan_create(c);
//		ch->rc = ch->sc;
//		ch->sv = cval;
//		ch->rv = cval;
//		ca[i] = ch;
//	}
//	return ca;
//}
//
//int expect(int v, int v0) {
//	if (v == v0) {
//		if (v%100 == 75) {
//			return end;
//		}
//		return v + 1;
//	}
//	printf("got %d expected %d\n",  v, v0+1);
//    abort();
//}
//
//bool sendOnChan(Chan *c) {
//	//	print("send ", c.sv, "\n");
//    eb_spinlock_lock(&totLock);
//	tots++;
//    eb_spinlock_unlock(&totLock);
//	c->sv = expect(c->sv, c->sv);
//	if (c->sv == end) {
//		c->sc = NULL;
//		return true;
//	}
//	return false;
//}
//
//void mySend(Chan *c) {
//	for (;;) {
//		for (int r = nrand(10); r >= 0; r--) {
//			usleep(10);
//		}
//        eb_chan_send(c->sc, (const void *)((intptr_t)c->sv));
//		if (sendOnChan(c)) {
//			break;
//		}
//	}
//	changeNproc(-1);
//}
//
//bool recvOnChan(Chan *c, int v)  {
//	//	print("recv ", v, "\n");
//    eb_spinlock_lock(&totLock);
//	totr++;
//    eb_spinlock_unlock(&totLock);
//	c->rv = expect(c->rv, v);
//	if (c->rv == end) {
//		c->rc = NULL;
//		return true;
//	}
//	return false;
//}
//
//void myRecv(Chan *c) {
//	int v = 0;
//
//	for (;;) {
//		for (int r = nrand(10); r >= 0; r--) {
//			usleep(10);
//		}
//        
//        const void *val;
//        eb_chan_recv(c->rc, &val);
//        v = (int)(intptr_t)val;
//        
//		if (recvOnChan(c, v)) {
//			break;
//		}
//	}
//	changeNproc(-1);
//}
//
//void sel(Chan *r0, Chan *r1, Chan *r2, Chan *r3, Chan *s0, Chan *s1, Chan *s2, Chan *s3) {
//	int v = 0;
//
//	int a = 0; // local chans running
//
//	if (r0->rc != NULL) {
//		a++;
//	}
//	if (r1->rc != NULL) {
//		a++;
//	}
//	if (r2->rc != NULL) {
//		a++;
//	}
//	if (r3->rc != NULL) {
//		a++;
//	}
//	if (s0->sc != NULL) {
//		a++;
//	}
//	if (s1->sc != NULL) {
//		a++;
//	}
//	if (s2->sc != NULL) {
//		a++;
//	}
//	if (s3->sc != NULL) {
//		a++;
//	}
//
//	for (;;) {
//		for (int r = nrand(5); r >= 0; r--) {
//			usleep(10);
//		}
//        
//        eb_chan_op ro0 = eb_chan_recv_op(r0->rc);
//        eb_chan_op ro1 = eb_chan_recv_op(r1->rc);
//        eb_chan_op ro2 = eb_chan_recv_op(r2->rc);
//        eb_chan_op ro3 = eb_chan_recv_op(r3->rc);
//        
//        eb_chan_op so0 = eb_chan_send_op(s0->sc, (void*)(intptr_t)s0->sv);
//        eb_chan_op so1 = eb_chan_send_op(s1->sc, (void*)(intptr_t)s1->sv);
//        eb_chan_op so2 = eb_chan_send_op(s2->sc, (void*)(intptr_t)s2->sv);
//        eb_chan_op so3 = eb_chan_send_op(s3->sc, (void*)(intptr_t)s3->sv);
//        
//        eb_chan_op *r = eb_chan_do(eb_nsec_forever, &ro0, &ro1, &ro2, &ro3, &so0, &so1, &so2, &so3);
//        
//        if (r == &ro0) {
//            v = (int)(intptr_t)r->val;
//			if (recvOnChan(r0, v)) {
//				a--;
//			}
//        }
//        else if (r == &ro1) {
//            v = (int)(intptr_t)r->val;
//			if (recvOnChan(r1, v)) {
//				a--;
//			}
//        }
//        else if (r == &ro2) {
//            v = (int)(intptr_t)r->val;
//			if (recvOnChan(r2, v)) {
//				a--;
//			}
//        }
//        else if (r == &ro3) {
//            v = (int)(intptr_t)r->val;
//			if (recvOnChan(r3, v)) {
//				a--;
//			}
//        }
//        else if (r == &so0) {
//			if (sendOnChan(s0)) {
//				a--;
//			}
//        }
//        else if (r == &so1) {
//			if (sendOnChan(s1)) {
//				a--;
//			}
//        }
//        else if (r == &so2) {
//			if (sendOnChan(s2)) {
//				a--;
//			}
//        }
//        else if (r == &so3) {
//			if (sendOnChan(s3)) {
//				a--;
//			}
//        }
//        
//		if (a == 0) {
//			break;
//		}
//	}
//	changeNproc(-1);
//}
//
//// direct send to direct recv
//void test1(Chan *c) {
//	changeNproc(2);
//    
//    go(^{ mySend(c); });
//    go(^{ myRecv(c); });
//}
//
//// direct send to select recv
//void test2(int c) {
//	Chan **ca = mkchan(c, 4);
//
//	changeNproc(4);
//    
//    go(^{ mySend(ca[0]); });
//    go(^{ mySend(ca[1]); });
//    go(^{ mySend(ca[2]); });
//    go(^{ mySend(ca[3]); });
//
//	changeNproc(1);
//    go(^{ sel(ca[0], ca[1], ca[2], ca[3], nc, nc, nc, nc); });
//}
//
//// select send to direct recv
//void test3(int c) {
//	Chan **ca = mkchan(c, 4);
//
//	changeNproc(4);
//    go(^{ myRecv(ca[0]); });
//    go(^{ myRecv(ca[1]); });
//    go(^{ myRecv(ca[2]); });
//    go(^{ myRecv(ca[3]); });
//    
//	changeNproc(1);
//    
//    go(^{ sel(nc, nc, nc, nc, ca[0], ca[1], ca[2], ca[3]); });
//}
//
//// select send to select recv
//void test4(int c) {
//	Chan **ca = mkchan(c, 4);
//
//	changeNproc(2);
//	go(^{ sel(nc, nc, nc, nc, ca[0], ca[1], ca[2], ca[3]); });
//	go(^{ sel(ca[0], ca[1], ca[2], ca[3], nc, nc, nc, nc); });
//}
//
//void test5(int c) {
//	Chan **ca = mkchan(c, 8);
//
//	changeNproc(2);
//	go(^{ sel(ca[4], ca[5], ca[6], ca[7], ca[0], ca[1], ca[2], ca[3]); });
//	go(^{ sel(ca[0], ca[1], ca[2], ca[3], ca[4], ca[5], ca[6], ca[7]); });
//}
//
//void test6(int c) {
//	Chan **ca = mkchan(c, 12);
//
//	changeNproc(4);
//	go(^{ mySend(ca[4]); });
//	go(^{ mySend(ca[5]); });
//	go(^{ mySend(ca[6]); });
//	go(^{ mySend(ca[7]); });
//
//	changeNproc(4);
//	go(^{ myRecv(ca[8]); });
//	go(^{ myRecv(ca[9]); });
//	go(^{ myRecv(ca[10]); });
//	go(^{ myRecv(ca[11]); });
//
//	changeNproc(2);
//	go(^{ sel(ca[4], ca[5], ca[6], ca[7], ca[0], ca[1], ca[2], ca[3]); });
//	go(^{ sel(ca[0], ca[1], ca[2], ca[3], ca[8], ca[9], ca[10], ca[11]); });
//}
//
//// wait for outstanding tests to finish
//void myWait() {
//	usleep(10);
//	while (changeNproc(0) != 0) {
//		usleep(1000);
//	}
//}
//
//// run all tests with specified buffer size
//void tests(int c) {
//	Chan **ca = mkchan(c, 4);
//	test1(ca[0]);
//	test1(ca[1]);
//	test1(ca[2]);
//	test1(ca[3]);
//	myWait();
//
//	test2(c);
//	myWait();
//
//	test3(c);
//	myWait();
//
//	test4(c);
//	myWait();
//    
//	test5(c);
//	myWait();
//
//	test6(c);
//	myWait();
//}
//
//// run all test with 4 buffser sizes
//int main() {
//    init();
//    
//	tests(0);
//	tests(1);
//	tests(10);
//	tests(100);
//
//	int t = 4 * // buffer sizes
//		(4*4 + // tests 1,2,3,4 channels
//			8 + // test 5 channels
//			12) * // test 6 channels
//		76; // sends/recvs on a channel
//
//	if (tots != t || totr != t) {
//		printf("tots=%d" " totr=%d" " sb=%d" "\n", tots, totr, t);
//		exit(1);
//	}
//	exit(0);
//    return 0;
//}













#include <pthread.h>
#include <math.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "eb_chan.h"

#define NTRIALS 1000000

eb_chan gChan = NULL;

void *threadDoSend(void *a)
{
    for (size_t i = 0; i < NTRIALS; i++) {
        eb_chan_send(gChan, "halla");
    }
    return NULL;
}

void *threadTryRecv(void *a)
{
    size_t count = 0;
    eb_nsec startTime = eb_time_now();
    for (;;) {
        if (eb_chan_try_recv(gChan, NULL, NULL)) {
            count++;
            if (count == NTRIALS) {
                break;
            }
        }
    }
    
    printf("elapsed: %f (%ju iterations)\n", ((double)(eb_time_now() - startTime) / eb_nsec_per_sec), (uintmax_t)NTRIALS);
    exit(0);
    return NULL;
}

void *threadDoRecv(void *a)
{
    for (size_t i = 0; i < NTRIALS; i++) {
        assert(eb_chan_recv(gChan, NULL));
    }
    return NULL;
}

void *threadTrySend(void *a)
{
    size_t count = 0;
    eb_nsec startTime = eb_time_now();
    for (;;) {
        if (eb_chan_try_send(gChan, "hello")) {
            count++;
            if (count == NTRIALS) {
                break;
            }
        }
    }
    printf("elapsed: %f (%ju iterations)\n", ((double)(eb_time_now() - startTime) / eb_nsec_per_sec), (uintmax_t)NTRIALS);
    exit(0);
    return NULL;
}

void *threadSend(void *a)
{
    for (size_t i = 0; i < NTRIALS; i++) {
        eb_chan_send(gChan, "hallo");
    }
    return NULL;
    
}

void *threadRecv(void *a)
{
//    assert(eb_chan_recv(gChan, NULL, eb_nsec_forever));
//    eb_nsec startTime = eb_time_now();
//    for (size_t i = 1; i < NTRIALS; i++) {
//        assert(eb_chan_recv(gChan, NULL, eb_nsec_forever));
//    }
//    printf("elapsed: %f (%ju iterations)", ((double)(eb_time_now() - startTime) / eb_nsec_per_sec), (uintmax_t)NTRIALS);
//    exit(0);
//    return NULL;
    
    eb_chan_recv(gChan, NULL);
    eb_nsec startTime = eb_time_now();
    for (size_t i = 1; i < NTRIALS; i++) {
        eb_chan_recv(gChan, NULL);
    }
    
    printf("elapsed: %f (%ju iterations)\n", ((double)(eb_time_now() - startTime) / eb_nsec_per_sec), (uintmax_t)NTRIALS);
//    exit(0);//
    return NULL;
}

void *thread(void *a)
{
    eb_chan_op send = eb_chan_send_op(gChan, "hallo");
    eb_chan_op recv = eb_chan_recv_op(gChan);
    assert(eb_chan_do(eb_nsec_forever, &send, &recv));
    
    eb_nsec startTime = eb_time_now();
    for (size_t i = 1; i < NTRIALS; i++) {
        assert(eb_chan_do(eb_nsec_forever, &send, &recv));
    }
    
    printf("elapsed: %f (%ju iterations)\n", ((double)(eb_time_now() - startTime) / eb_nsec_per_sec), (uintmax_t)NTRIALS);
    exit(0);
    return NULL;
}

void *timeoutTest(void *a)
{
    eb_chan_op recv = eb_chan_recv_op(gChan);
    eb_nsec startTime = eb_time_now();
    eb_chan_do(2.5 * eb_nsec_per_sec, &recv);
    printf("elapsed: %f (%ju iterations)\n", ((double)(eb_time_now() - startTime) / eb_nsec_per_sec), (uintmax_t)NTRIALS);
    
    exit(0);
    return NULL;
}

int main(int argc, const char * argv[])
{
    gChan = eb_chan_create(0);
    
    pthread_t thread1, thread2;
    
//    pthread_create(&thread1, NULL, threadDoSend, NULL);
//    pthread_create(&thread2, NULL, threadTryRecv, NULL);
    
//    pthread_create(&thread2, NULL, threadTrySend, NULL);
//    pthread_create(&thread1, NULL, threadDoRecv, NULL);
    
    pthread_create(&thread1, NULL, threadSend, NULL);
//    pthread_create(&thread1, NULL, threadSend, NULL);
//    pthread_create(&thread1, NULL, threadSend, NULL);
//    
//    pthread_create(&thread2, NULL, threadRecv, NULL);
//    pthread_create(&thread2, NULL, threadRecv, NULL);
    pthread_create(&thread2, NULL, threadRecv, NULL);
    
//    pthread_create(&thread1, NULL, thread, NULL);
//    pthread_create(&thread2, NULL, thread, NULL);
//    pthread_create(&thread2, NULL, thread, NULL);
//    pthread_create(&thread2, NULL, thread, NULL);
//    pthread_create(&thread2, NULL, thread, NULL);
//    pthread_create(&thread2, NULL, thread, NULL);
    
//    timeoutTest(NULL);
    
    for (;;) {
        sleep(-1);
        printf("SLEEPING\n");
    }
    
    return 0;
}
