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

// 开机以来的毫秒数 (syscall 44; os.clock / 计时用)
static inline long nep_ticks(void) {
    long r;
    __asm__ volatile("int $0x80" : "=a"(r) : "a"(44L) : "memory");
    return r;
}

// 毫秒级休眠 (syscall 45): 挂起任务, 到点由内核唤醒, 等待不耗 CPU。
// 0 = 成功
static inline int nep_sleep(long ms) {
    long r;
    __asm__ volatile("int $0x80" : "=a"(r) : "a"(45L), "D"(ms) : "memory");
    return (int)r;
}

// ── 原始键盘事件 (syscall 46; DOOM 等游戏程序用) ──
// 焦点窗口的每次按键 (按下/释放/修饰键/方向键等扩展键) 都进内核队列,
// 程序轮询取走。scancode 是 set-1 make code (0xE0 前缀和 0x80 break 位
// 已剥离; 方向键 Up/Down/Left/Right = 0x48/0x50/0x4B/0x4D, ext=1)
typedef struct {
    unsigned char scancode;
    unsigned char ext;    // 1 = 0xE0 扩展键
    unsigned char down;   // 1 = 按下, 0 = 释放
    unsigned char shift, ctrl, alt;
    char ascii;           // 对应字符 (0 = 无)
    unsigned char _pad;
} nep_key_event;

// 从窗口 raw 键队列取一个事件 → *ev。返回 1 = 取到, 0 = 队列空 (立即
// 返回, 不挂起 — 主循环里轮询), <0 = 窗口非法
static inline int nep_win_get_key(int win_id, nep_key_event* ev) {
    long r;
    __asm__ volatile("int $0x80" : "=a"(r)
                     : "a"(46L), "D"((long)win_id), "S"(ev) : "memory");
    return (int)r;
}

