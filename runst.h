#pragma once
// ── runst.h — NeoRunST 应用主 API (类似 windows.h 的角色) ──
// std 库 (stdio.h) 没有的东西都在这里: 控件、窗口、控制台、程序退出。
// 依赖 tools\nep.h (低层 ABI, 开发者不需要直接 include)。
//
// 两类程序:
//   控制台程序: 只用 printf/scanf (stdio.h), 内核自动给一个控制台窗口
//     int nep_main(void) {
//         printf("hi\n");
//         char name[64]; scanf("%63s", name);
//         return 0;                 // 返回即退出
//     }
//   窗口程序: window_create + 控件 (label/button/input) + window_wait
//     int nep_main(void) {
//         int w = window_create("demo", 320, 240);
//         widget_t lbl = { WIDGET_LABEL, 16, 16, 0, 0, "你好", 0 };
//         widget_add(w, &lbl);
//         window_wait();            // 挂起等事件 (按钮/X)
//     }

#include "nep.h"
#include "string.h"

// ── 程序 ──

// 结束程序 (永远不会返回)
static inline void __attribute__((noreturn)) program_exit(void) {
    nep_exit();
}

// ── 窗口 ──

// 创建窗口, 返回窗口 id (<0 失败)。尺寸会被内核夹取到合理范围。
// 窗口内容由控件组成 (见下方控件 API)。
static inline int window_create(const char* title, int w, int h) {
    return nep_win_create(title, w, h);
}

// 注册窗口 X 关闭回调: int cb(int win_id)。
// 返回非 0 = 关闭并结束程序; 返回 0 = 取消关闭 (窗口保留, 继续运行)。
// 典型用法: 点 X 弹确认框, 选"确定"才退出 (见 msgbox)。
// 不注册则点 X 只关窗口, 程序继续跑 (可被任务管理器结束)。
static inline void window_on_close(int win_id, int (*cb)(int win_id)) {
    nep_on_close(win_id, cb);
}

// ── 消息框 ──
// 模态弹窗: 标题 + 内容 ('\n' 分行, 最多 4 行) + 按钮组合。
// 阻塞直到用户点按钮, 返回按钮序号 (0 起, 从左到右); X 关闭返回 -1。
#define MSGBOX_OK             0   // [确定]
#define MSGBOX_OKCANCEL       1   // [确定,取消]            0=确定 1=取消
#define MSGBOX_IGNORE_RETRY_CANCEL 2 // [忽略,重试,取消]     0=忽略 1=重试 2=取消
#define MSGBOX_IGNORE_RETRY   3   // [忽略,重试]            0=忽略 1=重试
#define MSGBOX_RETRY_CANCEL   4   // [重试,取消]            0=重试 1=取消
static inline int msgbox(const char* title, const char* text, int type) {
    return nep_msgbox(title, text, type);
}

// ── 控件 ──
// 坐标是窗口客户区相对坐标 (客户区左上角 = (0,0), 不含标题栏/边框)。
// 三种: label 文本 / button 按钮(带回调) / input 输入框。
typedef nep_widget_info widget_t;

#define WIDGET_LABEL   NEP_WIDGET_LABEL
#define WIDGET_BUTTON NEP_WIDGET_BUTTON
#define WIDGET_INPUT  NEP_WIDGET_INPUT

// 添加控件, 返回控件 id (<0 失败)。widget_t 按值同步拷给内核,
// text 指向的字符串也会拷走 — 栈上临时变量/临时串都可以。
// 每窗口最多 16 个控件。
//   label : x,y = 文字位置 (w/h 忽略)
//   button: x,y,w,h = 按钮矩形; on_click(widget_id) 在点击时被调用
//   input : x,y,w,h = 输入框; h<=0 = 单行 16px, h 自定义 (16 的整数倍最好);
//           h>=32 可多行 — 回车插入换行 (行数没满时), 退格可删换行。
//           点击聚焦后接收键盘
static inline int widget_add(int win_id, widget_t* wg) {
    wg->tramp = (void*)nep__event_trampoline;   // 回调返回后回到事件等待
    return nep_widget_add(win_id, wg);
}

// 修改控件文字 (三种控件都可用)。0 = 成功
static inline int widget_set_text(int win_id, int wid, const char* text) {
    return nep_widget_set_text(win_id, wid, text);
}

// 读取控件文字 (input 的用户输入)。返回长度 (<0 失败), buf 收 NUL 结尾串
static inline int widget_get_text(int win_id, int wid, char* buf, int maxlen) {
    return (int)nep_widget_get_text(win_id, wid, buf, (long)maxlen);
}

// 销毁控件 (从窗口移除)。0 = 成功。控件 id 永不复用
static inline int widget_destroy(int win_id, int wid) {
    return nep_widget_destroy(win_id, wid);
}

// ── 控制台 (不需要 window_create) ──

