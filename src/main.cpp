#include "MainWindow.h"

#include <QApplication>
#include <QNetworkProxyFactory>
#include <QStyleFactory>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);

    QCoreApplication::setOrganizationName(QStringLiteral("YtArchive"));
    QCoreApplication::setApplicationName(QStringLiteral("YouTube Archive"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));

    // yt-dlp picks up the system proxy on its own; Qt's network stack does not
    // unless asked. Without this, thumbnails fail behind a proxy while every
    // download still succeeds - a confusing split to debug.
    QNetworkProxyFactory::setUseSystemConfiguration(true);

    // Fusion responds predictably to the stylesheet on every platform;
    // the native styles ignore parts of it.
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));

    MainWindow window;
    window.show();

    return app.exec();
}