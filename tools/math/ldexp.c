/* ldexp.c - standard algorithm (subnormal/overflow 边界与 musl 一致语义) */
#include "libm_impl.h"

double ldexp(double x, int n)
{
	/* scalbn 的基数是 2, 语义与 ldexp 完全相同 */
	return scalbn(x, n);
}
