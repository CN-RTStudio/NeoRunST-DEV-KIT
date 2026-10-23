#pragma once
// ── NEP 程序用户侧 ABI 头 ──
// NEP = NeoRunST Executable Program, 运行在 ring3, 每进程独立地址空间。
//
// 注意:
//  - 完全 freestanding: 没有 libc, 没有全局构造, 请只依赖自己的代码
//  - 抢占式调度: 纯死循环也会被 PIT 时间片强制打断, GUI 不会卡死
//  - FPU/SSE 寄存器跨 syscall 保存/恢复, 可放心在程序里用浮点
//  - 更高层封装 (printf/scanf) 在 stdio.h
//
// syscall 表 (int 0x80, rax=编号):
//   0 EXIT        — 结束程序
//   1 WIN_CREATE  — rdi=标题, rsi=宽, rdx=高 → rax=窗口 id (<0 失败)
//   2 WIN_PRINT   — rdi=窗口 id (负数=控制台), rsi=文本行; 满屏自动滚动
//   3 SET_ONCLOSE — rdi=窗口 id, rsi=回调, rdx=回调返回跳板
//   4 READ_LINE   — rdi=缓冲, rsi=长度 → rax=行长度; 无整行输入则阻塞
//                   (内核回退 rip 重执行本 syscall, 等待不耗时间片)
//   5 WIN_CLEAR   — rdi=窗口 id (负数=控制台)
//   6 WIDGET_ADD  — rdi=窗口 id, rsi=&nep_widget_info → rax=控件 id (<0 失败)
//   7 WIDGET_SET_TEXT — rdi=窗口 id, rsi=控件 id, rdx=文本
//   8 WIDGET_GET_TEXT — rdi=窗口 id, rsi=控件 id, rdx=缓冲, r10=长度 → rax=长度
//   9 EVENT_WAIT  — 事件等待 (控件程序主循环): 永远挂起; 按钮/关闭回调由
//                   内核改写上下文直接进入, 回调返回后经跳板落回这里
//  10 MSGBOX      — rdi=标题, rsi=内容, rdx=按钮类型(0-4) → rax=按钮序号
//                   (0 起, 左→右); X 关闭消息框 = -1。类型:
//                   0=[确定] 1=[确定,取消] 2=[忽略,重试,取消]
//                   3=[忽略,重试] 4=[重试,取消]
//  11 WIDGET_DESTROY — rdi=窗口 id, rsi=控件 id → 销毁该控件 (id 不复用)
//
// 控制台程序模型: 不调用 nep_win_create、直接 printf 的程序, 内核会
// 自动给它开一个 "<程序名> - 控制台" 窗口, 键盘输入在窗口聚焦时进
// 输入缓冲, scanf/读取函数按行取走。

// 结束程序 (永远不会返回)
static inline void __attribute__((noreturn)) nep_exit(void) {
    __asm__ volatile("int $0x80" :: "a"(0L));
    for (;;) __asm__ volatile("hlt");
}

// 创建窗口, 返回窗口 id (<0 失败)。宽高会被内核夹取到合理范围
static inline int nep_win_create(const char* title, int w, int h) {
    long r;
    __asm__ volatile("int $0x80" : "=a"(r)
                     : "a"(1L), "D"(title), "S"((long)w), "d"((long)h) : "memory");
    return (int)r;
}

// 追加一行文本 (win_id < 0 = 写到本程序的控制台, 没有则自动创建)。
// 0 = 成功
static inline int nep_win_print(int win_id, const char* text) {
    long r;
    __asm__ volatile("int $0x80" : "=a"(r)
                     : "a"(2L), "D"((long)win_id), "S"(text) : "memory");
    return (int)r;
}

// 清空窗口 (win_id < 0 = 控制台)。0 = 成功
static inline int nep_win_clear(int win_id) {
    long r;
    __asm__ volatile("int $0x80" : "=a"(r) : "a"(5L), "D"((long)win_id) : "memory");
    return (int)r;
}

// 阻塞读一行控制台输入 (窗口聚焦时键盘输入, 回车提交, 退格可编辑)。
// 返回行长度; 注意等待期间任务被挂起, 不消耗 CPU。
// 必须先 printf 过一次 (控制台窗口存在) — 或先调 nep_win_print(-1, "")。
static inline long nep_read_line(char* buf, long maxlen) {
    long r;
    __asm__ volatile("int $0x80" : "=a"(r)
                     : "a"(4L), "D"(buf), "S"(maxlen) : "memory");
    return r;
}

