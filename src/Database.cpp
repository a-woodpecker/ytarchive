#include "Database.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QUuid>
#include <QVariant>

namespace {

QDateTime dtFromDb(const QVariant &v)
{
    if (v.isNull())
        return QDateTime();
    const qint64 secs = v.toLongLong();
    if (secs <= 0)
        return QDateTime();
    return QDateTime::fromSecsSinceEpoch(secs, Qt::UTC);
}

QVariant dtToDb(const QDateTime &dt)
{
    if (!dt.isValid())
        return QVariant(QMetaType(QMetaType::LongLong));
    return dt.toSecsSinceEpoch();
}

VideoInfo readVideo(const QSqlQuery &q)
{
    VideoInfo v;
    v.pk               = q.value("id").toLongLong();
    v.channelPk        = q.value("channel_id").toLongLong();
    v.videoId          = q.value("video_id").toString();
    v.title            = q.value("title").toString();
    v.description      = q.value("description").toString();
    v.thumbUrl         = q.value("thumb_url").toString();
    v.filePath         = q.value("file_path").toString();
    v.infoJsonPath     = q.value("info_json_path").toString();
    v.errorText        = q.value("error_text").toString();
    v.uploadDate       = dtFromDb(q.value("upload_date"));
    v.dateIsApproximate= q.value("date_approx").toInt() != 0;
    v.durationSecs     = q.value("duration").toLongLong();
    v.viewCount        = q.value("view_count").toLongLong();
    v.kind             = static_cast<VideoKind>(q.value("kind").toInt());
    v.likeCount        = q.value("like_count").toLongLong();
    v.sha256           = q.value("sha256").toString();
    v.hashedAt         = dtFromDb(q.value("hashed_at"));
    // Present only on the joined queries used by the video list and by export.
    const QSqlRecord record = q.record();
    const int channelTitleColumn = record.indexOf(QStringLiteral("channel_title"));
    if (channelTitleColumn >= 0)
        v.channelTitle = q.value(channelTitleColumn).toString();
    const int channelIdentColumn = record.indexOf(QStringLiteral("channel_ident"));
    if (channelIdentColumn >= 0)
        v.channelIdent = q.value(channelIdentColumn).toString();
    const int channelUrlColumn = record.indexOf(QStringLiteral("channel_url"));
    if (channelUrlColumn >= 0)
        v.channelUrl = q.value(channelUrlColumn).toString();
    v.fileSize         = q.value("file_size").toLongLong();
    v.state            = static_cast<DownloadState>(q.value("state").toInt());
    return v;
}

} // namespace

Database::~Database()
{
    close();
}

bool Database::open(const QString &path, QString *error)
{
    close();

    QDir().mkpath(QFileInfo(path).absolutePath());

    m_connectionName = QStringLiteral("ytarchive-") +
                       QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
    m_db.setDatabaseName(path);

    if (!m_db.open()) {
        if (error) *error = m_db.lastError().text();
        return false;
    }

    QSqlQuery pragma(m_db);
    pragma.exec(QStringLiteral("PRAGMA journal_mode=WAL"));
    pragma.exec(QStringLiteral("PRAGMA synchronous=NORMAL"));
    pragma.exec(QStringLiteral("PRAGMA foreign_keys=ON"));

    return applySchema(error);
}

void Database::close()
{
    if (m_db.isOpen())
        m_db.close();
    if (!m_connectionName.isEmpty()) {
        m_db = QSqlDatabase();
        QSqlDatabase::removeDatabase(m_connectionName);
        m_connectionName.clear();
    }
}

