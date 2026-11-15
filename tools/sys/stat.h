#pragma once
// ── NEP sys/stat.h (freestanding shim) ──
// 无文件元数据接口; mkdir 用 stdio.h 的单参数版本
#define S_IFDIR  0040000
#define S_IFREG  0100000
#define S_IFMT   0170000