// 阻塞读一行控制台输入 (控制台窗口聚焦时键盘输入, 回车提交, 退格可编辑)。
// 返回行长度。等待期间任务挂起, 不消耗 CPU。
// 没有控制台窗口时会自动创建一个。
static inline long console_read(char* buf, long maxlen) {
    return nep_read_line(buf, maxlen);
}

// ── system: 执行终端命令 ──
// 在终端窗口执行一条命令 (没有终端窗口则自动打开), 效果等同在终端里
// 敲了这条命令: system("clr") 清终端屏幕, system("ver") 显示版本,
// system("help") 列命令... 输出显示在终端窗口里 (不在本程序控制台)。
// 返回 0 = 已执行, <0 = 命令串非法/过长。
static inline int system(const char* cmd) {
    return nep_system(cmd);
}

// ── 终端会话: 让自己的程序充当终端 (NEP 终端就用这套) ──
// 命令在会话里执行 (不开内置终端窗口), 输出用 term_read 增量取回,
// 会话按程序隔离 (每个程序只能看到自己的命令输出)。
// 典型循环:
//     term_exec("ls");                     // 提交命令 (异步)
//     while (!term_done()) {               // 等执行完 (含它启动的 nep 程序)
//         char buf[256];
//         int n = term_read(buf, sizeof(buf));   // 增量取输出
//         if (n > 0) printf("%s", buf);
//     }
// 会话里用 nep 启动的控制台程序: 输出也在 term_read 里;
// 要喂输入时 term_write("一行文本") (程序的 scanf/READ_LINE 读走)。

// 提交命令。0 = 已入队; -2 = 上一条还没执行完; <0 = 命令非法/过长
static inline int term_exec(const char* cmd) {
    return nep_term_exec(cmd);
}

// 1 = 当前命令执行完 (且它启动的 nep 程序都已退出), 0 = 还没完
static inline int term_done(void) {
    return nep_term_done();
}

// 增量取输出。返回字节数 (0 = 暂无新输出), buf 收 NUL 结尾串
static inline int term_read(char* buf, int cap) {
    return nep_term_read(buf, cap);
}

// 提交一行输入 (回显 + 交给会话里最新启动的寄宿程序读走)
static inline void term_write(const char* line) {
    nep_term_write(line);
}

// 非阻塞读一行控制台输入: 没有整行立即返回 -1 (不挂起), 有则返回长度。
// 终端类程序"边等命令输出边收键盘"时用它 (否则互相等会死锁)
static inline long console_read_nb(char* buf, long maxlen) {
    return nep_read_line_nb(buf, maxlen);
}

// ── 事件等待 (窗口程序主循环) ──

// 窗口程序没有更多事可做时用它挂住自己:
// 任务挂起不耗 CPU, 按钮回调 / X 关闭回调由内核直接唤醒进入,
// 回调返回后自动回到这里继续等下一个事件。
static inline void __attribute__((noreturn)) window_wait(void) {
    nep__event_trampoline();
}

// ── 注册表 (default.rreg, 程序配置) ──
// 程序只能读写 default.rreg (程序配置 hive);
// system.rreg (系统配置) 对程序 API 不可见, 内核直接拒绝。
// 键路径形如 "app\myapp\cfg", 相对根; 路径不存在时 set/create 自动逐级建键。

// 值类型
#define REG_NONE      NEP_REG_NONE       // 0 无类型
#define REG_SZ_UTF8   NEP_REG_SZ_UTF8    // 1 窄文本 (NUL 结尾)
#define REG_SZ_UTF16  NEP_REG_SZ_UTF16   // 2 Unicode 文本 (UTF-16LE, NUL 结尾)
#define REG_EXPAND_SZ NEP_REG_EXPAND_SZ  // 3 含 %VAR% 的可展开文本
#define REG_BINARY    NEP_REG_BINARY     // 4 原始字节
#define REG_DWORD     NEP_REG_DWORD      // 5 4 字节小端整数 (十进制/十六进制只是显示格式)
#define REG_QWORD     NEP_REG_QWORD      // 6 8 字节小端整数
#define REG_MULTI_SZ  NEP_REG_MULTI_SZ   // 7 多字符串 (双 NUL 结尾)

// 读值: 原始字节拷入 buf (dword/qword = 小端原样, 文本 = 带 NUL)。
// 返回值类型 (REG_*, 0..7); <0 = 路径不存在 (-1) / 值不存在 (-2)。
// 数据大于 buflen 会被截断。
static inline long reg_get(const char* path, const char* name,
                           void* buf, long buflen) {
    return nep_reg_get(path, name, buf, buflen);
}

// 写值: 同名值替换。0 = 成功
static inline long reg_set(const char* path, const char* name,
                           int type, const void* data, long size) {
    return nep_reg_set(path, name, type, data, size);
}

// 写便捷封装: 文本值 (REG_SZ_UTF8, size 自动 +NUL)
static inline long reg_set_str(const char* path, const char* name,
                               const char* text) {
    return nep_reg_set(path, name, REG_SZ_UTF8, text, (long)strlen(text) + 1);
}

