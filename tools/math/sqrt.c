/* sqrt.c - NEP freestanding: SSE2 sqrtsd 硬件实现。
 * 不用 musl 的 sqrt.c (依赖 fenv/fegetround)。sqrtsd 的 IEEE 语义:
 * 负数 -> 默认 NaN, -0 -> -0, inf -> inf, NaN 透传, 结果精确到 0.5 ulp。
 */
#include "libm_impl.h"

double sqrt(double x)
{
	double r;
	__asm__ ("sqrtsd %1, %0" : "=x" (r) : "x" (x));
	return r;
}
