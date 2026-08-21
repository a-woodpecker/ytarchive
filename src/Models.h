#pragma once

#include <QString>
#include <QDateTime>
#include <QVector>

// Which channel tab a video came from. The service keeps regular uploads,
// shorts and past livestreams on separate tabs, and a flat listing of one says
// nothing about the others.
enum class VideoKind {
    Video      = 0,
    Short      = 1,
    Livestream = 2
};

QString videoKindLabel(VideoKind kind);

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
    QString   channelTitle;   // filled in by queries, for the "All videos" view
    VideoKind kind = VideoKind::Video;
    qint64    durationSecs = 0;
    qint64    viewCount = -1;
    // Only known once a video has been downloaded: a flat channel listing does
    // not carry it, so it stays -1 until the info.json is read.
    qint64    likeCount = -1;

    // Fixity: a digest of the file as written, and when it was taken.
    QString   sha256;
    QDateTime hashedAt;
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
