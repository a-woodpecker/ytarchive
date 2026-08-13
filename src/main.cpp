#include "MainWindow.h"

#include <QApplication>
#include <QIcon>
#include <QNetworkProxyFactory>
#include <QSettings>
#include <QStyleFactory>

namespace {

// The program was called "YouTube Archive" up to 0.1.0, and QSettings keys the
// store on the application name. Without this, upgrading silently resets every
// preference, the archive folder and the window layout.
void migrateLegacySettings()
{
    QSettings current;
    if (!current.allKeys().isEmpty())
        return;   // already migrated, or a genuinely fresh install

    QSettings legacy(QStringLiteral("YtArchive"), QStringLiteral("YouTube Archive"));
    const QStringList keys = legacy.allKeys();
    if (keys.isEmpty())
        return;

    for (const QString &key : keys)
        current.setValue(key, legacy.value(key));
    current.sync();
}

} // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    QCoreApplication::setOrganizationName(QStringLiteral("YtArchive"));
    QCoreApplication::setApplicationName(QStringLiteral("YT Archive"));
    QCoreApplication::setApplicationVersion(QStringLiteral(YTA_VERSION));

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
