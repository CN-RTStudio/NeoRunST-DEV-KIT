#pragma once

#include "stdlib.h"

typedef long long intmax_t;
typedef unsigned long long uintmax_t;

typedef struct {
    intmax_t quot;
    intmax_t rem;
} imaxdiv_t;

#define PRId8 "d"
#define PRId16 "d"
#define PRId32 "d"
#define PRId64 "lld"
#define PRIi64 "lli"
#define PRIu64 "llu"
#define PRIx64 "llx"
#define PRIX64 "llX"
#define PRIo64 "llo"
#define PRIdMAX "lld"
#define PRIuMAX "llu"

static __attribute__((unused)) intmax_t imaxabs(intmax_t v) {
    return v < 0 ? -v : v;
}

static __attribute__((unused)) imaxdiv_t imaxdiv(intmax_t a, intmax_t b) {
    imaxdiv_t r;
    r.quot = a / b;
    r.rem = a % b;
    return r;
}

static __attribute__((unused)) intmax_t strtoimax(const char* s, char** end, int base) {
    return strtoll(s, end, base);
}

static __attribute__((unused)) uintmax_t strtoumax(const char* s, char** end, int base) {
    return strtoull(s, end, base);
}
