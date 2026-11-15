#pragma once

#include "hook/filterhost.h"

#include <QAbstractListModel>
#include <QAbstractNativeEventFilter>
#include <QString>
#include <QtQml/qqmlregistration.h>
#include <vector>
#include <Windows.h>

constexpr UINT WM_MY_CUSTOM_MESSAGE = WM_USER + 100;
constexpr ULONG64 kHookCodeSize = 14;

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
        QString returnAddress;
    };

    enum Roles {
        TimeRole = Qt::UserRole + 1,
        KindRole,
        TitleRole,
        ReturnAddressRole
    };

    explicit MessageLogModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    void append(const QString& time, const QString& kind, const QString& title, const QString& returnAddress);
    void clear();

private:
    std::vector<Row> m_rows;
};

class WindowIdFilterController : public FilterHost, public QAbstractNativeEventFilter
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QAbstractListModel* filters READ filters CONSTANT)
    Q_PROPERTY(QAbstractListModel* messages READ messages CONSTANT)

public:
    explicit WindowIdFilterController(QObject* parent = nullptr);
    ~WindowIdFilterController() override;

    QAbstractListModel* filters();
    QAbstractListModel* messages();

    Q_INVOKABLE void setFilterActive(int row, bool active);
    Q_INVOKABLE void startHook();
    Q_INVOKABLE bool prepareClose();

protected:
    bool nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result) override;

private:
    void handleWindowIdMessage(quint32 windowId, quint64 retAddress);

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
};
