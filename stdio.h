#pragma once
// ── NEP 类标准库: stdio ──
// 控制台 printf / scanf / sprintf, 建立在 nep.h 的 syscall 之上。
// 控制台程序不需要 nep_win_create — 第一次 printf 时内核自动开一个
// "<程序名> - 控制台" 窗口 (纯 print 的程序就是控制台程序)。
//
// 已支持的格式 (先慢慢来, 够用就好):
//   printf/	sprintf: %c %s %d %i %u %x %X %p %% 以及 l 前缀 (ld/lu/lx);
//                     浮点 %f %e %g (%F/%E/%G 同义), 支持精度/宽度/标志
//                     (- 0 + 空格, 如 %.14g %8.3f %-10.2e) — bigint 精确
//                     十进制转换, 末位舍入正确
//   scanf:           %d %i %u %x %s %c      支持宽度 (如 %63s)
//
// 用法:
//   #include "nep.h"
//   #include "stdio.h"
//   int nep_main(void) {
//       printf("hello, NeoRunST!\n");
//       char name[64];
//       printf("name? ");
//       if (scanf("%63s", name) == 1) printf("hi, %s!\n", name);
//       return 0;   // nep_main 返回后 crt0 自动 nep_exit()
//   }

#include "nep.h"

typedef __builtin_va_list va_list;
#define va_start(ap, last) __builtin_va_start(ap, last)
#define va_end(ap)         __builtin_va_end(ap)
#define va_arg(ap, type)   __builtin_va_arg(ap, type)

#ifndef __NR_SIZE_T
#define __NR_SIZE_T
typedef unsigned long size_t;
#endif

// 前置声明 (vprintf 要用)
__attribute__((unused))
static int printf(const char* fmt, ...);

__attribute__((unused))
static unsigned int nr_strlen(const char* s) {
    unsigned int n = 0;
    while (s && s[n]) n++;
    return n;
}

// ── 格式化核心: 输出写入 out (容量 cap, 恒 NUL 结尾), 返回写入长度 ──

// ── 浮点格式化支撑 (%f/%e/%g): bigint 精确十进制转换 ──
// double = m53 × 2^e2 (m53 ≤ 2^53)。e2≥0: 左移; e2<0: 乘 5^|e2| 再除 10^|e2|
// —— 全程整数运算, 数字串精确, 末位舍入可靠 (反复除 10 会积累误差)。
// 基数 1e9 的 limb 数组; 上限 150 limb = 1350 位 (5^1074 ≈ 10^751 够用)
typedef struct { unsigned int l[150]; int n; } nr_big_t;

__attribute__((unused))
static void nr_big_set(nr_big_t* b, unsigned long long v) {
    b->n = 0;
    while (v) { b->l[b->n++] = (unsigned int)(v % 1000000000u); v /= 1000000000u; }
    if (!b->n) { b->l[0] = 0; b->n = 1; }
}

__attribute__((unused))
static void nr_big_shl1(nr_big_t* b) {           // ×2
    unsigned long long carry = 0;
    for (int i = 0; i < b->n; i++) {
        unsigned long long cur = ((unsigned long long)b->l[i] << 1) + carry;
        b->l[i] = (unsigned int)(cur % 1000000000u);
        carry = cur / 1000000000u;
    }
    if (carry) b->l[b->n++] = (unsigned int)carry;
}

__attribute__((unused))
static void nr_big_mul5(nr_big_t* b) {           // ×5
    unsigned long long carry = 0;
    for (int i = 0; i < b->n; i++) {
        unsigned long long cur = (unsigned long long)b->l[i] * 5u + carry;
        b->l[i] = (unsigned int)(cur % 1000000000u);
        carry = cur / 1000000000u;
    }
    while (carry) { b->l[b->n++] = (unsigned int)(carry % 1000000000u); carry /= 1000000000u; }
}

// bigint → 十进制数字串 (无前导零), 返回位数
__attribute__((unused))
static int nr_big_str(const nr_big_t* b, char* out) {
    char tmp[1400];
    int m = 0;
    for (int i = b->n - 1; i >= 0; i--) {
        unsigned int v = b->l[i];
        if (i == b->n - 1) {                     // 最高 limb 不补零
            char t[10]; int k = 0;
            if (!v) t[k++] = '0';
            while (v) { t[k++] = (char)('0' + v % 10u); v /= 10u; }
            while (k) tmp[m++] = t[--k];
        } else {
            tmp[m++] = (char)('0' + (v / 100000000u) % 10);
            tmp[m++] = (char)('0' + (v / 10000000u) % 10);
            tmp[m++] = (char)('0' + (v / 1000000u) % 10);
            tmp[m++] = (char)('0' + (v / 100000u) % 10);
            tmp[m++] = (char)('0' + (v / 10000u) % 10);
            tmp[m++] = (char)('0' + (v / 1000u) % 10);
            tmp[m++] = (char)('0' + (v / 100u) % 10);
            tmp[m++] = (char)('0' + (v / 10u) % 10);
            tmp[m++] = (char)('0' + v % 10u);
        }
    }
    for (int i = 0; i < m; i++) out[i] = tmp[i];
    out[m] = 0;
    return m;
}

