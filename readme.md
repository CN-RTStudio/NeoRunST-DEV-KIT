# NeoRunST NEP SDK

NEP (NeoRunST Executable Program) 是 NeoRunST 的用户程序格式，运行在
ring3，**每进程独立地址空间**，任务/窗口数量无上限，抢占式调度。
开发者工具包 MIT 许可，见仓库根目录 LICENSE。

## 目录结构（开发者拿到的解压目录）

```
dev/
├── readme.md
├── stdio.h        类标准库: printf / sprintf / snprintf / scanf / atoi / abs / rand
│                  perror(红字报错) / puts / putchar / cputs(彩色输出)
│                  以及 fopen 系列文件操作 (FILE / fread / fwrite / remove / mkdir)
├── string.h       字符串/内存: strcpy / strcmp / memset / memcpy 等 (标准语义)
├── runst.h        应用主 API (类似 windows.h): 窗口、控件、消息框、控制台、
│                  system(执行终端命令)、程序退出
├── buildnep.exe   构建工具 — 直接双击/命令行用, 不需要 make 或其他脚本
└── tools/         (隐藏, 开发者不动 — 内含 nep.h、crt0.c 等所有依赖)
```

## 重要：必须用 dev 工具包的标准库

NeoRunST 是 **freestanding 环境，没有 libc**。程序里不要用编译器自带的
标准库头（`<stdio.h>`、`<stdlib.h>`、`<string.h>` 等）——那不是给
NeoRunST 的，没有对应的运行时支持。

**请只用工具包提供的头（用双引号包含，不要用尖括号）：**
示例：
```c
#include "stdio.h"   // printf / sprintf / scanf 等
#include "runst.h"   // 窗口 / 控制台 / 程序退出 等 (依赖 tools\nep.h, 自动找到)
```

buildnep.exe 会自动定位 tools\ 与本目录，把所有头都加进搜索路径。

## 目前提供的功能

### runst.h (应用主 API)

| 函数 | 说明 |
|------|------|
| `window_create(title, w, h)` | 创建窗口, 返回窗口 id (内核会夹取尺寸) |
| `window_on_close(win_id, cb)` | 注册 X 关闭回调 `int cb(int win_id)`: 返回非 0 关闭并结束程序, 返回 0 取消 (窗口保留) |
| `msgbox(title, text, type)` | 模态消息框, 返回按钮序号 (0 起从左到右); X 关闭 = -1 |
| `console_read(buf, maxlen)` | 阻塞读一行控制台输入 (等待不耗 CPU) |
| `system(cmd)` | 在终端窗口执行一条终端命令 (没有终端则自动开): `system("clr")` 清终端屏幕, `system("ver")` 显示版本等。**返回 0 = 已执行, <0 = 命令串非法/过长; 不返回命令的退出状态**, 输出显示在终端窗口里 (不在本程序控制台) |
| `program_exit()` | 结束程序 |
| `window_wait()` | 事件等待 (窗口程序主循环: 按钮回调/X 由内核直接唤醒) |
| `reg_get / reg_set / reg_del_value / reg_del_key / reg_list / reg_create_key` | 注册表读写 (default.rreg, 见下) |

### 消息框 (runst.h)

`msgbox(title, text, type)` — 内容按 `'\n'` 分行 (最多 4 行), 阻塞到用户点击:

| type | 按钮 | 返回值 |
|------|------|--------|
| `MSGBOX_OK` (0) | 确定 | 0 |
| `MSGBOX_OKCANCEL` (1) | 确定, 取消 | 0=确定 1=取消 |
| `MSGBOX_IGNORE_RETRY_CANCEL` (2) | 忽略, 重试, 取消 | 0=忽略 1=重试 2=取消 |
| `MSGBOX_IGNORE_RETRY` (3) | 忽略, 重试 | 0=忽略 1=重试 |
| `MSGBOX_RETRY_CANCEL` (4) | 重试, 取消 | 0=重试 1=取消 |

X 关闭消息框返回 -1。典型用法 (X 确认后才退出):

```c
static int on_close(int id) {
    (void)id;
    return msgbox("退出", "确定要退出吗?", MSGBOX_OKCANCEL) == 0;
}
```

### 注册表 (runst.h)

