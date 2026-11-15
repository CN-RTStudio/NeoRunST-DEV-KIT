/* log2.c - 基于 musl log (精度 ~1-2 ulp, math.log(x, 2) 够用) */
#include "libm_impl.h"

#define LN2_INV 1.44269504088896340736  // 1/ln2

double log2(double x)
{
	return log(x) * LN2_INV;
}
