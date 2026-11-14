#pragma once
// ── NEP ctype.h (freestanding shim) ──
// 纯 ASCII 实现 (Lua 只用于解析器/模式匹配, 不依赖 locale)

static __attribute__((unused)) int isspace(int c) {
    return c == ' ' || (c >= '\t' && c <= '\r');   // \t \n \v \f \r
}
static __attribute__((unused)) int isalpha(int c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}
static __attribute__((unused)) int isdigit(int c) {
    return c >= '0' && c <= '9';
}
static __attribute__((unused)) int isalnum(int c) {
    return isalpha(c) || isdigit(c);
}
static __attribute__((unused)) int isupper(int c) {
    return c >= 'A' && c <= 'Z';
}
static __attribute__((unused)) int islower(int c) {
    return c >= 'a' && c <= 'z';
}
static __attribute__((unused)) int isxdigit(int c) {
    return isdigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}
static __attribute__((unused)) int ispunct(int c) {
    return (c >= 32 && c < 127) && !isalnum(c) && c != ' ';
}
static __attribute__((unused)) int iscntrl(int c) {
    return (c >= 0 && c < 32) || c == 127;
}
static __attribute__((unused)) int isgraph(int c) {
    return c > 32 && c < 127;
}
static __attribute__((unused)) int tolower(int c) {
    return isupper(c) ? c + 32 : c;
}
static __attribute__((unused)) int toupper(int c) {
    return islower(c) ? c - 32 : c;
}
