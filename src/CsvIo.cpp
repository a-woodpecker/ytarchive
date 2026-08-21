#include "CsvIo.h"

#include "Database.h"

#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QCoreApplication>
#include <QSaveFile>
#include <QSet>
#include <QTextStream>

namespace Csv {

QString escape(const QString &field)
{
    const bool needsQuotes = field.contains(QChar(',')) || field.contains(QChar('"'))
                             || field.contains(QChar('\n')) || field.contains(QChar('\r'))
                             || field.startsWith(QChar(' ')) || field.endsWith(QChar(' '));
    if (!needsQuotes)
        return field;

    QString quoted = field;
    quoted.replace(QChar('"'), QStringLiteral("\"\""));
    return QChar('"') + quoted + QChar('"');
}

QString row(const QStringList &fields)
{
    QStringList escaped;
    escaped.reserve(fields.size());
    for (const QString &field : fields)
        escaped << escape(field);
    return escaped.join(QChar(',')) + QStringLiteral("\r\n");
}

QVector<QStringList> parse(const QString &text)
{
    QVector<QStringList> rows;
    QStringList current;
    QString field;
    bool inQuotes = false;
    bool fieldStarted = false;

    // A byte order mark would otherwise become part of the first header name.
    int i = (!text.isEmpty() && text.at(0) == QChar(0xFEFF)) ? 1 : 0;

    auto endField = [&] {
        current << field;
        field.clear();
        fieldStarted = false;
    };
    auto endRow = [&] {
        endField();
        // A trailing newline should not produce a final empty row.
        if (!(current.size() == 1 && current.first().isEmpty()))
            rows << current;
        current.clear();
    };

    for (; i < text.size(); ++i) {
        const QChar c = text.at(i);

        if (inQuotes) {
            if (c != QChar('"')) {
                field += c;
            } else if (i + 1 < text.size() && text.at(i + 1) == QChar('"')) {
                field += QChar('"');   // a doubled quote is one literal quote
                ++i;
            } else {
                inQuotes = false;
            }
            continue;
        }

        if (c == QChar('"') && !fieldStarted) {
            inQuotes = true;
            fieldStarted = true;
        } else if (c == QChar(',')) {
            endField();
        } else if (c == QChar('\n')) {
            endRow();
        } else if (c == QChar('\r')) {
            // Part of CRLF, or a lone CR from an old spreadsheet.
            if (i + 1 < text.size() && text.at(i + 1) == QChar('\n'))
                ++i;
            endRow();
        } else {
            field += c;
            fieldStarted = true;
        }
    }

    // Whatever is left after the last delimiter is a final row.
    if (!field.isEmpty() || !current.isEmpty())
        endRow();

    return rows;
}

namespace {

const char *kHeaders[] = {
    "channel_id", "channel_title", "channel_url",
    "video_id", "url", "title", "kind",
    "upload_date", "date_approximate", "duration_seconds",
    "view_count", "like_count",
    "state", "file_path", "file_size", "sha256",
};

QString kindToText(VideoKind kind)
{
    switch (kind) {
    case VideoKind::Short:      return QStringLiteral("short");
    case VideoKind::Livestream: return QStringLiteral("livestream");
    case VideoKind::Video:      break;
    }
    return QStringLiteral("video");
}

VideoKind kindFromText(const QString &text)
{
    const QString t = text.trimmed().toLower();
    if (t == QLatin1String("short"))      return VideoKind::Short;
    if (t == QLatin1String("livestream") || t == QLatin1String("stream"))
        return VideoKind::Livestream;
    return VideoKind::Video;
}

QString stateToText(DownloadState state)
{
    switch (state) {
    case DownloadState::Downloaded:  return QStringLiteral("archived");
    case DownloadState::Failed:      return QStringLiteral("failed");
    case DownloadState::Missing:     return QStringLiteral("missing");
    case DownloadState::Queued:      return QStringLiteral("queued");
    case DownloadState::Downloading: return QStringLiteral("downloading");
    case DownloadState::NotDownloaded: break;
    }
    return QStringLiteral("not_downloaded");
}

} // namespace

bool exportVideos(const QVector<VideoInfo> &videos, const QString &path, QString *error)
{
    // Written through QSaveFile so a failure part way leaves the previous file
    // untouched rather than a truncated one.
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (error) *error = file.errorString();
        return false;
    }

    QString out;
    out.reserve(videos.size() * 200);
    out += QChar(0xFEFF);           // BOM, so spreadsheets read UTF-8 properly

    QStringList headers;
    for (const char *h : kHeaders)
        headers << QString::fromLatin1(h);
    out += row(headers);

    for (const VideoInfo &v : videos) {
        out += row({
            v.channelIdent,
            v.channelTitle,
            v.channelUrl,
            v.videoId,
            v.watchUrl(),
            v.title,
            kindToText(v.kind),
            v.uploadDate.isValid() ? v.uploadDate.toUTC().toString(Qt::ISODate) : QString(),
            v.dateIsApproximate ? QStringLiteral("yes") : QStringLiteral("no"),
            v.durationSecs > 0 ? QString::number(v.durationSecs) : QString(),
            v.viewCount >= 0 ? QString::number(v.viewCount) : QString(),
            v.likeCount >= 0 ? QString::number(v.likeCount) : QString(),
            stateToText(v.state),
            v.filePath,
            v.fileSize > 0 ? QString::number(v.fileSize) : QString(),
            v.sha256,
        });
    }