// ── 窗口关闭回调 ──
// 用户点窗口 X 后: 内核把程序上下文改写为直接进入 cb(win_id)。
// cb 返回值 (rax): 非 0 = 关闭 (内核自动 EXIT 结束程序);
//                  0 = 取消关闭, 窗口保留, 回到事件等待继续跑。
// 典型用法 (点 X 弹确认框):
//
//     static int on_close(int w) {
//         return msgbox("退出", "确定要退出吗?", MSGBOX_OKCANCEL) == 0;
//     }
//
// 若不注册回调, X 点击只是关窗口, 程序继续运行 (可被任务管理器结束)。
// naked: 回调"返回"到这里时 rax = cb 的返回值, 必须先判断再动寄存器
__attribute__((used, naked))
static void nep__close_trampoline(void) {
    __asm__ volatile(
        "test %rax, %rax\n\t"          // 回调返回值: 真 = 关闭
        "jne 1f\n\t"
        "jmp nep__event_trampoline\n"  // 假: 回到事件等待 (窗口保留)
        "1:\n\t"
        "xor %eax, %eax\n\t"
        "int $0x80\n\t"                // EXIT 自毁 (窗口随任务销毁)
        "jmp 1b\n");
}

static inline void nep_on_close(int win_id, int (*cb)(int win_id)) {
    long r;
    __asm__ volatile("int $0x80" : "=a"(r)
                     : "a"(3L), "D"((long)win_id), "S"((long)cb),
                       "d"((long)nep__close_trampoline) : "memory");
    (void)r;
}

// ── 关机回调 (syscall 27) ──
// 用户点关机/重启后, 内核先触发所有已注册的程序 (改写上下文直接进入 cb),
// 给 ~3s 时间保存数据; 回调返回后程序自动 EXIT 正常退出。
// 之后剩余程序收到 X 关闭回调; 仍未退出的会被列在关机遮罩上,
// 由用户选择强杀或取消。不注册回调的程序直接进入 X 关闭/遮罩阶段。
__attribute__((used, naked))
static void nep__shutdown_trampoline(void) {
    __asm__ volatile(
        "xor %eax, %eax\n\t"           // 回调返回 = 收尾完成, EXIT 自毁
        "int $0x80\n\t"
        "jmp .\n");
}

static inline void nep_on_shutdown(void (*cb)(void)) {
    long r;
    __asm__ volatile("int $0x80" : "=a"(r)
                     : "a"(27L), "D"((long)cb),
                       "S"((long)nep__shutdown_trampoline) : "memory");
    (void)r;
}

// ── 命令行 (syscall 28, Win GetCommandLine 模型) ──
// nep test.nep 123 -x 启动时, 程序内查回 "test.nep 123 -x" 原文。
// 返回拷贝字节数 (含结尾 NUL), -1 = 参数无效
static inline int nep_get_cmdline(char* buf, int cap) {
    long r;
    __asm__ volatile("int $0x80" : "=a"(r)
                     : "a"(28L), "D"((long)buf), "S"((long)cap) : "memory");
    return (int)r;
}

// CommandLineToArgvW 等价: 把命令行切分成 argv[] (空格分隔,
// 双引号包裹的空格不切分)。返回 argv 指针数组 (静态存储, 下次调用
// 覆盖; argv[0] = 程序名), *argc = 参数个数; 失败返回 0
// 注意: 缓冲必须显式落 .data — 平坦镜像不含 .bss (未映射), 零初始化
// 静态变量默认进 .bss, 大 .data 程序里会落到映射外的新页
static __attribute__((section(".data"))) char  nep__cmdline_buf[192];
static __attribute__((section(".data"))) char* nep__argv[33];
static inline char** nep_get_args(int* argc) {
    if (!argc) return 0;
    if (nep_get_cmdline(nep__cmdline_buf, sizeof(nep__cmdline_buf)) < 0)
        return 0;
    int n = 0;
    const char* s = nep__cmdline_buf;
    // 命令行为空时仍返回 argv[0]="" (argc=1), 和 Win 保持一致即可
    while (*s && n < 32) {
        while (*s == ' ') s++;
        if (!*s) break;
        nep__argv[n++] = nep__cmdline_buf + (s - nep__cmdline_buf);
        int in_q = 0;
        char* w = nep__argv[n - 1];
        while (*s) {
            char c = *s;
            if (c == '"') { in_q = !in_q; s++; continue; }   // 引号剥掉
            if (c == ' ' && !in_q) { s++; break; }           // 先跨过分隔空格再断出:
            *w++ = c;                                        // w 与 s 同址时 NUL 会顶掉
            s++;                                             // 空格, 不先跨过会误判串尾
        }
        *w = '\0';
    }
    *argc = n;
    return nep__argv;
}

