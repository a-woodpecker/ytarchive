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
    v.jsRuntimes        = s.value("jsRuntimes").toString();
    v.maxConcurrent     = s.value("maxConcurrent", v.maxConcurrent).toInt();
    v.formatSelector    = s.value("formatSelector", v.formatSelector).toString();
    v.mergeContainer    = s.value("mergeContainer", v.mergeContainer).toString();
    v.rateLimitKiB      = s.value("rateLimitKiB", 0).toInt();
    v.retries           = s.value("retries", v.retries).toInt();
    v.extractorRetries  = s.value("extractorRetries", v.extractorRetries).toInt();
    v.sleepRequests     = s.value("sleepRequests", 0).toInt();
    v.sleepInterval     = s.value("sleepInterval", 0).toInt();
    v.maxSleepInterval  = s.value("maxSleepInterval", 0).toInt();
    v.extractorArgs     = s.value("extractorArgs").toString();
    v.extraArguments    = s.value("extraArguments").toString();
    v.verboseLogging    = s.value("verboseLogging", false).toBool();
    v.checksumDownloads = s.value("checksumDownloads", true).toBool();
    v.allowRemoteComponents = s.value("allowRemoteComponents", false).toBool();
    v.autoRetryAttempts = s.value("autoRetryAttempts", v.autoRetryAttempts).toInt();
    v.autoRetryDelay    = s.value("autoRetryDelay", v.autoRetryDelay).toInt();
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
    v.cookiesOnlyWhenNeeded = s.value("cookiesOnlyWhenNeeded", true).toBool();
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
    s.setValue("jsRuntimes", jsRuntimes);
    s.setValue("maxConcurrent", maxConcurrent);
    s.setValue("formatSelector", formatSelector);
    s.setValue("mergeContainer", mergeContainer);
    s.setValue("rateLimitKiB", rateLimitKiB);
    s.setValue("retries", retries);
    s.setValue("extractorRetries", extractorRetries);
    s.setValue("sleepRequests", sleepRequests);
    s.setValue("sleepInterval", sleepInterval);
    s.setValue("maxSleepInterval", maxSleepInterval);
    s.setValue("extractorArgs", extractorArgs);
    s.setValue("extraArguments", extraArguments);
    s.setValue("verboseLogging", verboseLogging);
    s.setValue("checksumDownloads", checksumDownloads);
    s.setValue("allowRemoteComponents", allowRemoteComponents);
    s.setValue("autoRetryAttempts", autoRetryAttempts);
    s.setValue("autoRetryDelay", autoRetryDelay);
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
    s.setValue("cookiesOnlyWhenNeeded", cookiesOnlyWhenNeeded);
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

QStringList additionalToolPaths()
{
    const QString home = QDir::homePath();
    QStringList dirs{
        home + QStringLiteral("/.deno/bin"),      // deno's own installer
        home + QStringLiteral("/.local/bin"),     // pipx, pip --user
        home + QStringLiteral("/.bun/bin"),
        home + QStringLiteral("/.cargo/bin"),
        home + QStringLiteral("/bin"),
        QStringLiteral("/usr/local/bin"),
        QStringLiteral("/opt/homebrew/bin"),      // Apple silicon Homebrew
    };
#ifdef Q_OS_WIN
    const QString profile = qEnvironmentVariable("USERPROFILE");
    if (!profile.isEmpty()) {
        dirs << profile + QStringLiteral("/.deno/bin")
             << profile + QStringLiteral("/.bun/bin");
    }
#endif

    QStringList existing;
    for (const QString &d : dirs)
        if (QFileInfo::exists(d))
            existing << QDir::cleanPath(d);
    return existing;
}

QProcessEnvironment toolProcessEnvironment()
{
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    const QStringList extras = additionalToolPaths();
    if (extras.isEmpty())
        return env;

    const QString key = QStringLiteral("PATH");
    const QString current = env.value(key);
    const QChar sep = QDir::listSeparator();

    // Prepended, so a user-installed tool beats an older packaged one.
    QStringList merged = extras;
    for (const QString &p : current.split(sep, Qt::SkipEmptyParts))
        if (!merged.contains(p))
            merged << p;

    env.insert(key, merged.join(sep));
    return env;
}

QString findToolExecutable(const QString &name)
{
    const QString onPath = QStandardPaths::findExecutable(name);
    if (!onPath.isEmpty())
        return onPath;
    return QStandardPaths::findExecutable(name, additionalToolPaths());
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
