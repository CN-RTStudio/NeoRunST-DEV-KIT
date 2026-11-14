#pragma once
// ── NEP setjmp/longjmp (freestanding 手写, x86-64 SysV, AT&T 语法) ──
// jmp_buf 保存 callee-saved 寄存器 + rsp + 返回地址。
// 裸函数 (naked, 无 prologue), 进函数时 rsp 指向返回地址。
//
// 语义注意:
//  - setjmp 返回 0 (直接调用) / val (longjmp 跳回, val==0 时返回 1)
//  - 它所在的函数里, setjmp 调用点之后的局部变量只有存在内存/
//    被恢复寄存器里的才可靠 (与标准一致)
//  - FPU/SSE 状态不保存 (与 x86-64 SysV ABI 一致, 无 fenv 用法可忽略)

typedef long jmp_buf[8];            // rbx rbp r12 r13 r14 r15 rsp rip

__attribute__((naked, returns_twice))
static int setjmp(jmp_buf env) {
    __asm__ volatile(
        "mov %rbx, 0(%rdi)\n\t"
        "mov %rbp, 8(%rdi)\n\t"
        "mov %r12, 16(%rdi)\n\t"
        "mov %r13, 24(%rdi)\n\t"
        "mov %r14, 32(%rdi)\n\t"
        "mov %r15, 40(%rdi)\n\t"
        "mov %rsp, 48(%rdi)\n\t"      // 当前 rsp (指向返回地址)
        "mov (%rsp), %rax\n\t"        // 返回地址 = longjmp 跳回点
        "mov %rax, 56(%rdi)\n\t"
        "xor %eax, %eax\n\t"          // 直接调用返回 0
        "ret\n\t");
}

__attribute__((naked, noreturn))
static void longjmp(jmp_buf env, long val) {
    __asm__ volatile(
        "mov %rsi, %rax\n\t"          // val (第二参数; rdi = env, 恢复 rsp 前有效)
        "mov 0(%rdi),  %rbx\n\t"
        "mov 8(%rdi),  %rbp\n\t"
        "mov 16(%rdi), %r12\n\t"
        "mov 24(%rdi), %r13\n\t"
        "mov 32(%rdi), %r14\n\t"
        "mov 40(%rdi), %r15\n\t"
        "mov 48(%rdi), %rsp\n\t"      // rsp 回到 setjmp 时位置 ([rsp] 已是保存的 rip)
        "mov 56(%rdi), %rsi\n\t"
        "mov %rsi, (%rsp)\n\t"        // 显式放回栈顶, ret 跳过去
        "test %rax, %rax\n\t"
        "jnz 1f\n\t"
        "mov $1, %rax\n\t"            // val == 0 时按标准返回 1
        "1:\n\t"
        "ret\n\t");
}