// ── 消息框 (syscall 10) ──
// 模态弹窗: 标题 + 内容 ('\n' 分行, 最多 4 行) + 按钮组合。
// 阻塞直到用户点按钮 (返回按钮序号 0 起, 左→右) 或 X 关闭 (返回 -1)。
// type: 0=[确定] 1=[确定,取消] 2=[忽略,重试,取消] 3=[忽略,重试] 4=[重试,取消]
static inline int nep_msgbox(const char* title, const char* text, int type) {
    long r;
    __asm__ volatile("int $0x80" : "=a"(r)
                     : "a"(10L), "D"(title), "S"(text), "d"((long)type) : "memory");
    return (int)r;
}

// ── 控件 ──
// 三种控件: label(文本), button(带回调), input(输入框, 点它聚焦后键盘输入)。
// 坐标是窗口客户区相对坐标 (窗口左上角为客户区 (0,0), 不含标题栏)。
// info 按值同步拷给内核, text 也可用栈上临时串。
#define NEP_WIDGET_LABEL  0
#define NEP_WIDGET_BUTTON 1
#define NEP_WIDGET_INPUT  2
#define NEP_WIDGET_TEXT   64

typedef struct {
    int  type;                  // NEP_WIDGET_*
    int  x, y, w, h;            // 客户区相对坐标 (label 忽略 w/h; input:
                                //   h<=0 = 单行 16px, h>=16 自定义高度,
                                //   h>=32 可多行, 回车换行/退格删换行)
    const char* text;           // label/button 文字 (input = 初始内容)
    void (*on_click)(int widget_id);  // 按钮回调 (其他控件填 0)
    void* tramp;                // 内部用: 回调返回后的落点, widget_add 自动填
} nep_widget_info;

static inline int nep_widget_add(int win_id, nep_widget_info* info) {
    long r;
    __asm__ volatile("int $0x80" : "=a"(r)
                     : "a"(6L), "D"((long)win_id), "S"(info) : "memory");
    return (int)r;
}

static inline int nep_widget_set_text(int win_id, int wid, const char* text) {
    long r;
    __asm__ volatile("int $0x80" : "=a"(r)
                     : "a"(7L), "D"((long)win_id), "S"((long)wid), "d"(text) : "memory");
    return (int)r;
}

static inline long nep_widget_get_text(int win_id, int wid, char* buf, long maxlen) {
    register long r10 __asm__("r10") = maxlen;
    long r;
    __asm__ volatile("int $0x80" : "=a"(r)
                     : "a"(8L), "D"((long)win_id), "S"((long)wid), "d"(buf), "r"(r10)
                     : "memory");
    return r;
}

// 销毁控件 (从窗口移除)。0 = 成功。控件 id 永不复用,
// 若它是当前聚焦的输入框则释放焦点。
static inline int nep_widget_destroy(int win_id, int wid) {
    long r;
    __asm__ volatile("int $0x80" : "=a"(r)
                     : "a"(11L), "D"((long)win_id), "S"((long)wid) : "memory");
    return (int)r;
}

// ── 事件等待 (控件程序主循环) ──
// 挂起程序等下一个事件 (按钮回调/X 关闭)。回调返回后经
// nep__event_trampoline 回到这里继续等 — 事件驱动程序的"主循环"。
__attribute__((used, noinline, noreturn))
static void nep__event_trampoline(void) {
    for (;;) __asm__ volatile("int $0x80" :: "a"(9L));
}

static inline __attribute__((noreturn)) void nep_event_wait(void) {
    __asm__ volatile("int $0x80" :: "a"(9L));
    // 返回 = 内核派发了回调并把跳板压栈, 实际不会执行到这里
    nep__event_trampoline();
}

