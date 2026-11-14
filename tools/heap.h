#pragma once
// ── NEP 用户态堆 (malloc/free/realloc/calloc) ──
// 存储布局: 每个任务有一段独立虚拟地址区 [NEP_HEAP_BASE, +1MB),
// 物理页由内核按需映射 (syscall 43 nep_heap_grow, 只增不减,
// 任务退出时内核一次性回收全部页表/物理页)。
// 分配算法: 分段链表 (header 前置, 相邻空闲合并, 不够时扩堆),
// 与内核 heap.c 同源, 但扩容走 syscall 且全部状态放在 .data
// (平坦镜像不含 .bss, 未映射!)。
//
// 用法: #include "heap.h" 后直接 malloc/free/calloc/realloc。
// 上限 1MB/进程, 超限 malloc 返回 0 (注意: 返回 0, 不是 NULL 宏)。

#include "nep.h"

#define NEP_HEAP_BASE_VA 0x0000300000000000ULL
#define NEP_HEAP_CHUNK   0x10000        // 每次扩堆 64KB
#define NEP_HEAP_LIMIT   (256UL * 0x1000 * 0x1000)   // 1MB (与内核一致)

typedef struct HeapSeg {
    unsigned long size;             // 数据区字节数 (不含 header)
    struct HeapSeg* head;           // 后一段 (地址增长方向)
    struct HeapSeg* tail;           // 前一段
    unsigned long free;             // 1 = 空闲
} HeapSeg;                          // 32 字节, 数据区 16 字节对齐保持

// ── 堆状态: 必须落 .data (平坦镜像不映射 .bss, 零初始化默认进 .bss!) ──
static __attribute__((section(".data"))) HeapSeg*    h__first = 0;
static __attribute__((section(".data"))) HeapSeg*    h__last  = 0;
static __attribute__((section(".data"))) unsigned long h__mapped = 0;  // 已映射字节数
static __attribute__((section(".data"))) int          h__inited = 0;

// 堆扩容到 total 字节 (内部用)。0 = 成功
static int h__grow(unsigned long total) {
    if (total > NEP_HEAP_LIMIT) return -1;
    if (nep_heap_grow((long)total) != 0) return -1;
    h__mapped = total;
    return 0;
}

static void h__merge_back(HeapSeg* self);

// 追加一段空闲区 (数据段), 失败返回 0
static HeapSeg* h__expand(unsigned long datasz) {
    unsigned long old = h__mapped;
    unsigned long want = old + ((datasz + sizeof(HeapSeg) + 0x10 + NEP_HEAP_CHUNK - 1)
                                / NEP_HEAP_CHUNK) * NEP_HEAP_CHUNK;
    if (h__grow(want) != 0) return 0;

    HeapSeg* seg = (HeapSeg*)(NEP_HEAP_BASE_VA + old);
    seg->size = (h__mapped - old) - sizeof(HeapSeg);
    seg->head = 0;
    seg->tail = 0;
    seg->free = 1;

    if (!h__first) { h__first = seg; h__last = seg; return seg; }
    // 挂到链表尾部 (expand 一定在最高地址, 若尾段紧邻则直接拼接)
    if ((HeapSeg*)((char*)h__last + sizeof(HeapSeg) + h__last->size) == seg) {
        h__last->head = seg;
        seg->tail = h__last;
    } else {
        seg->tail = h__last;        // 理论不会发生 (堆区连续映射), 保险
        h__last->head = seg;
    }
    h__last = seg;
    h__merge_back(seg);             // 尾段空闲且相邻 → 直接并入 (减碎片)
    return h__last;
}

// 合并 self 与其下一段 (head) 为一段; 下一段必须空闲
static void h__merge_fwd(HeapSeg* self) {
    HeapSeg* nxt = self->head;
    if (!nxt || !nxt->free) return;
    if (nxt->head) nxt->head->tail = self;
    self->size = self->size + nxt->size + sizeof(HeapSeg);
    self->head = nxt->head;
    if (h__last == nxt) h__last = self;
}

