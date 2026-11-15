#include "packetfilter.h"
#include "hook/bytecode.h"
#include "hook/format.h"

#include <QCoreApplication>
#include <QDateTime>

namespace {

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

size_t PacketFilterController::hookCodeSize() const
{
    return kPacketHookCodeSize;
}

bool PacketFilterController::releaseRemoteOnMissingWindow() const
{
    return true;
}

std::vector<BYTE> PacketFilterController::buildTrampoline(const HookTrampolineInput& in)
{
    const UINT_PTR my_window_handle = in.windowHandle;
    const ULONG64 PostMessageA = in.postMessage;

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

    // 重放被覆盖的原始指令，必须与目标地址处实际指令一致
    for (size_t i = 0; i < in.codeSize; ++i)
        asmData.push_back(in.rawBytecode[i]);

    asmData = asmData + makeByteArray({0xFF, 0x25, 0x00, 0x00, 0x00, 0x00});   // jmp [rip+0]
    asmData = asmData + longToBytes(in.hookAddress + in.codeSize);

    return asmData;
}