// x 的精确十进制数字串 (有效数字, 无前导零) + E10: x = d1.d2d3... × 10^E10
// s 容量 ≥ 1100。subnormal/超大都覆盖 (e2 ∈ [-1074, 971])
__attribute__((unused))
static void nr_dec(double x, char* s, int* D_out, int* E_out) {
    unsigned long long u;
    { union { double d; unsigned long long u; } cvt; cvt.d = x; u = cvt.u; }
    int neg = (int)(u >> 63);
    int be = (int)((u >> 52) & 0x7FF);
    unsigned long long frac = u & ((1ULL << 52) - 1);
    unsigned long long m; int e2;
    if (be == 0) { m = frac; e2 = -1074; }                    // 零/次正规
    else         { m = frac | (1ULL << 52); e2 = be - 1075; }
    (void)neg;
    nr_big_t b; nr_big_set(&b, m);
    if (e2 >= 0) {
        for (int i = 0; i < e2; i++) nr_big_shl1(&b);
        *D_out = nr_big_str(&b, s);
        *E_out = *D_out - 1;
    } else {
        int k = -e2;
        for (int i = 0; i < k; i++) nr_big_mul5(&b);
        *D_out = nr_big_str(&b, s);
        *E_out = *D_out - 1 - k;
    }
}

// 保留 P 位有效数字 (就地舍入, 半进位)。返回舍入后的位数
__attribute__((unused))
static int nr_round_sig(char* s, int D, int P, int* E10) {
    if (P < 1) P = 1;
    char c = (P < D) ? s[P] : '0';
    if (c < '5') return (P < D) ? P : D;
    int i = P - 1;
    while (i >= 0) {
        if (s[i] == '9') s[i] = '0';
        else { s[i]++; break; }
        i--;
    }
    if (i < 0) {                                  // 全 9 进位 → d1=1, E10++
        for (int j = P - 1; j > 0; j--) s[j] = s[j - 1];
        s[0] = '1';
        (*E10)++;
    }
    return P;
}

// 写 'e±XX' 指数 (至少 2 位)
__attribute__((unused))
static int nr_put_exp(char* out, int o, int E10) {
    out[o++] = (E10 < 0) ? '-' : '+';
    unsigned a = (unsigned)((E10 < 0) ? -E10 : E10);
    char t[4]; int m = 0;
    while (a) { t[m++] = (char)('0' + a % 10u); a /= 10u; }
    if (m < 2) { out[o++] = '0'; if (m == 0) out[o++] = '0'; }
    while (m) out[o++] = t[--m];
    return o;
}