static void h__merge_back(HeapSeg* self) {
    if (self->tail && self->tail->free) h__merge_fwd(self->tail);
}

// 切分: self 保留 keep 字节数据区, 剩余成为新的空闲段。
// 剩余不足一段 (sizeof(HeapSeg)+16) 则不切
static void h__split(HeapSeg* self, unsigned long keep) {
    long rest = (long)self->size - (long)keep - (long)sizeof(HeapSeg);
    if (rest < 0x20) return;
    HeapSeg* nw = (HeapSeg*)((char*)self + sizeof(HeapSeg) + keep);
    if (self->head) self->head->tail = nw;
    nw->head = self->head;
    nw->tail = self;
    nw->size = (unsigned long)rest;
    nw->free = 1;
    self->head = nw;
    self->size = keep;
    if (h__last == self) h__last = nw;
}

static void* malloc(unsigned long size) {
    if (size == 0) return 0;
    if (size & 0xF) size = (size & ~(unsigned long)0xF) + 0x10;

    if (!h__inited) {                       // 懒初始化: 首次映射 64KB
        if (h__grow(NEP_HEAP_CHUNK) != 0) return 0;
        HeapSeg* seg = (HeapSeg*)NEP_HEAP_BASE_VA;
        seg->size = h__mapped - sizeof(HeapSeg);
        seg->head = 0; seg->tail = 0; seg->free = 1;
        h__first = seg; h__last = seg;
        h__inited = 1;
    }

    for (;;) {
        HeapSeg* cur = h__first;
        while (cur) {
            if (cur->free) {
                if (cur->size > size) {
                    h__split(cur, size);
                    cur->free = 0;
                    return (void*)(cur + 1);
                }
                if (cur->size == size) {
                    cur->free = 0;
                    return (void*)(cur + 1);
                }
            }
            cur = cur->head;
        }
        // 没找到: 扩堆重扫 (扩容大小 = 本次需求 + 一个 chunk 余量)
        HeapSeg* seg = h__expand(size);
        if (!seg) return 0;                 // 1MB 上限 / 物理页耗尽
    }
}

static void free(void* p) {
    if (!p) return;
    HeapSeg* seg = (HeapSeg*)p - 1;
    seg->free = 1;
    h__merge_fwd(seg);
    h__merge_back(seg);
}

static void* calloc(unsigned long n, unsigned long size) {
    unsigned long total = n * size;
    void* p = malloc(total);
    if (!p) return 0;
    for (unsigned long i = 0; i < total; i++) ((char*)p)[i] = 0;
    return p;
}

static void* realloc(void* p, unsigned long size) {
    if (!p) return malloc(size);
    if (size == 0) { free(p); return 0; }
    HeapSeg* seg = (HeapSeg*)p - 1;
    if (size & 0xF) size = (size & ~(unsigned long)0xF) + 0x10;

    // 原地扩: 下一段空闲且合并后够大 → 吸收 (尽量不动数据)
    HeapSeg* nxt = seg->head;
    if (nxt && nxt->free) {
        unsigned long merged = seg->size + nxt->size + sizeof(HeapSeg);
        if (merged == size || merged > size + sizeof(HeapSeg) + 0x10) {
            if (nxt->head) nxt->head->tail = seg;
            seg->size = merged;
            seg->head = nxt->head;
            if (h__last == nxt) h__last = seg;
            h__split(seg, size);
            return p;
        }
        if (merged == size) {
            if (nxt->head) nxt->head->tail = seg;
            seg->size = merged;
            seg->head = nxt->head;
            if (h__last == nxt) h__last = seg;
            return p;
        }
    }
    if (seg->size >= size) {                // 缩小: 切掉尾巴
        h__split(seg, size);
        return p;
    }
    // 新分配 + 拷贝 + 释放 (旧数据大小 = seg->size)
    void* nw = malloc(size);
    if (!nw) return 0;
    unsigned long old = seg->size;
    for (unsigned long i = 0; i < old; i++) ((char*)nw)[i] = ((char*)p)[i];
    free(p);
    return nw;
}
