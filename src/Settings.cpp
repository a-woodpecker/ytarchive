#include "Settings.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>

QString installerProvidedArchiveRoot()
{
    const QString iniPath =
        QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("defaults.ini"));
    if (!QFileInfo::exists(iniPath))
        return QString();

    QSettings defaults(iniPath, QSettings::IniFormat);
    return defaults.value(QStringLiteral("archiveRoot")).toString().trimmed();
}

static QString defaultArchiveRoot()
{
    // Whatever the installer recorded wins on first run.
    const QString fromInstaller = installerProvidedArchiveRoot();
    if (!fromInstaller.isEmpty())
        return QDir::cleanPath(fromInstaller);

    QString movies = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
    if (movies.isEmpty())
        movies = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
    return QDir(movies).filePath(QStringLiteral("Archive"));
}

Settings Settings::load()
{
    QSettings s;
    Settings v;
    v.archiveRoot       = s.value("archiveRoot", defaultArchiveRoot()).toString();
    v.ytDlpPath         = s.value("ytDlpPath", v.ytDlpPath).toString();
    v.ffmpegPath        = s.value("ffmpegPath").toString();
    v.maxConcurrent     = s.value("maxConcurrent", v.maxConcurrent).toInt();
    v.formatSelector    = s.value("formatSelector", v.formatSelector).toString();
    v.mergeContainer    = s.value("mergeContainer", v.mergeContainer).toString();
    v.rateLimitKiB      = s.value("rateLimitKiB", 0).toInt();
    v.retries           = s.value("retries", v.retries).toInt();
    v.writeInfoJson     = s.value("writeInfoJson", true).toBool();
    v.writeThumbnail    = s.value("writeThumbnail", true).toBool();
    v.writeDescription  = s.value("writeDescription", true).toBool();
    v.writeSubtitles    = s.value("writeSubtitles", true).toBool();
    v.writeAutoSubs     = s.value("writeAutoSubs", false).toBool();
    v.writeComments     = s.value("writeComments", false).toBool();
    v.embedMetadata     = s.value("embedMetadata", true).toBool();
    v.embedChapters     = s.value("embedChapters", true).toBool();
    v.cookiesFromBrowser= s.value("cookiesFromBrowser").toString();
    v.cookiesFile       = s.value("cookiesFile").toString();
    v.filenameTemplate  = s.value("filenameTemplate", v.filenameTemplate).toString();
    v.theme             = s.value("theme", v.theme).toString();
    v.checkUpdatesOnStartup = s.value("checkUpdatesOnStartup", true).toBool();
    v.skippedVersion    = s.value("skippedVersion").toString();
    v.lastUpdateCheck   = s.value("lastUpdateCheck", 0).toLongLong();
    if (v.maxConcurrent < 1) v.maxConcurrent = 1;
    return v;
}

void Settings::save() const
{
    QSettings s;
    s.setValue("archiveRoot", archiveRoot);
    s.setValue("ytDlpPath", ytDlpPath);
    s.setValue("ffmpegPath", ffmpegPath);
    s.setValue("maxConcurrent", maxConcurrent);
    s.setValue("formatSelector", formatSelector);
    s.setValue("mergeContainer", mergeContainer);
    s.setValue("rateLimitKiB", rateLimitKiB);
    s.setValue("retries", retries);
    s.setValue("writeInfoJson", writeInfoJson);
    s.setValue("writeThumbnail", writeThumbnail);
    s.setValue("writeDescription", writeDescription);
    s.setValue("writeSubtitles", writeSubtitles);
    s.setValue("writeAutoSubs", writeAutoSubs);
    s.setValue("writeComments", writeComments);
    s.setValue("embedMetadata", embedMetadata);
    s.setValue("embedChapters", embedChapters);
    s.setValue("cookiesFromBrowser", cookiesFromBrowser);
    s.setValue("cookiesFile", cookiesFile);
    s.setValue("filenameTemplate", filenameTemplate);
    s.setValue("theme", theme);
    s.setValue("checkUpdatesOnStartup", checkUpdatesOnStartup);
    s.setValue("skippedVersion", skippedVersion);
    s.setValue("lastUpdateCheck", lastUpdateCheck);
}

QString Settings::databasePath() const
{
    return QDir(archiveRoot).filePath(QStringLiteral("catalog.db"));
}

QString Settings::channelDir(const QString &channelTitle, const QString &channelId) const
{
    QString name = sanitizeForPath(channelTitle);
    if (name.isEmpty())
        name = channelId;
    else if (!channelId.isEmpty())
        name += QStringLiteral(" [") + channelId + QChar(']');
    return QDir(archiveRoot).filePath(name);
}

QString Settings::thumbnailCacheDir() const
{
    return QDir(archiveRoot).filePath(QStringLiteral(".cache/thumbnails"));
}

QString sanitizeForPath(const QString &in)
{
    static const QString illegal = QStringLiteral("<>:\"/\\|?*");
    QString out;
    out.reserve(in.size());
    for (QChar c : in) {
        if (illegal.contains(c) || c.unicode() < 0x20)
            out += QChar('_');
        else
            out += c;
    }
    // Trailing dots and spaces are not allowed on Windows.
    while (!out.isEmpty() && (out.endsWith(QChar('.')) || out.endsWith(QChar(' '))))
        out.chop(1);
    return out.left(120).trimmed();
}
