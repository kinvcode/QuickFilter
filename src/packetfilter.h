#pragma once

#include "hook/hookcontrol.h"

#include <QAbstractListModel>
#include <QAbstractNativeEventFilter>
#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>
#include <array>
#include <vector>
#include <Windows.h>

constexpr UINT WM_PACKET_CUSTOM_MESSAGE = WM_USER + 101;
constexpr ULONG64 kPacketHookCodeSize = 13;
constexpr int kPacketReadSize = 1024;

// 发包报文模型：序号、时间、指针、快照和高亮状态
class PacketLogModel : public QAbstractListModel
{
    Q_OBJECT

public:
    struct Row {
        QString seq;
        QString time;
        QString pointer;
        quint64 address = 0;
        QByteArray bytes;
        bool highlighted = false;
    };

    enum Roles {
        SeqRole = Qt::UserRole + 1,
        TimeRole,
        PointerRole,
        HighlightedRole
    };

    explicit PacketLogModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    void append(Row row);
    const Row* rowAt(int row) const;
    Q_INVOKABLE void clear();

private:
    std::vector<Row> m_rows;
};

class PacketFilterModel : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Behavior {
        HighlightBehavior = 0,
        FilterBehavior = 1
    };
    Q_ENUM(Behavior)

    struct Entry {
        bool active = false;
        QString name = QStringLiteral("新滤镜");
        Behavior behavior = HighlightBehavior;
        std::array<qint16, kPacketReadSize> pattern = {};

        Entry()
        {
            pattern.fill(-1);
        }
    };

    enum Roles {
        ActiveRole = Qt::UserRole + 1,
        NameRole,
        BehaviorRole,
        BehaviorTextRole,
        FilledCountRole
    };

    enum MatchResult {
        NoMatch = 0,
        HighlightMatch = 1,
        FilterMatch = 2
    };

    explicit PacketFilterModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    int addDefault();
    int duplicate(int row);
    bool remove(int row);
    bool setActive(int row, bool active);
    void setAllActive(bool active);
    bool allActive() const;
    QVariantMap details(int row) const;
    bool update(int row, const QString& name, int behavior, const QVariantList& values);
    MatchResult evaluate(const QByteArray& bytes) const;

signals:
    void summaryChanged();

private:
    static int filledCount(const Entry& entry);
    static bool matches(const Entry& entry, const QByteArray& bytes);

    std::vector<Entry> m_entries;
};

class PacketFilterController : public HookControl, public QAbstractNativeEventFilter
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QAbstractListModel* messages READ messages CONSTANT)
    Q_PROPERTY(QAbstractListModel* filters READ filters CONSTANT)
    Q_PROPERTY(int filterCount READ filterCount NOTIFY filtersSummaryChanged)
    Q_PROPERTY(bool allFiltersActive READ allFiltersActive NOTIFY filtersSummaryChanged)
    Q_PROPERTY(bool collectionPaused READ collectionPaused NOTIFY collectionPausedChanged)
    Q_PROPERTY(QString selectedPacketAddress READ selectedPacketAddress NOTIFY memoryViewChanged)
    Q_PROPERTY(QString memoryHeader READ memoryHeader NOTIFY memoryViewChanged)
    Q_PROPERTY(QStringList memoryAddresses READ memoryAddresses NOTIFY memoryViewChanged)
    Q_PROPERTY(QStringList memoryDataRows READ memoryDataRows NOTIFY memoryViewChanged)
    Q_PROPERTY(QStringList memoryTextRows READ memoryTextRows NOTIFY memoryViewChanged)

public:
    explicit PacketFilterController(QObject* parent = nullptr);
    ~PacketFilterController() override;

    QAbstractListModel* messages();
    QAbstractListModel* filters();
    int filterCount() const;
    bool allFiltersActive() const;
    bool collectionPaused() const;

    QString selectedPacketAddress() const;
    QString memoryHeader() const;
    QStringList memoryAddresses() const;
    QStringList memoryDataRows() const;
    QStringList memoryTextRows() const;

    Q_INVOKABLE void clearPackets();
    Q_INVOKABLE void pauseCollection();
    Q_INVOKABLE void resumeCollection();
    Q_INVOKABLE void selectPacket(int row);
    Q_INVOKABLE void setMemoryAddressBase(int row);
    Q_INVOKABLE void setMemoryDisplay(int dataType, bool hexadecimal, int textEncoding);

    Q_INVOKABLE int addFilter();
    Q_INVOKABLE int duplicateFilter(int row);
    Q_INVOKABLE void deleteFilter(int row);
    Q_INVOKABLE void setFilterActive(int row, bool active);
    Q_INVOKABLE void setAllFiltersActive(bool active);
    Q_INVOKABLE QVariantMap filterDetails(int row) const;
    Q_INVOKABLE bool saveFilter(
        int row, const QString& name, int behavior, const QVariantList& values);

signals:
    void filtersSummaryChanged();
    void collectionPausedChanged();
    void memoryViewChanged();

protected:
    bool nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result) override;

private:
    void handlePacketMessage(quint64 pointer);
    void rebuildMemoryView();

    ULONG64 hookAddress() const override;
    size_t hookCodeSize() const override;
    bool releaseRemoteOnMissingWindow() const override;
    std::vector<BYTE> buildTrampoline(const HookTrampolineInput& in) override;

    quint32 m_sequence = 0;
    PacketLogModel m_messages;
    PacketFilterModel m_filters;
    bool m_collectionPaused = false;
    quint64 m_selectedAddress = 0;
    QByteArray m_selectedBytes;
    int m_addressBaseRow = -1;
    int m_dataType = 0;
    bool m_hexadecimal = true;
    int m_textEncoding = 0;
    QString m_memoryHeader;
    QStringList m_memoryAddresses;
    QStringList m_memoryDataRows;
    QStringList m_memoryTextRows;
};
