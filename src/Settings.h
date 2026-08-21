#pragma once

#include <QProcessEnvironment>
#include <QString>
#include <QStringList>

// Everything the user can tune. Loaded once at startup, written back on change.
struct Settings {
    QString archiveRoot;            // parent folder that holds catalog.db + one folder per channel
    QString ytDlpPath = QStringLiteral("yt-dlp");
    QString ffmpegPath;             // optional; passed to yt-dlp via --ffmpeg-location

    // Passed to yt-dlp as --js-runtimes RUNTIME[:PATH]. Recent yt-dlp needs a
    // JavaScript runtime to solve the service's challenges; only deno is
    // enabled by default, so anything else has to be named explicitly.
    QString jsRuntimes;

    // Download behaviour
    int     maxConcurrent = 2;
    QString formatSelector = QStringLiteral("bv*+ba/b");
    QString mergeContainer = QStringLiteral("mkv");
    int     rateLimitKiB = 0;       // 0 = unlimited
    int     retries = 10;
    int     extractorRetries = 3;

    // Rate limiting defences. All default to off, because they slow every
    // download and are only worth paying for when the service pushes back.
    int     sleepRequests = 0;      // seconds between metadata requests
    int     sleepInterval = 0;      // minimum seconds between videos
    int     maxSleepInterval = 0;   // upper bound; randomised within the range

    // Automatic retry of failures that look transient. Re-running yt-dlp from
    // scratch re-extracts the media URLs, which is what actually fixes a 403,
    // so a retry is far more than a repeat of the same request.
    int     autoRetryAttempts = 3;   // 0 disables
    int     autoRetryDelay = 20;     // seconds; multiplied by the attempt number

    // Appended verbatim to every yt-dlp invocation. This ecosystem changes far
    // faster than this program is rebuilt, so there has to be a way to pass a
    // flag it knows nothing about without waiting for a new version.
    QString extraArguments;

    // Passes --remote-components ejs:github, letting yt-dlp download the
    // JavaScript challenge solver it needs to sign media URLs. Off by default
    // because it fetches code at run time; installing the yt-dlp-ejs package
    // is the better answer where that is possible.
    bool allowRemoteComponents = false;

    // Records a SHA-256 of each finished file, and writes it beside the media
    // as a .sha256 sidecar. Nothing upstream publishes a hash of these files,
    // so this is the only way to detect later corruption.
    bool checksumDownloads = true;

    // Adds -v and stops suppressing warnings, and logs the exact command line
    // for every yt-dlp invocation. The only way to see plugin and PO token
    // diagnostics from inside the application.
    bool    verboseLogging = false;

    // Free-text --extractor-args, e.g. a GVS PO token or a client override.
    // Deliberately not a fixed set of options: what the service demands here
    // changes far faster than this program is rebuilt.
    QString extractorArgs;

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

    // Cookies unlock age-restricted and members-only videos, but they also make
    // the service offer formats that cannot be downloaded, so sending them on
    // every request breaks videos that would otherwise be fine. With this set,
    // a download is attempted without them and retried with them only when the
    // failure says authentication is what was missing.
    bool cookiesOnlyWhenNeeded = true;

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

// Directories that commonly hold user-installed tools but are added to PATH by
// shell startup files. A GUI application launched from a desktop menu never
// sources those, so deno, yt-dlp installed with pipx, and similar are invisible
// to it and to every process it starts.
QStringList additionalToolPaths();

// The system environment with the directories above prepended to PATH. Every
// process this application starts uses it, so a child yt-dlp can find a
// runtime that the desktop session did not put on PATH.
QProcessEnvironment toolProcessEnvironment();

// findExecutable, extended over the same directories.
QString findToolExecutable(const QString &name);

// Turns a channel name into something safe on every filesystem we care about.
QString sanitizeForPath(const QString &in);

// Reads defaults.ini from the application directory, if present. The Windows
// installer writes the archive folder the user chose there.
//
// A file beside the executable rather than the registry, because an elevated
// installer writing HKCU would write the administrator's hive - not the hive of
// the person who will actually run the program.
QString installerProvidedArchiveRoot();
