// DONE

// Test concurrency primitives: power series.

// Power series package
// A power series is a channel, along which flow rational
// coefficients.  A denominator of zero signifies the end.
// Original code in Newsqueak by Doug McIlroy.
// See Squinting at Power Series by Doug McIlroy,
//   http://www.cs.bell-labs.com/who/rsc/thread/squint.pdf

#include "testglue.h"

typedef struct  {
	int64_t num; // numerator
    int64_t den; // denominator
} rat;

rat *rat2heap(rat x) {
    rat *r = malloc(sizeof(*r));
    *r = x;
    return r;
}

rat heap2rat(rat *x) {
    rat r = *x;
    // not freeing here because sometimes we send global variables
//    free(x);
    return r;
}

void pr(rat u) {
	if (u.den==1) {
		printf("%jd", (intmax_t)u.num);
	} else {
		printf("%jd/%jd", (intmax_t)u.num, (intmax_t)u.den);
	}
	printf(" ");
}

bool eq(rat u, rat c) {
	return (u.num == c.num && u.den == c.den);
}

typedef struct {
    eb_chan req;
    eb_chan dat;
    int nam;
} dch;

typedef dch *dch2[2];

const char *chnames = NULL;
int chnameserial;
int seqno;

dch *mkdch() {
	int c = chnameserial % strlen(chnames);
	chnameserial++;
	dch *d = malloc(sizeof(*d));
	d->req = eb_chan_create(0);
	d->dat = eb_chan_create(0);
	d->nam = c;
	return d;
}

dch2 *mkdch2() {
	dch2 *d2 = malloc(sizeof(*d2));
	(*d2)[0] = mkdch();
	(*d2)[1] = mkdch();
	return d2;
}

// split reads a single demand channel and replicates its
// output onto two, which may be read at different rates.
// A process is created at first demand for a rat and dies
// after the rat has been sent to both outputs.

// When multiple generations of split exist, the newest
// will service requests on one channel, which is
// always renamed to be out[0]; the oldest will service
// requests on the other channel, out[1].  All generations but the
// newest hold queued data that has already been sent to
// out[0].  When data has finally been sent to out[1],
// a signal on the release-wait channel tells the next newer
// generation to begin servicing out[1].

void dosplit(dch *in, dch2 *out, eb_chan wait) {
	bool both = false;	// do not service both channels
    
    eb_chan_op out0recv = eb_chan_op_recv((*out)[0]->req);
    eb_chan_op out1recv = eb_chan_op_recv((*out)[1]->req);
    eb_chan_op waitrecv = eb_chan_op_recv(wait);
    eb_chan_op *r = eb_chan_do(eb_nsec_forever, &out0recv, &waitrecv);
    if (r == &out0recv) {
        // nothing
    } else if (r == &waitrecv) {
		both = true;
        
        eb_chan_op *r = eb_chan_do(eb_nsec_forever, &out0recv, &out1recv);
        if (r == &out0recv) {
            // nothing
        } else if (r == &out1recv) {
            // swap
            dch *temp = (*out)[0];
            (*out)[0] = (*out)[1];
            (*out)[1] = temp;
        } else {
            abort();
        }
    } else {
        abort();
    }

	seqno++;
    eb_chan_send(in->req, (void*)(intptr_t)seqno);
	eb_chan release = eb_chan_create(0);
	go( dosplit(in, out, release) );
    
    const void *dat;
    assert(eb_chan_recv(in->dat, &dat));
    eb_chan_send((*out)[0]->dat, dat);
	if (!both) {
        assert(eb_chan_recv(wait, NULL));
	}
    
    assert(eb_chan_recv((*out)[1]->req, NULL));
    eb_chan_send((*out)[1]->dat, dat);
    eb_chan_send(release, (void*)0);
}

void split(dch *in, dch2 *out) {
	eb_chan release = eb_chan_create(0);
	go( dosplit(in, out, release) );
    eb_chan_send(release, (void*)0);
}

void put(rat dat, dch *out) {
    assert(eb_chan_recv(out->req, NULL));
    eb_chan_send(out->dat, rat2heap(dat));
}

rat get(dch *in) {
	seqno++;
    eb_chan_send(in->req, (void*)(intptr_t)seqno);
    
    rat *v;
    assert(eb_chan_recv(in->dat, (void*)&v));
    return heap2rat(v);
}

