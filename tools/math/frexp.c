/* frexp.c - bit-manipulation, standard algorithm (subnormal 走 scalbn 归一化) */
#include "libm_impl.h"

double frexp(double x, int *e)
{
	union {double f; uint64_t i;} u = {x};
	uint64_t absi = u.i & ~0ULL>>1;
	int ex = (int)(absi>>52 & 0x7ff);

	/* 0 / inf / nan: 指数置 0, 原值返回 */
	if (absi == 0 || ex == 0x7ff) {
		*e = 0;
		return x;
	}

	if (ex == 0) {              /* subnormal: ×2^64 归一化 */
		u.f = scalbn(x, 64);
		ex = (int)(u.i>>52 & 0x7ff);
		u.i = (u.i & 0x800fffffffffffffULL) | 0x3fe0000000000000ULL;
		*e = ex - 0x3fe - 64;
		return u.f;
	}

	*e = ex - 0x3fe;
	u.i = (u.i & 0x800fffffffffffffULL) | 0x3fe0000000000000ULL;
	return u.f;
}
