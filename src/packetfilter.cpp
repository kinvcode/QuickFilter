#include "packetfilter.h"
#include "driver/PeaceDriverAPI.h"
#include "hook/bytecode.h"
#include "hook/format.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QChar>
#include <cstring>
#include <utility>

namespace {

// 目标发包函数的地址，需按实际特征码替换
constexpr ULONG64 PACKET_HOOK_ADDRESS = 0x1459DB3EC;

template <typename T>
T readUnaligned(const char* address)
{
    T value = {};
    std::memcpy(&value, address, sizeof(T));
    return value;
}

QString formatHex(quint64 value, int width)
{
    return QStringLiteral("%1").arg(value, width, 16, QLatin1Char('0')).toUpper();
}

QString formatRelativeOffset(qint64 offset)
{
    if (offset == 0)
        return QStringLiteral("0");

    const QString digits = QString::number(
        offset < 0 ? static_cast<quint64>(-offset) : static_cast<quint64>(offset),
        16).toUpper();
    return (offset < 0 ? QStringLiteral("-0x") : QStringLiteral("+0x")) + digits;
}

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
    case HighlightedRole:
        return row.highlighted;
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
        {HighlightedRole, "highlighted"},
    };
}

void PacketLogModel::append(Row row)
{
    const int insertRow = rowCount();
    beginInsertRows(QModelIndex(), insertRow, insertRow);
    m_rows.push_back(std::move(row));
    endInsertRows();
}

const PacketLogModel::Row* PacketLogModel::rowAt(int row) const
{
    if (row < 0 || row >= rowCount())
        return nullptr;
    return &m_rows[static_cast<size_t>(row)];
}

void PacketLogModel::clear()
{
    if (m_rows.empty())
        return;
    beginResetModel();
    m_rows.clear();
    endResetModel();
}

PacketFilterModel::PacketFilterModel(QObject* parent)
    : QAbstractListModel(parent)
{
}

int PacketFilterModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid())
        return 0;
    return static_cast<int>(m_entries.size());
}

QVariant PacketFilterModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= rowCount())
        return {};

    const Entry& entry = m_entries[static_cast<size_t>(index.row())];
    switch (role) {
    case ActiveRole:
        return entry.active;
    case NameRole:
    case Qt::DisplayRole:
        return entry.name;
    case BehaviorRole:
        return static_cast<int>(entry.behavior);
    case BehaviorTextRole:
        return entry.behavior == HighlightBehavior
            ? QStringLiteral("高亮")
            : QStringLiteral("过滤");
    case FilledCountRole:
        return filledCount(entry);
    default:
        return {};
    }
}

QHash<int, QByteArray> PacketFilterModel::roleNames() const
{
    return {
        {ActiveRole, "active"},
        {NameRole, "name"},
        {BehaviorRole, "behavior"},
        {BehaviorTextRole, "behaviorText"},
        {FilledCountRole, "filledCount"},
    };
}

int PacketFilterModel::addDefault()
{
    const int row = rowCount();
    beginInsertRows(QModelIndex(), row, row);
    m_entries.emplace_back();
    endInsertRows();
    emit summaryChanged();
    return row;
}

int PacketFilterModel::duplicate(int row)
{
    if (row < 0 || row >= rowCount())
        return -1;

    Entry copy = m_entries[static_cast<size_t>(row)];
    copy.active = false;
    copy.name += QStringLiteral(" - 副本");

    const int insertRow = row + 1;
    beginInsertRows(QModelIndex(), insertRow, insertRow);
    m_entries.insert(m_entries.begin() + insertRow, std::move(copy));
    endInsertRows();
    emit summaryChanged();
    return insertRow;
}

bool PacketFilterModel::remove(int row)
{
    if (row < 0 || row >= rowCount())
        return false;

    beginRemoveRows(QModelIndex(), row, row);
    m_entries.erase(m_entries.begin() + row);
    endRemoveRows();
    emit summaryChanged();
    return true;
}

bool PacketFilterModel::setActive(int row, bool active)
{
    if (row < 0 || row >= rowCount())
        return false;

    Entry& entry = m_entries[static_cast<size_t>(row)];
    if (entry.active == active)
        return true;
    entry.active = active;
    emit dataChanged(index(row), index(row), {ActiveRole});
    emit summaryChanged();
    return true;
}

