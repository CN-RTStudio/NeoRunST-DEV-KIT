/* copysign.c - ported from musl (MIT, git.musl-libc.org), algorithm unchanged */
#include "libm_impl.h"

double copysign(double x, double y) {
	union {double f; uint64_t i;} ux={x}, uy={y};
	ux.i &= -1ULL/2;
	ux.i |= uy.i & 1ULL<<63;
	return ux.f;
}
