#include "hook/hookcontrol.h"

#include "driver/PeaceDriverAPI.h"
#include "hook/cpu.h"
#include "hook/memprotect.h"

#include <QDebug>
#include <cstring>
#include <memory>

namespace {

constexpr ULONG64 kRemoteBufferSize = 0x1000;

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
        if (_hook_data.hook_pfn != 0) {
            qDebug() << "EPT HOOK卸载";
            const uint64_t hook_pfn = _hook_data.hook_pfn;
            for_each_cpu([hook_pfn](uint32_t cpu_id) {
                Q_UNUSED(cpu_id);
                PeaceDriver::remove_ept_hook(hook_pfn);
            });
        }
        _hook_data.is_hooked = false;
    }

    if (_hook_data.shadowpage_address != 0) {
        qDebug() << "释放影子页";
        if (!PeaceDriver::freeMemory(_hook_data.pid, _hook_data.shadowpage_address))
            return false;
        _hook_data.shadowpage_address = 0;
    }

    if (_hook_data.allocated_address != 0) {
        qDebug() << "释放汇编页";
        if (!PeaceDriver::freeMemory(_hook_data.pid, _hook_data.allocated_address))
            return false;
        _hook_data.allocated_address = 0;
    }

    _hook_data = {};
    return true;
}

bool HookControl::hook(DWORD pid)
{
    const size_t codeSize = hookCodeSize();
    const ULONG64 hook_address = hookAddress();

    ULONG64 shadowpage_address = 0;
    ULONG64 asmcode_address = 0;
    uint64_t hook_pfn = 0;
    uint64_t shadow_pfn = 0;
    bool ept_installed = false;
    ULONG64 target_cr3 = 0;

    const auto cleanupPartial = [&]() {
        if (ept_installed && hook_pfn != 0) {
            for_each_cpu([hook_pfn](uint32_t cpu_id) {
                Q_UNUSED(cpu_id);
                PeaceDriver::remove_ept_hook(hook_pfn);
            });
            ept_installed = false;
        }
        if (asmcode_address != 0) {
            PeaceDriver::freeMemory(pid, asmcode_address);
            asmcode_address = 0;
        }
        if (shadowpage_address != 0) {
            PeaceDriver::freeMemory(pid, shadowpage_address);
            shadowpage_address = 0;
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

    const uintptr_t hook_page_start = hook_address & ~0xFFF;
    void* hook_page_ptr = reinterpret_cast<void*>(hook_page_start);
    const uintptr_t hook_offset = hook_address - reinterpret_cast<uintptr_t>(hook_page_ptr);
    qDebug() << "HOOK页面起始地址：" << QString::number(static_cast<uint64_t>(reinterpret_cast<uintptr_t>(hook_page_ptr)), 16).toUpper();
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

    const uint64_t hook_phys_addr = PeaceDriver::get_physical_address(target_cr3, hook_page_ptr);
    if (!hook_phys_addr) {
        qDebug() << "无法获取HOOK物理页，后续操作中断";
        return false;
    }
    qDebug() << "HOOK物理地址：" << QString::number(hook_phys_addr, 16).toUpper();

    hook_pfn = hook_phys_addr >> 12;
    qDebug() << "HOOK物理页帧号(PFN)：" << QString::number(hook_pfn, 16).toUpper();

    shadowpage_address = PeaceDriver::allocMemory(pid, kRemoteBufferSize);
    if (shadowpage_address == 0) {
        qDebug() << "申请影子内存失败";
        return false;
    }
    qDebug() << "申请影子空间：" << QString::number(shadowpage_address, 16).toUpper();

    auto hook_page_cache = std::make_unique<byte[]>(0x1000);
    if (!PeaceDriver::readMemory(pid, reinterpret_cast<uint64_t>(hook_page_ptr), 0x1000, hook_page_cache.get())) {
        qDebug() << "读取HOOK页面失败";
        cleanupPartial();
        return false;
    }

    qDebug() << "准备复制页面数据";
    if (!PeaceDriver::writeMemory(pid, shadowpage_address, 0x1000, hook_page_cache.get())) {
        qDebug() << "写入影子页失败";
        cleanupPartial();
        return false;
    }

    const uint64_t shadow_phys_addr = PeaceDriver::get_physical_address(
        target_cr3, reinterpret_cast<void*>(shadowpage_address));
    if (!shadow_phys_addr) {
        qDebug() << "无法获取影子页物理地址，后续操作中断";
        cleanupPartial();
        return false;
    }
    qDebug() << "影子页物理地址：" << QString::number(shadow_phys_addr, 16).toUpper();
    shadow_pfn = shadow_phys_addr >> 12;
    qDebug() << "影子页物理页帧号(PFN)：" << QString::number(shadow_pfn, 16).toUpper();

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
    std::vector<BYTE> jmp_code(codeSize, 0);
    jmp_code[0] = 0x48;
    jmp_code[1] = 0xB8;
    const ULONG64 jmp_address = asmcode_address;
    memcpy(&jmp_code[2], &jmp_address, 8);
    jmp_code[10] = 0xFF;
    jmp_code[11] = 0xE0;
    for (size_t i = 12; i < codeSize; ++i)
        jmp_code[i] = 0x90;

    const ULONG64 shadow_patch_va = shadowpage_address + hook_offset;
    if (!PeaceDriver::writeMemory(pid, shadow_patch_va, codeSize, jmp_code.data())) {
        qDebug() << "写入影子页HOOK指令失败";
        cleanupPartial();
        return false;
    }

    const uint64_t shadow_phys_addr_after = PeaceDriver::get_physical_address(
        target_cr3, reinterpret_cast<void*>(shadowpage_address));
    if (!shadow_phys_addr_after) {
        qDebug() << "patch 后无法解析影子页物理地址";
        cleanupPartial();
        return false;
    }
    shadow_pfn = shadow_phys_addr_after >> 12;

    qDebug() << "开始安装EPT HOOK";

    SYSTEM_INFO cpu_info = {};
    GetSystemInfo(&cpu_info);
    const uint32_t cpu_count = cpu_info.dwNumberOfProcessors;
    uint32_t success_count = 0;

    for_each_cpu([&](uint32_t cpu_id) {
        if (PeaceDriver::install_ept_hook(hook_pfn, shadow_pfn)) {
            qDebug() << "EPT安装成功，CPU：" + QString::number(cpu_id);
            ++success_count;
        } else {
            qDebug() << "EPT安装失败，CPU：" + QString::number(cpu_id);
        }
    });

    if (success_count != cpu_count) {
        for_each_cpu([hook_pfn](uint32_t cpu_id) {
            Q_UNUSED(cpu_id);
            PeaceDriver::remove_ept_hook(hook_pfn);
        });
        cleanupPartial();
        return false;
    }

    qDebug() << "EPT HOOK安装完毕";
    ept_installed = true;

    _hook_data.is_hooked = true;
    _hook_data.pid = pid;
    _hook_data.hook_address = hook_address;
    _hook_data.allocated_address = asmcode_address;
    _hook_data.shadowpage_address = shadowpage_address;
    _hook_data.hook_pfn = hook_pfn;
    _hook_data.target_cr3 = target_cr3;
    _hook_data.hook_execute_patched = true;
    _hook_data.raw_bytecode = std::move(raw_bytecode);
    return true;
}
