#pragma once

#include "hook/filterhost.h"

#include <vector>
#include <Windows.h>

struct EptHookPageData {
    ULONG64 shadowpage_address = 0;
    uint64_t hook_pfn = 0;
};

// 一次 HOOK 安装后要还原的远程状态。原始字节长度由各窗口自己决定
struct HookData {
    bool is_hooked = false;
    DWORD pid = 0;
    ULONG64 hook_address = 0;
    ULONG64 allocated_address = 0;
    std::vector<EptHookPageData> pages;
    ULONG64 target_cr3 = 0;
    bool hook_execute_patched = false;
    std::vector<BYTE> raw_bytecode;
};

// 子类构造跳板时能用到的远程信息
struct HookTrampolineInput {
    ULONG64 hookAddress = 0;
    ULONG64 postMessage = 0;
    UINT_PTR windowHandle = 0;
    const BYTE* rawBytecode = nullptr;
    size_t codeSize = 0;
};

// 安装前检查、EPT 安装、卸载和关闭确认。跳板字节仍由子类构造
class HookControl : public FilterHost
{
    Q_OBJECT

public:
    explicit HookControl(QObject* parent = nullptr);

    Q_INVOKABLE void startHook();
    Q_INVOKABLE bool prepareClose();

protected:
    virtual ULONG64 hookAddress() const = 0;
    virtual size_t hookCodeSize() const = 0;
    // 句柄取失败时，发包过滤释放已申请的远程内存；窗口 ID 过滤保持不释放
    virtual bool releaseRemoteOnMissingWindow() const = 0;
    virtual std::vector<BYTE> buildTrampoline(const HookTrampolineInput& in) = 0;

    bool hook(DWORD pid);
    bool unhook();

    HookData _hook_data;
};