// 浮点核心: 生成带符号的串 (不含宽度填充)。返回长度
__attribute__((unused))
static int nr_fmt_float(char* out, int cap, double x, char conv, int prec,
                        int plus, int space) {
    unsigned long long u;
    { union { double d; unsigned long long u; } cvt; cvt.d = x; u = cvt.u; }
    int neg = (int)(u >> 63);
    int be = (int)((u >> 52) & 0x7FF);

    char sig = 0;
    if (neg) sig = '-';
    else if (plus) sig = '+';
    else if (space) sig = ' ';

    int o = 0;
    #define NR_PUT(c) do { if (o < cap - 1) out[o] = (c); o++; } while (0)

    // 特殊值 (符号只在这里定一次; nan 按无符号打印 — Windows/MSVC 约定,
    // x86 默认 QNaN 符号位为 1, 不能按位直接打)
    if (be == 0x7FF) {
        unsigned long long fr = u & ((1ULL << 52) - 1);
        const char* w = fr ? "nan" : ((neg && !sig) ? "-inf" : "inf");
        if (sig && !fr) NR_PUT(sig);
        while (*w) NR_PUT(*w++);
        out[(o < cap - 1) ? o : cap - 1] = 0;
        return o;
    }

    int is_zero = (be == 0 && (u & ((1ULL << 52) - 1)) == 0);
    char s[1100]; int D = 0, E10 = 0;
    if (is_zero) {                                // ±0
        s[0] = '0'; D = 1; E10 = 0;
    } else {
        nr_dec(x, s, &D, &E10);
    }

    if (conv == 'g' || conv == 'G') {
        int P = (prec < 0) ? 6 : (prec == 0 ? 1 : prec);
        if (P > 760) P = 760;
        if (is_zero) {                            // 0 → "0"
            if (sig) NR_PUT(sig);
            NR_PUT('0');
        } else {
            D = nr_round_sig(s, D, P, &E10);
            int X = E10;
            if (X >= -4 && X < P) {               // f 风格
                int pf = P - 1 - X;
                int last = -1;                    // 最后一个非零 frac 位
                for (int i = 0; i < pf; i++) {
                    int idx = X + 1 + i;
                    if (idx >= 0 && idx < D && s[idx] != '0') last = i;
                }
                int ip = X + 1;                   // 整数位数
                if (sig) NR_PUT(sig);
                if (ip <= 0) NR_PUT('0');
                for (int i = 0; i < ip; i++) NR_PUT((i < D) ? s[i] : '0');
                if (last >= 0) {
                    NR_PUT('.');
                    for (int i = 0; i <= last; i++) {
                        int idx = X + 1 + i;
                        NR_PUT((idx >= 0 && idx < D) ? s[idx] : '0');
                    }
                }
            } else {                              // e 风格
                if (sig) NR_PUT(sig);
                NR_PUT(s[0]);
                int last = -1;
                for (int i = 0; i < P - 1; i++) if (s[1 + i] != '0') last = i;
                if (last >= 0) {
                    NR_PUT('.');
                    for (int i = 0; i <= last; i++) NR_PUT(s[1 + i]);
                }
                NR_PUT((conv == 'G') ? 'E' : 'e');
                o = nr_put_exp(out, o, X);
            }
        }
    }
    else if (conv == 'e' || conv == 'E') {
        int P = (prec < 0) ? 6 : prec;
        if (P > 760) P = 760;
        if (sig) NR_PUT(sig);
        if (is_zero) {
            NR_PUT('0');
            if (P > 0) { NR_PUT('.'); for (int i = 0; i < P; i++) NR_PUT('0'); }
            NR_PUT((conv == 'E') ? 'E' : 'e');
            o = nr_put_exp(out, o, 0);
        } else {
            D = nr_round_sig(s, D, P + 1, &E10);  // P 位小数 → P+1 位有效
            NR_PUT(s[0]);
            if (P > 0) {
                NR_PUT('.');
                for (int i = 0; i < P; i++) NR_PUT((1 + i < D) ? s[1 + i] : '0');
            }
            NR_PUT((conv == 'E') ? 'E' : 'e');
            o = nr_put_exp(out, o, E10);
        }
    }
    else {                                        // f
        int pf = (prec < 0) ? 6 : prec;
        if (pf > 500) pf = 500;
        if (is_zero) {
            if (sig) NR_PUT(sig);
            NR_PUT('0');
            if (pf > 0) { NR_PUT('.'); for (int i = 0; i < pf; i++) NR_PUT('0'); }
        } else {
            // 在小数点后 pf 位处舍入 (pos = 绝对数字位)
            int pos = E10 + 1 + pf;
            int Deff;
            if (pos <= 0) {
                if (pos == 0 && D > 0 && s[0] >= '5') { E10++; s[0] = '1'; Deff = 1; }
                else Deff = 0;                    // 舍入后为 0
            } else {
                char c = (pos < D) ? s[pos] : '0';
                if (c >= '5') {
                    int i = pos - 1;
                    while (i >= 0) {
                        if (s[i] == '9') s[i] = '0';
                        else { s[i]++; break; }
                        i--;
                    }
                    if (i < 0) {                  // 全 9 → 进位新位
                        int top = (pos < D) ? pos : D;
                        for (int j = top - 1; j > 0; j--) s[j] = s[j - 1];
                        s[0] = '1';
                        E10++;
                    }
                }
                Deff = pos;
            }
            if (sig) NR_PUT(sig);
            if (E10 < 0) NR_PUT('0');             // 整数部分
            else for (int i = 0; i <= E10; i++) NR_PUT((i < Deff && i < D) ? s[i] : '0');
            if (pf > 0) {
                NR_PUT('.');
                for (int i = 0; i < pf; i++) {
                    int idx = E10 + 1 + i;
                    NR_PUT((idx >= 0 && idx < Deff && idx < D) ? s[idx] : '0');
                }
            }
        }
    }
    #undef NR_PUT
    out[(o < cap - 1) ? o : cap - 1] = 0;
    return o;
}