// Get one rat from each of n demand channels

rat *getn(dch **in, size_t n) {
    assert(n == 2);
    
    eb_chan *req = calloc(2, sizeof(*req));
    eb_chan *dat = calloc(2, sizeof(*dat));
    rat *out = calloc(2, sizeof(*out));
	for (int i=0; i<n; i++) {
		req[i] = in[i]->req;
		dat[i] = NULL;
	}
	for (n=2*n; n>0; n--) {
		seqno++;
        
        eb_chan_op op1 = eb_chan_op_send(req[0], (void*)(intptr_t)seqno);
        eb_chan_op op2 = eb_chan_op_send(req[1], (void*)(intptr_t)seqno);
        eb_chan_op op3 = eb_chan_op_recv(dat[0]);
        eb_chan_op op4 = eb_chan_op_recv(dat[1]);
        eb_chan_op *r = eb_chan_do(eb_nsec_forever, &op1, &op2, &op3, &op4);
        
        if (r == &op1) {
			dat[0] = in[0]->dat;
			req[0] = NULL;
        } else if (r == &op2) {
			dat[1] = in[1]->dat;
			req[1] = NULL;
        } else if (r == &op3) {
			out[0] = heap2rat((rat*)r->val);
			dat[0] = NULL;
        } else if (r == &op4) {
			out[1] = heap2rat((rat*)r->val);
			dat[1] = NULL;
        } else {
            abort();
        }
	}
	return out;
}

// Get one rat from each of 2 demand channels

rat *get2(dch *in0, dch *in1) {
	return getn((dch *[]){in0, in1}, 2);
}

void copy(dch *in, dch *out) {
	for (;;) {
        assert(eb_chan_recv(out->req, NULL));
        eb_chan_send(out->dat, rat2heap(get(in)));
	}
}

void repeat(rat dat, dch *out) {
	for (;;) {
		put(dat, out);
	}
}

typedef dch *PS;	// power series
typedef PS *PS2[2]; // pair of power series

PS Ones;
PS Twos;

dch *mkPS() {
	return mkdch();
}

dch2 *mkPS2() {
	return mkdch2();
}

// Conventions
// Upper-case for power series.
// Lower-case for rationals.
// Input variables: U,V,...
// Output variables: ...,Y,Z

// Integer gcd; needed for rational arithmetic

int64_t gcd(int64_t u, int64_t v) {
	if (u < 0) { return gcd(-u, v); }
	if (u == 0) { return v; }
	return gcd(v%u, u);
}

// Make a rational from two ints and from one int

rat i2tor(int64_t u, int64_t v) {
	int64_t g = gcd(u,v);
	rat r;
	if (v > 0) {
		r.num = u/g;
		r.den = v/g;
	} else {
		r.num = -u/g;
		r.den = -v/g;
	}
	return r;
}

rat itor(int64_t u) {
	return i2tor(u, 1);
}

rat zero;
rat one;


// End mark and end test

rat finis;

int64_t end(rat u) {
	if (u.den==0) { return 1; }
	return 0;
}

// Operations on rationals

rat add(rat u, rat v) {
	int64_t g = gcd(u.den,v.den);
	return i2tor(u.num*(v.den/g)+v.num*(u.den/g),u.den*(v.den/g));
}

rat mul(rat u, rat v) {
	int64_t g1 = gcd(u.num,v.den);
	int64_t g2 = gcd(u.den,v.num);
	rat r;
	r.num = (u.num/g1)*(v.num/g2);
	r.den = (u.den/g2)*(v.den/g1);
	return r;
}

rat neg(rat u) {
	return i2tor(-u.num, u.den);
}

rat sub(rat u, rat v) {
	return add(u, neg(v));
}

rat inv(rat u) {	// invert a rat
    assert(u.num != 0);
	return i2tor(u.den, u.num);
}

// print eval in floating point of PS at x=c to n terms
void evaln(rat c, PS U, int n) {
	double xn = 1;
	double x = (double)c.num/(double)c.den;
	double val = 0;
	for (int i=0; i<n; i++) {
		rat u = get(U);
		if (end(u) != 0) {
			break;
		}
		val = val + x * (double)u.num/(double)u.den;
		xn = xn*x;
	}
	printf("%f\n", val);
}

