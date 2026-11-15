#include "windowidfilter.h"
#include "hook/bytecode.h"
#include "hook/format.h"

#include <QCoreApplication>
#include <QDateTime>

namespace {

constexpr ULONG64 HOOK_ADDRESS = 0x1466CCE00;

QString formatId(quint32 id)
{
    return QStringLiteral("0x%1").arg(id, 0, 16).toUpper();
}

} // namespace

FilterCatalogModel::FilterCatalogModel(QObject* parent)
    : QAbstractListModel(parent)
{
    m_entries = {
        {0x97, QStringLiteral("编年史窗口"), true},
        {0xC1E, QStringLiteral("ESC窗口"), true},
        {0x4AF, QStringLiteral("NPC对话窗口"), true},
        {0x84, QStringLiteral("任务窗口"), true},
        {0xB58, QStringLiteral("动画窗口"), true},
        {0x2D0, QStringLiteral("个人信息窗口"), true},
        {0x219, QStringLiteral("物品栏窗口"), true},
        {0x568, QStringLiteral("技能窗口"), true},
        {0x2C9, QStringLiteral("活动1窗口"), true},
    };
}

int FilterCatalogModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid())
        return 0;
    return static_cast<int>(m_entries.size());
}

QVariant FilterCatalogModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= rowCount())
        return {};

    const Entry& entry = m_entries[static_cast<size_t>(index.row())];
    switch (role) {
    case ActiveRole:
        return entry.active;
    case IdTextRole:
        return formatId(entry.id);
    case NameRole:
    case Qt::DisplayRole:
        return entry.name;
    default:
        return {};
    }
}

QHash<int, QByteArray> FilterCatalogModel::roleNames() const
{
    return {
        {ActiveRole, "active"},
        {IdTextRole, "idText"},
        {NameRole, "name"},
    };
}

void FilterCatalogModel::setActive(int row, bool active)
{
    if (row < 0 || row >= rowCount())
        return;

    Entry& entry = m_entries[static_cast<size_t>(row)];
    if (entry.active == active)
        return;

    entry.active = active;
    const QModelIndex modelIndex = index(row);
    emit dataChanged(modelIndex, modelIndex, {ActiveRole});
}

QString FilterCatalogModel::nameForId(quint32 id) const
{
    for (const Entry& entry : m_entries) {
        if (entry.id == id)
            return entry.name;
    }
    return QStringLiteral("未知");
}

bool FilterCatalogModel::isActive(quint32 id) const
{
    for (const Entry& entry : m_entries) {
        if (entry.id == id)
            return entry.active;
    }
    return false;
}

MessageLogModel::MessageLogModel(QObject* parent)
    : QAbstractListModel(parent)
{
}

int MessageLogModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid())
        return 0;
    return static_cast<int>(m_rows.size());
}

QVariant MessageLogModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= rowCount())
        return {};

    const Row& row = m_rows[static_cast<size_t>(index.row())];
    switch (role) {
    case TimeRole:
        return row.time;
    case KindRole:
        return row.kind;
    case TitleRole:
    case Qt::DisplayRole:
        return row.title;
    case ReturnAddressRole:
        return row.returnAddress;
    default:
        return {};
    }
}

QHash<int, QByteArray> MessageLogModel::roleNames() const
{
    return {
        {TimeRole, "time"},
        {KindRole, "kind"},
        {TitleRole, "title"},
        {ReturnAddressRole, "returnAddress"},
    };
}

void MessageLogModel::append(const QString& time, const QString& kind, const QString& title, const QString& returnAddress)
{
    const int row = rowCount();
    beginInsertRows(QModelIndex(), row, row);
    m_rows.push_back({time, kind, title, returnAddress});
    endInsertRows();
}

void MessageLogModel::clear()
{
    if (m_rows.empty())
        return;
    beginResetModel();
    m_rows.clear();
    endResetModel();
}

WindowIdFilterController::WindowIdFilterController(QObject* parent)
    : HookControl(parent)
{
    QCoreApplication::instance()->installNativeEventFilter(this);
}

WindowIdFilterController::~WindowIdFilterController()
{
    if (QCoreApplication* app = QCoreApplication::instance())
        app->removeNativeEventFilter(this);
}

bool WindowIdFilterController::nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result)
{
    // QML 窗口是 QQuickWindow，拿不到可覆写的 nativeEvent，只能在这里收原生消息
    if (eventType != "windows_generic_MSG")
        return false;

    const MSG* msg = static_cast<const MSG*>(message);
    if (!msg || msg->message != WM_MY_CUSTOM_MESSAGE)
        return false;

    handleWindowIdMessage(static_cast<quint32>(msg->lParam), static_cast<quint64>(msg->wParam));

    if (result)
        *result = 0;
    return true;
}

void WindowIdFilterController::handleWindowIdMessage(quint32 windowId, quint64 retAddress)
{
    qDebug() << "[MSG] 收到 WM_MY_CUSTOM_MESSAGE lParam=0x"
             << QString::number(windowId, 16).toUpper();

    // 勾选了过滤的窗口 ID 不记录
    if (m_filters.isActive(windowId))
        return;

    m_messages.append(
        QDateTime::currentDateTime().toString(QStringLiteral("hh:mm:ss.zzz")),
        formatId(windowId),
        m_filters.nameForId(windowId),
        formatAddress(retAddress));
}

QAbstractListModel* WindowIdFilterController::filters()
{
    return &m_filters;
}

