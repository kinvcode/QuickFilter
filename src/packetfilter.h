#pragma once

#include <QAbstractListModel>
#include <QAbstractNativeEventFilter>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QWindow>
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

class PacketFilterController : public QObject, public QAbstractNativeEventFilter
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString processName READ processName WRITE setProcessName NOTIFY processNameChanged)
    Q_PROPERTY(QString notice READ notice NOTIFY noticeChanged)
    Q_PROPERTY(QAbstractListModel* messages READ messages CONSTANT)

public:
    explicit PacketFilterController(QObject* parent = nullptr);
    ~PacketFilterController() override;

    QString processName() const;
    void setProcessName(const QString& name);
    QString notice() const;
    QAbstractListModel* messages();

    Q_INVOKABLE void startHook();
    Q_INVOKABLE bool prepareClose();
    Q_INVOKABLE void bindWindow(QWindow* window);

signals:
    void processNameChanged();
    void noticeChanged();

protected:
    bool nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result) override;

private:
    void setNotice(const QString& notice);
    void handlePacketMessage(quint64 pointer);

    QString m_processName = QStringLiteral("DFO.exe");
    QString m_notice;
    quint32 m_sequence = 0;
    PacketLogModel m_messages;

private:
    struct HOOKDATA {
        bool is_hooked = false;
        DWORD pid = 0;
        ULONG64 hook_address = 0;
        ULONG64 allocated_address = 0;
        ULONG64 shadowpage_address = 0;
        uint64_t hook_pfn = 0;
        ULONG64 target_cr3 = 0;
        bool hook_execute_patched = false;
        BYTE raw_bytecode[kPacketHookCodeSize] = {0};
    }_hook_data;

    bool hook(DWORD pid);
    bool unhook();
    HWND hostWindowHandle() const;

    QPointer<QWindow> m_hostWindow;
};
