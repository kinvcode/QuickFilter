#pragma once

#include <cstdint>
#include <Windows.h>

// 把当前线程绑到每个逻辑 CPU 上执行一次，用于按核安装或卸载 EPT
template <typename Fn>
inline void for_each_cpu(Fn const fn)
{
    SYSTEM_INFO info = {};
    GetSystemInfo(&info);

    for (uint32_t i = 0; i < info.dwNumberOfProcessors; ++i) {
        auto const prev_affinity = SetThreadAffinityMask(GetCurrentThread(), 1ull << i);
        fn(i);
        SetThreadAffinityMask(GetCurrentThread(), prev_affinity);
    }
}