void PacketFilterModel::setAllActive(bool active)
{
    bool changed = false;
    for (Entry& entry : m_entries) {
        if (entry.active != active) {
            entry.active = active;
            changed = true;
        }
    }
    if (!changed)
        return;

    if (!m_entries.empty())
        emit dataChanged(index(0), index(rowCount() - 1), {ActiveRole});
    emit summaryChanged();
}

bool PacketFilterModel::allActive() const
{
    if (m_entries.empty())
        return false;
    for (const Entry& entry : m_entries) {
        if (!entry.active)
            return false;
    }
    return true;
}

QVariantMap PacketFilterModel::details(int row) const
{
    if (row < 0 || row >= rowCount())
        return {};

    const Entry& entry = m_entries[static_cast<size_t>(row)];
    QVariantList values;
    values.reserve(kPacketReadSize);
    for (qint16 value : entry.pattern) {
        values.push_back(value < 0
            ? QString()
            : formatHex(static_cast<quint8>(value), 2));
    }

    return {
        {QStringLiteral("name"), entry.name},
        {QStringLiteral("behavior"), static_cast<int>(entry.behavior)},
        {QStringLiteral("values"), values},
        {QStringLiteral("filledCount"), filledCount(entry)},
    };
}

bool PacketFilterModel::update(
    int row, const QString& name, int behavior, const QVariantList& values)
{
    if (row < 0 || row >= rowCount())
        return false;
    if (behavior != HighlightBehavior && behavior != FilterBehavior)
        return false;

    Entry updated = m_entries[static_cast<size_t>(row)];
    updated.name = name.trimmed().isEmpty() ? QStringLiteral("未命名滤镜") : name.trimmed();
    updated.behavior = static_cast<Behavior>(behavior);
    updated.pattern.fill(-1);

    const int count = values.size() < kPacketReadSize
        ? static_cast<int>(values.size())
        : kPacketReadSize;
    for (int i = 0; i < count; ++i) {
        QString text = values.at(i).toString().trimmed();
        if (text.isEmpty())
            continue;
        if (text.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive))
            text.remove(0, 2);
        bool ok = false;
        const int value = text.toInt(&ok, 16);
        if (!ok || value < 0 || value > 0xFF)
            return false;
        updated.pattern[static_cast<size_t>(i)] = static_cast<qint16>(value);
    }

    m_entries[static_cast<size_t>(row)] = std::move(updated);
    emit dataChanged(
        index(row),
        index(row),
        {NameRole, BehaviorRole, BehaviorTextRole, FilledCountRole});
    return true;
}

PacketFilterModel::MatchResult PacketFilterModel::evaluate(const QByteArray& bytes) const
{
    bool highlighted = false;
    for (const Entry& entry : m_entries) {
        if (!entry.active || !matches(entry, bytes))
            continue;
        if (entry.behavior == FilterBehavior)
            return FilterMatch;
        highlighted = true;
    }
    return highlighted ? HighlightMatch : NoMatch;
}

int PacketFilterModel::filledCount(const Entry& entry)
{
    int count = 0;
    for (qint16 value : entry.pattern) {
        if (value >= 0)
            ++count;
    }
    return count;
}

bool PacketFilterModel::matches(const Entry& entry, const QByteArray& bytes)
{
    bool hasValue = false;
    for (size_t i = 0; i < entry.pattern.size(); ++i) {
        const qint16 expected = entry.pattern[i];
        if (expected < 0)
            continue;
        hasValue = true;
        if (i >= static_cast<size_t>(bytes.size())
            || static_cast<quint8>(bytes.at(static_cast<int>(i))) != expected) {
            return false;
        }
    }
    return hasValue;
}

PacketFilterController::PacketFilterController(QObject* parent)
    : HookControl(parent),
      m_messages(this),
      m_filters(this)
{
    QCoreApplication::instance()->installNativeEventFilter(this);
    connect(
        &m_filters,
        &PacketFilterModel::summaryChanged,
        this,
        &PacketFilterController::filtersSummaryChanged);
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
    if (m_collectionPaused)
        return;

    QByteArray bytes(kPacketReadSize, '\0');
    if (pointer == 0
        || _hook_data.pid == 0
        || !PeaceDriver::readMemory(
            _hook_data.pid, pointer, kPacketReadSize, bytes.data())) {
        bytes.clear();
    }

    const PacketFilterModel::MatchResult match = m_filters.evaluate(bytes);
    if (match == PacketFilterModel::FilterMatch)
        return;

    PacketLogModel::Row row;
    row.seq = QString::number(++m_sequence);
    row.time = QDateTime::currentDateTime().toString(QStringLiteral("hh:mm:ss.zzz"));
    row.pointer = formatAddress(pointer);
    row.address = pointer;
    row.bytes = std::move(bytes);
    row.highlighted = match == PacketFilterModel::HighlightMatch;
    m_messages.append(std::move(row));
}

