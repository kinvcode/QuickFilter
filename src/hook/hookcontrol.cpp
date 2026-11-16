#include "hook/hookcontrol.h"

#include "driver/PeaceDriverAPI.h"
#include "hook/cpu.h"
#include "hook/memprotect.h"

#include <QDebug>
#include <cstring>
#include <memory>

namespace {

constexpr ULONG64 kRemoteBufferSize = 0x1000;
constexpr ULONG64 kPageSize = 0x1000;
constexpr size_t kJumpStubSize = 12;

struct PendingHookPage {
    ULONG64 source_page_address = 0;
    ULONG64 shadowpage_address = 0;
    uint64_t hook_pfn = 0;
    uint64_t shadow_pfn = 0;
};

// 当前实现支持补丁落在一个页内，或从当前页尾跨到紧邻的下一个页。
size_t hookPageCount(ULONG64 address, size_t codeSize)
{
    if (codeSize == 0)
        return 0;

    const size_t firstPageBytes =
        static_cast<size_t>(kPageSize - (address & (kPageSize - 1)));
    if (codeSize <= firstPageBytes)
        return 1;
    if (codeSize - firstPageBytes <= kPageSize)
        return 2;
    return 0;
}

}

HookControl::HookControl(QObject* parent)
    : FilterHost(parent)
{
}

void HookControl::startHook()
{
    if (m_processName.trimmed().isEmpty()) {
        setNotice(QStringLiteral("请输入目标进程名"));
        return;
    }

    if (!PeaceDriver::pingREG()) {
        setNotice(QStringLiteral("未安装驱动！"));
        return;
    }

    if (!PeaceDriver::is_hv_running()) {
        setNotice(QStringLiteral("VT尚未启动，启动VT中..."));
        if (!PeaceDriver::enableVT(true)) {
            setNotice(QStringLiteral("VT启动失败！！！"));
            return;
        }
        setNotice(QStringLiteral("VT启动完成~"));
    }

    if (_hook_data.is_hooked) {
        setNotice(QStringLiteral("当前窗口已经启用HOOK。"));
        return;
    }

    const DWORD target_pid = PeaceDriver::getPid(m_processName.trimmed().toStdWString().c_str());
    if (target_pid == 0) {
        setNotice(QStringLiteral("找不到目标进程，请检查进程ID或进程名。"));
        return;
    }

    const ULONG64 target_address = hookAddress();

    ULONG meminfo = 0;
    if (!PeaceDriver::queryMemory(target_pid, target_address, meminfo)) {
        setNotice(QStringLiteral("无法查询目标进程中的HOOK地址。"));
        return;
    }
    if (!isExecutableProtection(meminfo)) {
        setNotice(QStringLiteral("HOOK地址所在内存不可执行，请检查目标函数地址。"));
        return;
    }

    const size_t code_size = hookCodeSize();
    if (code_size < kJumpStubSize) {
        setNotice(QStringLiteral("HOOK覆盖长度不足12字节，无法写入跳转指令。"));
        return;
    }

    const size_t page_count = hookPageCount(target_address, code_size);
    if (page_count == 0) {
        setNotice(QStringLiteral("HOOK补丁超过两个4KB页面，暂不支持安装。"));
        return;
    }

    if (page_count == 2) {
        const ULONG64 second_page_address =
            (target_address & ~(kPageSize - 1)) + kPageSize;
        ULONG second_meminfo = 0;
        if (!PeaceDriver::queryMemory(target_pid, second_page_address, second_meminfo)
            || !isExecutableProtection(second_meminfo)) {
            setNotice(QStringLiteral("HOOK跨页后的第二个页面不可执行，无法安装。"));
            return;
        }

        qDebug() << "检测到HOOK跨页，将安装两个影子页。地址："
                 << QString::number(target_address, 16).toUpper()
                 << " 页内偏移："
                 << QString::number(target_address & (kPageSize - 1), 16).toUpper()
                 << " 长度：" << code_size;
    }

    qDebug() << "目标PID：" << target_pid;

    if (!hook(target_pid)) {
        setNotice(QStringLiteral("安装HOOK失败，已清理本次分配的远程内存。"));
        return;
    }

    setNotice(QStringLiteral("HOOK已成功安装。"));
}

