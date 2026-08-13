#include "YtDlp.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QUrl>

namespace YtDlp {

namespace {

// Common flags for every invocation: never colourise, never touch the user's
// shell config, and always emit machine-readable progress.
QStringList baseArgs()
{
    return {
        QStringLiteral("--no-colors"),
        QStringLiteral("--no-warnings"),
        QStringLiteral("--ignore-config"),
        QStringLiteral("--no-playlist-reverse")
    };
}

// Only deno is enabled by default, so any other runtime must be named. Applied
// to every invocation: extraction needs it as much as downloading does.
void appendRuntimeArgs(QStringList &args, const Settings &s)
{
    if (!s.jsRuntimes.trimmed().isEmpty())
        args << QStringLiteral("--js-runtimes") << s.jsRuntimes.trimmed();
}

// User-supplied extractor arguments. yt-dlp accepts the flag repeatedly, so
// this never conflicts with the ones the program sets itself.
void appendExtractorArgs(QStringList &args, const Settings &s)
{
    const QString extra = s.extractorArgs.trimmed();
    if (!extra.isEmpty())
        args << QStringLiteral("--extractor-args") << extra;
}

// Slowing down is the only lever against IP-based rate limiting.
void appendPacingArgs(QStringList &args, const Settings &s)
{
    if (s.sleepRequests > 0)
        args << QStringLiteral("--sleep-requests") << QString::number(s.sleepRequests);
    if (s.sleepInterval > 0) {
        args << QStringLiteral("--sleep-interval") << QString::number(s.sleepInterval);
        if (s.maxSleepInterval > s.sleepInterval) {
            args << QStringLiteral("--max-sleep-interval")
                 << QString::number(s.maxSleepInterval);
        }
    }
}

void appendAuthArgs(QStringList &args, const Settings &s)
{
    if (!s.cookiesFromBrowser.isEmpty())
        args << QStringLiteral("--cookies-from-browser") << s.cookiesFromBrowser;
    if (!s.cookiesFile.isEmpty())
        args << QStringLiteral("--cookies") << s.cookiesFile;
}

QString bestThumbnail(const QJsonObject &obj)
{
    // Prefer the largest thumbnail yt-dlp reports; fall back to the flat field.
    const QJsonArray thumbs = obj.value(QStringLiteral("thumbnails")).toArray();
    QString best;
    int bestWidth = -1;
    for (const QJsonValue &tv : thumbs) {
        const QJsonObject t = tv.toObject();
        const int w = t.value(QStringLiteral("width")).toInt(0);
        const QString url = t.value(QStringLiteral("url")).toString();
        if (url.isEmpty())
            continue;
        // Anything wider than ~640 is wasted in a grid card.
        if (w > bestWidth && w <= 800) {
            bestWidth = w;
            best = url;
        }
        if (best.isEmpty())
            best = url;
    }
    if (best.isEmpty())
        best = obj.value(QStringLiteral("thumbnail")).toString();
    return best;
}

} // namespace

QStringList channelProbeArgs(const QString &channelUrl, const Settings &settings)
{
    QStringList args = baseArgs();
    appendRuntimeArgs(args, settings);
    appendExtractorArgs(args, settings);
    args << QStringLiteral("--flat-playlist")
         << QStringLiteral("--playlist-items") << QStringLiteral("0")  // header only
         << QStringLiteral("--dump-single-json")
         << channelUrl;
    return args;
}

QStringList channelListArgs(const QString &channelUrl, const Settings &settings)
{
    QStringList args = baseArgs();
    appendRuntimeArgs(args, settings);
    appendExtractorArgs(args, settings);
    appendPacingArgs(args, settings);
    args << QStringLiteral("--flat-playlist")
         // One JSON object per line, flushed as each video is found. The
         // single-document form would withhold everything until the end.
         << QStringLiteral("--dump-json")
         << QStringLiteral("--ignore-errors")
         // Asks the YouTube tab extractor to estimate upload dates during a flat
         // listing. Without this, a flat listing carries no dates at all.
         << QStringLiteral("--extractor-args") << QStringLiteral("youtubetab:approximate_date")
         << channelUrl;
    return args;
}

QStringList downloadArgs(const VideoInfo &video,
                         const QString &destinationDir,
                         const QString &printFile,
                         const Settings &settings)
{
    QStringList args = baseArgs();

    args << QStringLiteral("--newline")
         << QStringLiteral("--no-playlist")
         << QStringLiteral("--retries") << QString::number(settings.retries)
         << QStringLiteral("--fragment-retries") << QString::number(settings.retries)
         << QStringLiteral("--continue");

    // We stamp files ourselves from the upload date, so yt-dlp must not set
    // mtime from the HTTP Last-Modified header.
    args << QStringLiteral("--no-mtime");

    args << QStringLiteral("-f") << settings.formatSelector;
    if (!settings.mergeContainer.isEmpty())
        args << QStringLiteral("--merge-output-format") << settings.mergeContainer;

    if (settings.rateLimitKiB > 0)
        args << QStringLiteral("--limit-rate") << QString::number(settings.rateLimitKiB) + QStringLiteral("K");

    if (!settings.ffmpegPath.isEmpty())
        args << QStringLiteral("--ffmpeg-location") << settings.ffmpegPath;

    // Sidecar artefacts.
    if (settings.writeInfoJson)    args << QStringLiteral("--write-info-json");
    if (settings.writeThumbnail)   args << QStringLiteral("--write-thumbnail");
    if (settings.writeDescription) args << QStringLiteral("--write-description");
    if (settings.writeComments)    args << QStringLiteral("--write-comments");
    if (settings.writeSubtitles) {
        args << QStringLiteral("--write-subs")
             << QStringLiteral("--sub-langs") << QStringLiteral("all,-live_chat");
    }
    if (settings.writeAutoSubs)    args << QStringLiteral("--write-auto-subs");
    if (settings.embedMetadata)    args << QStringLiteral("--embed-metadata");
    if (settings.embedChapters)    args << QStringLiteral("--embed-chapters");

    args << QStringLiteral("--extractor-retries")
         << QString::number(qMax(0, settings.extractorRetries));

    appendRuntimeArgs(args, settings);
    appendExtractorArgs(args, settings);
    appendPacingArgs(args, settings);
    appendAuthArgs(args, settings);

    // Output layout: finished files in the channel folder, partial files in a
    // sibling .part directory so an interrupted run never litters the archive.
    args << QStringLiteral("--paths") << QStringLiteral("home:") + QDir::toNativeSeparators(destinationDir)
         << QStringLiteral("--paths") << QStringLiteral("temp:") + QDir::toNativeSeparators(
                QDir(destinationDir).filePath(QStringLiteral(".incomplete")))
         << QStringLiteral("-o") << settings.filenameTemplate;

    // After the final move, yt-dlp writes the resulting path here. That is the
    // only reliable way to learn the real filename once templates, merging and
    // extension changes have been applied.
    args << QStringLiteral("--print-to-file")
         << QStringLiteral("after_move:filepath")
         << QDir::toNativeSeparators(printFile);

    // Machine-readable progress on stdout, one line per tick.
    args << QStringLiteral("--progress-template")
         << QStringLiteral("download:@P@|%(progress.downloaded_bytes)s"
                           "|%(progress.total_bytes,progress.total_bytes_estimate)s"
                           "|%(progress.speed)s|%(progress.eta)s");

    args << video.watchUrl();
    return args;
}

QString normalizeChannelUrl(const QString &input)
{
    QString s = input.trimmed();
    if (s.isEmpty())
        return QString();

    // Bare handle: "@veritasium"
    if (s.startsWith(QChar('@')))
        return QStringLiteral("https://www.youtube.com/") + s + QStringLiteral("/videos");

    // Bare channel id: "UCxxxxxxxx"
    static const QRegularExpression rawId(QStringLiteral("^UC[A-Za-z0-9_-]{22}$"));
    if (rawId.match(s).hasMatch())
        return QStringLiteral("https://www.youtube.com/channel/") + s + QStringLiteral("/videos");

    if (!s.startsWith(QStringLiteral("http")))
        s.prepend(QStringLiteral("https://"));

    QUrl url(s);
    if (!url.isValid() || !url.host().contains(QStringLiteral("youtube")))
        return QString();

    QString path = url.path();
    while (path.endsWith(QChar('/')))
        path.chop(1);

    // Already pointing at a tab we understand? Redirect it to /videos.
    static const QRegularExpression tab(
        QStringLiteral("/(videos|streams|shorts|playlists|featured|community|about)$"));
    path.remove(tab);

    if (path.isEmpty())
        return QString();

    return QStringLiteral("https://www.youtube.com") + path + QStringLiteral("/videos");
}

ChannelInfo parseChannelHeader(const QJsonObject &root)
{
    ChannelInfo c;
    c.channelId = root.value(QStringLiteral("channel_id")).toString();
    if (c.channelId.isEmpty())
        c.channelId = root.value(QStringLiteral("uploader_id")).toString();
    if (c.channelId.isEmpty())
        c.channelId = root.value(QStringLiteral("id")).toString();

    c.title = root.value(QStringLiteral("channel")).toString();
    if (c.title.isEmpty())
        c.title = root.value(QStringLiteral("uploader")).toString();
    if (c.title.isEmpty())
        c.title = root.value(QStringLiteral("title")).toString();

    // Tab listings arrive as "Channel Name - Videos". Strip unconditionally:
    // which field carries the suffix varies between extractor versions.
    static const QRegularExpression tabSuffix(
        QStringLiteral("\\s+-\\s+(Videos|Shorts|Live|Streams)$"));
    c.title.remove(tabSuffix);

    const QString uploaderId = root.value(QStringLiteral("uploader_id")).toString();
    if (uploaderId.startsWith(QChar('@')))
        c.handle = uploaderId;

    c.url = root.value(QStringLiteral("channel_url")).toString();
    if (c.url.isEmpty())
        c.url = root.value(QStringLiteral("uploader_url")).toString();
    if (c.url.isEmpty() && !c.channelId.isEmpty())
        c.url = QStringLiteral("https://www.youtube.com/channel/") + c.channelId;

    c.lastSynced = QDateTime::currentDateTimeUtc();
    return c;
}

void mergeChannelFromEntry(ChannelInfo &channel, const QJsonObject &entry)
{
    auto pick = [&entry](std::initializer_list<const char *> keys) -> QString {
        for (const char *k : keys) {
            const QString v = entry.value(QLatin1String(k)).toString();
            if (!v.isEmpty())
                return v;
        }
        return QString();
    };

    if (channel.channelId.isEmpty())
        channel.channelId = pick({ "channel_id", "uploader_id" });
    if (channel.title.isEmpty())
        channel.title = pick({ "channel", "uploader", "playlist_title", "playlist" });
    if (channel.url.isEmpty())
        channel.url = pick({ "channel_url", "uploader_url" });

    if (channel.handle.isEmpty()) {
        const QString uploaderId = entry.value(QStringLiteral("uploader_id")).toString();
        if (uploaderId.startsWith(QChar('@')))
            channel.handle = uploaderId;
    }

    // A playlist title arrives as "Channel Name - Videos"; strip the tab suffix.
    if (!channel.title.isEmpty()) {
        static const QRegularExpression suffix(
            QStringLiteral("\\s+-\\s+(Videos|Shorts|Live|Streams)$"));
        channel.title.remove(suffix);
    }

    if (channel.url.isEmpty() && !channel.channelId.isEmpty())
        channel.url = QStringLiteral("https://www.youtube.com/channel/") + channel.channelId;
}

VideoInfo parseFlatEntry(const QJsonObject &entry)
{
    VideoInfo v;
    v.videoId = entry.value(QStringLiteral("id")).toString();
    v.title   = entry.value(QStringLiteral("title")).toString();
    v.description = entry.value(QStringLiteral("description")).toString();
    v.durationSecs = static_cast<qint64>(entry.value(QStringLiteral("duration")).toDouble(0));

    const QJsonValue views = entry.value(QStringLiteral("view_count"));
    v.viewCount = views.isNull() || views.isUndefined()
                      ? -1
                      : static_cast<qint64>(views.toDouble(-1));

    v.thumbUrl = bestThumbnail(entry);

    // In a flat listing these timestamps are estimates supplied by the tab
    // extractor. The exact date is confirmed from info.json after download.
    qint64 ts = 0;
    for (const char *key : { "timestamp", "release_timestamp" }) {
        const QJsonValue val = entry.value(QLatin1String(key));
        if (!val.isNull() && !val.isUndefined() && val.toDouble(0) > 0) {
            ts = static_cast<qint64>(val.toDouble(0));
            break;
        }
    }
    if (ts > 0) {
        v.uploadDate = QDateTime::fromSecsSinceEpoch(ts, Qt::UTC);
        v.dateIsApproximate = true;
    }

    const QString uploadDate = entry.value(QStringLiteral("upload_date")).toString();
    if (uploadDate.size() == 8) {
        const QDate d = QDate::fromString(uploadDate, QStringLiteral("yyyyMMdd"));
        if (d.isValid()) {
            v.uploadDate = QDateTime(d, QTime(0, 0), Qt::UTC);
            v.dateIsApproximate = false;
        }
    }

    return v;
}

QDateTime uploadDateFromInfoJson(const QString &infoJsonPath)
{
    QFile f(infoJsonPath);
    if (!f.open(QIODevice::ReadOnly))
        return QDateTime();

    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isObject())
        return QDateTime();
    const QJsonObject o = doc.object();

