/* __math_oflow.c - ported from musl (MIT, git.musl-libc.org), algorithm unchanged */
#include "libm_impl.h"

double __math_oflow(uint32_t sign)
{
	return __math_xflow(sign, 0x1p769);
}
