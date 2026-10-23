// ── buildnep.cpp — NEP (NeoRunST Executable Program) 构建工具 ──
// 编译:  g++ -O2 -std=c++17 buildnep.cpp -o buildnep.exe
// 用法:  buildnep.exe <src.c> [src2.c ...] [-o out.nep] [--base 0x400000] [--tc x86_64-elf-]
//        buildnep.exe -l <obj.o> [obj2.o ...] [-o out.nep] [--base ...] [--tc ...]
//        -o 省略时默认输出 <第一个源文件名>.nep
//        -l: 输入是已编译的 .o (跳过编译直接链接; crt0 仍会自动编译),
//            支持多个 .o, 适合只改部分文件的增量构建
//        独立可用 (不依赖 make): exe 放在 dev 工具包根目录或目录树下均可,
//        自动定位 tools\ 里的 crt0.c / nep.h 和根目录的 stdio.h / runst.h
//
// 流程: x86_64-elf-gcc 编译 crt0.c + 源文件 → ld 链接到固定基址 →
//       objcopy 抽平坦二进制 → 前面拼 24 字节 NEP 头
//
// 产出格式 (内核 kernel/src/nep/nep.h 的 NepHeader):
//     magic[4]="NEP1"  hdr_size=24  base(u64)  entry_off(u32)  image_size(u32)
//
// base 说明: 内存隔离实现后每个任务有独立地址空间, 所有程序用同一
// 默认基址 0x400000 也不会冲突 (同一程序可多开)。--base 仅用于特殊
// 需要时覆盖, 必须页对齐且落在内核允许的用户区。
//
// 工具链: 默认前缀 x86_64-elf-, 可用 --tc 或环境变量 NEP_TC 覆盖。
// 安装:  mcopy -i bin/NeoRunST.img hello.nep ::/  然后在文件管理器点击。

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <direct.h>   // _mkdir

static std::string TOOLS_DIR;            // dev\tools (crt0.c / nep.h 在这里)
static std::string DEV_DIR;              // dev 根 (stdio.h 在这里)

static const unsigned long long NEP_BASE    = 0x400000ULL;
static const unsigned long        MAX_IMAGE = 512 * 1024;

static const char* LDSCRIPT =
    "/* 由 buildnep.cpp 生成 */\n"
    "ENTRY(_start)\n"
    "SECTIONS\n"
    "{\n"
    "    . = @BASE@;\n"
    "    .text   : { *(.text .text.*) }\n"
    "    .rodata : { *(.rodata .rodata.*) }\n"
    "    .data   : { *(.data .data.*) }\n"
    "    .bss    : { *(.bss .bss.*) *(COMMON) }\n"
    "}\n";

static void die(const std::string& msg) {
    std::fprintf(stderr, "buildnep: %s\n", msg.c_str());
    std::exit(1);
}

// 运行命令 (打印 + 失败即退出)。参数里含空格的路径需调用方先加引号。
static void run(const std::string& cmd) {
    std::printf("  %s\n", cmd.c_str());
    int rc = std::system(cmd.c_str());
    if (rc != 0)
        die("命令失败 (exit " + std::to_string(rc) + "): " + cmd);
}

// 给 Windows 命令行的参数加引号
static std::string q(const std::string& s) { return "\"" + s + "\""; }

static bool read_file(const std::string& path, std::vector<unsigned char>& out) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    out.clear();
    unsigned char buf[4096];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0)
        out.insert(out.end(), buf, buf + n);
    std::fclose(f);
    return true;
}

static void write_file(const std::string& path, const void* data, size_t len) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) die("无法写入: " + path);
    if (len && std::fwrite(data, 1, len, f) != len) {
        std::fclose(f);
        die("写入失败: " + path);
    }
    std::fclose(f);
}