// Print n terms of a power series
void printn(PS U, int n) {
	bool done = false;
	for (; !done && n>0; n--) {
		rat u = get(U);
		if (end(u) != 0) {
			done = true;
		} else {
			pr(u);
		}
	}
	printf("\n");
}

// Evaluate n terms of power series U at x=c
rat eval(rat c, PS U, int n) {
	if (n==0) { return zero; }
	rat y = get(U);
	if (end(y) != 0) { return zero; }
	return add(y,mul(c,eval(c,U,n-1)));
}

// Power-series constructors return channels on which power
// series flow.  They start an encapsulated generator that
// puts the terms of the series on the channel.

// Make a pair of power series identical to a given power series

dch2 *Split(PS U) {
	dch2 *UU = mkdch2();
	go( split(U,UU) );
	return UU;
}

// Add two power series
PS Add(PS U, PS V) {
	PS Z = mkPS();
	go(
		rat *uv;
		for (;;) {
            assert(eb_chan_recv(Z->req, NULL));
			uv = get2(U,V);
			switch (end(uv[0])+2*end(uv[1])) {
			case 0: {
                eb_chan_send(Z->dat, rat2heap(add(uv[0], uv[1])));
                break;
            }
			case 1: {
                eb_chan_send(Z->dat, rat2heap(uv[1]));
				copy(V,Z);
                break;
            }
			case 2: {
                eb_chan_send(Z->dat, rat2heap(uv[0]));
				copy(U,Z);
                break;
            }
			case 3: {
                eb_chan_send(Z->dat, &finis);
                break;
            }
			}
		}
	);
	return Z;
}

// Multiply a power series by a constant
PS Cmul(rat c, PS U) {
	PS Z = mkPS();
	go(
		bool done = false;
		while (!done) {
            eb_chan_recv(Z->req, NULL);
			rat u = get(U);
			if (end(u) != 0) {
				done = true;
			} else {
                eb_chan_send(Z->dat, rat2heap(mul(c,u)));
			}
		}
        eb_chan_send(Z->dat, &finis);
	);
	return Z;
}

// Subtract

PS Sub(PS U, PS V) {
	return Add(U, Cmul(neg(one), V));
}

// Multiply a power series by the monomial x^n

PS Monmul(PS U, int n) {
	PS Z = mkPS();
	go(
        int nn = n;
		for (; nn>0; nn--) { put(zero,Z); }
		copy(U,Z);
	);
	return Z;
}

// Multiply by x

PS Xmul(PS U) {
	return Monmul(U,1);
}

PS Rep(rat c) {
	PS Z = mkPS();
	go(
        repeat(c,Z);
    );
	return Z;
}

// Monomial c*x^n

PS Mon(rat c, int n) {
	PS Z = mkPS();
	go(
		if(c.num!=0) {
            int nn = n;
			for (; nn>0; nn=nn-1) { put(zero,Z); }
			put(c,Z);
		}
		put(finis,Z);
	);
	return Z;
}

PS Shift(rat c, PS U) {
	PS Z = mkPS();
	go(
		put(c,Z);
		copy(U,Z);
	);
	return Z;
}

// simple pole at 1: 1/(1-x) = 1 1 1 1 1 ...

// Convert array of coefficients, constant term first
// to a (finite) power series

/*
func Poly(a []rat) PS {
	Z:=mkPS()
	begin func(a []rat, Z PS) {
		j:=0
		done:=0
		for j=len(a); !done&&j>0; j=j-1)
			if(a[j-1].num!=0) done=1
		i:=0
		for(; i<j; i=i+1) put(a[i],Z)
		put(finis,Z)
	}()
	return Z
}
*/

// Multiply. The algorithm is
//	let U = u + x*UU
//	let V = v + x*VV
//	then UV = u*v + x*(u*VV+v*UU) + x*x*UU*VV

