#pragma once
// ── NEP 类标准库: stdio ──
// 控制台 printf / scanf / sprintf, 建立在 nep.h 的 syscall 之上。
// 控制台程序不需要 nep_win_create — 第一次 printf 时内核自动开一个
// "<程序名> - 控制台" 窗口 (纯 print 的程序就是控制台程序)。
//
// 已支持的格式 (先慢慢来, 够用就好):
//   printf/	sprintf: %c %s %d %i %u %x %X %p %%  以及 l 前缀 (ld/lu/lx)
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
__attribute__((unused))
static int nr_radix(char* tmp, unsigned long long v, int base, const char* digits) {
    int m = 0;
    if (!v) tmp[m++] = '0';
    while (v) { tmp[m++] = digits[v % (unsigned)base]; v /= (unsigned)base; }
    return m;
}

static int nr_vfmt(char* out, int cap, const char* fmt, va_list ap) {
    int writable = (cap > 0);
    if (cap < 1) cap = 1;
    long n = 0;
    long room = (long)cap - 1;
    #define NR_PUT(c) do { if (writable && n < room) out[n] = (c); n++; } while (0)
    for (; *fmt; fmt++) {
        if (*fmt != '%') { NR_PUT(*fmt); continue; }
        fmt++;
        // 长度前缀
        int lng = 0;
        while (*fmt == 'l') { lng++; fmt++; }
        int prec = -1;
        if (*fmt == '.') {
            fmt++;
            prec = 0;
            while (*fmt >= '0' && *fmt <= '9') { prec = prec * 10 + (*fmt - '0'); fmt++; }
        }
        char c = *fmt;
        if (!c) break;
        if (c == '%') { NR_PUT('%'); continue; }
        if (c == 'c') { NR_PUT((char)va_arg(ap, int)); continue; }
        if (c == 's') {
            const char* s = va_arg(ap, const char*);
            if (!s) s = "(null)";
            int lim = 0;
            while (*s && (prec < 0 || lim < prec)) { NR_PUT(*s++); lim++; }
            continue;
        }
        if (c == 'p') {
            unsigned long long v = (unsigned long long)va_arg(ap, void*);
            NR_PUT('0'); NR_PUT('x');
            char tmp[20];
            int m = nr_radix(tmp, v, 16, "0123456789abcdef");
            while (m) NR_PUT(tmp[--m]);
            continue;
        }
        if (c == 'd' || c == 'i' || c == 'u' || c == 'o' || c == 'x' || c == 'X') {
            unsigned long long v;
            int neg = 0;
            if (c == 'd' || c == 'i') {
                long long sv = lng ? va_arg(ap, long long) : va_arg(ap, int);
                if (sv < 0) { neg = 1; v = (unsigned long long)(-(sv + 1)) + 1ULL; }
                else v = (unsigned long long)sv;
            } else {
                v = lng ? va_arg(ap, unsigned long long) : va_arg(ap, unsigned int);
            }
            const char* hex = (c == 'X') ? "0123456789ABCDEF" : "0123456789abcdef";
            int base = (c == 'o') ? 8 : ((c == 'x' || c == 'X') ? 16 : 10);
            char tmp[24];
            int m = nr_radix(tmp, v, base, hex);
            if (neg) NR_PUT('-');
            while (m) NR_PUT(tmp[--m]);
            continue;
        }
        if (c == 'f' || c == 'F') {
            double d = va_arg(ap, double);
            if (d != d) { NR_PUT('n'); NR_PUT('a'); NR_PUT('n'); continue; }
            if (d < 0 || (d == 0 && 1.0 / d < 0)) { NR_PUT('-'); d = -d; }
            if (d > 1e15) { NR_PUT('i'); NR_PUT('n'); NR_PUT('f'); continue; }
            unsigned long long ip = (unsigned long long)d;
            char tmp[24];
            int m = nr_radix(tmp, ip, 10, "0123456789");
            while (m) NR_PUT(tmp[--m]);
            int fd = (prec >= 0 && prec <= 9) ? prec : 6;
            if (fd > 0) {
                NR_PUT('.');
                double frac = d - (double)ip;
                for (int i = 0; i < fd; i++) {
                    frac *= 10.0;
                    int dig = (int)frac;
                    if (dig < 0) dig = 0;
                    if (dig > 9) dig = 9;
                    NR_PUT((char)('0' + dig));
                    frac -= (double)dig;
                }
            }
            continue;
        }
        if (c == 'e' || c == 'E') {
            double d = va_arg(ap, double);
            if (d != d) { NR_PUT('n'); NR_PUT('a'); NR_PUT('n'); continue; }
            int eneg = (d < 0 || (d == 0 && 1.0 / d < 0));
            if (eneg) d = -d;
            int ex = 0;
            if (d != 0) {
                while (d >= 10.0) { d *= 0.1; ex++; }
                while (d < 1.0) { d *= 10.0; ex--; }
                if (d >= 10.0) { d *= 0.1; ex++; }
            }
            int ed = (prec >= 0 && prec <= 9) ? prec : 6;
            char tmp[32];
            int m = 0;
            if (eneg) tmp[m++] = '-';
            int ip = (int)d;
            tmp[m++] = (char)('0' + ip);
            if (ed > 0) {
                tmp[m++] = '.';
                double frac = d - (double)ip;
                for (int i = 0; i < ed; i++) {
                    frac *= 10.0;
                    int dig = (int)frac;
                    if (dig < 0) dig = 0;
                    if (dig > 9) dig = 9;
                    tmp[m++] = (char)('0' + dig);
                    frac -= (double)dig;
                }
            }
            tmp[m++] = (c == 'E') ? 'E' : 'e';
            tmp[m++] = (ex < 0) ? '-' : '+';
            int ae = ex < 0 ? -ex : ex;
            if (ae >= 100) tmp[m++] = (char)('0' + (ae / 100) % 10);
            tmp[m++] = (char)('0' + (ae / 10) % 10);
            tmp[m++] = (char)('0' + ae % 10);
            for (int i = 0; i < m; i++) NR_PUT(tmp[i]);
            continue;
        }
        // 未知转换符: 原样输出
        NR_PUT('%'); NR_PUT(c);
    }
    if (writable && n < (long)cap) out[(n < room) ? n : room] = '\0';
    if (n > 2147483647L) return 2147483647;
    return (int)n;
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
    if (!buf || cap < 1) {
        char __nr_scratch[1];
        return nr_vfmt(__nr_scratch, 1, fmt, ap);
    }
    return nr_vfmt(buf, (int)cap, fmt, ap);
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
            int any = 0, over = 0;
            if (c == 'x') {
                while (*p) {
                    int d;
                    if (*p >= '0' && *p <= '9') d = *p - '0';
                    else if (*p >= 'a' && *p <= 'f') d = *p - 'a' + 10;
                    else if (*p >= 'A' && *p <= 'F') d = *p - 'A' + 10;
                    else break;
                    if (v > (0xFFFFFFFFFFFFFFFFULL - (unsigned)d) / 16) over = 1;
                    else v = v * 16 + (unsigned)d;
                    any = 1; p++;
                }
            } else {
                while (*p >= '0' && *p <= '9') {
                    unsigned d = (unsigned)(*p - '0');
                    if (v > (0xFFFFFFFFFFFFFFFFULL - d) / 10) over = 1;
                    else v = v * 10 + d;
                    any = 1; p++;
                }
            }
            if (!any || over) break;
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

static long long nr_parse_signed(const char* s, long long limit) {
    long long r = 0; int neg = 0; int clamped = 0;
    while (*s == ' ' || *s == '\t') s++;
    if (*s == '-') { neg = 1; s++; }
    else if (*s == '+') s++;
    while (*s >= '0' && *s <= '9') {
        int d = *s - '0';
        if (!clamped) {
            if (r > (limit - d) / 10) {
                r = limit;
                clamped = 1;
            } else {
                r = r * 10 + d;
            }
        }
        s++;
    }
    return neg ? -r : r;
}

static int atoi(const char* s) {
    long long r = nr_parse_signed(s, 2147483648LL);
    if (r > 2147483647LL) return 2147483647;
    if (r < (-2147483647LL - 1)) return -2147483647 - 1;
    return (int)r;
}

static long atol(const char* s) {
    long long r = nr_parse_signed(s, 9223372036854775807LL);
    if (r > 9223372036854775807LL) return (long)9223372036854775807LL;
    if (r < (-9223372036854775807LL - 1)) return (long)(-9223372036854775807LL - 1);
    return (long)r;
}

static int abs(int v) { return v < 0 ? -v : v; }
static long labs(long v) { return v < 0 ? -v : v; }

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

typedef struct {
    int fd;    // 内核 fd (>=1); 0 = 空槽
    int eof;
    int err;
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

__attribute__((unused))
static int nr_size_total(size_t sz, size_t n, unsigned long long* total) {
    long lim = (long)((unsigned long)~0UL >> 1);
    unsigned long long t = (unsigned long long)sz * n;
    if (n && t / n != sz) return 0;
    if (t > (unsigned long long)lim) return 0;
    *total = t;
    return 1;
}

// 读 nmemb 个 size 字节的元素。返回完整元素个数 (不足 = EOF 或错误)
__attribute__((unused))
static size_t fread(void* ptr, size_t sz, size_t n, FILE* f) {
    if (!f || f->fd <= 0 || !ptr || sz == 0 || n == 0) return 0;
    unsigned long long total;
    if (!nr_size_total(sz, n, &total)) { f->err = 1; return 0; }
    long r = nep_file_read(f->fd, ptr, (long)total);
    if (r < 0) { f->err = 1; return 0; }
    if ((unsigned long long)r < total) f->eof = 1;
    return (size_t)((unsigned long long)r / sz);
}

// 写 nmemb 个 size 字节的元素。返回完整元素个数
__attribute__((unused))
static size_t fwrite(const void* ptr, size_t sz, size_t n, FILE* f) {
    if (!f || f->fd <= 0 || !ptr || sz == 0 || n == 0) return 0;
    unsigned long long total;
    if (!nr_size_total(sz, n, &total)) { f->err = 1; return 0; }
    long r = nep_file_write(f->fd, ptr, (long)total);
    if (r < 0) { f->err = 1; return 0; }
    if ((unsigned long long)r < total) f->err = 1;
    return (size_t)((unsigned long long)r / sz);
}

__attribute__((unused))
static int vsprintf(char* buf, const char* fmt, va_list ap) {
    return nr_vfmt(buf, 1 << 30, fmt, ap);
}

__attribute__((unused))
static int vfprintf(FILE* f, const char* fmt, va_list ap) {
    if (!f || f->fd <= 0 || !fmt) return -1;
    char __nr_fbuf[1024];
    int n = nr_vfmt(__nr_fbuf, (int)sizeof(__nr_fbuf), fmt, ap);
    if (n < 0) return -1;
    if (n >= (int)sizeof(__nr_fbuf)) {
        f->err = 1;
        return -1;
    }
    if (n > 0) fwrite(__nr_fbuf, 1, (size_t)n, f);
    return n;
}

__attribute__((unused))
static int fprintf(FILE* f, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = vfprintf(f, fmt, ap);
    va_end(ap);
    return n;
}

__attribute__((unused))
static int fileno(FILE* f) {
    if (!f || f->fd <= 0) return -1;
    return f->fd;
}

__attribute__((unused))
static int setvbuf(FILE* f, char* buf, int mode, size_t size) {
    (void)buf;
    (void)size;
    if (!f || f->fd <= 0) return -1;
    if (mode != 0 && mode != 1 && mode != 2) return -1;
    return 0;
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
    if (r < 0) return -1L;
    if (r > (long long)((unsigned long)~0UL >> 1)) { f->err = 1; return -1L; }
    return (long)r;
}

__attribute__((unused))
static void rewind(FILE* f) { fseek(f, 0L, SEEK_SET); }

__attribute__((unused))
static int feof(FILE* f)  { return (f && f->fd != 0) ? f->eof : 0; }
__attribute__((unused))
static int ferror(FILE* f) { return (f && f->fd != 0) ? f->err : 0; }

// 读一个字符 (返回 0..255), EOF = 结束/错误
__attribute__((unused))
static int fgetc(FILE* f) {
    unsigned char c;
    if (!f || f->fd <= 0) return EOF;
    if (nep_file_read(f->fd, &c, 1) != 1) { f->eof = 1; return EOF; }
    return (int)c;
}

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
    if (!path) return EOF;
    return nep_file_delete(path) == 0 ? 0 : EOF;
}

// 创建目录 (仅末级, 父目录须存在)。0 = 成功, EOF = 失败
__attribute__((unused))
static int mkdir(const char* path) {
    if (!path) return EOF;
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
    if (!s) return EOF;
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
__attribute__((unused))
static int putchar(int ch) {
    static __attribute__((section(".data"))) char __pb[118];   // 内核单行缓冲 120 字节, 留 2 字节余量 (须落 .data, 见 FILE 池注释)
    static __attribute__((section(".data"))) int  __pl = 0;
    char c = (char)ch;
    if (__pl >= (int)sizeof(__pb) - 1) {
        __pb[(int)sizeof(__pb) - 1] = '\0';
        nep_win_print(-1, __pb);
        __pl = 0;
    }
    __pb[__pl++] = c;
    if (c == '\n') {
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