QAbstractListModel* WindowIdFilterController::messages()
{
    return &m_messages;
}

void WindowIdFilterController::setFilterActive(int row, bool active)
{
    m_filters.setActive(row, active);
}

ULONG64 WindowIdFilterController::hookAddress() const
{
    return HOOK_ADDRESS;
}

size_t WindowIdFilterController::hookCodeSize() const
{
    return kHookCodeSize;
}

bool WindowIdFilterController::releaseRemoteOnMissingWindow() const
{
    return false;
}

std::vector<BYTE> WindowIdFilterController::buildTrampoline(const HookTrampolineInput& in)
{
    /**
 HOOK函数入口位置反汇编
 48 8B C4              - mov rax,rsp
 4C 89 48 20           - mov [rax+20],r9
 4C 89 40 18           - mov [rax+18],r8
 89 50 10              - mov [rax+10],edx { 窗口ID }
**/

    const UINT_PTR my_window_handle = in.windowHandle;
    const ULONG64 PostMessageA = in.postMessage;
    const ULONG64 hook_address = in.hookAddress;

    std::vector<byte> asmData;

    asmData = asmData + makeByteArray({0x9C});                      // pushfq
    asmData = asmData + makeByteArray({0x50});                      // push rax
    asmData = asmData + makeByteArray({0x53});                      // push rbx
    asmData = asmData + makeByteArray({0x51});                      // push rcx
    asmData = asmData + makeByteArray({0x52});                      // push rdx
    asmData = asmData + makeByteArray({0x56});                      // push rsi
    asmData = asmData + makeByteArray({0x57});                      // push rdi
    asmData = asmData + makeByteArray({0x55});                      // push rbp
    asmData = asmData + makeByteArray({0x41, 0x50});                // push r8
    asmData = asmData + makeByteArray({0x41, 0x51});                // push r9
    asmData = asmData + makeByteArray({0x41, 0x52});                // push r10
    asmData = asmData + makeByteArray({0x41, 0x53});                // push r11
    asmData = asmData + makeByteArray({0x41, 0x54});                // push r12
    asmData = asmData + makeByteArray({0x41, 0x55});                // push r13
    asmData = asmData + makeByteArray({0x41, 0x56});                // push r14
    asmData = asmData + makeByteArray({0x41, 0x57});                // push r15
    asmData = asmData + makeByteArray({0x48, 0x83, 0xEC, 0x28});    // sub rsp, 0x28 → RSP ≡ 0 (mod 16)

    // hook后的实际逻辑
    // 读取返回上个函数的地址，rsp + 0x28 + 0x80,rsp + 0xA8
    asmData = asmData + makeByteArray({0x4C, 0x8B, 0x84, 0x24, 0xA8, 0x00, 0x00, 0x00});    // mov r8, qword ptr [rsp+0xA8]
    asmData = asmData + makeByteArray({0x4C, 0x8B, 0xCA});                                  // mov r9, rdx // 数据指针传给参数4
    // asmData = asmData + makeByteArray({0x4D, 0x31, 0xC0});                                  // xor r8, r8
    asmData = asmData + makeByteArray({0x48, 0xBA}) + longToBytes(WM_MY_CUSTOM_MESSAGE);    // mov rdx,消息类型
    asmData = asmData + makeByteArray({0x48, 0xB9}) + longToBytes(my_window_handle);        // mov rcx,窗口句柄
    asmData = asmData + makeByteArray({0x48, 0xB8}) + longToBytes(PostMessageA);            // mov rax, PostMessageA
    asmData = asmData + makeByteArray({0xFF, 0xD0});                                        // call rax

    asmData = asmData + makeByteArray({0x48, 0x83, 0xC4, 0x28});    // add rsp, 0x28
    asmData = asmData + makeByteArray({0x41, 0x5F});                // pop r15
    asmData = asmData + makeByteArray({0x41, 0x5E});                // pop r14
    asmData = asmData + makeByteArray({0x41, 0x5D});                // pop r13
    asmData = asmData + makeByteArray({0x41, 0x5C});                // pop r12
    asmData = asmData + makeByteArray({0x41, 0x5B});                // pop r11
    asmData = asmData + makeByteArray({0x41, 0x5A});                // pop r10
    asmData = asmData + makeByteArray({0x41, 0x59});                // pop r9
    asmData = asmData + makeByteArray({0x41, 0x58});                // pop r8
    asmData = asmData + makeByteArray({0x5D});                      // pop rbp
    asmData = asmData + makeByteArray({0x5F});                      // pop rdi
    asmData = asmData + makeByteArray({0x5E});                      // pop rsi
    asmData = asmData + makeByteArray({0x5A});                      // pop rdx
    asmData = asmData + makeByteArray({0x59});                      // pop rcx
    asmData = asmData + makeByteArray({0x5B});                      // pop rbx
    asmData = asmData + makeByteArray({0x58});                      // pop rax
    asmData = asmData + makeByteArray({0x9D});                      // popfq

    // 重放被覆盖的原始指令，必须与目标地址处实际指令一致
    for (size_t i = 0; i < in.codeSize; ++i)
        asmData.push_back(in.rawBytecode[i]);

    asmData = asmData + makeByteArray({0xFF, 0x25, 0x00, 0x00, 0x00, 0x00});	// jmp [rip+0]
    asmData = asmData + longToBytes(hook_address + in.codeSize);              // hook位置,kHookCodeSize是覆盖指令的大小

    return asmData;
}