bool Database::applySchema(QString *error)
{
    const QStringList statements = {
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS channels ("
            "  id           INTEGER PRIMARY KEY,"
            "  channel_id   TEXT NOT NULL UNIQUE,"
            "  handle       TEXT,"
            "  title        TEXT,"
            "  url          TEXT,"
            "  avatar_path  TEXT,"
            "  last_synced  INTEGER)"),
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS videos ("
            "  id             INTEGER PRIMARY KEY,"
            "  video_id       TEXT NOT NULL UNIQUE,"
            "  channel_id     INTEGER NOT NULL REFERENCES channels(id) ON DELETE CASCADE,"
            "  title          TEXT,"
            "  description    TEXT,"
            "  thumb_url      TEXT,"
            "  upload_date    INTEGER,"
            "  date_approx    INTEGER NOT NULL DEFAULT 1,"
            "  duration       INTEGER NOT NULL DEFAULT 0,"
            "  view_count     INTEGER NOT NULL DEFAULT -1,"
            "  file_path      TEXT,"
            "  info_json_path TEXT,"
            "  file_size      INTEGER NOT NULL DEFAULT 0,"
            "  state          INTEGER NOT NULL DEFAULT 0,"
            "  error_text     TEXT,"
            "  first_seen     INTEGER)"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_videos_channel ON videos(channel_id)"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_videos_date ON videos(upload_date DESC)"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_videos_state ON videos(state)"),
        // Added after the first release; ALTER is the only way to reach an
        // existing catalog, and it is harmless to attempt every start-up.
        QStringLiteral("ALTER TABLE videos ADD COLUMN like_count INTEGER NOT NULL DEFAULT -1"),
        QStringLiteral("ALTER TABLE videos ADD COLUMN sha256 TEXT"),
        QStringLiteral("ALTER TABLE videos ADD COLUMN hashed_at INTEGER"),
        QStringLiteral("ALTER TABLE videos ADD COLUMN kind INTEGER NOT NULL DEFAULT 0")
    };

    QSqlQuery q(m_db);
    for (const QString &s : statements) {
        if (q.exec(s))
            continue;
        // A duplicate column means the migration already ran; anything else is
        // a real failure.
        if (s.startsWith(QStringLiteral("ALTER TABLE"))
            && q.lastError().text().contains(QStringLiteral("duplicate column"),
                                             Qt::CaseInsensitive)) {
            continue;
        }
        if (error) *error = q.lastError().text();
        return false;
    }
    return true;
}

qint64 Database::upsertChannel(const ChannelInfo &c)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "INSERT INTO channels (channel_id, handle, title, url, avatar_path, last_synced) "
        "VALUES (:cid, :handle, :title, :url, :avatar, :synced) "
        "ON CONFLICT(channel_id) DO UPDATE SET "
        "  handle      = COALESCE(NULLIF(excluded.handle,''), channels.handle),"
        "  title       = COALESCE(NULLIF(excluded.title,''), channels.title),"
        "  url         = COALESCE(NULLIF(excluded.url,''), channels.url),"
        "  avatar_path = COALESCE(NULLIF(excluded.avatar_path,''), channels.avatar_path),"
        "  last_synced = excluded.last_synced"));
    q.bindValue(":cid",    c.channelId);
    q.bindValue(":handle", c.handle);
    q.bindValue(":title",  c.title);
    q.bindValue(":url",    c.url);
    q.bindValue(":avatar", c.avatarPath);
    q.bindValue(":synced", dtToDb(c.lastSynced.isValid() ? c.lastSynced
                                                         : QDateTime::currentDateTimeUtc()));
    if (!q.exec())
        return -1;

    QSqlQuery sel(m_db);
    sel.prepare(QStringLiteral("SELECT id FROM channels WHERE channel_id = :cid"));
    sel.bindValue(":cid", c.channelId);
    if (sel.exec() && sel.next())
        return sel.value(0).toLongLong();
    return -1;
}

QVector<ChannelInfo> Database::channels()
{
    QVector<ChannelInfo> out;
    QSqlQuery q(m_db);
    const bool ok = q.exec(QStringLiteral(
        "SELECT c.*, "
        "  (SELECT COUNT(*) FROM videos v WHERE v.channel_id = c.id) AS n_total,"
        "  (SELECT COUNT(*) FROM videos v WHERE v.channel_id = c.id AND v.state = 3) AS n_done "
        "FROM channels c ORDER BY LOWER(c.title) ASC"));
    if (!ok)
        return out;
    while (q.next()) {
        ChannelInfo c;
        c.pk         = q.value("id").toLongLong();
        c.channelId  = q.value("channel_id").toString();
        c.handle     = q.value("handle").toString();
        c.title      = q.value("title").toString();
        c.url        = q.value("url").toString();
        c.avatarPath = q.value("avatar_path").toString();
        c.lastSynced = dtFromDb(q.value("last_synced"));
        c.videoCount = q.value("n_total").toInt();
        c.downloadedCount = q.value("n_done").toInt();
        out.append(c);
    }
    return out;
}

ChannelInfo Database::channel(qint64 pk)
{
    for (const ChannelInfo &c : channels())
        if (c.pk == pk)
            return c;
    return ChannelInfo();
}

bool Database::removeChannel(qint64 channelPk)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("DELETE FROM channels WHERE id = :id"));
    q.bindValue(":id", channelPk);
    return q.exec();
}

