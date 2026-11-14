#pragma once
// ── string.h — 字符串 / 内存工具 (freestanding 自实现) ──
// 与标准 C 语义一致; NeoRunST 没有 libc, 这份就是实现本身。
// 用法: #include "string.h" (双引号, 不要用编译器自带的 <string.h>)

static unsigned int strlen(const char* s) {
    unsigned int n = 0;
    while (s[n]) n++;
    return n;
}

static unsigned int strnlen(const char* s, unsigned int max) {
    unsigned int n = 0;
    while (n < max && s[n]) n++;
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

static int strcasecmp(const char* a, const char* b) {
    while (*a && *b) {
        unsigned char x = (unsigned char)*a;
        unsigned char y = (unsigned char)*b;
        if (x >= 'A' && x <= 'Z') x = (unsigned char)(x + 32);
        if (y >= 'A' && y <= 'Z') y = (unsigned char)(y + 32);
        if (x != y) return (int)x - (int)y;
        a++;
        b++;
    }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
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

static void* memchr(const void* p, int v, unsigned int n) {
    const unsigned char* d = (const unsigned char*)p;
    for (unsigned int i = 0; i < n; i++) {
        if (d[i] == (unsigned char)v) return (void*)(d + i);
    }
    return (void*)0;
}

static int memcmp(const void* a, const void* b, unsigned int n) {
    const unsigned char* x = (const unsigned char*)a;
    const unsigned char* y = (const unsigned char*)b;
    for (unsigned int i = 0; i < n; i++) {
        if (x[i] != y[i]) return (int)x[i] - (int)y[i];
    }
    return 0;
}

static char* strncat(char* dst, const char* src, unsigned int n) {
    char* d = dst;
    while (*d) d++;
    while (n-- && *src) *d++ = *src++;
    *d = '\0';
    return dst;
}

static unsigned int strlcpy(char* dst, const char* src, unsigned int cap) {
    if (cap == 0) return strlen(src);
    unsigned int i = 0;
    while (i + 1 < cap && src[i]) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
    while (src[i]) i++;
    return i;
}

static unsigned int strlcat(char* dst, const char* src, unsigned int cap) {
    unsigned int used = 0;
    while (used < cap && dst[used]) used++;
    if (used == cap) return cap + strlen(src);
    unsigned int i = 0;
    while (used + i + 1 < cap && src[i]) {
        dst[used + i] = src[i];
        i++;
    }
    dst[used + i] = '\0';
    return used + strlen(src);
}

static const char* strpbrk(const char* s, const char* accept) {
    for (; *s; s++) {
        const char* a = accept;
        while (*a) {
            if (*a == *s) return s;
            a++;
        }
    }
    return (const char*)0;
}

static unsigned int strspn(const char* s, const char* accept) {
    unsigned int i = 0;
    for (; s[i]; i++) {
        const char* a = accept;
        while (*a && *a != s[i]) a++;
        if (!*a) break;
    }
    return i;
}

static unsigned int strcspn(const char* s, const char* reject) {
    unsigned int i = 0;
    for (; s[i]; i++) {
        const char* r = reject;
        while (*r && *r != s[i]) r++;
        if (*r) break;
    }
    return i;
}
static char* strtok_r(char* s, const char* delim, char** save) {
    if (!s) s = *save;
    if (!s) return (char*)0;
    s += strspn(s, delim);
    if (!*s) {
        *save = s;
        return (char*)0;
    }
    char* end = s + strcspn(s, delim);
    if (*end) {
        *end = '\0';
        *save = end + 1;
    } else {
        *save = end;
    }
    return s;
}
