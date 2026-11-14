#pragma once
// ── NEP signal.h (freestanding shim) ──
// 只为满足 #include (lua.c 已排除, 没人真正调用 signal/raise)。

typedef int sig_atomic_t;
#define SIGINT  2
#define SIGILL  4
#define SIGFPE  8
#define SIGSEGV 11
#define SIGTERM 15
#define SIG_DFL ((void*)0)
#define SIG_ERR ((void*)-1)
