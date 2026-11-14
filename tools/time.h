#pragma once
// ── NEP time.h (freestanding shim) ──
// 无 RTC: time() 返回开机以来的秒数 (uptime)。
// 无时区: localtime == gmtime (UTC)。
// 全部实现做成 header static (onelua 单编译单元)。

#include "nep.h"

#ifndef NULL
#define NULL ((void*)0)
#endif

typedef long long time_t;                 // Lua: time_t 传给 lua_Integer
typedef unsigned long clock_t;
#define CLOCKS_PER_SEC 1000               // nep_ticks = 毫秒

struct tm {
    int tm_sec;    // 0-60
    int tm_min;    // 0-59
    int tm_hour;   // 0-23
    int tm_mday;   // 1-31
    int tm_mon;    // 0-11
    int tm_year;   // 年-1900
    int tm_wday;   // 0-6 (周日=0)
    int tm_yday;   // 0-365
    int tm_isdst;
};

// ── civil_from_days (Hinnant): 1970-01-01 起的天数 → 年月日 ──
static __attribute__((unused)) void nr_civil_from_days(long long z, int* y, int* m, int* d) {
    z += 719468;
    long long era = (z >= 0 ? z : z - 146096) / 146097;
    unsigned doe = (unsigned)(z - era * 146097);                 // [0, 146096]
    unsigned yoe = (doe - doe/1460 + doe/36524 - doe/146096) / 365; // [0, 399]
    long long yr = (long long)yoe + era * 400;
    unsigned doy = doe - (365*yoe + yoe/4 - yoe/100);            // [0, 365]
    unsigned mp = (5*doy + 2)/153;                               // [0, 11]
    unsigned dd = doy - (153*mp+2)/5 + 1;                        // [1, 31]
    unsigned mm = (mp < 10) ? mp+3 : mp-9;                       // [1, 12]
    *y = (int)(yr + ((mm <= 2) ? 1 : 0));
    *m = (int)mm;
    *d = (int)dd;
}

// days_from_civil (Hinnant): 年月日 → 1970-01-01 起的天数
static __attribute__((unused)) long long nr_days_from_civil(int y, int m, int d) {
    y -= (m <= 2);
    long long era = (y >= 0 ? y : y-399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);                    // [0, 399]
    unsigned doy = (153u*(m + (m > 2 ? -3 : 9)) + 2)/5 + (unsigned)d - 1;
    unsigned doe = yoe * 365 + yoe/4 - yoe/100 + doy;
    return era * 146097 + (long long)doe - 719468;
}

static const char nr_wday_name[7][10] = {
    "Sunday","Monday","Tuesday","Wednesday","Thursday","Friday","Saturday"
};
static const char nr_mon_name[12][10] = {
    "January","February","March","April","May","June",
    "July","August","September","October","November","December"
};
static const char nr_wday_abbr[7][4] = {
    "Sun","Mon","Tue","Wed","Thu","Fri","Sat"
};
static const char nr_mon_abbr[12][4] = {
    "Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"
};

static __attribute__((unused)) void nr_fill_tm(struct tm* tm, time_t t) {
    long long days = t / 86400;
    long long rem = t % 86400;
    if (rem < 0) { rem += 86400; days--; }
    int y, m, d;
    nr_civil_from_days(days, &y, &m, &d);
    tm->tm_hour = (int)(rem / 3600);
    tm->tm_min  = (int)((rem / 60) % 60);
    tm->tm_sec  = (int)(rem % 60);
    tm->tm_year = y - 1900;
    tm->tm_mon  = m - 1;
    tm->tm_mday = d;
    tm->tm_wday = (int)((days + 4) % 7);          // 1970-01-01 是周四
    if (tm->tm_wday < 0) tm->tm_wday += 7;
    tm->tm_yday = (int)(days - nr_days_from_civil(y, 1, 1));
    tm->tm_isdst = 0;
}

// ── 对外接口 ──

static __attribute__((unused)) time_t time(time_t* out) {
    time_t t = (time_t)(nep_ticks() / 1000);
    if (out) *out = t;
    return t;
}

static __attribute__((unused)) clock_t clock(void) {
    return (clock_t)nep_ticks();
}