系统注册表有两个 hive 文件 (在系统卷 `runst/system/config/` 下):

- **default.rreg** — 程序配置。**程序 API 只能读写这一个**。
- **system.rreg** — 系统配置。程序 API 不可见, 内核直接拒绝; 只有系统
  自带工具 (注册表编辑器 / shell 的 reg 命令) 能改。

程序通过 `reg_*` 系列函数访问, 走 syscall, 不需要知道文件在哪:

```c
reg_set_str("app\\myapp", "name", "NeoRunST");     // 写文本 (自动加 NUL)
reg_set_dword("app\\myapp", "count", 0x1F);        // 写 DWORD (0x1F 和 31 存储相同)
char buf[64];
long t = reg_get("app\\myapp", "name", buf, sizeof(buf));  // t = 值类型
reg_list("app\\myapp", buf, sizeof(buf));          // 列条目名 (逗号分隔)
reg_del_value("app\\myapp", "name");               // 删值
reg_del_key("app\\myapp");                         // 删键 (必须为空键)
reg_create_key("app\\myapp\\sub");                 // 建键 (路径逐级自动建)
```

| 函数 | 说明 |
|------|------|
| `reg_get(path, name, buf, buflen)` | 读值, 返回值类型 (`REG_*`); <0 = 路径/值不存在; 数据超长截断 |
| `reg_set(path, name, type, data, size)` | 写值 (原始字节); 路径不存在自动逐级建键, 同名替换 |
| `reg_set_str(path, name, text)` | 写文本值便捷封装 (REG_SZ_UTF8) |
| `reg_set_dword(path, name, value)` | 写 DWORD 便捷封装 |
| `reg_del_value(path, name)` | 删值 |
| `reg_del_key(path)` | 删键 (键必须为空: 无子键无值) |
| `reg_list(path, buf, maxlen)` | 列出键下所有条目名 (子键+值, 逗号分隔), 返回个数 |
| `reg_create_key(path)` | 创建键 (逐级自动建, 已存在也成功) |

值类型:

| 常量 | 值 | 说明 |
|------|---|------|
| `REG_NONE` | 0 | 无类型 |
| `REG_SZ_UTF8` | 1 | 窄文本 (NUL 结尾) |
| `REG_SZ_UTF16` | 2 | Unicode 文本 (UTF-16LE, NUL 结尾) |
| `REG_EXPAND_SZ` | 3 | 含 `%VAR%` 的可展开文本 |
| `REG_BINARY` | 4 | 原始字节 |
| `REG_DWORD` | 5 | 4 字节小端整数 |
| `REG_QWORD` | 6 | 8 字节小端整数 |
| `REG_MULTI_SZ` | 7 | 多字符串 (双 NUL 结尾) |

说明:

- 键路径相对根, 形如 `"app\\myapp\\cfg"`, 根名 `ROOT` 不用写
- **十进制/十六进制只是显示格式**: `reg_set_dword(..., 0x1F)` 和
  `reg_set_dword(..., 31)` 存储后完全一样, 显示成什么由界面决定
- 单值最大 1024 字节 (syscall 限长); 更大的数据请放文件, 注册表只存路径
- 区分 `reg_list` 列出的名字是子键还是值: 对它试 `reg_get`,
  能读到就是值, 读不到就是子键
- 完整示例见 `regdemo.c` (构建后点击运行, 同时演示五种类型读写/删除/隔离性)

### string.h (字符串/内存)

标准语义, freestanding 自实现: `strlen strcpy strncpy strcat strcmp strncmp
strchr strrchr strstr memset memcpy memmove memcmp`

### stdio.h 其他工具

| 函数 | 说明 |
|------|------|
| `atoi / atol` | 字符串转整数 |
| `abs / labs` | 绝对值 |
| `srand / rand` | 伪随机数 (LCG, 0..2^31-1) |

### 控件 (runst.h)

坐标是**窗口客户区相对坐标** (客户区左上角 = (0,0), 不含标题栏)。
每窗口最多 16 个控件; 控件与文字在 `widget_add` 时按值拷给内核。