int Database::upsertVideos(qint64 channelPk, const QVector<VideoInfo> &videos, int *newCount)
{
    if (channelPk < 0)
        return 0;

    m_db.transaction();

    QSqlQuery exists(m_db);
    exists.prepare(QStringLiteral("SELECT 1 FROM videos WHERE video_id = :vid"));

    // Metadata is refreshed on every sync, but file_path / state / info_json_path
    // are deliberately left alone: a re-sync must never forget what we already have.
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "INSERT INTO videos (video_id, channel_id, title, description, thumb_url,"
        "                    upload_date, date_approx, duration, view_count, kind,"
        "                    first_seen) "
        "VALUES (:vid, :cid, :title, :desc, :thumb, :date, :approx, :dur, :views, :kind,"
        "        :seen) "
        "ON CONFLICT(video_id) DO UPDATE SET "
        "  title      = COALESCE(NULLIF(excluded.title,''), videos.title),"
        "  thumb_url  = COALESCE(NULLIF(excluded.thumb_url,''), videos.thumb_url),"
        "  duration   = MAX(excluded.duration, videos.duration),"
        // A past livestream also appears on the uploads tab. Whichever listing
        // identified it as something other than a plain video was the better
        // informed, so a specific kind is never overwritten by the default.
        "  kind = CASE WHEN videos.kind = 0 THEN excluded.kind ELSE videos.kind END,"
        "  view_count = MAX(excluded.view_count, videos.view_count),"
        // Only overwrite the date while ours is still the approximate one.
        "  upload_date = CASE WHEN videos.date_approx = 1 AND excluded.upload_date IS NOT NULL"
        "                     THEN excluded.upload_date ELSE videos.upload_date END"));

    int changed = 0;
    int fresh = 0;
    const qint64 now = QDateTime::currentSecsSinceEpoch();

    for (const VideoInfo &v : videos) {
        if (v.videoId.isEmpty())
            continue;

        exists.bindValue(":vid", v.videoId);
        exists.exec();
        const bool isNew = !exists.next();
        exists.finish();

        q.bindValue(":vid",    v.videoId);
        q.bindValue(":cid",    channelPk);
        q.bindValue(":title",  v.title);
        q.bindValue(":desc",   v.description);
        q.bindValue(":thumb",  v.thumbUrl);
        q.bindValue(":date",   dtToDb(v.uploadDate));
        q.bindValue(":approx", v.dateIsApproximate ? 1 : 0);
        q.bindValue(":dur",    v.durationSecs);
        q.bindValue(":views",  v.viewCount);
        q.bindValue(":kind",   static_cast<int>(v.kind));
        q.bindValue(":seen",   now);
        if (q.exec()) {
            ++changed;
            if (isNew) ++fresh;
        }
    }

    m_db.commit();
    if (newCount) *newCount = fresh;
    return changed;
}

QVector<VideoInfo> Database::videos(qint64 channelPk)
{
    QVector<VideoInfo> out;
    QSqlQuery q(m_db);
    if (channelPk < 0) {
        q.prepare(QStringLiteral(
            "SELECT v.*, c.title AS channel_title, c.channel_id AS channel_ident,"
            "       c.url AS channel_url FROM videos v "
            "JOIN channels c ON c.id = v.channel_id "
            "ORDER BY v.upload_date DESC NULLS LAST, v.id DESC"));
    } else {
        q.prepare(QStringLiteral(
            "SELECT v.*, c.title AS channel_title, c.channel_id AS channel_ident,"
            "       c.url AS channel_url FROM videos v "
            "JOIN channels c ON c.id = v.channel_id "
            "WHERE v.channel_id = :cid "
            "ORDER BY v.upload_date DESC NULLS LAST, v.id DESC"));
        q.bindValue(":cid", channelPk);
    }
    if (!q.exec())
        return out;
    while (q.next())
        out.append(readVideo(q));
    return out;
}

VideoInfo Database::video(qint64 pk)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT * FROM videos WHERE id = :id"));
    q.bindValue(":id", pk);
    if (q.exec() && q.next())
        return readVideo(q);
    return VideoInfo();
}

bool Database::setVideoState(qint64 pk, DownloadState state, const QString &errorText)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "UPDATE videos SET state = :st, error_text = :err WHERE id = :id"));
    q.bindValue(":st",  static_cast<int>(state));
    q.bindValue(":err", errorText);
    q.bindValue(":id",  pk);
    return q.exec();
}

