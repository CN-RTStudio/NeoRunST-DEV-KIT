/* __math_xflow.c - ported from musl (MIT, git.musl-libc.org), algorithm unchanged */
#include "libm_impl.h"

double __math_xflow(uint32_t sign, double y)
{
	return eval_as_double(fp_barrier(sign ? -y : y) * y);
}
