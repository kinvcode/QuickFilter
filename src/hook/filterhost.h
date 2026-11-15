#pragma once

#include <QObject>
#include <QPointer>
#include <QString>
#include <QWindow>
#include <Windows.h>

// 两个过滤窗口共用的界面状态：进程名、提示、QML 窗口句柄
class FilterHost : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString processName READ processName WRITE setProcessName NOTIFY processNameChanged)
    Q_PROPERTY(QString notice READ notice NOTIFY noticeChanged)

public:
    explicit FilterHost(QObject* parent = nullptr);

    QString processName() const;
    void setProcessName(const QString& name);
    QString notice() const;

    Q_INVOKABLE void bindWindow(QWindow* window);

signals:
    void processNameChanged();
    void noticeChanged();

protected:
    void setNotice(const QString& notice);
    HWND hostWindowHandle() const;

    QString m_processName = QStringLiteral("DFO.exe");
    QString m_notice;

private:
    QPointer<QWindow> m_hostWindow;
};
