#pragma once

#include <QString>
#include <QDateTime>
#include <QVector>

// Lifecycle of a single video inside the archive.
enum class DownloadState {
    NotDownloaded = 0,
    Queued        = 1,
    Downloading   = 2,
    Downloaded    = 3,
    Failed        = 4,
    Missing       = 5   // catalogued as downloaded, but the file is gone from disk
};

QString downloadStateLabel(DownloadState s);

struct ChannelInfo {
    qint64    pk = -1;              // primary key in our catalog
    QString   channelId;            // canonical "UC..." id from YouTube
    QString   handle;               // "@somechannel", when known
    QString   title;
    QString   url;                  // canonical channel URL
    QString   avatarPath;           // local file, may be empty
    QDateTime lastSynced;
    int       videoCount = 0;
    int       downloadedCount = 0;
};

struct VideoInfo {
    qint64    pk = -1;
    qint64    channelPk = -1;
    QString   videoId;
    QString   title;
    QString   description;
    QString   thumbUrl;
    QString   filePath;             // absolute path once downloaded
    QString   infoJsonPath;
    QString   errorText;
    QDateTime uploadDate;
    bool      dateIsApproximate = true;  // true until confirmed from the real info.json
    qint64    durationSecs = 0;
    qint64    viewCount = -1;
    qint64    fileSize = 0;
    DownloadState state = DownloadState::NotDownloaded;

    // Transient, view-only state (never persisted).
    bool    checked = false;
    double  progress = 0.0;         // 0.0 - 1.0
    QString progressText;

    QString watchUrl() const { return QStringLiteral("https://www.youtube.com/watch?v=") + videoId; }
};

QString formatDuration(qint64 seconds);
QString formatBytes(qint64 bytes);
QString formatCount(qint64 n);
