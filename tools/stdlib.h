#pragma once
// ── NEP stdlib.h (freestanding shim) ──
// gcc freestanding 不提供 stdlib.h; malloc 家族来自 heap.h (单编译单元
// 内 static 实现, 故这里不重复声明, 直接 include)。
// strtod 在 dev\lua\main.c 里提供非 static 实现 (数字解析核心)。

#include "heap.h"

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

typedef struct { int quot; int rem; } div_t;
typedef struct { long quot; long rem; } ldiv_t;

static inline div_t div(int num, int den) {
    div_t r;
    r.quot = num / den;
    r.rem = num % den;
    return r;
}

static inline ldiv_t ldiv(long num, long den) {
    ldiv_t r;
    r.quot = num / den;
    r.rem = num % den;
    return r;
}

static inline long long llabs(long long v) { return v < 0 ? -v : v; }

static inline int strtol_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'z') return c - 'a' + 10;
    if (c >= 'A' && c <= 'Z') return c - 'A' + 10;
    return -1;
}

static inline long strtol(const char* s, char** end, int base) {
    const char* p = s;
    int neg = 0;
    if (!p) { if (end) *end = (char*)0; return 0; }
    while (*p == ' ' || (*p >= 9 && *p <= 13)) p++;
    if (*p == '+' || *p == '-') { neg = (*p == '-'); p++; }
    if (base == 0) {
        if (*p == '0' && (p[1] == 'x' || p[1] == 'X')) { base = 16; p += 2; }
        else if (*p == '0') { base = 8; p++; }
        else base = 10;
    } else if (base == 16 && *p == '0' && (p[1] == 'x' || p[1] == 'X')) {
        p += 2;
    }
    if (base < 2 || base > 36) { if (end) *end = (char*)s; return 0; }
    long acc = 0;
    int any = 0;
    int d;
    while ((d = strtol_digit(*p)) >= 0 && d < base) {
        if (acc > (0x7FFFFFFFFFFFFFFFL - d) / base) {
            acc = 0x7FFFFFFFFFFFFFFFL;
        } else {
            acc = acc * base + d;
        }
        any = 1;
        p++;
    }
    if (!any) { if (end) *end = (char*)s; return 0; }
    if (end) *end = (char*)p;
    return neg ? -acc : acc;
}

static inline unsigned long strtoul(const char* s, char** end, int base) {
    const char* p = s;
    int neg = 0;
    if (!p) { if (end) *end = (char*)0; return 0; }
    while (*p == ' ' || (*p >= 9 && *p <= 13)) p++;
    if (*p == '+' || *p == '-') { neg = (*p == '-'); p++; }
    if (base == 0) {
        if (*p == '0' && (p[1] == 'x' || p[1] == 'X')) { base = 16; p += 2; }
        else if (*p == '0') { base = 8; p++; }
        else base = 10;
    } else if (base == 16 && *p == '0' && (p[1] == 'x' || p[1] == 'X')) {
        p += 2;
    }
    if (base < 2 || base > 36) { if (end) *end = (char*)s; return 0; }
    unsigned long acc = 0;
    int any = 0;
    int d;
    while ((d = strtol_digit(*p)) >= 0 && d < base) {
        if (acc > (0xFFFFFFFFFFFFFFFFUL - (unsigned long)d) / (unsigned long)base) {
            acc = 0xFFFFFFFFFFFFFFFFUL;
        } else {
            acc = acc * base + (unsigned long)d;
        }
        any = 1;
        p++;
    }
    if (!any) { if (end) *end = (char*)s; return 0; }
    if (end) *end = (char*)p;
    return neg ? (unsigned long)(0UL - acc) : acc;
}

static inline void* bsearch(const void* key, const void* base, unsigned int n, unsigned int size,
                            int (*cmp)(const void*, const void*)) {
    unsigned int lo = 0;
    unsigned int hi = n;
    const char* b = (const char*)base;
    while (lo < hi) {
        unsigned int mid = lo + (hi - lo) / 2;
        int c = cmp(key, b + (unsigned int)0 + mid * size);
        if (c == 0) return (void*)(b + mid * size);
        if (c < 0) hi = mid;
        else lo = mid + 1;
    }
    return (void*)0;
}

static inline void nr_qsort_swap(char* a, char* b, unsigned int size) {
    unsigned int i = 0;
    while (i < size) {
        char t = a[i];
        a[i] = b[i];
        b[i] = t;
        i++;
    }
}

static inline void* nr_qsort_median(char* lo, char* mid, char* hi, unsigned int size,
                                    int (*cmp)(const void*, const void*)) {
    if (cmp(lo, mid) < 0) {
        if (cmp(mid, hi) < 0) return mid;
        return cmp(lo, hi) < 0 ? hi : lo;
    }
    if (cmp(lo, hi) < 0) return lo;
    return cmp(mid, hi) < 0 ? hi : mid;
}

