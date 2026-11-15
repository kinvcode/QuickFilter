#include "hook/filterhost.h"

FilterHost::FilterHost(QObject* parent)
    : QObject(parent)
{
}

QString FilterHost::processName() const
{
    return m_processName;
}

void FilterHost::setProcessName(const QString& name)
{
    if (m_processName == name)
        return;
    m_processName = name;
    emit processNameChanged();
}

QString FilterHost::notice() const
{
    return m_notice;
}

void FilterHost::setNotice(const QString& notice)
{
    if (m_notice == notice)
        return;
    m_notice = notice;
    emit noticeChanged();
}

void FilterHost::bindWindow(QWindow* window)
{
    m_hostWindow = window;
}

HWND FilterHost::hostWindowHandle() const
{
    QWindow* window = m_hostWindow;
    if (!window) {
        for (QObject* object = parent(); object; object = object->parent()) {
            window = qobject_cast<QWindow*>(object);
            if (window)
                break;
        }
    }
    if (!window)
        return nullptr;
    return reinterpret_cast<HWND>(window->winId());
}
