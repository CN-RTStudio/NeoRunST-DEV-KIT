#pragma once
// ── NEP stdlib.h (freestanding shim) ──
// gcc freestanding 不提供 stdlib.h; malloc 家族来自 heap.h (单编译单元
// 内 static 实现, 故这里不重复声明, 直接 include)。
// strtod 在 dev\lua\main.c 里提供非 static 实现 (数字解析核心)。

#include "heap.h"
#include "string.h"      // strlen/memcpy (strdup 用)

#ifndef NULL
#define NULL ((void*)0)
#endif

#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1

#define RAND_MAX 0x7FFFFFFF

static inline void __attribute__((noreturn)) exit(int status) {
    (void)status;
    nep_exit();
}
static inline void __attribute__((noreturn)) _Exit(int status) {
    (void)status;
    nep_exit();
}
static inline void __attribute__((noreturn)) abort(void) {
    nep_exit();
}

// 无环境变量 / 无子进程: getenv 恒 NULL, system 恒失败
static __attribute__((unused)) char* getenv(const char* name) {
    (void)name;
    return NULL;
}
static __attribute__((unused)) int system(const char* cmd) {
    (void)cmd;
    return -1;
}
static __attribute__((unused)) char* tmpnam(char* buf) {
    (void)buf;
    return NULL;
}

// 数字 → 字符串解析 (main.c 实现, 支持 10/16 进制, inf/nan)
double strtod(const char* s, char** end);

// ── DOOM 等大程序用 (abs/labs 已在 dev\stdio.h, 双方共用防重宏)
#ifndef __NR_ABS_DEFINED
#define __NR_ABS_DEFINED 1
static inline int abs(int a) { return a < 0 ? -a : a; }
static inline long labs(long a) { return a < 0 ? -a : a; }
#endif
static inline double atof(const char* s) { return strtod(s, (char**)0); }

static __attribute__((unused)) char* strdup(const char* s) {
    unsigned long n = (unsigned long)strlen(s) + 1;
    char* p = (char*)malloc(n);
    if (p) memcpy(p, s, n);
    return p;
}