// 改窗口标题栏文字 (syscall 47)。0 = 成功
static inline int nep_win_set_title(int win_id, const char* title) {
    long r;
    __asm__ volatile("int $0x80" : "=a"(r)
                     : "a"(47L), "D"((long)win_id), "S"(title) : "memory");
    return (int)r;
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

// 非阻塞读一行: 没有整行输入立即返回 -1 (不挂起), 有则返回行长度。
// 用于"边等后台输出边收键盘"的程序 (如终端); 普通交互用 nep_read_line
static inline long nep_read_line_nb(char* buf, long maxlen) {
    long r;
    __asm__ volatile("int $0x80" : "=a"(r)
                     : "a"(37L), "D"(buf), "S"(maxlen) : "memory");
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
    int  font_id;               // label 可选: 字体句柄 (nep_font_open 返回值;
                                //   0 = 系统字体)。input/button 忽略
    int  font_size;             // label 可选: 像素字号 (0 = 默认 16, 4..96)
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

// ── 字体句柄 (syscall 31/32): 程序自备 TTF 字体 ──
// 打开字体文件 (路径同文件 syscall, ≤4MB) 并解析缓存。
// → 句柄 (1..4), <0 = 文件不存在/不是有效 TTF/槽位占满。
// 句柄不随程序退出自动回收, 退出前建议 nep_font_close
static inline int nep_font_open(const char* path) {
    long r;
    __asm__ volatile("int $0x80" : "=a"(r) : "a"(31L), "D"(path) : "memory");
    return (int)r;
}

static inline int nep_font_close(int handle) {
    long r;
    __asm__ volatile("int $0x80" : "=a"(r) : "a"(32L), "D"((long)handle) : "memory");
    return (int)r;
}

// ═══ NEP 终端会话 (33~36): 不开内置终端窗口, 自己的程序就是终端 ═══
// 会话按调用程序的 pid 区分 (各程序完全隔离, 读不到别人的输出/输入)。
// 典型流程: term_exec(cmd) → 循环 term_done()/term_read() → 完成后收尾。
// nep 命令寄宿的控制台程序: 输出进会话 (term_read 取), 输入用
// term_write 提交 (它的 scanf/READ_LINE 读走)。

// 提交一条命令 (异步: 入队立即返回, 系统在后台执行, 不阻塞本程序)。
// 0 = 已入队; -2 = 上一条还没执行完; <0 = 命令串非法/过长
static inline int nep_term_exec(const char* cmd) {
    long r;
    __asm__ volatile("int $0x80" : "=a"(r) : "a"(33L), "D"(cmd) : "memory");
    return (int)r;
}

// 是否执行完: 1 = 命令跑完且它启动的 nep 程序都已退出; 0 = 还没完
static inline int nep_term_done(void) {
    long r;
    __asm__ volatile("int $0x80" : "=a"(r) : "a"(34L) : "memory");
    return (int)r;
}

// 增量取命令输出 (从上次取走的位置继续; 拿不下的下次接着取)。
// 返回字节数 (0 = 暂无新输出), buf 收 NUL 结尾串
static inline int nep_term_read(char* buf, int cap) {
    long r;
    __asm__ volatile("int $0x80" : "=a"(r)
                     : "a"(35L), "D"(buf), "S"((long)cap) : "memory");
    return (int)r;
}

// 提交一行输入: 回显进会话, 并交给会话里最新启动的寄宿程序
// (它的 scanf/READ_LINE 读走; 没有寄宿程序时只回显, 无副作用)
static inline void nep_term_write(const char* line) {
    long r;
    __asm__ volatile("int $0x80" : "=a"(r) : "a"(36L), "D"(line) : "memory");
    (void)r;
}

// ── 网络 (syscall 38-42; TCP 精简版 + ping) ──
// 阻塞语义: 等待期间任务挂起 (不耗时间片), 其他程序正常跑。
// ip = 4 字节数组 (a.b.c.d 各一字节)。同时最多 4 条 TCP 连接。

// 建立连接 (含 ARP + SYN 握手, 最多等 ~8s)。
// 返回句柄 0..3; -1=表满/NIC, -2=ARP 失败, -3=被 RST, -4=超时
static inline int nep_net_connect(const unsigned char ip[4], int port) {
    long r;
    __asm__ volatile("int $0x80" : "=a"(r)
                     : "a"(38L), "D"(ip), "S"((long)port) : "memory");
    return (int)r;
}

// 发送 ≤1400 字节并等对方确认 (重传自动; 总超时 10s)。0 = 成功
static inline int nep_net_send(int h, const void* buf, int len) {
    long r;
    __asm__ volatile("int $0x80" : "=a"(r)
                     : "a"(39L), "D"((long)h), "S"(buf), "d"((long)len) : "memory");
    return (int)r;
}

// 收数据 (等待期间挂起)。返回字节数; 0 = 对端关闭 (EOF);
// -1 = 超时; -3 = 连接失效。一次最多取 1400 字节
static inline int nep_net_recv(int h, void* buf, int len, int timeout_ms) {
    long r;
    __asm__ volatile("int $0x80" : "=a"(r)
                     : "a"(40L), "D"((long)h), "S"(buf), "d"((long)len),
                       "c"((long)timeout_ms) : "memory");
    return (int)r;
}

// 关闭连接 (发 FIN 等确认, 最多 5s)。0 = 成功
static inline int nep_net_close(int h) {
    long r;
    __asm__ volatile("int $0x80" : "=a"(r) : "a"(41L), "D"((long)h) : "memory");
    return (int)r;
}

// ping (阻塞, count 次, 每个 1s 超时)。返回成功次数; -1=已有 ping 在跑, -2=NIC
static inline int nep_ping(const unsigned char ip[4], int count) {
    long r;
    __asm__ volatile("int $0x80" : "=a"(r)
                     : "a"(42L), "D"(ip), "S"((long)count) : "memory");
    return (int)r;
}

// 用户堆扩容: 保证堆区前 total_bytes 字节已映射 (heap.h 内部用,
// 程序一般不直接调)。0 = 成功, -1 = 超过上限 (1MB) / 物理页耗尽
static inline int nep_heap_grow(long total_bytes) {
    long r;
    __asm__ volatile("int $0x80" : "=a"(r)
                     : "a"(43L), "D"((long)total_bytes) : "memory");
    return (int)r;
}

// ═══ 异常回调 (syscall 48) ═══
// 注册用户态异常/信号处理器 (除 0、非法内存访问、kill 等内核送出的
// exc)。返回旧处理器 (链式保存), 传 0 = 注销。注册后内核不再直接
// 杀进程, 而是改写上下文跳进 handler(exc, addr)
typedef void (*nep_except_handler)(long exc, unsigned long addr);
static inline nep_except_handler nep_except_set(nep_except_handler h) {
    long r;
    __asm__ volatile("int $0x80" : "=a"(r) : "a"(48L), "D"(h) : "memory");
    return (nep_except_handler)r;
}

// ═══ 音频 (49~57): 软件混音器源 (PCM 16bit 小端) ═══
// open 指定采样率/声道 (内核重采样到 48kHz/立体声 经 HDA 输出); 最多
// 4 路并发; 程序退出时内核自动回收未关闭的源。写法: 先 aud_space 看
// 源环可写字节, 再 aud_write 喂数据 (返回实际接受字节)。
// 更高层封装 (audio_*) 在 runst.h
static inline int nep_aud_open(long rate, long channels, long loop) {
    long r;
    __asm__ volatile("int $0x80" : "=a"(r)
                     : "a"(49L), "D"(rate), "S"(channels), "d"(loop) : "memory");
    return (int)r;
}
static inline long nep_aud_write(int id, const void* pcm16, long len) {
    long r;
    __asm__ volatile("int $0x80" : "=a"(r)
                     : "a"(50L), "D"((long)id), "S"(pcm16), "d"(len) : "memory");
    return r;
}
static inline long nep_aud_space(int id) {
    long r;
    __asm__ volatile("int $0x80" : "=a"(r)
                     : "a"(51L), "D"((long)id) : "memory");
    return r;
}
static inline int nep_aud_close(int id) {
    long r;
    __asm__ volatile("int $0x80" : "=a"(r)
                     : "a"(52L), "D"((long)id) : "memory");
    return (int)r;
}
static inline int nep_aud_set_gain(int id, int vol) {   // 每流音量 0..100 → 旧值
    long r;
    __asm__ volatile("int $0x80" : "=a"(r)
                     : "a"(53L), "D"((long)id), "S"((long)vol) : "memory");
    return (int)r;
}
static inline int nep_aud_get_gain(int id) {
    long r;
    __asm__ volatile("int $0x80" : "=a"(r)
                     : "a"(54L), "D"((long)id) : "memory");
    return (int)r;
}
static inline int nep_aud_master_get(void) {            // 总音量 0..100
    long r;
    __asm__ volatile("int $0x80" : "=a"(r) : "a"(55L) : "memory");
    return (int)r;
}
static inline int nep_aud_master_set(int vol) {         // 总音量 0..100 → 旧值
    long r;
    __asm__ volatile("int $0x80" : "=a"(r)
                     : "a"(56L), "D"((long)vol) : "memory");
    return (int)r;
}
// 播放进度: pos[0]=已播字节, pos[1]=已写字节 (源格式)。0 = 成功
static inline int nep_aud_pos(int id, unsigned long long pos[2]) {
    long r;
    __asm__ volatile("int $0x80" : "=a"(r)
                     : "a"(57L), "D"((long)id), "S"(pos) : "memory");
    return (int)r;
}
