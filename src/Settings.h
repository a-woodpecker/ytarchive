#pragma once

#include <QString>
#include <QStringList>

// Everything the user can tune. Loaded once at startup, written back on change.
struct Settings {
    QString archiveRoot;            // parent folder that holds catalog.db + one folder per channel
    QString ytDlpPath = QStringLiteral("yt-dlp");
    QString ffmpegPath;             // optional; passed to yt-dlp via --ffmpeg-location

    // Download behaviour
    int     maxConcurrent = 2;
    QString formatSelector = QStringLiteral("bv*+ba/b");
    QString mergeContainer = QStringLiteral("mkv");
    int     rateLimitKiB = 0;       // 0 = unlimited
    int     retries = 10;

    // Sidecar artefacts worth keeping for a preservation archive
    bool writeInfoJson  = true;
    bool writeThumbnail = true;
    bool writeDescription = true;
    bool writeSubtitles = true;
    bool writeAutoSubs  = false;
    bool writeComments  = false;    // slow; off by default
    bool embedMetadata  = true;
    bool embedChapters  = true;

    // Auth for age-gated or members-only material you have access to
    QString cookiesFromBrowser;     // e.g. "firefox", "chrome:Default"
    QString cookiesFile;

    QString filenameTemplate =
        QStringLiteral("%(upload_date>%Y-%m-%d)s [%(id)s] %(title).120B.%(ext)s");

    // Appearance: "dark", "light" or "system"
    QString theme = QStringLiteral("dark");

    // Updates
    bool    checkUpdatesOnStartup = true;
    QString skippedVersion;         // "don't tell me about this one again"
    qint64  lastUpdateCheck = 0;    // unix seconds; throttles startup checks

    static Settings load();
    void save() const;

    QString databasePath() const;
    QString channelDir(const QString &channelTitle, const QString &channelId) const;
    QString thumbnailCacheDir() const;
};

// Turns a channel name into something safe on every filesystem we care about.
QString sanitizeForPath(const QString &in);

// Reads defaults.ini from the application directory, if present. The Windows
// installer writes the archive folder the user chose there.
//
// A file beside the executable rather than the registry, because an elevated
// installer writing HKCU would write the administrator's hive - not the hive of
// the person who will actually run the program.
QString installerProvidedArchiveRoot();