int main(int argc, char** argv) {
    std::vector<std::string> sources;
    std::string output, tc = "x86_64-elf-";
    unsigned long long base = NEP_BASE;
    bool have_out = false;
    bool link_only = false;   // -l: 输入是 .o, 跳过编译直接链接

    // 目录定位 (独立可用): 从 argv[0] 所在目录开始, 每一级先试
    //   <目录>\tools\crt0.c  再试 <目录>\crt0.c,
    //   找到的就是 tools 目录 (crt0.c / nep.h); 其上一级是 dev 根 (stdio.h / runst.h)
    {
        std::string a0 = argv[0];
        size_t p = a0.find_last_of("\\/");
        std::string dir = (p == std::string::npos) ? "." : a0.substr(0, p);
        if (dir.empty()) dir = ".";
        for (int i = 0; i < 4 && TOOLS_DIR.empty(); i++) {
            const char* candidates[2] = { "\\tools\\crt0.c", "\\crt0.c" };
            for (int k = 0; k < 2; k++) {
                std::string probe = dir + candidates[k];
                FILE* f = std::fopen(probe.c_str(), "rb");
                if (f) {
                    std::fclose(f);
                    TOOLS_DIR = dir + ((k == 0) ? "\\tools" : "");
                    break;
                }
            }
            if (!TOOLS_DIR.empty()) break;
            size_t q = dir.find_last_of("\\/");
            if (q == std::string::npos || dir == ".") break;
            dir = dir.substr(0, q);
        }
        if (TOOLS_DIR.empty())
            die("找不到 tools\\crt0.c — 请把 buildnep.exe 放在 dev 工具包目录内运行");
        size_t r = TOOLS_DIR.find_last_of("\\/");
        DEV_DIR = (r == std::string::npos) ? "." : TOOLS_DIR.substr(0, r);
    }

    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "-o" || a == "--output") {
            if (++i >= argc) die("-o 缺参数");
            output = argv[i];
            have_out = true;
        } else if (a == "--base") {
            if (++i >= argc) die("--base 缺参数");
            base = std::strtoull(argv[i], nullptr, 0);
        } else if (a == "--tc") {
            if (++i >= argc) die("--tc 缺参数");
            tc = argv[i];
        } else if (a == "-l" || a == "--link") {
            link_only = true;
        } else if (!a.empty() && a[0] == '-') {
            die("未知选项: " + a);
        } else {
            if (link_only) {
                // -l 模式: 只收 .o (大小写不敏感)
                size_t dot = a.find_last_of('.');
                std::string ext = (dot == std::string::npos) ? "" : a.substr(dot);
                for (auto& c : ext) c = (char)((c >= 'A' && c <= 'Z') ? c + 32 : c);
                if (ext != ".o")
                    die("-l 模式下输入必须是 .o 文件: " + a);
            }
            sources.push_back(a);
        }
    }

    if (sources.empty()) die("缺少源文件");
    if (!have_out) {
        // -o 省略: 默认 <第一个源文件名>.nep
        output = sources[0];
        size_t dot = output.find_last_of('.');
        size_t sep = output.find_last_of("\\/");
        if (dot != std::string::npos && (sep == std::string::npos || dot > sep))
            output = output.substr(0, dot);
        output += ".nep";
    }
    if (base & 0xFFF) die("--base 必须页对齐");

    const char* env_tc = std::getenv("NEP_TC");
    if (env_tc && std::strcmp(env_tc, "") != 0 && tc == "x86_64-elf-")
        tc = env_tc;

    std::string gcc = tc + "gcc", ld = tc + "ld",
                objcopy = tc + "objcopy", nm = tc + "nm";

    // 临时目录
    const char* tmpenv = std::getenv("TEMP");
    std::string tmp = (tmpenv ? tmpenv : ".") + std::string("\\nepbuild");
    _mkdir(tmp.c_str());   // 已存在不报错

    // 编译: crt0 最先 → _start 落在 .text 开头
    std::vector<std::string> objs;
    std::vector<std::string> srcs;
    srcs.push_back(TOOLS_DIR + "\\crt0.c");
    for (auto& s : sources) srcs.push_back(s);

    const char* CFLAGS =
        "-ffreestanding -fno-stack-protector -fno-pic "
        "-mno-red-zone -fno-builtin -O2 -Wall";

    // 编译: crt0 最先 → _start 落在 .text 开头。
    // -l 模式: 用户输入已是 .o, 只有 crt0 需要编译, 其余直接进链接
    for (size_t i = 0; i < srcs.size(); i++) {
        char objname[64];
        std::snprintf(objname, sizeof(objname), "\\o%zu.o", i);
        std::string obj = tmp + objname;
        if (link_only && i > 0) {
            std::printf("  (skip) %s 已是目标文件, 直接链接\n", srcs[i].c_str());
            objs.push_back(srcs[i]);
            continue;
        }
        run(gcc + " -c " + q(srcs[i]) + " -o " + q(obj) + " " + CFLAGS +
            " -I " + q(TOOLS_DIR) + " -I " + q(DEV_DIR));
        objs.push_back(obj);
    }

    // 链接
    std::string lds = tmp + "\\nep.ld";
    {
        std::string script = LDSCRIPT;
        char buf[32];
        std::snprintf(buf, sizeof(buf), "0x%llX", (unsigned long long)base);
        size_t at = script.find("@BASE@");
        script.replace(at, 6, buf);
        write_file(lds, script.data(), script.size());
    }
    std::string elf = tmp + "\\out.elf";
    {
        std::string cmd = ld + " -T " + q(lds) + " -o " + q(elf);
        for (auto& o : objs) cmd += " " + q(o);
        run(cmd);
    }

    // 从符号表找 _start 的实际偏移 (正常应为 0)
    unsigned long entry_off = 0;
    {
        std::string nmout_file = tmp + "\\nm.txt";
        run(nm + " " + q(elf) + " > " + q(nmout_file));
        std::vector<unsigned char> nmout;
        if (!read_file(nmout_file, nmout)) die("nm 输出读取失败");
        std::string text(nmout.begin(), nmout.end());
        size_t pos = 0;
        while ((pos = text.find("_start", pos)) != std::string::npos) {
            // 行首 16 进制地址 + 空格 + 类型 + 空格 + _start
            size_t ls = text.find_last_of('\n', pos);
            ls = (ls == std::string::npos) ? 0 : ls + 1;
            std::string line = text.substr(ls, pos - ls);
            if (line.find(' ') != std::string::npos) {
                unsigned long addr = std::strtoul(line.c_str(), nullptr, 16);
                if (addr >= base) {
                    long off = (long)(addr - base);
                    if (off < 0 || (unsigned long)off >= MAX_IMAGE)
                        die("_start 符号异常");
                    entry_off = (unsigned long)off;
                }
                break;
            }
            pos += 6;
        }
    }

    // 抽平坦二进制
    std::string binpath = tmp + "\\out.bin";
    run(objcopy + " -O binary " + q(elf) + " " + q(binpath));
    std::vector<unsigned char> image;
    if (!read_file(binpath, image)) die("镜像读取失败");

    if (image.empty()) die("镜像为空");
    if (image.size() > MAX_IMAGE) {
        char buf[128];
        std::snprintf(buf, sizeof(buf), "镜像 %zu 字节, 超过上限 %lu",
                      image.size(), MAX_IMAGE);
        die(buf);
    }

    // 写 NEP 头 (24 字节, 小端) + 镜像
    unsigned char hdr[24];
    std::memcpy(hdr, "NEP1", 4);
    unsigned int hdr_size = 24;
    unsigned int img_size = (unsigned int)image.size();
    unsigned int entry = (unsigned int)entry_off;
    std::memcpy(hdr + 4, &hdr_size, 4);
    std::memcpy(hdr + 8, &base, 8);
    std::memcpy(hdr + 16, &entry, 4);
    std::memcpy(hdr + 20, &img_size, 4);

    FILE* out = std::fopen(output.c_str(), "wb");
    if (!out) die("无法写入输出: " + output);
    std::fwrite(hdr, 1, sizeof(hdr), out);
    if (!image.empty()) std::fwrite(image.data(), 1, image.size(), out);
    std::fclose(out);

    std::printf("OK: %s  (base=0x%llX entry=+%lu image=%zu bytes)\n",
                output.c_str(), (unsigned long long)base, entry_off, image.size());
    return 0;
}
