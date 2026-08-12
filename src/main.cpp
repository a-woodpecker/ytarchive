#include "MainWindow.h"

#include <QApplication>
#include <QStyleFactory>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    QCoreApplication::setOrganizationName(QStringLiteral("YtArchive"));
    QCoreApplication::setApplicationName(QStringLiteral("YouTube Archive"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));

    // Fusion responds predictably to the stylesheet on every platform;
    // the native styles ignore parts of it.
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));

    MainWindow window;
    window.show();

    return app.exec();
}
