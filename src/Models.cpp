#include "Models.h"

#include <QCoreApplication>
#include <QLocale>

QString downloadStateLabel(DownloadState s)
{
    switch (s) {
    case DownloadState::NotDownloaded: return QCoreApplication::translate("state", "Not downloaded");
    case DownloadState::Queued:        return QCoreApplication::translate("state", "Queued");
    case DownloadState::Downloading:   return QCoreApplication::translate("state", "Downloading");
    case DownloadState::Downloaded:    return QCoreApplication::translate("state", "In archive");
    case DownloadState::Failed:        return QCoreApplication::translate("state", "Failed");
    case DownloadState::Missing:       return QCoreApplication::translate("state", "File missing");
    }
    return QString();
}

QString videoKindLabel(VideoKind kind)
{
    switch (kind) {
    case VideoKind::Short:      return QCoreApplication::translate("kind", "Short");
    case VideoKind::Livestream: return QCoreApplication::translate("kind", "Livestream");
    case VideoKind::Video:      break;
    }
    return QCoreApplication::translate("kind", "Video");
}

QString formatDuration(qint64 seconds)
{
    if (seconds <= 0)
        return QString();
    const qint64 h = seconds / 3600;
    const qint64 m = (seconds % 3600) / 60;
    const qint64 s = seconds % 60;
    if (h > 0)
        return QString::asprintf("%lld:%02lld:%02lld", h, m, s);
    return QString::asprintf("%lld:%02lld", m, s);
}

QString formatBytes(qint64 bytes)
{
    if (bytes <= 0)
        return QStringLiteral("—");
    return QLocale().formattedDataSize(bytes, 1, QLocale::DataSizeIecFormat);
}

QString formatCount(qint64 n)
{
    if (n < 0)
        return QString();
    if (n < 1000)
        return QString::number(n);
    if (n < 1'000'000)
        return QString::number(n / 1000.0, 'f', n < 10'000 ? 1 : 0) + QStringLiteral("K");
    if (n < 1'000'000'000)
        return QString::number(n / 1'000'000.0, 'f', n < 10'000'000 ? 1 : 0) + QStringLiteral("M");
    return QString::number(n / 1'000'000'000.0, 'f', 1) + QStringLiteral("B");
}
