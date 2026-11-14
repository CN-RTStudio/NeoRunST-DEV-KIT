#pragma once
// ── NEP errno.h (freestanding shim) ──
// 单编译单元方案 (onelua.c): errno 做成静态全局即可。
// 内核不提供真实错误码, strerror 返回通用描述。

#define EINVAL 22
#define ENOENT 2
#define EACCES 13
#define EEXIST 17
#define EBADF  9
#define ENOMEM 12
#define EIO    5
#define EISDIR 21
#define ENOSPC 28

static __attribute__((section(".data"))) int nr_errno = 0;
#define errno nr_errno

static __attribute__((unused)) const char* strerror(int e) {
    (void)e;
    return "error";
}