QAbstractListModel* PacketFilterController::messages()
{
    return &m_messages;
}

QAbstractListModel* PacketFilterController::filters()
{
    return &m_filters;
}

int PacketFilterController::filterCount() const
{
    return m_filters.rowCount();
}

bool PacketFilterController::allFiltersActive() const
{
    return m_filters.allActive();
}

bool PacketFilterController::collectionPaused() const
{
    return m_collectionPaused;
}

QString PacketFilterController::selectedPacketAddress() const
{
    return m_selectedAddress == 0 ? QString() : formatAddress(m_selectedAddress);
}

QString PacketFilterController::memoryHeader() const
{
    return m_memoryHeader;
}

QStringList PacketFilterController::memoryAddresses() const
{
    return m_memoryAddresses;
}

QStringList PacketFilterController::memoryDataRows() const
{
    return m_memoryDataRows;
}

QStringList PacketFilterController::memoryTextRows() const
{
    return m_memoryTextRows;
}

void PacketFilterController::clearPackets()
{
    m_messages.clear();
    m_selectedAddress = 0;
    m_selectedBytes.clear();
    m_addressBaseRow = -1;
    rebuildMemoryView();
}

void PacketFilterController::pauseCollection()
{
    if (m_collectionPaused)
        return;
    m_collectionPaused = true;
    emit collectionPausedChanged();
}

void PacketFilterController::resumeCollection()
{
    if (!m_collectionPaused)
        return;
    m_collectionPaused = false;
    emit collectionPausedChanged();
}

void PacketFilterController::selectPacket(int row)
{
    const PacketLogModel::Row* packet = m_messages.rowAt(row);
    if (!packet) {
        m_selectedAddress = 0;
        m_selectedBytes.clear();
        m_addressBaseRow = -1;
        rebuildMemoryView();
        return;
    }

    m_selectedAddress = packet->address;
    m_selectedBytes = packet->bytes;
    m_addressBaseRow = -1;
    if (m_selectedBytes.isEmpty()
        && m_selectedAddress != 0
        && _hook_data.pid != 0) {
        m_selectedBytes.resize(kPacketReadSize);
        if (!PeaceDriver::readMemory(
                _hook_data.pid,
                m_selectedAddress,
                kPacketReadSize,
                m_selectedBytes.data())) {
            m_selectedBytes.clear();
        }
    }
    rebuildMemoryView();
}

void PacketFilterController::setMemoryAddressBase(int row)
{
    const int rowCount = (m_selectedBytes.size() + 15) / 16;
    if (row < 0 || row >= rowCount)
        return;
    m_addressBaseRow = row;
    rebuildMemoryView();
}

void PacketFilterController::setMemoryDisplay(
    int dataType, bool hexadecimal, int textEncoding)
{
    if (dataType < 0 || dataType > 5)
        return;
    if (textEncoding < 0 || textEncoding > 1)
        return;
    m_dataType = dataType;
    m_hexadecimal = hexadecimal;
    m_textEncoding = textEncoding;
    rebuildMemoryView();
}

int PacketFilterController::addFilter()
{
    return m_filters.addDefault();
}

int PacketFilterController::duplicateFilter(int row)
{
    return m_filters.duplicate(row);
}

void PacketFilterController::deleteFilter(int row)
{
    m_filters.remove(row);
}

void PacketFilterController::setFilterActive(int row, bool active)
{
    m_filters.setActive(row, active);
}

void PacketFilterController::setAllFiltersActive(bool active)
{
    m_filters.setAllActive(active);
}

QVariantMap PacketFilterController::filterDetails(int row) const
{
    return m_filters.details(row);
}

bool PacketFilterController::saveFilter(
    int row, const QString& name, int behavior, const QVariantList& values)
{
    return m_filters.update(row, name, behavior, values);
}

