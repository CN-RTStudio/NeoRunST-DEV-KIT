#pragma once
// ── NEP inttypes.h (freestanding shim) ──
// gcc freestanding 提供 stdint.h; inttypes 只需转发 (PRI 宏未被用到)
#include <stdint.h>
#include <stddef.h>     // size_t (m_cheat.h 等经 doomtype 间接使用)