PS Mul(PS U, PS V) {
	PS Z = mkPS();
	go(
        assert(eb_chan_recv(Z->req, NULL));
		rat *uv = get2(U,V);
		if (end(uv[0])!=0 || end(uv[1]) != 0) {
            eb_chan_send(Z->dat, rat2heap(finis));
		} else {
            eb_chan_send(Z->dat, rat2heap(mul(uv[0],uv[1])));
			dch2 *UU = Split(U);
			dch2 *VV = Split(V);
			PS W = Add(Cmul(uv[0],(*VV)[0]),Cmul(uv[1],(*UU)[0]));
            assert(eb_chan_recv(Z->req, NULL));
            eb_chan_send(Z->dat, rat2heap(get(W)));
			copy(Add(W,Mul((*UU)[1],(*VV)[1])),Z);
		}
	);
	return Z;
}

// Differentiate

PS Diff(PS U) {
	PS Z = mkPS();
	go(
        assert(eb_chan_recv(Z->req, NULL));
		rat u = get(U);
		if (end(u) == 0) {
			bool done = false;
			for (int i=1; !done; i++) {
				u = get(U);
				if (end(u) != 0) {
					done = true;
				} else {
                    eb_chan_send(Z->dat, rat2heap(mul(itor(i),u)));
					assert(eb_chan_recv(Z->req, NULL));
				}
			}
		}
        eb_chan_send(Z->dat, rat2heap(finis));
	);
	return Z;
}

// Integrate, with const of integration
PS Integ(rat c, PS U) {
	PS Z = mkPS();
	go(
		put(c,Z);
		bool done = false;
		for (int i=1; !done; i++) {
			assert(eb_chan_recv(Z->req, NULL));
			rat u = get(U);
			if (end(u) != 0) { done= true; }
            eb_chan_send(Z->dat, rat2heap(mul(i2tor(1,i),u)));
		}
		eb_chan_send(Z->dat, rat2heap(finis));
	);
	return Z;
}

// Binomial theorem (1+x)^c

PS Binom(rat c) {
	PS Z = mkPS();
	go(
        rat cc = c;
		int n = 1;
		rat t = itor(1);
		while (cc.num!=0) {
			put(t,Z);
			t = mul(mul(t,cc),i2tor(1,n));
			cc = sub(cc,one);
			n++;
		}
		put(finis,Z);
	);
	return Z;
}

// Reciprocal of a power series
//	let U = u + x*UU
//	let Z = z + x*ZZ
//	(u+x*UU)*(z+x*ZZ) = 1
//	z = 1/u
//	u*ZZ + z*UU +x*UU*ZZ = 0
//	ZZ = -UU*(z+x*ZZ)/u

PS Recip(PS U) {
	PS Z=mkPS();
	go(
		dch2 *ZZ=mkPS2();
		assert(eb_chan_recv(Z->req, NULL));
		rat z = inv(get(U));
        eb_chan_send(Z->dat, rat2heap(z));
		split(Mul(Cmul(neg(z),U),Shift(z,(*ZZ)[0])),ZZ);
		copy((*ZZ)[1],Z);
	);
	return Z;
}

// Exponential of a power series with constant term 0
// (nonzero constant term would make nonrational coefficients)
// bug: the constant term is simply ignored
//	Z = exp(U)
//	DZ = Z*DU
//	integrate to get Z

PS Exp(PS U) {
	dch2 *ZZ = mkPS2();
	split(Integ(one,Mul((*ZZ)[0],Diff(U))),ZZ);
	return (*ZZ)[1];
}

// Substitute V for x in U, where the leading term of V is zero
//	let U = u + x*UU
//	let V = v + x*VV
//	then S(U,V) = u + VV*S(V,UU)
// bug: a nonzero constant term is ignored

PS Subst(PS U, PS V) {
	PS Z = mkPS();
	go(
		dch2 *VV = Split(V);
		assert(eb_chan_recv(Z->req, NULL));
		rat u = get(U);
        eb_chan_send(Z->dat, rat2heap(u));
		if (end(u) == 0) {
			if (end(get((*VV)[0])) != 0) {
				put(finis,Z);
			} else {
				copy(Mul((*VV)[0],Subst(U,(*VV)[1])),Z);
			}
		}
	);
	return Z;
}

// Monomial Substition: U(c x^n)
// Each Ui is multiplied by c^i and followed by n-1 zeros

