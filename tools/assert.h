#pragma once
// ── NEP assert.h (freestanding shim) ──
// Lua 的 lua_assert 默认就是 no-op; assert.h 只为满足 #include。
// 真断言会引入 __assert_fail 外部符号, freestanding 下不可用。
#define assert(c) ((void)0)
