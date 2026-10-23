// ── NEP crt0: 内核从这里进入 (镜像偏移 0), 调 nep_main 后自动退出 ──
#include "nep.h"

extern int nep_main(void);

void _start(void) {
    nep_main();
    nep_exit();   // noreturn
}