PS MonSubst(PS U, rat c0, int n) {
	PS Z = mkPS();
	go(
		rat c = one;
		for (;;) {
			assert(eb_chan_recv(Z->req, NULL));
			rat u = get(U);
            eb_chan_send(Z->dat, rat2heap(mul(u, c)));
			c = mul(c, c0);
			if (end(u) != 0) {
				eb_chan_send(Z->dat, rat2heap(finis));
				break;
			}
			for (int i = 1; i < n; i++) {
				assert(eb_chan_recv(Z->req, NULL));
				eb_chan_send(Z->dat, rat2heap(zero));
			}
		}
	);
	return Z;
}


void Init() {
	chnameserial = -1;
	seqno = 0;
	chnames = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
	zero = itor(0);
	one = itor(1);
	finis = i2tor(1,0);
	Ones = Rep(one);
	Twos = Rep(itor(2));
}

void check(PS U, rat c, int count, const char *str) {
	for (int i = 0; i < count; i++) {
		rat r = get(U);
		if (!eq(r, c)) {
			printf("got: ");
			pr(r);
			printf("should get ");
			pr(c);
			printf("\n");
            abort();
		}
	}
}

const int N=10;
void checka(PS U, rat *a, const char *str) {
	for (int i = 0; i < N; i++) {
		check(U, a[i], 1, str);
	}
}

int main(int argc, const char *argv[]) {
	Init();
	if (argc > 1) {  // print
		printf("Ones: "); printn(Ones, 10);
		printf("Twos: "); printn(Twos, 10);
		printf("Add: "); printn(Add(Ones, Twos), 10);
		printf("Diff: "); printn(Diff(Ones), 10);
		printf("Integ: "); printn(Integ(zero, Ones), 10);
		printf("CMul: "); printn(Cmul(neg(one), Ones), 10);
		printf("Sub: "); printn(Sub(Ones, Twos), 10);
		printf("Mul: "); printn(Mul(Ones, Ones), 10);
		printf("Exp: "); printn(Exp(Ones), 15);
		printf("MonSubst: "); printn(MonSubst(Ones, neg(one), 2), 10);
		printf("ATan: "); printn(Integ(zero, MonSubst(Ones, neg(one), 2)), 10);
	} else {  // test
        check(Ones, one, 5, "Ones");
        check(Add(Ones, Ones), itor(2), 0, "Add Ones Ones");  // 1 1 1 1 1
        check(Add(Ones, Twos), itor(3), 0, "Add Ones Twos"); // 3 3 3 3 3
        rat *a = calloc(N, sizeof(*a));
        PS d = Diff(Ones);
        for (int i=0; i < N; i++) {
            a[i] = itor(i+1);
        }
        checka(d, a, "Diff");  // 1 2 3 4 5
        PS in = Integ(zero, Ones);
        a[0] = zero;  // integration constant
        for (int i=1; i < N; i++) {
            a[i] = i2tor(1, i);
        }
        checka(in, a, "Integ");  // 0 1 1/2 1/3 1/4 1/5
        check(Cmul(neg(one), Twos), itor(-2), 10, "CMul");  // -1 -1 -1 -1 -1
        check(Sub(Ones, Twos), itor(-1), 0, "Sub Ones Twos");  // -1 -1 -1 -1 -1
        PS m = Mul(Ones, Ones);
        for (int i=0; i < N; i++) {
            a[i] = itor(i+1);
        }
        checka(m, a, "Mul");  // 1 2 3 4 5
        PS e = Exp(Ones);
        a[0] = itor(1);
        a[1] = itor(1);
        a[2] = i2tor(3,2);
        a[3] = i2tor(13,6);
        a[4] = i2tor(73,24);
        a[5] = i2tor(167,40);
        a[6] = i2tor(4051,720);
        a[7] = i2tor(37633,5040);
        a[8] = i2tor(43817,4480);
        a[9] = i2tor(4596553,362880);
        checka(e, a, "Exp");  // 1 1 3/2 13/6 73/24
        PS at = Integ(zero, MonSubst(Ones, neg(one), 2));
        for (int c = 1, i = 0; i < N; i++) {
            if (i%2 == 0) {
                a[i] = zero;
            } else {
                a[i] = i2tor(c, i);
                c *= -1;
            }
        }
        checka(at, a, "ATan");  // 0 -1 0 -1/3 0 -1/5
    }
    return 0;
}
