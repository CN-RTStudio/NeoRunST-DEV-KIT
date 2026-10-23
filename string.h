#pragma once
// ── string.h — 字符串 / 内存工具 (freestanding 自实现) ──
// 与标准 C 语义一致; NeoRunST 没有 libc, 这份就是实现本身。
// 用法: #include "string.h" (双引号, 不要用编译器自带的 <string.h>)

static unsigned int strlen(const char* s) {
    unsigned int n = 0;
    while (s[n]) n++;
    return n;
}

static char* strcpy(char* dst, const char* src) {
    char* d = dst;
    while ((*d++ = *src++)) ;
    return dst;
}

static char* strncpy(char* dst, const char* src, unsigned int n) {
    unsigned int i = 0;
    for (; i < n && src[i]; i++) dst[i] = src[i];
    for (; i < n; i++) dst[i] = '\0';
    return dst;
}

static char* strcat(char* dst, const char* src) {
    char* d = dst;
    while (*d) d++;
    while ((*d++ = *src++)) ;
    return dst;
}

static int strcmp(const char* a, const char* b) {
    while (*a && *a == *b) { a++; b++; }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

static int strncmp(const char* a, const char* b, unsigned int n) {
    for (unsigned int i = 0; i < n; i++) {
        if (a[i] != b[i] || !a[i]) return (int)(unsigned char)a[i] - (int)(unsigned char)b[i];
    }
    return 0;
}

static const char* strchr(const char* s, int c) {
    for (; *s; s++)
        if (*s == (char)c) return s;
    return (c == 0) ? s : (const char*)0;
}

static const char* strrchr(const char* s, int c) {
    const char* last = 0;
    for (; *s; s++)
        if (*s == (char)c) last = s;
    if (c == 0) last = s;
    return last;
}

static const char* strstr(const char* hay, const char* needle) {
    if (!*needle) return hay;
    for (; *hay; hay++) {
        const char* h = hay;
        const char* n = needle;
        while (*h && *n && *h == *n) { h++; n++; }
        if (!*n) return hay;
    }
    return (const char*)0;
}

static void* memset(void* p, int v, unsigned int n) {
    unsigned char* d = (unsigned char*)p;
    for (unsigned int i = 0; i < n; i++) d[i] = (unsigned char)v;
    return p;
}

static void* memcpy(void* dst, const void* src, unsigned int n) {
    unsigned char* d = (unsigned char*)dst;
    const unsigned char* s = (const unsigned char*)src;
    for (unsigned int i = 0; i < n; i++) d[i] = s[i];
    return dst;
}

static void* memmove(void* dst, const void* src, unsigned int n) {
    unsigned char* d = (unsigned char*)dst;
    const unsigned char* s = (const unsigned char*)src;
    if (d < s) {
        for (unsigned int i = 0; i < n; i++) d[i] = s[i];
    } else {
        for (unsigned int i = n; i > 0; i--) d[i - 1] = s[i - 1];
    }
    return dst;
}

static int memcmp(const void* a, const void* b, unsigned int n) {
    const unsigned char* x = (const unsigned char*)a;
    const unsigned char* y = (const unsigned char*)b;
    for (unsigned int i = 0; i < n; i++) {
        if (x[i] != y[i]) return (int)x[i] - (int)y[i];
    }
    return 0;
}
