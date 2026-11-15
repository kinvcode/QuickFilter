#pragma once

#include <Windows.h>

// 页保护是否可执行。带 PAGE_GUARD 的页不算可执行
inline bool isExecutableProtection(ULONG protection)
{
    if ((protection & PAGE_GUARD) != 0)
        return false;

    switch (protection & 0xFF) {
    case PAGE_EXECUTE:
    case PAGE_EXECUTE_READ:
    case PAGE_EXECUTE_READWRITE:
    case PAGE_EXECUTE_WRITECOPY:
        return true;
    default:
        return false;
    }
}