void PacketFilterController::rebuildMemoryView()
{
    m_memoryHeader.clear();
    m_memoryAddresses.clear();
    m_memoryDataRows.clear();
    m_memoryTextRows.clear();

    if (m_selectedBytes.isEmpty()) {
        emit memoryViewChanged();
        return;
    }

    int itemSize = 1;
    int cellWidth = 2;
    bool floatingPoint = false;
    switch (m_dataType) {
    case 0:
        itemSize = 1;
        cellWidth = m_hexadecimal ? 2 : 3;
        break;
    case 1:
        itemSize = 2;
        cellWidth = m_hexadecimal ? 4 : 5;
        break;
    case 2:
        itemSize = 4;
        cellWidth = m_hexadecimal ? 8 : 10;
        break;
    case 3:
        itemSize = 8;
        cellWidth = m_hexadecimal ? 16 : 20;
        break;
    case 4:
        itemSize = 4;
        cellWidth = 14;
        floatingPoint = true;
        break;
    case 5:
        itemSize = 8;
        cellWidth = 22;
        floatingPoint = true;
        break;
    default:
        break;
    }

    QStringList headers;
    for (int offset = 0; offset < 16; offset += itemSize) {
        headers.push_back(
            formatHex(static_cast<quint64>(offset), 2).rightJustified(cellWidth));
    }
    m_memoryHeader = headers.join(QLatin1Char(' '));

    for (int rowOffset = 0; rowOffset < m_selectedBytes.size(); rowOffset += 16) {
        if (m_addressBaseRow >= 0) {
            const qint64 relativeOffset =
                static_cast<qint64>(rowOffset)
                - static_cast<qint64>(m_addressBaseRow) * 16;
            m_memoryAddresses.push_back(formatRelativeOffset(relativeOffset));
        } else {
            m_memoryAddresses.push_back(formatAddress(m_selectedAddress + rowOffset));
        }

        QStringList values;
        for (int columnOffset = 0; columnOffset < 16; columnOffset += itemSize) {
            const int offset = rowOffset + columnOffset;
            QString text;
            if (offset + itemSize <= m_selectedBytes.size()) {
                const char* data = m_selectedBytes.constData() + offset;
                if (floatingPoint) {
                    text = m_dataType == 4
                        ? QString::number(readUnaligned<float>(data), 'g', 7)
                        : QString::number(readUnaligned<double>(data), 'g', 15);
                } else {
                    quint64 value = 0;
                    switch (itemSize) {
                    case 1:
                        value = static_cast<quint8>(*data);
                        break;
                    case 2:
                        value = readUnaligned<quint16>(data);
                        break;
                    case 4:
                        value = readUnaligned<quint32>(data);
                        break;
                    case 8:
                        value = readUnaligned<quint64>(data);
                        break;
                    }
                    text = m_hexadecimal
                        ? formatHex(value, itemSize * 2)
                        : QString::number(value);
                }
            }
            values.push_back(text.rightJustified(cellWidth));
        }
        m_memoryDataRows.push_back(values.join(QLatin1Char(' ')));

        QString textRow;
        if (m_textEncoding == 0) {
            for (int i = 0; i < 16 && rowOffset + i < m_selectedBytes.size(); ++i) {
                const quint8 value =
                    static_cast<quint8>(m_selectedBytes.at(rowOffset + i));
                textRow += value >= 0x20 && value <= 0x7E
                    ? QChar(value)
                    : QChar('.');
            }
        } else {
            for (int i = 0; i + 1 < 16 && rowOffset + i + 1 < m_selectedBytes.size(); i += 2) {
                const quint16 value = readUnaligned<quint16>(
                    m_selectedBytes.constData() + rowOffset + i);
                const QChar character(value);
                if (character.isHighSurrogate()
                    && i + 3 < 16
                    && rowOffset + i + 3 < m_selectedBytes.size()) {
                    const quint16 nextValue = readUnaligned<quint16>(
                        m_selectedBytes.constData() + rowOffset + i + 2);
                    const QChar nextCharacter(nextValue);
                    if (nextCharacter.isLowSurrogate()) {
                        textRow += character;
                        textRow += nextCharacter;
                        i += 2;
                        continue;
                    }
                }
                textRow += character.isPrint() && !character.isSurrogate()
                    ? character
                    : QChar('.');
            }
        }
        m_memoryTextRows.push_back(textRow);
    }

    emit memoryViewChanged();
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

    // 当前目标点的数据包缓冲区指针保存在 RSI：
    // lParam 传 RSI，wParam 未使用；界面固定抓取 1024 字节快照。
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