| 函数/类型 | 说明 |
|------|------|
| `widget_t` | 控件描述: `{type, x, y, w, h, text, on_click}` |
| `WIDGET_LABEL` | label: 文本, x,y = 位置 (w/h 忽略) |
| `WIDGET_BUTTON` | button: 按钮, x,y,w,h = 矩形; `on_click(widget_id)` 点击回调 |
| `WIDGET_INPUT` | input: 输入框, x,y,w,h = 矩形; h<=0 = 单行 16px, h>=32 可多行 (回车换行/退格删换行); 点击聚焦后接收键盘 |
| `widget_add(win_id, &wg)` | 添加控件, 返回控件 id (<0 失败) |
| `widget_set_text(win_id, id, text)` | 修改控件文字 |
| `widget_get_text(win_id, id, buf, max)` | 读取控件文字 (input 的用户输入) |
| `widget_destroy(win_id, id)` | 销毁控件 (从窗口移除; id 不复用) |

### stdio.h (类标准库)

| 函数 | 说明 |
|------|------|
| `printf(fmt, ...)` | 输出到**控制台**; 支持 `%c %s %d %i %u %x %X %p %%` 和 `l` 前缀 |
| `sprintf(buf, fmt, ...)` | 格式化到缓冲区 (注意自备足够大的缓冲) |
| `snprintf(buf, cap, fmt, ...)` / `vsnprintf` | 格式化到缓冲区, 最多 cap-1 字符恒 NUL 结尾; 返回"应有"长度 (同 C 标准) |
| `vprintf(fmt, ap)` | printf 的 va_list 版本 |
| `puts(s)` | 输出字符串 + 换行 |
| `putchar(c)` | 输出一个字符 (内部行缓冲, 凑满一行或遇 '\n' 才输出) |
| `perror(s)` | 把错误信息打到控制台, **红字** (无 errno 机制, 只打印 s; 不传则打印 "error") |
| `cputs(win_id, s, color)` | 以任意颜色输出一行到窗口/控制台 (win_id<0 = 本程序控制台), 自动加换行 |
| `CONSOLE_RED / GREEN / BLUE / YELLOW / GRAY` | cputs 用的颜色常量 (0x00RRGGBB) |
| `scanf(fmt, ...)` | 从控制台读一行并解析; `%d %i %u %x %s %c`, 支持宽度如 `%63s` |
| `fopen / fclose / fread / fwrite / fseek / ftell / rewind / feof / ferror` | 文件读写 (见下) |
| `fgetc / fputc / fgets / fputs` | 按字符/行读写文件 |
| `remove / mkdir` | 删除文件 / 创建目录 |

### 文件操作 (stdio.h 的 fopen 系列)

路径用**盘符形式**: `"AA://目录/文件.txt"` (AA~ZZ 为盘符, 系统盘 ZZ)。
走 syscall 由内核完成, 不需要自己找卷。每程序最多同时打开 8 个文件,
程序退出时内核自动回收没关闭的 fd。

```c
FILE* f = fopen("AA://docs/note.txt", "w");     // "w" 清零写, 不存在自动建
if (f) {
    fputs("hello file\n", f);
    fwrite(data, 1, len, f);
    fclose(f);
}

f = fopen("AA://docs/note.txt", "r");           // "r" 只读, 文件必须存在
if (f) {
    char buf[64];
    int n = fread(buf, 1, sizeof(buf) - 1, f);
    buf[n] = 0;
    fseek(f, 0, SEEK_END);                       // 移到末尾
    long size = ftell(f);                        // 取文件大小
    fclose(f);
}

remove("AA://docs/old.txt");                     // 删除文件
mkdir("AA://docs/newdir");                       // 建目录 (父目录须存在)
```

mode 与 C 标准一致: `"r"` 读 / `"w"` 写 (清零, 自动创建) / `"a"` 追加
(自动创建, 位置=末尾), 可加 `'+'` (补开读写) 和 `'b'` (忽略, 无影响)。

| 函数 | 说明 |
|------|------|
| `fopen(path, mode)` | 打开文件, 失败返回 NULL |
| `fread(ptr, sz, n, f)` | 读, 返回**完整元素个数** (不足 = EOF/错误, 可用 `feof`/`ferror` 区分) |
| `fwrite(ptr, sz, n, f)` | 写, 返回完整元素个数 |
| `fseek(f, off, whence)` | 移动位置; `SEEK_SET/CUR/END` |
| `ftell(f)` | 当前位置 (相对文件头) |
| `fgetc/fputc/fgets/fputs` | 按字符/行读写 |
| `fclose(f)` | 关闭 |
| `remove(path)` | 删除文件 |
| `mkdir(path)` | 创建目录 |