// 写便捷封装: DWORD (传 unsigned 值, 小端存储; 十进制/十六进制写法由调用方决定)
static inline long reg_set_dword(const char* path, const char* name,
                                 unsigned value) {
    return nep_reg_set(path, name, REG_DWORD, &value, 4);
}

// 删值。0 = 成功, <0 = 路径/值不存在
static inline long reg_del_value(const char* path, const char* name) {
    return nep_reg_del_value(path, name);
}

// 删键 (键必须为空: 无子键无值)。0 = 成功
static inline long reg_del_key(const char* path) {
    return nep_reg_del_key(path);
}

// 列出键下所有条目名 (子键 + 值, 逗号分隔) → buf。
// 返回条目个数; <0 = 路径不存在。区分方法: 对每个名字试 reg_get,
// 能读到就是值, 否则是子键。
static inline long reg_list(const char* path, char* buf, long maxlen) {
    return nep_reg_list(path, buf, maxlen);
}

// 创建键 (逐级自动建, 已存在也成功)。0 = 成功
static inline long reg_create_key(const char* path) {
    return nep_reg_create_key(path);
}

// ── 网络 (TCP + ping) ──
// 简易阻塞式 TCP: connect → send/recv 循环 → close。
// 等待期间任务挂起 (抢占式调度), 不消耗 CPU, 桌面照常响应。
// 单次 send/recv ≤1400 字节; 一次只支持少数并发连接 (内核 TCP_MAX=4)。

// 建立 TCP 连接, 返回连接句柄 (<0 失败, 含 ARP/DHCP 未就绪等)
static inline int net_connect(const unsigned char ip[4], int port) {
    return nep_net_connect(ip, port);
}

// 发送数据 (自动重传, 总超时 10s)。0 = 成功
static inline int net_send(int h, const void* buf, int len) {
    return nep_net_send(h, buf, len);
}

// 收数据。返回字节数; 0 = 对端关闭 (EOF); -1 = 超时; -3 = 连接失效。
// 一次最多取 1400 字节, 大响应循环调用直到 0
static inline int net_recv(int h, void* buf, int len, int timeout_ms) {
    return nep_net_recv(h, buf, len, timeout_ms);
}

// 关闭连接 (发 FIN, 最多等 5s)。0 = 成功
static inline int net_close(int h) {
    return nep_net_close(h);
}

// 解析点分十进制 IP ("10.0.2.2" → out[4])。0 = 成功, <0 = 格式错
static inline int net_parse_ip(const char* s, unsigned char out[4]) {
    for (int i = 0; i < 4; i++) {
        unsigned v = 0; int n = 0;
        while (*s >= '0' && *s <= '9' && n < 3) { v = v * 10 + (*s - '0'); s++; n++; }
        if (n == 0 || v > 255) return -1;
        out[i] = (unsigned char)v;
        if (i < 3) { if (*s != '.') return -1; s++; }
    }
    return (*s == '\0') ? 0 : -1;
}

// ping (阻塞, count 次, 每个 1s 超时, 中文 Windows 风格输出打在本程序
// 控制台)。返回成功次数; -1 = 已有 ping 在跑, -2 = 网卡未就绪
static inline int ping(const unsigned char ip[4], int count) {
    return nep_ping(ip, count);
}

// ── 音频播放 (底层: nep_aud_*, PCM 16bit 小端, 内核重采样 48kHz 立体声) ──
// 打开一路混音源, 返回句柄 (<0 失败)。rate/channels = 源格式, loop = 循环
static inline int audio_open(long rate, long channels) {
    return nep_aud_open(rate, channels, 0);
}

// 写 PCM 数据, 返回实际接受字节 (< 请求 = 源环满, 稍后再补)
static inline long audio_write(int h, const void* pcm16, long bytes) {
    return nep_aud_write(h, pcm16, bytes);
}

// 当前可写字节数 (写入前查水位, 避免阻塞式重试)
static inline long audio_free_space(int h) {
    return nep_aud_space(h);
}

// 关闭源 (立即释放混音槽; 程序退出时内核也会回收)
static inline int audio_close(int h) {
    return nep_aud_close(h);
}

// 本路音量 0..100, 返回旧值
static inline int audio_set_volume(int h, int vol) {
    return nep_aud_set_gain(h, vol);
}
static inline int audio_get_volume(int h) {
    return nep_aud_get_gain(h);
}

// 系统总音量 0..100
static inline int audio_master_volume(void) {
    return nep_aud_master_get();
}
static inline int audio_set_master(int v) {
    return nep_aud_master_set(v);
}

// 播放进度: pos[0]=已播字节, pos[1]=已写字节 (源格式)。0 = 成功
static inline int audio_position(int h, unsigned long long pos[2]) {
    return nep_aud_pos(h, pos);
}
