#include "ArchiveScanner.h"

#include "Database.h"
#include "Models.h"
#include "YtDlp.h"

#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>
#include <QSet>

namespace Archive {

namespace {

// Only containers this program produces or could plausibly have written.
bool isMedia(const QString &suffix)
{
    static const QSet<QString> kinds{
        QStringLiteral("mkv"), QStringLiteral("mp4"), QStringLiteral("webm"),
        QStringLiteral("m4a"), QStringLiteral("mov"), QStringLiteral("avi"),
    };
    return kinds.contains(suffix.toLower());
}

// Every "[...]" group in a filename. The title may contain brackets of its own,
// so each candidate is looked up rather than the first being assumed correct.
QStringList bracketedTokens(const QString &fileName)
{
    static const QRegularExpression re(QStringLiteral("\\[([A-Za-z0-9_-]{4,64})\\]"));
    QStringList tokens;
    auto it = re.globalMatch(fileName);
    while (it.hasNext())
        tokens << it.next().captured(1);
    return tokens;
}

QString readSidecarDigest(const QString &mediaPath)
{
    const QFileInfo fi(mediaPath);
    const QString path =
        fi.absoluteDir().filePath(fi.completeBaseName() + QStringLiteral(".sha256"));
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return QString();
    // "<digest>  <filename>"
    const QString first = QString::fromUtf8(f.readLine()).trimmed();
    const QString digest = first.section(QChar(' '), 0, 0).trimmed();
    static const QRegularExpression hex(QStringLiteral("^[0-9a-fA-F]{64}$"));
    return hex.match(digest).hasMatch() ? digest.toLower() : QString();
}

} // namespace

ScanResult adopt(Database &db, const QString &root)
{
    ScanResult result;
    if (root.isEmpty() || !QFileInfo(root).isDir())
        return result;

    // Index the catalog by video id, so each file is a single lookup.
    QHash<QString, VideoInfo> wanted;
    for (const VideoInfo &v : db.videos())
        wanted.insert(v.videoId, v);
    if (wanted.isEmpty()) {
        result.ok = true;
        return result;
    }

    QDirIterator it(root, QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        const QFileInfo fi(path);

        // Partial downloads are not archived copies.
        if (path.contains(QStringLiteral("/.incomplete/"))
            || path.contains(QStringLiteral("\\.incomplete\\")))
            continue;
        if (!isMedia(fi.suffix()))
            continue;

        ++result.filesSeen;

        VideoInfo match;
        bool found = false;
        for (const QString &token : bracketedTokens(fi.fileName())) {
            const auto entry = wanted.constFind(token);
            if (entry != wanted.constEnd()) {
                match = entry.value();
                found = true;
                break;
            }
        }
        if (!found) {
            ++result.unmatched;
            continue;
        }

        if (match.state == DownloadState::Downloaded && match.filePath == path) {
            ++result.alreadyArchived;
            continue;
        }

        // The sidecars are found by name, exactly as they were written.
        const QString infoJson = YtDlp::infoJsonPathFor(path);
        const bool haveInfoJson = QFileInfo::exists(infoJson);

        qint64 likeCount = -1;
        qint64 viewCount = -1;
        QDateTime uploadDate;
        if (haveInfoJson) {
            uploadDate = YtDlp::uploadDateFromInfoJson(infoJson);
            YtDlp::countsFromInfoJson(infoJson, &likeCount, &viewCount);
        }

        db.recordDownload(match.pk, path, haveInfoJson ? infoJson : QString(),
                          fi.size(), uploadDate, likeCount, viewCount);

        // A digest written by a previous run is adopted as-is rather than
        // recomputed: it is a record of what the file was when archived, and
        // re-hashing here would quietly bless a file that had since changed.
        const QString digest = readSidecarDigest(path);
        if (!digest.isEmpty())
            db.setChecksum(match.pk, digest);

        ++result.adopted;
    }

    result.ok = true;
    return result;
}

} // namespace Archive