// ═══ 注册表 syscall (12~17) — 只操作 default.rreg (程序配置) ═══
// system.rreg (系统配置) 对程序 API 不可见, 内核直接拒绝。
// 值类型: 0=none 1=sz(utf8) 2=utf16 3=expand 4=binary 5=dword 6=qword 7=multi

#define NEP_REG_NONE      0
#define NEP_REG_SZ_UTF8   1
#define NEP_REG_SZ_UTF16  2
#define NEP_REG_EXPAND_SZ 3
#define NEP_REG_BINARY    4
#define NEP_REG_DWORD     5
#define NEP_REG_QWORD     6
#define NEP_REG_MULTI_SZ  7

// 读值: 原始字节拷入 buf (dword/qword = 小端原样), 返回值类型 (0..7);
// <0 错误 (-1=路径不存在, -2=值不存在)。数据大于 buf 会被截断。
static inline long nep_reg_get(const char* path, const char* name,
                               void* buf, long buflen) {
    register long r10 __asm__("r10") = buflen;
    long r;
    __asm__ volatile("int $0x80" : "=a"(r)
                     : "a"(12L), "D"(path), "S"(name), "d"(buf), "r"(r10)
                     : "memory");
    return r;
}

// 写值: 路径不存在自动逐级建键, 同名值替换。0 = 成功
static inline long nep_reg_set(const char* path, const char* name,
                               int type, const void* data, long size) {
    register long r10 __asm__("r10") = (long)data;
    register long r8v __asm__("r8")  = size;
    long r;
    __asm__ volatile("int $0x80" : "=a"(r)
                     : "a"(13L), "D"(path), "S"(name), "d"((long)type),
                       "r"(r10), "r"(r8v)
                     : "memory");
    return r;
}

// 删值。0 = 成功, <0 = 路径/值不存在
static inline long nep_reg_del_value(const char* path, const char* name) {
    long r;
    __asm__ volatile("int $0x80" : "=a"(r)
                     : "a"(14L), "D"(path), "S"(name) : "memory");
    return r;
}

// 删键 (键必须为空: 无子键无值)。0 = 成功
static inline long nep_reg_del_key(const char* path) {
    long r;
    __asm__ volatile("int $0x80" : "=a"(r) : "a"(15L), "D"(path) : "memory");
    return r;
}

// 列出键下所有条目名 (子键 + 值, 逗号分隔) → buf; 返回个数, <0 = 路径不存在
static inline long nep_reg_list(const char* path, char* buf, long maxlen) {
    long r;
    __asm__ volatile("int $0x80" : "=a"(r)
                     : "a"(16L), "D"(path), "S"(buf), "d"(maxlen) : "memory");
    return r;
}

// 创建键 (逐级自动建, 已存在也成功)。0 = 成功
static inline long nep_reg_create_key(const char* path) {
    long r;
    __asm__ volatile("int $0x80" : "=a"(r) : "a"(17L), "D"(path) : "memory");
    return r;
}

// ═══ 文件 syscall (18~24) — 路径形如 "AA://dir/file" (AA..ZZ = 盘符) ═══
// 系统卷的 runst/system 子树只读 (程序不能写/删/建目录)。
// fd 从 1 起; 程序退出时内核自动回收它打开的全部 fd。

#define NEP_SEEK_SET 0
#define NEP_SEEK_CUR 1
#define NEP_SEEK_END 2

// 打开文件: mode = "r"(读, 须存在) / "w"(写, 清零, 自动建) / "a"(追加,
// 自动建, 位置=末尾); 可加 '+' (补开读写) 和 'b' (忽略)。
// 返回 fd (>=1), <0 = 失败 (无此文件/盘符错/路径问题/fd 用尽)
static inline long nep_file_open(const char* path, const char* mode) {
    long r;
    __asm__ volatile("int $0x80" : "=a"(r)
                     : "a"(18L), "D"(path), "S"(mode) : "memory");
    return r;
}

// 读最多 len 字节到 buf。返回读到的字节数 (0 = EOF), <0 = 错误
static inline long nep_file_read(int fd, void* buf, long len) {
    long r;
    __asm__ volatile("int $0x80" : "=a"(r)
                     : "a"(19L), "D"((long)fd), "S"(buf), "d"(len) : "memory");
    return r;
}

