#include "packetfilter.h"
#include "driver/PeaceDriverAPI.h"
#include "hook/bytecode.h"
#include "hook/cpu.h"
#include "hook/format.h"

#include <QCoreApplication>
#include <QDateTime>

namespace {

constexpr ULONG64 kRemoteBufferSize = 0x1000;

// 目标发包函数的地址，需按实际特征码替换
constexpr ULONG64 PACKET_HOOK_ADDRESS = 0x1459DB3EC;

} // namespace

PacketLogModel::PacketLogModel(QObject* parent)
    : QAbstractListModel(parent)
{
}

int PacketLogModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid())
        return 0;
    return static_cast<int>(m_rows.size());
}

QVariant PacketLogModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= rowCount())
        return {};

    const Row& row = m_rows[static_cast<size_t>(index.row())];
    switch (role) {
    case SeqRole:
        return row.seq;
    case TimeRole:
    case Qt::DisplayRole:
        return row.time;
    case PointerRole:
        return row.pointer;
    default:
        return {};
    }
}

QHash<int, QByteArray> PacketLogModel::roleNames() const
{
    return {
        {SeqRole, "seq"},
        {TimeRole, "time"},
        {PointerRole, "pointer"},
    };
}

void PacketLogModel::append(const QString& seq, const QString& time, const QString& pointer)
{
    const int row = rowCount();
    beginInsertRows(QModelIndex(), row, row);
    m_rows.push_back({seq, time, pointer});
    endInsertRows();
}

void PacketLogModel::clear()
{
    if (m_rows.empty())
        return;
    beginResetModel();
    m_rows.clear();
    endResetModel();
}

PacketFilterController::PacketFilterController(QObject* parent)
    : HookControl(parent)
{
    QCoreApplication::instance()->installNativeEventFilter(this);
}

PacketFilterController::~PacketFilterController()
{
    if (QCoreApplication* app = QCoreApplication::instance())
        app->removeNativeEventFilter(this);
}

bool PacketFilterController::nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result)
{
    if (eventType != "windows_generic_MSG")
        return false;

    const MSG* msg = static_cast<const MSG*>(message);
    if (!msg || msg->message != WM_PACKET_CUSTOM_MESSAGE)
        return false;

    // lParam 即 r9，就是我们要记录的指针
    handlePacketMessage(static_cast<quint64>(msg->lParam));

    if (result)
        *result = 0;
    return true;
}

void PacketFilterController::handlePacketMessage(quint64 pointer)
{
    m_messages.append(
        QString::number(++m_sequence),
        QDateTime::currentDateTime().toString(QStringLiteral("hh:mm:ss.zzz")),
        formatAddress(pointer));
}

QAbstractListModel* PacketFilterController::messages()
{
    return &m_messages;
}

ULONG64 PacketFilterController::hookAddress() const
{
    return PACKET_HOOK_ADDRESS;
}

