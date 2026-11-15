#pragma once
// ── NEP unistd.h (freestanding shim) ──
// POSIX 接口在 NEP 上不存在; 大程序 (DOOM) 的 include 只是编译期
// 需要, 运行时用到的功能 (usleep/isatty/getcwd) 都有 NEP 等价物或
// 已在移植层短路。恒空即可。