// ── 格式化核心 ──
__attribute__((unused))
static int nr_vfmt(char* out, int cap, const char* fmt, va_list ap) {
    int n = 0;
    #define NR_PUT(c) do { if (n < cap - 1) out[n] = (c); n++; } while (0)
    for (; *fmt; fmt++) {
        if (*fmt != '%') { NR_PUT(*fmt); continue; }
        fmt++;
        if (!*fmt) break;
        // 标志
        int f_minus = 0, f_zero = 0, f_plus = 0, f_space = 0;
        for (;; fmt++) {
            if (*fmt == '-') f_minus = 1;
            else if (*fmt == '0') f_zero = 1;
            else if (*fmt == '+') f_plus = 1;
            else if (*fmt == ' ') f_space = 1;
            else break;
        }
        // 宽度
        int width = 0;
        while (*fmt >= '0' && *fmt <= '9') { width = width * 10 + (*fmt - '0'); fmt++; }
        // 精度
        int prec = -1;
        if (*fmt == '.') {
            fmt++; prec = 0;
            while (*fmt >= '0' && *fmt <= '9') { prec = prec * 10 + (*fmt - '0'); fmt++; }
        }
        // 长度前缀
        int lng = 0;
        while (*fmt == 'l') { lng++; fmt++; }
        char c = *fmt;
        if (!c) break;
        if (c == '%') { NR_PUT('%'); continue; }
        if (c == 'c') { NR_PUT((char)va_arg(ap, int)); continue; }
        if (c == 'f' || c == 'e' || c == 'g' || c == 'F' || c == 'E' || c == 'G') {
            double v = va_arg(ap, double);
            char core[1300];
            int cl = nr_fmt_float(core, (int)sizeof(core), v, c, prec, f_plus, f_space);
            if (width > cl) {                     // 宽度填充
                int pad = width - cl;
                if (f_minus) {
                    for (int i = 0; i < cl; i++) NR_PUT(core[i]);
                    while (pad--) NR_PUT(' ');
                } else if (f_zero) {
                    int p = (core[0] == '-' || core[0] == '+' || core[0] == ' ') ? 1 : 0;
                    for (int i = 0; i < p; i++) NR_PUT(core[i]);
                    while (pad--) NR_PUT('0');
                    for (int i = p; i < cl; i++) NR_PUT(core[i]);
                } else {
                    while (pad--) NR_PUT(' ');
                    for (int i = 0; i < cl; i++) NR_PUT(core[i]);
                }
            } else {
                for (int i = 0; i < cl; i++) NR_PUT(core[i]);
            }
            continue;
        }
        if (c == 's') {
            const char* s = va_arg(ap, const char*);
            if (!s) s = "(null)";
            while (*s) NR_PUT(*s++);
            continue;
        }
        if (c == 'p') {
            unsigned long long v = (unsigned long long)va_arg(ap, void*);
            const char* hex = "0123456789abcdef";
            NR_PUT('0'); NR_PUT('x');
            char tmp[16];
            int m = 0;
            if (!v) tmp[m++] = '0';
            while (v) { tmp[m++] = hex[v & 15]; v >>= 4; }
            while (m) NR_PUT(tmp[--m]);
            continue;
        }
        if (c == 'd' || c == 'i' || c == 'u' || c == 'x' || c == 'X') {
            unsigned long long v;
            int neg = 0;
            if (c == 'd' || c == 'i') {
                long long sv = lng ? va_arg(ap, long long) : va_arg(ap, int);
                if (sv < 0) { neg = 1; v = (unsigned long long)(-sv); }
                else v = (unsigned long long)sv;
            } else {
                v = lng ? va_arg(ap, unsigned long long) : va_arg(ap, unsigned int);
            }
            const char* hex = (c == 'X') ? "0123456789ABCDEF" : "0123456789abcdef";
            int base = (c == 'x' || c == 'X') ? 16 : 10;
            char tmp[24];
            int m = 0;
            if (!v) tmp[m++] = '0';
            while (v) { tmp[m++] = hex[v % (unsigned)base]; v /= (unsigned)base; }
            if (neg) NR_PUT('-');
            while (m) NR_PUT(tmp[--m]);
            continue;
        }
        // 未知转换符: 原样输出
        NR_PUT('%'); NR_PUT(c);
    }
    if (n < cap) out[(n < cap - 1) ? n : cap - 1] = '\0';
    return (n < cap) ? n : cap - 1;
    #undef NR_PUT
}

// sprintf: 返回写入字符数 (不含 NUL)
__attribute__((unused))
static int sprintf(char* buf, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = nr_vfmt(buf, 1 << 30, fmt, ap);
    va_end(ap);
    return n;
}

// snprintf: 最多写 cap-1 字符, 恒 NUL 结尾。返回"应有"长度 (不含 NUL,
// 被截断时大于实际写入 — 同 C 标准, 可用于先探长度)
__attribute__((unused))
static int vsnprintf(char* buf, size_t cap, const char* fmt, va_list ap) {
    if (!buf || cap < 1) { buf = 0; }
    int n = nr_vfmt(buf ? buf : (char*)"", buf && cap > 0 ? (int)cap : 1, fmt, ap);
    return n;
}

__attribute__((unused))
static int snprintf(char* buf, size_t cap, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, cap, fmt, ap);
    va_end(ap);
    return n;
}

// vprintf: printf 的 va_list 版本
__attribute__((unused))
static int vprintf(const char* fmt, va_list ap) {
    char buf[512];
    nr_vfmt(buf, sizeof(buf), fmt, ap);
    return printf("%s", buf);
}

