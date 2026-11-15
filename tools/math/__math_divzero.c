/* __math_divzero.c - ported from musl (MIT, git.musl-libc.org), algorithm unchanged */
#include "libm_impl.h"

double __math_divzero(uint32_t sign)
{
	return fp_barrier(sign ? -1.0 : 1.0) / 0.0;
}