static __attribute__((unused)) double difftime(time_t a, time_t b) {
    return (double)a - (double)b;
}

static __attribute__((unused)) long long nr_timegm_days(int y, int m, int d) {
    return nr_days_from_civil(y, m, d);
}

static __attribute__((unused)) struct tm* gmtime_r(const time_t* t, struct tm* out) {
    if (!t || !out) return NULL;
    nr_fill_tm(out, *t);
    return out;
}

static __attribute__((unused)) struct tm* localtime_r(const time_t* t, struct tm* out) {
    return gmtime_r(t, out);
}

static int nr_put_2(char* b, int v) {
    b[0] = (char)('0' + (v / 10) % 10);
    b[1] = (char)('0' + v % 10);
    return 2;
}

static __attribute__((unused)) char* asctime_r(const struct tm* tm, char* buf) {
    if (!tm || !buf) return NULL;
    int o = 0;
    const char* w = nr_wday_abbr[tm->tm_wday % 7];
    for (int i = 0; w[i] && i < 3; i++) buf[o++] = w[i];
    buf[o++] = ' ';
    const char* mn = nr_mon_abbr[tm->tm_mon % 12];
    for (int i = 0; mn[i] && i < 3; i++) buf[o++] = mn[i];
    buf[o++] = ' ';
    buf[o] = (char)('0' + (tm->tm_mday / 10) % 10); o++;
    buf[o] = (char)('0' + tm->tm_mday % 10); o++;
    buf[o++] = ' ';
    o += nr_put_2(buf + o, tm->tm_hour);
    buf[o++] = ':';
    o += nr_put_2(buf + o, tm->tm_min);
    buf[o++] = ':';
    o += nr_put_2(buf + o, tm->tm_sec);
    buf[o++] = ' ';
    int y = tm->tm_year + 1900;
    char n[6];
    int k = 0;
    if (y <= 0) n[k++] = '0';
    while (y > 0) { n[k++] = (char)('0' + y % 10); y /= 10; }
    while (k > 0 && o < 25) buf[o++] = n[--k];
    buf[o++] = '\n';
    buf[o] = 0;
    return buf;
}

static __attribute__((unused)) __attribute__((section(".data"))) char nr_asctime_buf[32];

static __attribute__((unused)) char* asctime(const struct tm* tm) {
    return asctime_r(tm, nr_asctime_buf);
}

static __attribute__((unused)) char* ctime_r(const time_t* t, char* buf) {
    if (!t) return NULL;
    struct tm tmp;
    if (!gmtime_r(t, &tmp)) return NULL;
    return asctime_r(&tmp, buf);
}

static __attribute__((unused)) __attribute__((section(".data"))) char nr_ctime_buf[32];

static __attribute__((unused)) char* ctime(const time_t* t) {
    return ctime_r(t, nr_ctime_buf);
}

static __attribute__((unused)) time_t timegm(struct tm* tm) {
    if (!tm) return 0;
    long long days = nr_days_from_civil(tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday);
    long long secs = days * 86400 + (long long)tm->tm_hour * 3600
                   + (long long)tm->tm_min * 60 + tm->tm_sec;
    time_t out = (time_t)secs;
    gmtime_r(&out, tm);
    return out;
}


static __attribute__((section(".data"))) struct tm nr_gmtime_buf;
static __attribute__((section(".data"))) struct tm nr_localtime_buf;

static __attribute__((unused)) struct tm* gmtime(const time_t* t) {
    return gmtime_r(t, &nr_gmtime_buf);
}
static __attribute__((unused)) struct tm* localtime(const time_t* t) {
    return gmtime_r(t, &nr_localtime_buf);
}

// mktime: 按字段构造时间 (越界字段自动规范化, UTC)
static __attribute__((unused)) time_t mktime(struct tm* tm) {
    long long days = nr_days_from_civil(tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday);
    time_t t = days * 86400LL
             + tm->tm_hour * 3600LL
             + tm->tm_min  * 60LL
             + tm->tm_sec;
    nr_fill_tm(tm, t);                            // 规范化回填 (os.time 语义)
    return t;
}

