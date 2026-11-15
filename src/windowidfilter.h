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

constexpr UINT WM_MY_CUSTOM_MESSAGE = WM_USER + 100;
constexpr ULONG64 kHookCodeSize = 14;

template <typename Fn>
inline void for_each_cpu(Fn const fn) {
    SYSTEM_INFO info = {};
    GetSystemInfo(&info);

    for (uint32_t i = 0; i < info.dwNumberOfProcessors; ++i) {
        auto const prev_affinity = SetThreadAffinityMask(GetCurrentThread(), 1ull << i);
        fn(i);
        SetThreadAffinityMask(GetCurrentThread(), prev_affinity);
    }
}

inline std::vector<byte> operator+(std::vector<byte> a, std::vector<byte> b)
{
    for (int i = 0; i < b.size(); i++)
    {
        a.push_back(b[i]);
    }

    b.clear();

    return a;
}

inline std::vector<byte> intToBytes(int length)
{
    __int64  adr = (__int64)&length;
    std::vector<byte>c;
    for (size_t i = 0; i < 4; i++) {
        c.push_back(*(byte*)adr++);
    }
    return c;
}

inline std::vector<byte> longToBytes(long long length)
{
    __int64  adr = (__int64)&length;
    std::vector<byte>c;
    for (size_t i = 0; i < 8; i++) {
        c.push_back(*(byte*)adr++);
    }
    return c;
}

inline std::vector<byte> makeByteArray(std::vector<byte> data)
{
    return data;
}

class FilterCatalogModel : public QAbstractListModel
{
    Q_OBJECT

public:
    struct Entry {
        quint32 id = 0;
        QString name;
        bool active = false;
    };

    enum Roles {
        ActiveRole = Qt::UserRole + 1,
        IdTextRole,
        NameRole
    };

    explicit FilterCatalogModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setActive(int row, bool active);
    QString nameForId(quint32 id) const;
    bool isActive(quint32 id) const;

private:
    std::vector<Entry> m_entries;
};

class MessageLogModel : public QAbstractListModel
{
    Q_OBJECT

public:
    struct Row {
        QString time;
        QString kind;
        QString title;
    };

    enum Roles {
        TimeRole = Qt::UserRole + 1,
        KindRole,
        TitleRole
    };

    explicit MessageLogModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    void append(const QString& time, const QString& kind, const QString& title);
    void clear();

private:
    std::vector<Row> m_rows;
};

class WindowIdFilterController : public QObject, public QAbstractNativeEventFilter
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString processName READ processName WRITE setProcessName NOTIFY processNameChanged)
    Q_PROPERTY(QString notice READ notice NOTIFY noticeChanged)
    Q_PROPERTY(QAbstractListModel* filters READ filters CONSTANT)
    Q_PROPERTY(QAbstractListModel* messages READ messages CONSTANT)

public:
    explicit WindowIdFilterController(QObject* parent = nullptr);
    ~WindowIdFilterController() override;

    QString processName() const;
    void setProcessName(const QString& name);
    QString notice() const;
    QAbstractListModel* filters();
    QAbstractListModel* messages();

    Q_INVOKABLE void setFilterActive(int row, bool active);
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
    void handleWindowIdMessage(quint32 windowId);

    QString m_processName = QStringLiteral("DFO.exe");
    QString m_notice;
    FilterCatalogModel m_filters;
    MessageLogModel m_messages;

private:
    struct HOOKDATA{
        bool is_hooked = false;
        DWORD pid = 0;
        ULONG64 hook_address = 0;
        ULONG64 allocated_address = 0;
        ULONG64 shadowpage_address = 0;
        uint64_t hook_pfn = 0;
        ULONG64 target_cr3 = 0;
        bool hook_execute_patched = false;
        BYTE raw_bytecode[kHookCodeSize] = {0};
    }_hook_data;

    bool hook(DWORD pid);
    bool unhook();
    HWND hostWindowHandle() const;

    QPointer<QWindow> m_hostWindow;
};
