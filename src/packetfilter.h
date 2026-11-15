#pragma once

#include "hook/hookcontrol.h"

#include <QAbstractListModel>
#include <QAbstractNativeEventFilter>
#include <QString>
#include <QtQml/qqmlregistration.h>
#include <vector>
#include <Windows.h>

constexpr UINT WM_PACKET_CUSTOM_MESSAGE = WM_USER + 101;
constexpr ULONG64 kPacketHookCodeSize = 13;

// 发包报文模型：序号、时间、指针（lParam，即 r9）
class PacketLogModel : public QAbstractListModel
{
    Q_OBJECT

public:
    struct Row {
        QString seq;
        QString time;
        QString pointer;
    };

    enum Roles {
        SeqRole = Qt::UserRole + 1,
        TimeRole,
        PointerRole
    };

    explicit PacketLogModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    void append(const QString& seq, const QString& time, const QString& pointer);
    void clear();

private:
    std::vector<Row> m_rows;
};

class PacketFilterController : public HookControl, public QAbstractNativeEventFilter
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QAbstractListModel* messages READ messages CONSTANT)

public:
    explicit PacketFilterController(QObject* parent = nullptr);
    ~PacketFilterController() override;

    QAbstractListModel* messages();

protected:
    bool nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result) override;

private:
    void handlePacketMessage(quint64 pointer);

    ULONG64 hookAddress() const override;
    bool hook(DWORD pid) override;

    quint32 m_sequence = 0;
    PacketLogModel m_messages;
};
