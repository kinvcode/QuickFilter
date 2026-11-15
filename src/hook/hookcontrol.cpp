#include "hook/hookcontrol.h"

#include "driver/PeaceDriverAPI.h"
#include "hook/cpu.h"
#include "hook/memprotect.h"

#include <QDebug>

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