    if (file.write(out.toUtf8()) < 0) {
        if (error) *error = file.errorString();
        return false;
    }
    if (!file.commit()) {
        if (error) *error = file.errorString();
        return false;
    }
    return true;
}

ImportResult importCatalog(Database &db, const QString &path)
{
    ImportResult result;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        result.problems << QCoreApplication::translate(
            "Csv", "Could not open %1: %2").arg(QFileInfo(path).fileName(),
                                                file.errorString());
        return result;
    }

    const QVector<QStringList> rows = parse(QString::fromUtf8(file.readAll()));
    file.close();

    if (rows.size() < 2) {
        result.problems << QCoreApplication::translate(
            "Csv", "The file has no rows beneath its header.");
        return result;
    }

    // Column order is read from the header, so a file with columns rearranged,
    // removed or added still imports.
    QHash<QString, int> column;
    const QStringList header = rows.first();
    for (int i = 0; i < header.size(); ++i)
        column.insert(header.at(i).trimmed().toLower(), i);

    auto field = [&column](const QStringList &r, const char *name) -> QString {
        const int index = column.value(QString::fromLatin1(name), -1);
        return (index >= 0 && index < r.size()) ? r.at(index).trimmed() : QString();
    };

    if (!column.contains(QStringLiteral("video_id"))
        && !column.contains(QStringLiteral("url"))) {
        result.problems << QCoreApplication::translate(
            "Csv", "No \"video_id\" or \"url\" column was found.");
        return result;
    }

    // Existing videos, so the result can distinguish added from already known.
    QSet<QString> known;
    for (const VideoInfo &v : db.videos())
        known.insert(v.videoId);

    QHash<QString, QVector<VideoInfo>> byChannel;   // channel id -> videos
    QHash<QString, ChannelInfo> channels;

    for (int i = 1; i < rows.size(); ++i) {
        const QStringList &r = rows.at(i);
        if (r.isEmpty() || (r.size() == 1 && r.first().isEmpty()))
            continue;

        QString videoId = field(r, "video_id");
        if (videoId.isEmpty()) {
            // Recover the id from a watch URL when the column is absent.
            const QString url = field(r, "url");
            const int marker = url.indexOf(QStringLiteral("v="));
            if (marker >= 0)
                videoId = url.mid(marker + 2).section(QChar('&'), 0, 0);
        }
        if (videoId.isEmpty()) {
            ++result.rowsSkipped;
            continue;
        }

        QString channelId = field(r, "channel_id");
        const QString channelTitle = field(r, "channel_title");
        const QString channelUrl = field(r, "channel_url");
        if (channelId.isEmpty())
            channelId = channelUrl.isEmpty() ? channelTitle : channelUrl;
        if (channelId.isEmpty()) {
            ++result.rowsSkipped;
            if (result.problems.size() < 5) {
                result.problems << QCoreApplication::translate(
                    "Csv", "Row %1 names no channel.").arg(i + 1);
            }
            continue;
        }

        if (!channels.contains(channelId)) {
            ChannelInfo c;
            c.channelId = channelId;
            c.title = channelTitle.isEmpty() ? channelId : channelTitle;
            c.url = channelUrl;
            channels.insert(channelId, c);
        }

        VideoInfo v;
        v.videoId = videoId;
        v.title = field(r, "title");
        v.kind = kindFromText(field(r, "kind"));
        v.durationSecs = field(r, "duration_seconds").toLongLong();
        v.viewCount = field(r, "view_count").isEmpty() ? -1
                                                       : field(r, "view_count").toLongLong();
        v.likeCount = field(r, "like_count").isEmpty() ? -1
                                                       : field(r, "like_count").toLongLong();

        const QString date = field(r, "upload_date");
        if (!date.isEmpty()) {
            v.uploadDate = QDateTime::fromString(date, Qt::ISODate);
            if (!v.uploadDate.isValid()) {
                // Accept a plain date as well as a full timestamp.
                const QDate plain = QDate::fromString(date, QStringLiteral("yyyy-MM-dd"));
                if (plain.isValid())
                    v.uploadDate = QDateTime(plain, QTime(0, 0), Qt::UTC);
            }
            v.dateIsApproximate =
                field(r, "date_approximate").compare(QStringLiteral("yes"),
                                                     Qt::CaseInsensitive) == 0;
        }

        // Everything describing a file is left out on purpose. Those columns
        // describe the disk of whichever machine produced the export.
        if (known.contains(videoId))
            ++result.videosAlreadyKnown;
        else
            ++result.videosAdded;

        byChannel[channelId].append(v);
    }

    for (auto it = channels.cbegin(); it != channels.cend(); ++it) {
        const qint64 pk = db.upsertChannel(it.value());
        if (pk < 0) {
            result.problems << QCoreApplication::translate(
                "Csv", "Could not add the channel \"%1\".").arg(it.value().title);
            continue;
        }
        ++result.channelsAdded;
        db.upsertVideos(pk, byChannel.value(it.key()));
    }

    result.ok = true;
    return result;
}

} // namespace Csv
