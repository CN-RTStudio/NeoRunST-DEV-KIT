/* __math_uflow.c - ported from musl (MIT, git.musl-libc.org), algorithm unchanged */
#include "libm_impl.h"

double __math_uflow(uint32_t sign)
{
	return __math_xflow(sign, 0x1p-767);
}