static inline void qsort(void* base, unsigned int n, unsigned int size,
                         int (*cmp)(const void*, const void*)) {
    if (!base || n < 2 || size == 0 || !cmp) return;
    char* b = (char*)base;
    unsigned int lo = 0;
    unsigned int hi = n - 1;
    while (hi > lo) {
        if (hi - lo < 8) {
            for (unsigned int i = lo + 1; i <= hi; i++) {
                for (unsigned int j = i; j > lo; j--) {
                    char* cj = b + j * size;
                    char* pj = b + (j - 1) * size;
                    if (cmp(cj, pj) < 0) nr_qsort_swap(cj, pj, size);
                    else break;
                }
            }
            if (hi == 0) break;
            hi = lo;
            continue;
        }
        char* pivot = (char*)nr_qsort_median(b + lo * size, b + ((lo + hi) / 2) * size, b + hi * size, size, cmp);
        nr_qsort_swap(pivot, b + hi * size, size);
        pivot = b + hi * size;
        unsigned int i = lo;
        unsigned int j = hi;
        while (i < j) {
            while (i < j && cmp(b + i * size, pivot) < 0) i++;
            while (i < j && cmp(b + j * size, pivot) >= 0) j--;
            if (i < j) nr_qsort_swap(b + i * size, b + j * size, size);
        }
        nr_qsort_swap(b + i * size, b + hi * size, size);
        if (i == hi) {
            if (i > 0) hi = i - 1; else break;
        } else if (i == lo) {
            lo = i + 1;
        } else {
            hi = i - 1;
        }
    }
}

static inline double atof(const char* s) {
    if (!s) return 0.0;
    const char* p = s;
    while (*p == ' ' || (*p >= 9 && *p <= 13)) p++;
    int neg = 0;
    if (*p == '+' || *p == '-') { neg = (*p == '-'); p++; }
    double v = 0.0;
    int digits = 0;
    while (*p >= '0' && *p <= '9') { v = v * 10.0 + (double)(*p - '0'); p++; digits++; }
    if (*p == '.') {
        p++;
        double scale = 0.1;
        while (*p >= '0' && *p <= '9') {
            v += (double)(*p - '0') * scale;
            scale *= 0.1;
            p++;
            digits++;
        }
    }
    if (!digits) return 0.0;
    if (*p == 'e' || *p == 'E') {
        const char* q = p + 1;
        int eneg = 0;
        if (*q == '+' || *q == '-') { eneg = (*q == '-'); q++; }
        int ex = 0;
        int ed = 0;
        while (*q >= '0' && *q <= '9') { ex = ex * 10 + (*q - '0'); q++; ed++; }
        if (ed && ex < 400) {
            double f = 1.0;
            while (ex-- > 0) f *= 10.0;
            if (eneg) v /= f; else v *= f;
        }
    }
    return neg ? -v : v;
}

static __attribute__((unused)) long long atoll(const char* s) {
    if (!s) return 0;
    while (*s == ' ' || (*s >= '\t' && *s <= '\r')) s++;
    int neg = 0;
    if (*s == '+' || *s == '-') { neg = (*s == '-'); s++; }
    long long v = 0;
    int digits = 0;
    while (*s >= '0' && *s <= '9') {
        if (v > 922337203685477580LL) { v = 9223372036854775807LL; digits = 1; break; }
        v = v * 10 + (*s - '0');
        digits = 1;
        s++;
    }
    if (!digits) return 0;
    return neg ? -v : v;
}

static __attribute__((unused)) long long strtoll(const char* s, char** end, int base) {
    long long v = 0;
    int any = 0;
    int neg = 0;
    const char* p = s;
    if (!p) { if (end) *end = (char*)s; return 0; }
    while (*p == ' ' || (*p >= '\t' && *p <= '\r')) p++;
    if (*p == '+' || *p == '-') { neg = (*p == '-'); p++; }
    if ((base == 0 || base == 16) && p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
        p += 2;
        base = 16;
    } else if (base == 0 && p[0] == '0') {
        base = 8;
    } else if (base == 0) {
        base = 10;
    }
    if (base < 2 || base > 36) { if (end) *end = (char*)s; return 0; }
    while (*p) {
        int d = strtol_digit(*p);
        if (d < 0 || d >= base) break;
        if (v > 922337203685477580LL) {
            v = 9223372036854775807LL;
            any = 1;
            p++;
            continue;
        }
        v = v * 10 + d;
        any = 1;
        p++;
    }
    if (!any) { if (end) *end = (char*)s; return 0; }
    if (end) *end = (char*)p;
    return neg ? -v : v;
}

static __attribute__((unused)) unsigned long long strtoull(const char* s, char** end, int base) {
    unsigned long long v = 0;
    int any = 0;
    int neg = 0;
    const char* p = s;
    if (!p) { if (end) *end = (char*)s; return 0; }
    while (*p == ' ' || (*p >= '\t' && *p <= '\r')) p++;
    if (*p == '+' || *p == '-') { neg = (*p == '-'); p++; }
    if ((base == 0 || base == 16) && p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
        p += 2;
        base = 16;
    } else if (base == 0 && p[0] == '0') {
        base = 8;
    } else if (base == 0) {
        base = 10;
    }
    if (base < 2 || base > 36) { if (end) *end = (char*)s; return 0; }
    while (*p) {
        int d = strtol_digit(*p);
        if (d < 0 || d >= base) break;
        if (v > 1844674407370955161ULL) {
            v = 18446744073709551615ULL;
            any = 1;
            p++;
            continue;
        }
        v = v * 10 + (unsigned)d;
        any = 1;
        p++;
    }
    if (!any) { if (end) *end = (char*)s; return 0; }
    if (end) *end = (char*)p;
    return neg ? (unsigned long long)(0ULL - v) : v;
}
