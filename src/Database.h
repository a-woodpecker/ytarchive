#pragma once

#include "Models.h"

#include <QSqlDatabase>
#include <QString>
#include <QVector>

// The catalog: a SQLite index over an on-disk media tree.
//
// Video files themselves live on the filesystem, not inside the database.
// A single 4K video can exceed SQLite's practical BLOB comfort zone, and
// keeping real files means the archive stays usable even if this program
// disappears - which is the whole point of a preservation archive.
class Database
{
public:
    Database() = default;
    ~Database();

    bool open(const QString &path, QString *error = nullptr);
    void close();
    bool isOpen() const { return m_db.isOpen(); }

    // Channels
    qint64 upsertChannel(const ChannelInfo &c);
    QVector<ChannelInfo> channels();
    bool removeChannel(qint64 channelPk);
    ChannelInfo channel(qint64 pk);

    // Videos
    // Inserts new rows and refreshes metadata on existing ones, without ever
    // clobbering download state or local paths.
    int upsertVideos(qint64 channelPk, const QVector<VideoInfo> &videos, int *newCount = nullptr);
    QVector<VideoInfo> videos(qint64 channelPk = -1);
    VideoInfo video(qint64 pk);

    bool setVideoState(qint64 pk, DownloadState state, const QString &errorText = QString());
    bool recordDownload(qint64 pk,
                        const QString &filePath,
                        const QString &infoJsonPath,
                        qint64 fileSize,
                        const QDateTime &confirmedUploadDate,
                        qint64 likeCount = -1,
                        qint64 viewCount = -1);
    bool clearDownload(qint64 pk);

    // Marks rows whose media file has vanished from disk.
    int reconcileWithDisk();

    int totalVideos();
    int totalDownloaded();
    qint64 totalBytes();

private:
    bool applySchema(QString *error);
    QSqlDatabase m_db;
    QString m_connectionName;
};
