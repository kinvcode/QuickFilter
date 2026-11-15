#pragma once

#include "hook/filterhost.h"

#include <vector>
#include <Windows.h>

// 一次 HOOK 安装后要还原的远程状态。原始字节长度由各窗口自己决定
struct HookData {
    bool is_hooked = false;
    DWORD pid = 0;
    ULONG64 hook_address = 0;
    ULONG64 allocated_address = 0;
    ULONG64 shadowpage_address = 0;
    uint64_t hook_pfn = 0;
    ULONG64 target_cr3 = 0;
    bool hook_execute_patched = false;
    std::vector<BYTE> raw_bytecode;
};

// 安装前检查、卸载和关闭确认。跳板字节仍由子类的 hook() 构造
class HookControl : public FilterHost
{
    Q_OBJECT

public:
    explicit HookControl(QObject* parent = nullptr);

    Q_INVOKABLE void startHook();
    Q_INVOKABLE bool prepareClose();

protected:
    virtual ULONG64 hookAddress() const = 0;
    virtual bool hook(DWORD pid) = 0;

    bool unhook();

    HookData _hook_data;
};
