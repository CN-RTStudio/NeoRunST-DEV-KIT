#pragma once
// ── NEP fcntl.h (freestanding shim) ──
// open() 走 stdio.h 的 fopen (nep_file_open syscall); 这里只满足
// DOOM 等大程序的 #include <fcntl.h>。O_RDONLY 等给出无意义占位。
#define O_RDONLY 0
#define O_WRONLY 1
#define O_RDWR   2
#define O_CREAT  0100
#define O_TRUNC  01000
#define O_APPEND 02000