bool HookControl::prepareClose()
{
    if (!_hook_data.is_hooked)
        return true;

    if (!unhook()) {
        setNotice(QStringLiteral("还原HOOK或释放远程内存失败。窗口将保持打开，请确认目标进程仍在运行后重试。"));
        return false;
    }

    setNotice(QString());
    return true;
}

bool HookControl::unhook()
{
    if (_hook_data.hook_execute_patched && _hook_data.target_cr3 != 0
        && _hook_data.hook_address != 0) {
        const size_t restored = PeaceDriver::write_virt_mem(
            _hook_data.target_cr3,
            reinterpret_cast<void*>(_hook_data.hook_address),
            _hook_data.raw_bytecode.data(),
            _hook_data.raw_bytecode.size());
        qDebug() << "[EPT] 还原执行页 raw_bytecode bytes=" << restored;
    }

    if (_hook_data.is_hooked) {
        qDebug() << "EPT HOOK卸载，页面数：" << _hook_data.pages.size();
        for_each_cpu([this](uint32_t cpu_id) {
            Q_UNUSED(cpu_id);
            for (const EptHookPageData& page : _hook_data.pages) {
                if (page.hook_pfn != 0)
                    PeaceDriver::remove_ept_hook(page.hook_pfn);
            }
        });
        _hook_data.is_hooked = false;
    }

    bool released = true;
    for (EptHookPageData& page : _hook_data.pages) {
        if (page.shadowpage_address == 0)
            continue;

        qDebug() << "释放影子页："
                 << QString::number(page.shadowpage_address, 16).toUpper();
        if (PeaceDriver::freeMemory(_hook_data.pid, page.shadowpage_address))
            page.shadowpage_address = 0;
        else
            released = false;
    }

    if (_hook_data.allocated_address != 0) {
        qDebug() << "释放汇编页";
        if (PeaceDriver::freeMemory(_hook_data.pid, _hook_data.allocated_address))
            _hook_data.allocated_address = 0;
        else
            released = false;
    }

    if (!released)
        return false;

    _hook_data = {};
    return true;
}

