/* fabs.c - ported from musl (MIT, git.musl-libc.org), algorithm unchanged */
#include "libm_impl.h"

double fabs(double x)
{
	union {double f; uint64_t i;} u = {x};
	u.i &= -1ULL/2;
	return u.f;
}