    // "timestamp" is the precise publication instant when YouTube exposes it.
    const QJsonValue ts = o.value(QStringLiteral("timestamp"));
    if (!ts.isNull() && !ts.isUndefined() && ts.toDouble(0) > 0)
        return QDateTime::fromSecsSinceEpoch(static_cast<qint64>(ts.toDouble(0)), Qt::UTC);

    // Otherwise fall back to the date-only field, anchored at UTC midnight.
    const QString ud = o.value(QStringLiteral("upload_date")).toString();
    if (ud.size() == 8) {
        const QDate d = QDate::fromString(ud, QStringLiteral("yyyyMMdd"));
        if (d.isValid())
            return QDateTime(d, QTime(0, 0), Qt::UTC);
    }
    return QDateTime();
}

QString infoJsonPathFor(const QString &mediaPath)
{
    QFileInfo fi(mediaPath);
    return fi.absoluteDir().filePath(fi.completeBaseName() + QStringLiteral(".info.json"));
}

QString extractError(const QString &stderrText)
{
    const QStringList lines = stderrText.split(QChar('\n'), Qt::SkipEmptyParts);
    for (auto it = lines.crbegin(); it != lines.crend(); ++it) {
        const QString line = it->trimmed();
        if (line.startsWith(QStringLiteral("ERROR:"), Qt::CaseInsensitive))
            return line.mid(6).trimmed();
    }
    return lines.isEmpty() ? QString() : lines.last().trimmed();
}

} // namespace YtDlp