// 控制台 printf: 按 '\n' 拆行送给内核 (内核按行显示, 满屏自动滚动)。
// win_id = -1 让内核自动路由到本程序的控制台窗口
__attribute__((unused))
static int printf(const char* fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    nr_vfmt(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    int total = 0;
    char* p = buf;
    while (*p) {
        char* nl = p;
        while (*nl && *nl != '\n') nl++;
        char saved = *nl;
        *nl = '\0';
        nep_win_print(-1, p);
        total += (int)nr_strlen(p);
        if (saved == '\0') break;
        total++;                       // 计入换行本身
        p = nl + 1;
        if (!saved) break;
    }
    return total;
}

// ── scanf ──

__attribute__((unused))
static int nr_isspace(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

// 从行缓冲解析下一个格式转换; 成功返回 1 并推进 *pp/*pap — 用宏简化
// (静态函数传 va_list* 在 C 里没问题, 直接这么写)

// 读一行并按 fmt 解析。返回成功转换的项数; 行为:
//  - 数字转换 (%d/%i/%u/%x) 自动跳过前导空白
//  - %s 读到空白为止 (可带宽度上限)
//  - %c 读一个字符
//  - 一行内可放多个转换 (空白分隔); 不匹配立即停止
static int scanf(const char* fmt, ...) {
    char line[256];
    long len = nep_read_line(line, (long)sizeof(line));
    if (len < 0) return -1;

    va_list ap;
    va_start(ap, fmt);
    int got = 0;
    const char* p = line;
    for (; *fmt; fmt++) {
        if (nr_isspace(*fmt)) continue;          // 格式里的空白: 跳过
        if (*fmt != '%') {                        // 字面字符: 必须匹配
            if (*p == *fmt) p++;
            break;
        }
        fmt++;
        // 宽度 (十进制数字)
        int width = 0;
        while (*fmt >= '0' && *fmt <= '9') { width = width * 10 + (*fmt - '0'); fmt++; }
        int lng = 0;
        while (*fmt == 'l') { lng++; fmt++; }
        char c = *fmt;
        if (!c) break;

        if (c == 's') {
            while (nr_isspace(*p)) p++;
            if (!*p) break;
            char* dst = va_arg(ap, char*);
            int max = width > 0 ? width : 255;
            int k = 0;
            while (*p && !nr_isspace(*p) && k < max) dst[k++] = *p++;
            dst[k] = '\0';
            got++;
            continue;
        }
        if (c == 'c') {
            if (!*p) break;
            char* dst = va_arg(ap, char*);
            *dst = *p++;
            got++;
            continue;
        }
        if (c == 'd' || c == 'i' || c == 'u' || c == 'x') {
            while (nr_isspace(*p)) p++;
            int neg = 0;
            if (*p == '-') { neg = 1; p++; }
            else if (*p == '+') p++;
            unsigned long long v = 0;
            int any = 0;
            if (c == 'x') {
                while (*p) {
                    int d;
                    if (*p >= '0' && *p <= '9') d = *p - '0';
                    else if (*p >= 'a' && *p <= 'f') d = *p - 'a' + 10;
                    else if (*p >= 'A' && *p <= 'F') d = *p - 'A' + 10;
                    else break;
                    v = v * 16 + (unsigned)d; any = 1; p++;
                }
            } else {
                while (*p >= '0' && *p <= '9') {
                    v = v * 10 + (unsigned)(*p - '0'); any = 1; p++;
                }
            }
            if (!any) break;
            if (c == 'u') {
                if (lng) { unsigned long* dst = va_arg(ap, unsigned long*); *dst = (unsigned long)v; }
                else { unsigned int* dst = va_arg(ap, unsigned int*); *dst = (unsigned int)v; }
            } else if (lng) {
                long* dst = va_arg(ap, long*);
                *dst = neg ? -(long)v : (long)v;
            } else {
                int* dst = va_arg(ap, int*);
                *dst = neg ? -(int)v : (int)v;
            }
            got++;
            continue;
        }
        break;   // 不支持的转换: 停
    }
    va_end(ap);
    return got;
}

// ── 数值 / 随机数工具 (系统可直接支持的部分) ──

static int atoi(const char* s) {
    int r = 0, neg = 0;
    while (*s == ' ' || *s == '\t') s++;
    if (*s == '-') { neg = 1; s++; }
    else if (*s == '+') s++;
    while (*s >= '0' && *s <= '9') { r = r * 10 + (*s - '0'); s++; }
    return neg ? -r : r;
}

static long atol(const char* s) {
    long r = 0; int neg = 0;
    while (*s == ' ' || *s == '\t') s++;
    if (*s == '-') { neg = 1; s++; }
    else if (*s == '+') s++;
    while (*s >= '0' && *s <= '9') { r = r * 10 + (*s - '0'); s++; }
    return neg ? -r : r;
}

#ifndef __NR_ABS_DEFINED
#define __NR_ABS_DEFINED 1
static int abs(int v) { return v < 0 ? -v : v; }
static long labs(long v) { return v < 0 ? -v : v; }
#endif

// LCG 伪随机数 (跨 syscall 状态保存在程序自己的静态区)
static unsigned long long __rand_seed = 0x2545F4914F6CDD1DULL;
static void srand(unsigned int seed) {
    __rand_seed = (unsigned long long)seed * 6364136223846793005ULL + 1442695040888963407ULL;
}
static int rand(void) {
    __rand_seed = __rand_seed * 6364136223846793005ULL + 1442695040888963407ULL;
    return (int)((__rand_seed >> 33) & 0x7FFFFFFF);
}

// ── 文件 I/O (fopen 系列, 建立在 nep_file_* syscall 之上) ──
// 路径形如 "AA://dir/file.txt" (AA..ZZ 为盘符, 系统盘 ZZ 只读 runst/system)。
// 没有 libc 的 stdin/stdout/stderr — 终端输出请用 printf。

#ifndef NULL
#define NULL ((void*)0)
#endif

#define EOF       (-1)
#define SEEK_SET  0
#define SEEK_CUR  1
#define SEEK_END  2
#define BUFSIZ    1024
#define L_tmpnam  260
#define _IONBF    0
#define _IOLBF    1
#define _IOFBF    2

typedef struct {
    int fd;    // 内核 fd (>=1); 0 = 空槽
    int eof;
    int err;
    int ungot; // ungetc 预读字符 (-1 = 无)
} FILE;

// 本编译单元的 FILE 池 (fopen 分配, fclose 归还; 上限 8 个同开)
// 显式落 .data: 平坦镜像不含 .bss, 零初始化静态变量默认进 .bss 未映射区
static __attribute__((section(".data"))) FILE __nr_file_pool[8];

// 打开文件。mode 同 C 标准: "r" / "w" / "a", 可加 '+' 和 'b'。
// 失败返回 NULL (文件不存在 / 盘符错 / fd 用尽)
__attribute__((unused))
static FILE* fopen(const char* path, const char* mode) {
    if (!path || !mode) return NULL;
    for (int i = 0; i < 8; i++) {
        if (__nr_file_pool[i].fd) continue;
        long fd = nep_file_open(path, mode);
        if (fd < 0) return NULL;
        __nr_file_pool[i].fd = (int)fd;
        __nr_file_pool[i].eof = 0;
        __nr_file_pool[i].err = 0;
        __nr_file_pool[i].ungot = -1;
        return &__nr_file_pool[i];
    }
    return NULL;
}

__attribute__((unused))
static int fclose(FILE* f) {
    if (!f || f->fd <= 0) return EOF;
    int rc = nep_file_close(f->fd);
    f->fd = 0;                                 // 槽位归还 (重复 fclose 安全)
    return rc == 0 ? 0 : EOF;
}

// 读 nmemb 个 size 字节的元素。返回完整元素个数 (不足 = EOF 或错误)
__attribute__((unused))
static size_t fread(void* ptr, size_t sz, size_t n, FILE* f) {
    if (!f || f->fd <= 0 || !ptr || sz == 0 || n == 0) return 0;
    unsigned long long total = (unsigned long long)sz * n;
    long r = nep_file_read(f->fd, ptr, (long)total);
    if (r < 0) { f->err = 1; return 0; }
    if ((unsigned long long)r < total) f->eof = 1;
    return (size_t)((unsigned long long)r / sz);
}

// 写 nmemb 个 size 字节的元素。返回完整元素个数
__attribute__((unused))
static size_t fwrite(const void* ptr, size_t sz, size_t n, FILE* f) {
    if (!f || f->fd <= 0 || !ptr || sz == 0 || n == 0) return 0;
    unsigned long long total = (unsigned long long)sz * n;
    long r = nep_file_write(f->fd, ptr, (long)total);
    if (r < 0) { f->err = 1; return 0; }
    if ((unsigned long long)r < total) f->err = 1;
    return (size_t)((unsigned long long)r / sz);
}

// 移动读写位置。0 = 成功, EOF = 失败 (成功时清除 eof 标志)
__attribute__((unused))
static int fseek(FILE* f, long off, int whence) {
    if (!f || f->fd <= 0) return EOF;
    long long r = nep_file_seek(f->fd, (long long)off, whence);
    if (r < 0) return EOF;
    f->eof = 0;
    return 0;
}

// 当前读写位置 (相对文件头); -1L = 失败
__attribute__((unused))
static long ftell(FILE* f) {
    if (!f || f->fd <= 0) return -1L;
    long long r = nep_file_seek(f->fd, 0, SEEK_CUR);
    return (long)r;
}

__attribute__((unused))
static void rewind(FILE* f) { fseek(f, 0L, SEEK_SET); }

__attribute__((unused))
static int feof(FILE* f)  { return (f && f->fd > 0) ? f->eof : 0; }
__attribute__((unused))
static int ferror(FILE* f) { return (f && f->fd > 0) ? f->err : 0; }

// 读一个字符 (返回 0..255), EOF = 结束/错误
__attribute__((unused))
static int fgetc(FILE* f) {
    unsigned char c;
    if (!f || f->fd <= 0) return EOF;
    if (f->ungot >= 0) { int r = f->ungot; f->ungot = -1; return r; }
    if (nep_file_read(f->fd, &c, 1) != 1) { f->eof = 1; return EOF; }
    return (int)c;
}
#define getc(f) fgetc(f)

// 写一个字符; EOF = 失败
__attribute__((unused))
static int fputc(int ch, FILE* f) {
    unsigned char c = (unsigned char)ch;
    if (!f || f->fd <= 0) return EOF;
    if (nep_file_write(f->fd, &c, 1) != 1) { f->err = 1; return EOF; }
    return (int)c;
}

// 读一行 (含 '\n', NUL 结尾; 最多 n-1 字符)。EOF 前无数据返回 NULL
__attribute__((unused))
static char* fgets(char* buf, int n, FILE* f) {
    if (!f || !buf || n < 1 || f->fd <= 0) return NULL;
    if (n == 1) { buf[0] = '\0'; return buf; }
    int i = 0;
    while (i < n - 1) {
        int c = fgetc(f);
        if (c == EOF) {
            if (i == 0) return NULL;
            break;
        }
        buf[i++] = (char)c;
        if (c == '\n') break;
    }
    buf[i] = '\0';
    return buf;
}

// 写字符串 (不加换行)。EOF = 失败
__attribute__((unused))
static int fputs(const char* s, FILE* f) {
    if (!f || !s || f->fd <= 0) return EOF;
    while (*s) {
        if (fputc(*s++, f) == EOF) return EOF;
    }
    return 0;
}

// 删除文件。0 = 成功, EOF = 失败
__attribute__((unused))
static int remove(const char* path) {
    return nep_file_delete(path) == 0 ? 0 : EOF;
}

// 重命名: 内核无 rename syscall — 失败 (os.rename 会报错)
__attribute__((unused))
static int rename(const char* from, const char* to) {
    (void)from; (void)to;
    return EOF;
}

// 字符推回 (一次一个; 推 EOF 为 no-op)。0 = 成功, EOF = 失败
__attribute__((unused))
static int ungetc(int ch, FILE* f) {
    if (!f || f->fd <= 0 || ch == EOF || f->ungot >= 0) return EOF;
    f->ungot = (unsigned char)ch;
    f->eof = 0;
    return ch;
}

// 无缓冲模型: fflush 恒成功 (数据已直接写内核)
__attribute__((unused))
static int fflush(FILE* f) {
    (void)f;
    return 0;
}

// setvbuf: 恒成功 (无用户可调缓冲)
__attribute__((unused))
static int setvbuf(FILE* f, char* buf, int mode, size_t size) {
    (void)f; (void)buf; (void)mode; (void)size;
    return 0;
}

// clearerr: 清除 eof/err 标志
__attribute__((unused))
static void clearerr(FILE* f) {
    if (f) { f->eof = 0; f->err = 0; }
}

// 关闭旧流并重新打开。失败返回 NULL (原流状态不定, 调用方不应再用)
__attribute__((unused))
static FILE* freopen(const char* path, const char* mode, FILE* f) {
    if (!f) return NULL;
    fclose(f);
    long fd = nep_file_open(path, mode);
    if (fd < 0) { f->fd = 0; return NULL; }
    f->fd = (int)fd;
    f->eof = 0;
    f->err = 0;
    f->ungot = -1;
    return f;
}

// 临时文件: 不支持 (内核无临时目录概念)
__attribute__((unused))
static FILE* tmpfile(void) {
    return NULL;
}

// 创建目录 (仅末级, 父目录须存在)。0 = 成功, EOF = 失败
__attribute__((unused))
static int mkdir(const char* path) {
    return nep_file_mkdir(path) == 0 ? 0 : EOF;
}

// ── 控制台输出 (建立在 printf / nep_win_print_color 之上) ──

// 彩色文字色值 (给 nep_win_print_color / cputs 用; 0xAARRGGBB)
#define CONSOLE_RED       0x00D02020
#define CONSOLE_GREEN     0x0020A020
#define CONSOLE_BLUE      0x002030C0
#define CONSOLE_YELLOW    0x00C0A000
#define CONSOLE_GRAY      0x00808080

// 彩色输出一行 (自动加换行; win_id<0 = 控制台)。返回 0 = 成功
__attribute__((unused))
static int cputs(int win_id, const char* s, unsigned color) {
    return nep_win_print_color(win_id, s, color);
}

// puts: 输出字符串 + 换行。非负 = 成功
__attribute__((unused))
static int puts(const char* s) {
    printf("%s\n", s ? s : "(null)");
    return 0;
}

// putchar: 输出一个字符。为避免逐字符 syscall 各自成行, 内部做行缓冲:
// 凑满一行或遇到 '\n' 才真正输出。返回写入的字符
static __attribute__((section(".data"))) char __pb[118];   // 内核单行缓冲 120 字节, 留 2 字节余量 (须落 .data, 见 FILE 池注释)
static __attribute__((section(".data"))) int  __pl = 0;

// 立即输出行缓冲中的残行 (不补换行) — 提示符/交互输出用它保证顺序
__attribute__((unused))
static void console_flush(void) {
    if (__pl > 0) {
        __pb[__pl] = '\0';
        nep_win_print(-1, __pb);
        __pl = 0;
    }
}

__attribute__((unused))
static int putchar(int ch) {
    char c = (char)ch;
    __pb[__pl++] = c;
    if (c == '\n' || __pl >= (int)sizeof(__pb) - 1) {
        __pb[__pl] = '\0';
        nep_win_print(-1, __pb);
        __pl = 0;
    }
    return (unsigned char)c;
}

// perror: 把错误信息打到控制台, 红字 (没有 libc 的 errno 机制,
// 所以只打印调用方给的描述串; 不传串则打印 "error")
__attribute__((unused))
static void perror(const char* s) {
    nep_win_print_color(-1, (s && *s) ? s : "error", CONSOLE_RED);
}

// ── fprintf / stdin / stdout / stderr (DOOM 等大程序用) ──
// 没有 libc 流概念: stdin/stdout/stderr 是 NULL 哨兵, fprintf 分流 —
// NULL → 控制台 printf, 其余 → fputs 写文件流
#define stdin  ((FILE*)0)
#define stdout ((FILE*)0)
#define stderr ((FILE*)0)

__attribute__((unused))
static int vfprintf(FILE* f, const char* fmt, va_list ap) {
    char buf[512];
    nr_vfmt(buf, sizeof(buf), fmt, ap);
    if (!f) return printf("%s", buf);      // stdout/stderr → 控制台
    return fputs(buf, f);
}

__attribute__((unused))
static int fprintf(FILE* f, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = vfprintf(f, fmt, ap);
    va_end(ap);
    return n;
}

// ── sscanf (最小实现): 支持 %d %i %u %x %o %s %c %% 与空白/字面匹配。
// DOOM 的配置/DEH 解析只用整数转换, 够用。返回成功赋值数
static int nr_ss_isspace(int c) {
    return c == ' ' || (c >= '\t' && c <= '\r');
}

__attribute__((unused))
static int vsscanf(const char* s, const char* fmt, va_list ap) {
    int assigned = 0;
    while (*fmt) {
        if (nr_ss_isspace(*fmt)) {              // 格式空白: 跳过输入空白
            while (nr_ss_isspace(*s)) s++;
            fmt++;
            continue;
        }
        if (*fmt != '%') {                      // 字面字符: 必须精确匹配
            if (*s != *fmt) break;
            s++; fmt++;
            continue;
        }
        fmt++;
        if (*fmt == '%') {                      // %%: 匹配一个 '%'
            if (*s != '%') break;
            s++; fmt++;
            continue;
        }
        int suppress = (*fmt == '*');
        if (suppress) fmt++;
        while (nr_ss_isspace(*s)) s++;          // 转换前跳空白
        if (*fmt == 'x' || *fmt == 'X') {
            unsigned v = 0; int n = 0;
            if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s += 2;
            while (*s) {
                int d;
                if (*s >= '0' && *s <= '9') d = *s - '0';
                else if (*s >= 'a' && *s <= 'f') d = *s - 'a' + 10;
                else if (*s >= 'A' && *s <= 'F') d = *s - 'A' + 10;
                else break;
                v = v * 16 + (unsigned)d; n++; s++;
            }
            if (!n) break;
            if (!suppress) { *va_arg(ap, unsigned*) = v; assigned++; }
        } else if (*fmt == 'd' || *fmt == 'i' || *fmt == 'u' || *fmt == 'o') {
            int neg = 0, base = 10, n = 0;
            long v = 0;
            if (*s == '-' || *s == '+') { neg = (*s == '-'); s++; }
            if (*fmt == 'o') base = 8;
            if (*fmt == 'i' && s[0] == '0') {    // %i: 0x 前缀 = 16 进制
                if (s[1] == 'x' || s[1] == 'X') { base = 16; s += 2; }
                else { base = 8; }
            }
            while (*s) {
                int d;
                if (*s >= '0' && *s <= '9') d = *s - '0';
                else if (*s >= 'a' && *s <= 'f') d = *s - 'a' + 10;
                else if (*s >= 'A' && *s <= 'F') d = *s - 'A' + 10;
                else break;
                if (d >= base) break;
                v = v * base + d; n++; s++;
            }
            if (!n) break;
            if (neg) v = -v;
            if (!suppress) { *va_arg(ap, int*) = (int)v; assigned++; }
        } else if (*fmt == 's') {
            if (!*s) break;
            if (!suppress) {
                char* d = va_arg(ap, char*);
                while (*s && !nr_ss_isspace(*s)) *d++ = *s++;
                *d = '\0';
                assigned++;
            } else {
                while (*s && !nr_ss_isspace(*s)) s++;
            }
        } else if (*fmt == 'c') {
            if (!*s) break;
            if (!suppress) { *va_arg(ap, char*) = *s; assigned++; }
            s++;
        } else {
            break;                               // 未知转换: 停止
        }
        fmt++;
    }
    return assigned;
}

__attribute__((unused))
static int sscanf(const char* s, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = vsscanf(s, fmt, ap);
    va_end(ap);
    return n;
}