bool PacketFilterController::hook(DWORD pid)
{
    ULONG64 shadowpage_address = 0;
    ULONG64 asmcode_address    = 0;
    uint64_t hook_pfn          = 0;
    uint64_t shadow_pfn        = 0;
    bool     ept_installed     = false;
    ULONG64  target_cr3        = 0;

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

    const ULONG64 PostMessageA = PeaceDriver::getModuleExport(pid, moduleBase, "PostMessageA");
    if (PostMessageA == 0) {
        qDebug() << "PostMessageA基址未找到";
        return false;
    }

    const ULONG64 hook_address = PACKET_HOOK_ADDRESS;
    const uintptr_t hook_page_start = hook_address & ~0xFFF;
    void* hook_page_ptr = reinterpret_cast<void*>(hook_page_start);
    const uintptr_t hook_offset = hook_address - hook_page_start;
    qDebug() << "HOOK页面起始地址：" << QString::number(hook_page_start, 16).toUpper();
    qDebug() << "HOOK函数偏移：" << QString::number(hook_offset, 16).toUpper();

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
    hook_pfn = hook_phys_addr >> 12;
    qDebug() << "HOOK物理页帧号(PFN)：" << QString::number(hook_pfn, 16).toUpper();

    shadowpage_address = PeaceDriver::allocMemory(pid, kRemoteBufferSize);
    if (shadowpage_address == 0) {
        qDebug() << "申请影子内存失败";
        return false;
    }

    auto hook_page_cache = std::make_unique<byte[]>(0x1000);
    if (!PeaceDriver::readMemory(pid, static_cast<uint64_t>(hook_page_start), 0x1000, hook_page_cache.get())) {
        qDebug() << "读取HOOK页面失败";
        cleanupPartial();
        return false;
    }
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
    shadow_pfn = shadow_phys_addr >> 12;
    qDebug() << "影子页物理页帧号(PFN)：" << QString::number(shadow_pfn, 16).toUpper();

    asmcode_address = PeaceDriver::allocMemory(pid, kRemoteBufferSize);
    if (asmcode_address == 0) {
        qDebug() << "申请汇编内存失败";
        cleanupPartial();
        return false;
    }

    BYTE raw_bytecode[kPacketHookCodeSize] = {0};
    if (!PeaceDriver::readMemory(pid, hook_address, kPacketHookCodeSize, raw_bytecode)) {
        qDebug() << "读取HOOK原始字节码失败";
        cleanupPartial();
        return false;
    }

    const HWND hwnd = hostWindowHandle();
    if (!hwnd) {
        qDebug() << "未取得QML窗口句柄";
        cleanupPartial();
        return false;
    }
    const UINT_PTR my_window_handle = reinterpret_cast<UINT_PTR>(hwnd);

    // 发包函数按 (rcx=套接字, rdx=数据指针, r8=长度) 取参：
    // lParam 传数据指针，wParam 直接用 r8 的长度，无需搬移
    std::vector<byte> asmData;
    asmData = asmData + makeByteArray({0x9C});                          // pushfq
    asmData = asmData + makeByteArray({0x50});                          // push rax
    asmData = asmData + makeByteArray({0x53});                          // push rbx
    asmData = asmData + makeByteArray({0x51});                          // push rcx
    asmData = asmData + makeByteArray({0x52});                          // push rdx
    asmData = asmData + makeByteArray({0x56});                          // push rsi
    asmData = asmData + makeByteArray({0x57});                          // push rdi
    asmData = asmData + makeByteArray({0x55});                          // push rbp
    asmData = asmData + makeByteArray({0x41, 0x50});                    // push r8
    asmData = asmData + makeByteArray({0x41, 0x51});                    // push r9
    asmData = asmData + makeByteArray({0x41, 0x52});                    // push r10
    asmData = asmData + makeByteArray({0x41, 0x53});                    // push r11
    asmData = asmData + makeByteArray({0x41, 0x54});                    // push r12
    asmData = asmData + makeByteArray({0x41, 0x55});                    // push r13
    asmData = asmData + makeByteArray({0x41, 0x56});                    // push r14
    asmData = asmData + makeByteArray({0x41, 0x57});                    // push r15
    asmData = asmData + makeByteArray({0x48, 0x83, 0xEC, 0x28});        // sub rsp, 0x28 → 16字节对齐

    asmData = asmData + makeByteArray({0x4C, 0x8B, 0xCE});              // mov r9, RSI（数据指针 → lParam）
    asmData = asmData + makeByteArray({0x4D, 0x31, 0xC0});                                  // xor r8, r8
    asmData = asmData + makeByteArray({0x48, 0xBA}) + longToBytes(WM_PACKET_CUSTOM_MESSAGE);  // mov rdx, 消息类型
    asmData = asmData + makeByteArray({0x48, 0xB9}) + longToBytes(my_window_handle);          // mov rcx, 窗口句柄
    asmData = asmData + makeByteArray({0x48, 0xB8}) + longToBytes(PostMessageA);              // mov rax, PostMessageA
    asmData = asmData + makeByteArray({0xFF, 0xD0});                    // call rax

    asmData = asmData + makeByteArray({0x48, 0x83, 0xC4, 0x28});        // add rsp, 0x28
    asmData = asmData + makeByteArray({0x41, 0x5F});                    // pop r15
    asmData = asmData + makeByteArray({0x41, 0x5E});                    // pop r14
    asmData = asmData + makeByteArray({0x41, 0x5D});                    // pop r13
    asmData = asmData + makeByteArray({0x41, 0x5C});                    // pop r12
    asmData = asmData + makeByteArray({0x41, 0x5B});                    // pop r11
    asmData = asmData + makeByteArray({0x41, 0x5A});                    // pop r10
    asmData = asmData + makeByteArray({0x41, 0x59});                    // pop r9
    asmData = asmData + makeByteArray({0x41, 0x58});                    // pop r8
    asmData = asmData + makeByteArray({0x5D});                          // pop rbp
    asmData = asmData + makeByteArray({0x5F});                          // pop rdi
    asmData = asmData + makeByteArray({0x5E});                          // pop rsi
    asmData = asmData + makeByteArray({0x5A});                          // pop rdx
    asmData = asmData + makeByteArray({0x59});                          // pop rcx
    asmData = asmData + makeByteArray({0x5B});                          // pop rbx
    asmData = asmData + makeByteArray({0x58});                          // pop rax
    asmData = asmData + makeByteArray({0x9D});                          // popfq

    // 重放被覆盖的 14 字节原始指令，必须与目标地址处实际指令一致
    for (int i = 0; i < kPacketHookCodeSize; ++i)
        asmData.push_back(raw_bytecode[i]);

    asmData = asmData + makeByteArray({0xFF, 0x25, 0x00, 0x00, 0x00, 0x00});   // jmp [rip+0]
    asmData = asmData + longToBytes(hook_address + kPacketHookCodeSize);

    if (!PeaceDriver::writeMemory(pid, asmcode_address, asmData.size(), asmData.data())) {
        qDebug() << "写入汇编失败~";
        cleanupPartial();
        return false;
    }

    BYTE jmp_code[kPacketHookCodeSize] = {0};
    jmp_code[0] = 0x48;
    jmp_code[1] = 0xB8;
    const ULONG64 jmp_address = asmcode_address;
    memcpy(&jmp_code[2], &jmp_address, 8);
    jmp_code[10] = 0xFF;
    jmp_code[11] = 0xE0;
    jmp_code[12] = 0x90;

    const ULONG64 shadow_patch_va = shadowpage_address + hook_offset;
    if (!PeaceDriver::writeMemory(pid, shadow_patch_va, kPacketHookCodeSize, jmp_code)) {
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

    SYSTEM_INFO cpu_info = {};
    GetSystemInfo(&cpu_info);
    const uint32_t cpu_count = cpu_info.dwNumberOfProcessors;
    uint32_t success_count = 0;

    for_each_cpu([&](uint32_t cpu_id) {
        if (PeaceDriver::install_ept_hook(hook_pfn, shadow_pfn))
            ++success_count;
        else
            qDebug() << "EPT安装失败，CPU：" << cpu_id;
    });

    if (success_count != cpu_count) {
        for_each_cpu([hook_pfn](uint32_t cpu_id) {
            Q_UNUSED(cpu_id);
            PeaceDriver::remove_ept_hook(hook_pfn);
        });
        cleanupPartial();
        return false;
    }
    ept_installed = true;

    _hook_data.is_hooked = true;
    _hook_data.pid = pid;
    _hook_data.hook_address = hook_address;
    _hook_data.allocated_address = asmcode_address;
    _hook_data.shadowpage_address = shadowpage_address;
    _hook_data.hook_pfn = hook_pfn;
    _hook_data.target_cr3 = target_cr3;
    _hook_data.hook_execute_patched = true;
    _hook_data.raw_bytecode.assign(raw_bytecode, raw_bytecode + kPacketHookCodeSize);

    return true;
}
