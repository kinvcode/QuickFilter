#include "driver/PeaceDriverAPI.h"
#include "driver/peacedriverload.h"
#include "vmp/VMProtectSDK.h"

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>

namespace {

QString loadDriverMessage()
{
    VMProtectBegin("loadDriver");
    QString message;
    if (!PeaceDriver::pingREG()) {
        PeaceDriverLoad driver;
        if (!driver.driverIsLoaded()) {
            const PeaceDriverLoad::LoadDriverResult result = driver.loadPeaceDriver();
            if (!result.success)
                message = QString::fromWCharArray(result.message);
            if(!PeaceDriver::enableVT(true))
                message = QStringLiteral("VT启动失败");
        }
        if (message.isEmpty() && !PeaceDriver::pingREG())
            message = QStringLiteral("驱动初始化失败");
    }
    VMProtectEnd();
    return message;
}

} // namespace

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);

    qDebug() << "加载qml引擎~";
    const QString driverMessage = loadDriverMessage();
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("driverMessage", driverMessage);
    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        []() { QCoreApplication::exit(-1); },
        Qt::QueuedConnection);
    engine.loadFromModule("QuickFilter", "Main");
    qDebug() << "qml引擎启动完毕！";
    return app.exec();
}
