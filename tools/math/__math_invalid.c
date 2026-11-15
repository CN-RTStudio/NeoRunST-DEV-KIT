/* __math_invalid.c - ported from musl (MIT, git.musl-libc.org), algorithm unchanged */
#include "libm_impl.h"

double __math_invalid(double x)
{
	return (x - x) / (x - x);
}
