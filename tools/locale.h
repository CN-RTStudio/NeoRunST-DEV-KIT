#pragma once
// ── NEP locale.h (freestanding shim) ──
// 固定 "C" locale, 小数点 '.'。localeconv 只为满足 lua_getlocaledecpoint。

struct lconv {
    char* decimal_point;
    char* thousands_sep;
    char* grouping;
};

static __attribute__((section(".data"))) char nr_decpt_str[] = ".";
static __attribute__((section(".data"))) char nr_empty_str[] = "";

static __attribute__((section(".data"))) struct lconv nr_lconv = {
    nr_decpt_str, nr_empty_str, nr_empty_str
};

static __attribute__((unused)) struct lconv* localeconv(void) {
    return &nr_lconv;
}

// LC_* 类别 + setlocale: 恒 "C" locale, setlocale(cat, NULL) 返回 "C"
#define LC_ALL      0
#define LC_COLLATE  1
#define LC_CTYPE    2
#define LC_MONETARY 3
#define LC_NUMERIC  4
#define LC_TIME     5

static __attribute__((section(".data"))) char nr_locale_str[] = "C";

static __attribute__((unused)) char* setlocale(int category, const char* name) {
    (void)category;
    return name ? NULL : nr_locale_str;
}
