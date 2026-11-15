#pragma once

#include "hook/hookcontrol.h"

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

class WindowIdFilterController : public HookControl, public QAbstractNativeEventFilter
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

protected:
    bool nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result) override;

private:
    void handleWindowIdMessage(quint32 windowId, quint64 retAddress);

    ULONG64 hookAddress() const override;
    size_t hookCodeSize() const override;
    bool releaseRemoteOnMissingWindow() const override;
    std::vector<BYTE> buildTrampoline(const HookTrampolineInput& in) override;

    FilterCatalogModel m_filters;
    MessageLogModel m_messages;
};
