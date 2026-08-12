#pragma once

#include "Models.h"
#include "Settings.h"

#include <QJsonObject>
#include <QStringList>

// Thin, well-documented seam around the yt-dlp command line.
// Keeping every argument in one place makes the tool's behaviour auditable,
// which matters when the output is meant to be an archive of record.
namespace YtDlp {

    // Arguments that list a channel's uploads without resolving each video
    // (fast: one request per page instead of one per video).
    //
    // Uses --dump-json, not --dump-single-json: one JSON object per line, streamed
    // as videos are discovered, so the caller can report progress as it goes.
    QStringList channelListArgs(const QString& channelUrl);

    // Arguments that fetch only the playlist header - used to resolve a pasted
    // URL into a canonical channel id and title before committing it to the catalog.
    QStringList channelProbeArgs(const QString& channelUrl);

    // Full download invocation for one video.
    // `printFile` receives the final on-disk path of the merged media file.
    QStringList downloadArgs(const VideoInfo& video,
        const QString& destinationDir,
        const QString& printFile,
        const Settings& settings);

    // Normalises whatever the user pasted (handle, /c/ URL, /channel/UC..., a video
    // URL) into a channel uploads URL. Returns an empty string if unusable.
    QString normalizeChannelUrl(const QString& input);

    // Parsers
    ChannelInfo parseChannelHeader(const QJsonObject& root);

    // Fills any empty fields of `channel` from a video entry. Used when the header
    // probe fails and the channel's identity has to come from the entries.
    void mergeChannelFromEntry(ChannelInfo& channel, const QJsonObject& entry);
    VideoInfo   parseFlatEntry(const QJsonObject& entry);

    // Reads a downloaded .info.json and returns the authoritative upload time.
    QDateTime uploadDateFromInfoJson(const QString& infoJsonPath);

    // The sidecar path yt-dlp uses for a given media file.
    QString infoJsonPathFor(const QString& mediaPath);

    // Human-readable summary of a failed run, pulled out of yt-dlp's stderr.
    QString extractError(const QString& stderrText);

} // namespace YtDlp