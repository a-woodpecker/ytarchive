#pragma once

#include "Models.h"
#include "Settings.h"

#include <QByteArray>
#include <QObject>
#include <QProcess>

// Enumerates a channel's uploads in two stages so the UI can show real progress:
//
//   1. Probe   - fetches only the playlist header (no entries). Cheap, and it
//                gives us the channel's name to display, plus an expected item
//                count when YouTube reports one.
//   2. Listing - streams one JSON object per video as yt-dlp discovers them, so
//                the count climbs live instead of appearing all at once.
//
// The second stage is why this uses --dump-json rather than --dump-single-json:
// a single JSON document only arrives once the whole channel has been walked,
// which leaves nothing to report until the work is already finished.
class ChannelSync : public QObject
{
    Q_OBJECT
public:
    explicit ChannelSync(QObject* parent = nullptr);
    ~ChannelSync() override;

    void setSettings(const Settings& s) { m_settings = s; }
    bool isRunning() const { return m_stage != Stage::Idle; }

    // `input` may be a handle, a channel URL, or a bare channel id.
    void start(const QString& input);
    void cancel();

signals:
    void statusChanged(const QString& message);
    // `total` is -1 when the channel's size is not known in advance, which is
    // the common case. Callers should show an indeterminate bar plus the count.
    void progress(int loaded, int total);
    void finished(const ChannelInfo& channel, const QVector<VideoInfo>& videos);
    void failed(const QString& message);
    void cancelled();

private:
    enum class Stage { Idle, Probing, Listing };

    void startProbe();
    void startListing();
    void handleProbeFinished(int exitCode);
    void handleListingReadyRead();
    void handleListingFinished(int exitCode);
    void consumeEntryLine(const QByteArray& line);
    void teardown();
    QProcess* makeProcess();

    Settings   m_settings;
    QProcess* m_process = nullptr;
    Stage      m_stage = Stage::Idle;

    QString    m_url;
    ChannelInfo m_channel;
    QVector<VideoInfo> m_videos;

    QByteArray m_stdoutBuffer;   // holds a partial trailing line between reads
    QByteArray m_probeStdout;
    QByteArray m_stderr;

    int  m_expectedTotal = -1;
    bool m_cancelRequested = false;
};