// 写 len 字节。返回写入字节数, <0 = 错误
static inline long nep_file_write(int fd, const void* buf, long len) {
    long r;
    __asm__ volatile("int $0x80" : "=a"(r)
                     : "a"(20L), "D"((long)fd), "S"(buf), "d"(len) : "memory");
    return r;
}

// 关闭 fd。0 = 成功 (重复关闭返回 <0, 无副作用)
static inline int nep_file_close(int fd) {
    long r;
    __asm__ volatile("int $0x80" : "=a"(r) : "a"(21L), "D"((long)fd) : "memory");
    return (int)r;
}

// 移动读写位置。whence: NEP_SEEK_SET/CUR/END。返回新位置, <0 = 错误
// (ftell 等价于 nep_file_seek(fd, 0, NEP_SEEK_CUR))
static inline long long nep_file_seek(int fd, long long off, int whence) {
    long r;
    __asm__ volatile("int $0x80" : "=a"(r)
                     : "a"(22L), "D"((long)fd), "S"(off), "d"((long)whence) : "memory");
    return (long long)r;
}

// 删除文件。0 = 成功, <0 = 不存在/IO 错误
static inline int nep_file_delete(const char* path) {
    long r;
    __asm__ volatile("int $0x80" : "=a"(r) : "a"(23L), "D"(path) : "memory");
    return (int)r;
}

// 创建目录 (仅末级, 父目录须已存在)。0 = 成功
static inline int nep_file_mkdir(const char* path) {
    long r;
    __asm__ volatile("int $0x80" : "=a"(r)
                     : "a"(24L), "D"(path) : "memory");
    return (int)r;
}

// ═══ 控制台彩色行 / 终端命令 (25~26) ═══

// 彩色输出一行到窗口/控制台 (win_id<0 = 控制台, 没有则自动创建)。
// color = 0x00RRGGBB 或 0xAARRGGBB; 0 = 默认窗口文字色
static inline int nep_win_print_color(int win_id, const char* text,
                                      unsigned color) {
    long r;
    __asm__ volatile("int $0x80" : "=a"(r)
                     : "a"(25L), "D"((long)win_id), "S"(text), "d"((long)color)
                     : "memory");
    return (int)r;
}

// 执行一条终端命令 (输出显示在终端窗口, 没开则自动开)。
// 如 nep_system("clr") 清终端屏幕, nep_system("ver") 显示版本。
// 0 = 已执行, <0 = 命令串非法/过长
static inline int nep_system(const char* cmd) {
    long r;
    __asm__ volatile("int $0x80" : "=a"(r) : "a"(26L), "D"(cmd) : "memory");
    return (int)r;
}

// ── 位图绘制 (syscall 29) ──
// 把 32bpp (0xAARRGGBB, 逐行, 每像素 4 字节) 位图拷进窗口的内容状态;
// 内核每帧重绘窗口时把位图 blit 到客户区 (x, y) 偏移处, 越界自动裁剪。
// 单次 w*h ≤ 1024*1024。可分多次调用灌入不同区域 (已灌内容保留, 层
// 矩形自动并集扩张) — 没有大块连续内存的程序可逐行/逐条带灌入。
// 0 = 成功, <0 = 参数/窗口非法
static inline int nep_win_draw_blit(int win_id, int x, int y, int w, int h,
                                    const void* argb) {
    register long r10v __asm__("r10") = w;
    register long r8v  __asm__("r8")  = h;
    register long r9v  __asm__("r9")  = (long)argb;
    long r;
    __asm__ volatile("int $0x80" : "=a"(r)
                     : "a"(29L), "D"((long)win_id), "S"((long)x), "d"((long)y),
                       "r"(r10v), "r"(r8v), "r"(r9v) : "memory");
    return (int)r;
}

// ── 窗口客户区尺寸 (syscall 30) ──
// 返回低 32 位 = 客户区宽, 高 32 位 = 客户区高 (不含边框/标题栏;
// 控件/位图坐标都以客户区左上角为原点)。窗口 id 非法返回 -1
static inline long nep_win_get_size(int win_id) {
    long r;
    __asm__ volatile("int $0x80" : "=a"(r) : "a"(30L), "D"((long)win_id) : "memory");
    return r;
}