bool HookControl::hook(DWORD pid)
{
    const size_t codeSize = hookCodeSize();
    const ULONG64 hook_address = hookAddress();
    const size_t page_count = hookPageCount(hook_address, codeSize);

    if (codeSize < kJumpStubSize || page_count == 0) {
        qDebug() << "HOOK覆盖范围无效。地址："
                 << QString::number(hook_address, 16).toUpper()
                 << " 长度：" << codeSize
                 << " 最小长度：" << kJumpStubSize;
        return false;
    }

    std::vector<PendingHookPage> pages(page_count);
    ULONG64 asmcode_address = 0;
    bool ept_install_started = false;
    ULONG64 target_cr3 = 0;

    const auto cleanupPartial = [&]() {
        if (ept_install_started) {
            for_each_cpu([&pages](uint32_t cpu_id) {
                Q_UNUSED(cpu_id);
                for (const PendingHookPage& page : pages) {
                    if (page.hook_pfn != 0)
                        PeaceDriver::remove_ept_hook(page.hook_pfn);
                }
            });
            ept_install_started = false;
        }
        if (asmcode_address != 0) {
            PeaceDriver::freeMemory(pid, asmcode_address);
            asmcode_address = 0;
        }
        for (PendingHookPage& page : pages) {
            if (page.shadowpage_address != 0) {
                PeaceDriver::freeMemory(pid, page.shadowpage_address);
                page.shadowpage_address = 0;
            }
        }
    };

    const ULONG64 moduleBase = PeaceDriver::getModuleBase(pid, L"user32.dll");
    if (moduleBase == 0) {
        qDebug() << "模块基址未找到";
        return false;
    }

    const ULONG64 postMessage = PeaceDriver::getModuleExport(pid, moduleBase, "PostMessageA");
    if (postMessage == 0) {
        qDebug() << "PostMessageA基址未找到";
        return false;
    }

    const ULONG64 hook_page_start = hook_address & ~(kPageSize - 1);
    const size_t hook_offset = static_cast<size_t>(hook_address - hook_page_start);
    qDebug() << "HOOK页面起始地址：" << QString::number(hook_page_start, 16).toUpper();
    qDebug() << "HOOK函数偏移：" << QString::number(static_cast<uint64_t>(hook_offset), 16).toUpper();

    target_cr3 = PeaceDriver::query_process_cr3(pid);
    if (!target_cr3) {
        qDebug() << "cr3查询失败";
        return false;
    }
    if ((target_cr3 & 0xFFF) != 0) {
        qDebug() << "CR3值无效（未页对齐）：" << QString::number(target_cr3, 16).toUpper();
        return false;
    }

    auto hook_page_cache = std::make_unique<BYTE[]>(kPageSize);
    for (size_t i = 0; i < pages.size(); ++i) {
        PendingHookPage& page = pages[i];
        page.source_page_address = hook_page_start + i * kPageSize;

        const uint64_t hook_phys_addr = PeaceDriver::get_physical_address(
            target_cr3, reinterpret_cast<void*>(page.source_page_address));
        if (!hook_phys_addr) {
            qDebug() << "无法获取HOOK物理页，页面：" << i;
            cleanupPartial();
            return false;
        }
        page.hook_pfn = hook_phys_addr >> 12;

        page.shadowpage_address = PeaceDriver::allocMemory(pid, kRemoteBufferSize);
        if (page.shadowpage_address == 0) {
            qDebug() << "申请影子内存失败，页面：" << i;
            cleanupPartial();
            return false;
        }

        if (!PeaceDriver::readMemory(
                pid, page.source_page_address, kPageSize, hook_page_cache.get())) {
            qDebug() << "读取HOOK页面失败，页面：" << i;
            cleanupPartial();
            return false;
        }
        if (!PeaceDriver::writeMemory(
                pid, page.shadowpage_address, kPageSize, hook_page_cache.get())) {
            qDebug() << "写入影子页失败，页面：" << i;
            cleanupPartial();
            return false;
        }

        qDebug() << "HOOK页面" << i
                 << "虚拟地址："
                 << QString::number(page.source_page_address, 16).toUpper()
                 << " 原PFN：" << QString::number(page.hook_pfn, 16).toUpper()
                 << " 影子地址："
                 << QString::number(page.shadowpage_address, 16).toUpper();
    }

    asmcode_address = PeaceDriver::allocMemory(pid, kRemoteBufferSize);
    if (asmcode_address == 0) {
        qDebug() << "申请汇编内存失败";
        cleanupPartial();
        return false;
    }
    qDebug() << "申请汇编空间：" << QString::number(asmcode_address, 16).toUpper();

    std::vector<BYTE> raw_bytecode(codeSize);
    if (!PeaceDriver::readMemory(pid, hook_address, codeSize, raw_bytecode.data())) {
        qDebug() << "读取HOOK原始字节码失败";
        cleanupPartial();
        return false;
    }

    const HWND hwnd = hostWindowHandle();
    if (!hwnd) {
        qDebug() << "未取得QML窗口句柄";
        if (releaseRemoteOnMissingWindow())
            cleanupPartial();
        return false;
    }

    // 构造汇编
    HookTrampolineInput trampolineInput;
    trampolineInput.hookAddress = hook_address;
    trampolineInput.postMessage = postMessage;
    trampolineInput.windowHandle = reinterpret_cast<UINT_PTR>(hwnd);
    trampolineInput.rawBytecode = raw_bytecode.data();
    trampolineInput.codeSize = codeSize;

    std::vector<BYTE> asmData = buildTrampoline(trampolineInput);
    if (asmData.empty()) {
        cleanupPartial();
        return false;
    }
    // 构造汇编完毕

    if (!PeaceDriver::writeMemory(pid, asmcode_address, asmData.size(), asmData.data())) {
        qDebug() << "写入汇编失败~";
        cleanupPartial();
        return false;
    }

    // mov rax, imm64; jmp rax。剩下的字节填 nop，长度等于覆盖的原始指令
    std::vector<BYTE> jmp_code(codeSize, 0x90);
    jmp_code[0] = 0x48;
    jmp_code[1] = 0xB8;
    const ULONG64 jmp_address = asmcode_address;
    memcpy(&jmp_code[2], &jmp_address, 8);
    jmp_code[10] = 0xFF;
    jmp_code[11] = 0xE0;

    // 已写入补丁的字节数，从 jmp_code 开头算
    size_t patch_offset = 0;
    // 第一页从钩子点的页内偏移开始写
    size_t page_offset = hook_offset;
    for (size_t i = 0; i < pages.size() && patch_offset < codeSize; ++i) {
        // 补丁还没写完的字节数
        const size_t bytes_remaining = codeSize - patch_offset;

        // 当前影子页从写入点到页尾的剩余空间
        const size_t page_remaining = static_cast<size_t>(kPageSize) - page_offset;

        // 本页实际写入长度，不越过页尾
        const size_t chunk_size = bytes_remaining < page_remaining ? bytes_remaining : page_remaining;

        // 当前影子页上的写入地址
        const ULONG64 shadow_patch_va = pages[i].shadowpage_address + page_offset;

        if (!PeaceDriver::writeMemory(pid, shadow_patch_va, chunk_size, jmp_code.data() + patch_offset))
        {
            // 记录失败的页序号和本页分片长度
            qDebug() << "写入影子页HOOK指令失败，页面：" << i << " 分片长度：" << chunk_size;
            cleanupPartial();                                   // 释放已申请的影子页和汇编页
            return false;                                       // 本页写入失败，停止安装
        }

        // 推进补丁游标，下一次从剩余字节继续
        patch_offset += chunk_size;
        // 下一页从页首接着写
        page_offset = 0;
    }

    // 校验补丁长度是否与hook代码长度一致
    if (patch_offset != codeSize) {
        qDebug() << "HOOK补丁未完整写入影子页";
        cleanupPartial();
        return false;
    }

    for (size_t i = 0; i < pages.size(); ++i) {
        PendingHookPage& page = pages[i];
        const uint64_t shadow_phys_addr =
            PeaceDriver::get_physical_address(
                target_cr3,
                reinterpret_cast<void*>(page.shadowpage_address));
        if (!shadow_phys_addr) {
            qDebug() << "patch 后无法解析影子页物理地址，页面：" << i;
            cleanupPartial();
            return false;
        }
        page.shadow_pfn = shadow_phys_addr >> 12;
        qDebug() << "影子页" << i << "PFN："
                 << QString::number(page.shadow_pfn, 16).toUpper();
    }

    qDebug() << "开始安装EPT HOOK，页面数：" << pages.size();

    SYSTEM_INFO cpu_info = {};
    GetSystemInfo(&cpu_info);
    const uint32_t cpu_count = cpu_info.dwNumberOfProcessors;
    size_t success_count = 0;

    ept_install_started = true;
    for_each_cpu([&](uint32_t cpu_id) {
        for (size_t i = 0; i < pages.size(); ++i) {
            const PendingHookPage& page = pages[i];
            if (PeaceDriver::install_ept_hook(page.hook_pfn, page.shadow_pfn)) {
                qDebug() << "EPT安装成功，CPU：" << cpu_id << " 页面：" << i;
                ++success_count;
            } else {
                qDebug() << "EPT安装失败，CPU：" << cpu_id << " 页面：" << i;
            }
        }
    });

    const size_t expected_count =
        static_cast<size_t>(cpu_count) * pages.size();
    if (success_count != expected_count) {
        cleanupPartial();
        return false;
    }

    qDebug() << "EPT HOOK安装完毕";
    ept_install_started = false;

    _hook_data.is_hooked = true;
    _hook_data.pid = pid;
    _hook_data.hook_address = hook_address;
    _hook_data.allocated_address = asmcode_address;
    _hook_data.pages.reserve(pages.size());
    for (const PendingHookPage& page : pages) {
        _hook_data.pages.push_back(
            {page.shadowpage_address, page.hook_pfn});
    }
    _hook_data.target_cr3 = target_cr3;
    _hook_data.hook_execute_patched = true;
    _hook_data.raw_bytecode = std::move(raw_bytecode);
    return true;
}