// ── strftime 子集 (os.date 用): %Y %m %d %H %M %S %y %p %I
//    %A %a %B %b %c %x %X %j %% ──
static __attribute__((unused)) size_t strftime(char* s, size_t max,
                                               const char* fmt, const struct tm* tm) {
    size_t o = 0;
    #define NR_PUT(c) do { if (o + 1 >= max) { s[o] = 0; return 0; } s[o++] = (char)(c); } while (0)
    #define NR_PUTS(str) do { for (const char* p_ = (str); *p_; p_++) NR_PUT(*p_); } while (0)
    #define NR_NUM2(v) do { int v_ = (v); NR_PUT((char)('0' + (v_ / 10) % 10)); NR_PUT((char)('0' + v_ % 10)); } while (0)

    for (const char* p = fmt; *p; p++) {
        if (*p != '%') { NR_PUT(*p); continue; }
        p++;
        switch (*p) {
        case 'Y': {                              // 4 位年
            int y = tm->tm_year + 1900;
            if (y < 0) y = 0;
            char b[6]; int k = 0;
            do { b[k++] = (char)('0' + y % 10); y /= 10; } while (y);
            while (k) NR_PUT(b[--k]);
            break;
        }
        case 'y': NR_NUM2(tm->tm_year % 100); break;
        case 'm': NR_NUM2(tm->tm_mon + 1); break;
        case 'd': NR_NUM2(tm->tm_mday); break;
        case 'H': NR_NUM2(tm->tm_hour); break;
        case 'M': NR_NUM2(tm->tm_min); break;
        case 'S': NR_NUM2(tm->tm_sec); break;
        case 'I': {
            int h = tm->tm_hour % 12; if (h == 0) h = 12;
            NR_NUM2(h); break;
        }
        case 'p': NR_PUTS(tm->tm_hour < 12 ? "AM" : "PM"); break;
        case 'A': NR_PUTS(nr_wday_name[tm->tm_wday % 7]); break;
        case 'a': NR_PUTS(nr_wday_abbr[tm->tm_wday % 7]); break;
        case 'B': NR_PUTS(nr_mon_name[tm->tm_mon % 12]); break;
        case 'b': NR_PUTS(nr_mon_abbr[tm->tm_mon % 12]); break;
        case 'c':                                 // %a %b %d %H:%M:%S %Y
            NR_PUTS(nr_wday_abbr[tm->tm_wday % 7]); NR_PUT(' ');
            NR_PUTS(nr_mon_abbr[tm->tm_mon % 12]); NR_PUT(' ');
            NR_NUM2(tm->tm_mday); NR_PUT(' ');
            NR_NUM2(tm->tm_hour); NR_PUT(':');
            NR_NUM2(tm->tm_min); NR_PUT(':');
            NR_NUM2(tm->tm_sec); NR_PUT(' ');
            { int y = tm->tm_year + 1900; if (y < 0) y = 0;
              char b[6]; int k = 0;
              do { b[k++] = (char)('0' + y % 10); y /= 10; } while (y);
              while (k) NR_PUT(b[--k]); }
            break;
        case 'x':                                 // %m/%d/%y
            NR_NUM2(tm->tm_mon + 1); NR_PUT('/');
            NR_NUM2(tm->tm_mday); NR_PUT('/');
            NR_NUM2(tm->tm_year % 100); break;
        case 'X':                                 // %H:%M:%S
            NR_NUM2(tm->tm_hour); NR_PUT(':');
            NR_NUM2(tm->tm_min); NR_PUT(':');
            NR_NUM2(tm->tm_sec); break;
        case 'j': {                               // 年内天 (3 位, 1 起)
            int doy = tm->tm_yday + 1;
            NR_PUT((char)('0' + (doy / 100) % 10));
            NR_PUT((char)('0' + (doy / 10) % 10));
            NR_PUT((char)('0' + doy % 10));
            break;
        }
        case '%': NR_PUT('%'); break;
        case 0:   p--; break;                     // 尾部孤立 '%'
        default:  NR_PUT('%'); NR_PUT(*p); break;
        }
    }
    s[o] = 0;
    return o;
}
