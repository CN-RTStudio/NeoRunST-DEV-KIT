// ── NEP crt0: 内核从这里进入 (镜像偏移 0), 调 nep_main 后自动退出 ──
#include "heap.h"

// 用户堆全局状态 (heap.h 的 extern 声明在这里定义)。
// 显式置零 + .data section: 平坦镜像不映射 .bss, 零初始化会进 .bss!
__attribute__((section(".data"))) HeapSeg*      h__first  = 0;
__attribute__((section(".data"))) HeapSeg*      h__last   = 0;
__attribute__((section(".data"))) unsigned long h__mapped = 0;
__attribute__((section(".data"))) int           h__inited = 0;

extern int nep_main(void);

void _start(void) {
    nep_main();
    nep_exit();   // noreturn
}