bool Database::recordDownload(qint64 pk,
                              const QString &filePath,
                              const QString &infoJsonPath,
                              qint64 fileSize,
                              const QDateTime &confirmedUploadDate,
                              qint64 likeCount,
                              qint64 viewCount)
{
    // Written separately so a hidden or missing count never overwrites a good
    // one from an earlier download.
    if (likeCount >= 0 || viewCount >= 0) {
        QSqlQuery counts(m_db);
        counts.prepare(QStringLiteral(
            "UPDATE videos SET "
            "  like_count = CASE WHEN :like >= 0 THEN :like ELSE like_count END,"
            "  view_count = CASE WHEN :view >= 0 THEN :view ELSE view_count END "
            "WHERE id = :id"));
        counts.bindValue(":like", likeCount);
        counts.bindValue(":view", viewCount);
        counts.bindValue(":id", pk);
        counts.exec();
    }

    QSqlQuery q(m_db);
    if (confirmedUploadDate.isValid()) {
        q.prepare(QStringLiteral(
            "UPDATE videos SET state = 3, error_text = NULL, file_path = :path,"
            "  info_json_path = :info, file_size = :size,"
            "  upload_date = :date, date_approx = 0 WHERE id = :id"));
        q.bindValue(":date", dtToDb(confirmedUploadDate));
    } else {
        q.prepare(QStringLiteral(
            "UPDATE videos SET state = 3, error_text = NULL, file_path = :path,"
            "  info_json_path = :info, file_size = :size WHERE id = :id"));
    }
    q.bindValue(":path", filePath);
    q.bindValue(":info", infoJsonPath);
    q.bindValue(":size", fileSize);
    q.bindValue(":id",   pk);
    return q.exec();
}

bool Database::setChecksum(qint64 pk, const QString &sha256)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "UPDATE videos SET sha256 = :hash, hashed_at = :now WHERE id = :id"));
    q.bindValue(":hash", sha256);
    q.bindValue(":now", QDateTime::currentSecsSinceEpoch());
    q.bindValue(":id", pk);
    return q.exec();
}

QVector<VideoInfo> Database::videosWithChecksums()
{
    QVector<VideoInfo> out;
    QSqlQuery q(m_db);
    if (!q.exec(QStringLiteral(
            "SELECT v.*, c.title AS channel_title, c.channel_id AS channel_ident,"
            "       c.url AS channel_url FROM videos v "
            "JOIN channels c ON c.id = v.channel_id "
            "WHERE v.state = 3 AND v.sha256 IS NOT NULL AND v.sha256 <> '' "
            "ORDER BY v.upload_date DESC NULLS LAST, v.id DESC")))
        return out;
    while (q.next())
        out.append(readVideo(q));
    return out;
}

bool Database::clearDownload(qint64 pk)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "UPDATE videos SET state = 0, file_path = NULL, info_json_path = NULL,"
        "  file_size = 0, error_text = NULL, sha256 = NULL, hashed_at = NULL "
        "WHERE id = :id"));
    q.bindValue(":id", pk);
    return q.exec();
}

int Database::reconcileWithDisk()
{
    QSqlQuery q(m_db);
    if (!q.exec(QStringLiteral("SELECT id, file_path, state FROM videos WHERE state IN (3,5)")))
        return 0;

    struct Row { qint64 id; bool present; };
    QVector<Row> rows;
    while (q.next()) {
        const qint64 id = q.value(0).toLongLong();
        const QString path = q.value(1).toString();
        rows.append({ id, !path.isEmpty() && QFileInfo::exists(path) });
    }

    m_db.transaction();
    QSqlQuery upd(m_db);
    upd.prepare(QStringLiteral("UPDATE videos SET state = :st WHERE id = :id"));
    int changed = 0;
    for (const Row &r : rows) {
        upd.bindValue(":st", static_cast<int>(r.present ? DownloadState::Downloaded
                                                        : DownloadState::Missing));
        upd.bindValue(":id", r.id);
        if (upd.exec())
            changed += upd.numRowsAffected() > 0 ? 1 : 0;
    }
    m_db.commit();
    return changed;
}

int Database::totalVideos()
{
    QSqlQuery q(m_db);
    if (q.exec(QStringLiteral("SELECT COUNT(*) FROM videos")) && q.next())
        return q.value(0).toInt();
    return 0;
}

int Database::totalDownloaded()
{
    QSqlQuery q(m_db);
    if (q.exec(QStringLiteral("SELECT COUNT(*) FROM videos WHERE state = 3")) && q.next())
        return q.value(0).toInt();
    return 0;
}

qint64 Database::totalBytes()
{
    QSqlQuery q(m_db);
    if (q.exec(QStringLiteral("SELECT COALESCE(SUM(file_size),0) FROM videos WHERE state = 3"))
        && q.next())
        return q.value(0).toLongLong();
    return 0;
}