注意事项:

- 没有 `stdin/stdout/stderr` — 终端输出用 printf, 不要 fopen 它们
- **系统卷的 `runst/system` 子树对程序只读** (内核、注册表、系统配置
  都在那里): 读取 OK, 写/删除/建目录会被内核拒绝
- 读到文件末尾 `fread`/`fgetc` 返回不足并把 `feof` 置位; `fseek` 成功
  会清除该标志
- 更底层的接口 (`nep_file_*`) 在 `tools/nep.h`, 一般不直接用

### 控制台程序模型

**不创建窗口、只 printf/scanf 的程序就是控制台程序**——内核会自动开一个
`<程序名> - 控制台` 的类终端窗口：键盘输入在窗口聚焦时进入输入缓冲
（可退格，回车提交），`scanf`/`console_read` 按行取走；输出满屏自动滚动。

### 命令行参数 (nep.h, runst.h 已包含)

程序被带参数启动时 (终端 `nep app.nep 123` 或"打开方式"双击关联文件),
经 GetCommandLine 模型反查:

```c
#include "runst.h"   // nep.h 随之可用

int nep_main(void) {
    int argc = 0;
    char** argv = nep_get_args(&argc);   // argv[0] = 程序名, 空格分隔,
    if (argc >= 2)                       // 双引号包裹的空格不切分, 引号剥掉
        printf("第一个参数: %s\n", argv[1]);
    return 0;
}
```

| 函数 | 说明 |
|------|------|
| `nep_get_args(&argc)` | 切分成 argv[], 返回静态数组 (下次调用覆盖); 失败/无参数返回 0 |
| `nep_get_cmdline(buf, cap)` | 命令行原文 (含程序名), 返回拷贝字节数 |

关联文件启动时 `argv[1]` 就是被打开文件的完整盘符路径
(见 docs/默认打开方式.md, 参考 sysapp/bmpview.c)。

### 图像直绘与关机回调 (nep.h, 高级)

| 函数 | 说明 |
|------|------|
| `nep_win_draw_blit(win, x, y, w, h, pixels32)` | 把 32bpp (0xAARRGGBB) 像素块灌入窗口位图层, 内核每帧重绘时合成, 越界自动裁剪 |
| `nep_win_get_size(win, &w, &h)` | 读窗口客户区尺寸 |
| `nep_on_shutdown(cb)` | 注册关机回调 `int cb(void)`: 返回 0 = 阻止关机, 非 0 = 同意退出; 关机流程会轮询等待 |

### 执行环境特性

- 抢占式调度: 纯死循环也会被 PIT 时间片强制打断, GUI 不卡死
- FPU/SSE 寄存器跨 syscall 保存/恢复, 可放心用浮点
- 内存隔离: 每任务独立地址空间, 同一程序可多开
- 入口: `int nep_main(void)`, 返回后自动退出 (也可调 program_exit)

## 构建（不需要 make）

```sh
cd dev
buildnep.exe hello.c               # 输出 hello.nep (默认取源文件名)
buildnep.exe myapp.c -o out.nep    # 指定输出
buildnep.exe a.c b.c -o app.nep    # 多个源文件
buildnep.exe -l a.o b.o -o app.nep # 已编译的 .o 直接链接 (跳过编译)
```

- 需要系统 PATH 里有 x86_64-elf-gcc（elf 工具链），其余全自动
- `-l` 模式: 输入是 `.o` 目标文件 (crt0 仍自动编译), 适合只改部分
  文件时增量构建——自己用 gcc 把改动的 .c 编译成 .o, 没变的直接链
- 工具链前缀默认 `x86_64-elf-`，可用 `--tc` 或环境变量 `NEP_TC` 覆盖
- `--base 0x...` 一般不需要——隔离后所有程序固定用 0x400000 也不冲突

## 安装与运行

```sh
mcopy -i ../NeoRunST-CustomOS/kernel/bin/NeoRunST.img hello.nep ::/
```

然后在 NeoRunST 的文件管理器里点击 .nep 运行。


