#include "ChannelSync.h"
#include "YtDlp.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

ChannelSync::ChannelSync(QObject *parent)
    : QObject(parent)
{
}

ChannelSync::~ChannelSync()
{
    if (m_process) {
        m_process->disconnect(this);
        m_process->kill();
        m_process->waitForFinished(1000);
    }
}

void ChannelSync::start(const QString &input)
{
    if (isRunning()) {
        emit failed(tr("A channel sync is already running."));
        return;
    }

    m_url = YtDlp::normalizeChannelUrl(input);
    if (m_url.isEmpty()) {
        emit failed(tr("That does not look like a YouTube channel. Try a handle such as "
                       "@channelname, or a full channel URL."));
        return;
    }

    m_channel = ChannelInfo();
    m_videos.clear();
    m_stdoutBuffer.clear();
    m_probeStdout.clear();
    m_stderr.clear();
    m_expectedTotal = -1;
    m_cancelRequested = false;

    startProbe();
}

QProcess *ChannelSync::makeProcess()
{
    auto *proc = new QProcess(this);
    proc->setProcessEnvironment(toolProcessEnvironment());
    proc->setProgram(m_settings.ytDlpPath);

    connect(proc, &QProcess::readyReadStandardError, this, [this] {
        if (m_process)
            m_stderr += m_process->readAllStandardError();
    });
    connect(proc, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e != QProcess::FailedToStart)
            return;
        const QString path = m_settings.ytDlpPath;
        teardown();
        emit failed(tr("Could not run yt-dlp (looked for \"%1\"). Install it, or set its "
                       "full path in Preferences.").arg(path));
    });
    return proc;
}

// ------------------------------------------------------------ stage one ----

void ChannelSync::startProbe()
{
    m_stage = Stage::Probing;

    m_process = makeProcess();
    m_process->setArguments(YtDlp::channelProbeArgs(m_url, m_settings));

    connect(m_process, &QProcess::readyReadStandardOutput, this, [this] {
        m_probeStdout += m_process->readAllStandardOutput();
    });
    connect(m_process, &QProcess::finished, this,
            [this](int code, QProcess::ExitStatus) { handleProbeFinished(code); });

    emit statusChanged(tr("Contacting channel…"));
    emit progress(0, -1);
    m_process->start();
}

void ChannelSync::handleProbeFinished(int exitCode)
{
    if (m_cancelRequested)
        return;

    if (m_process) {
        m_process->disconnect(this);
        m_process->deleteLater();
        m_process = nullptr;
    }

    // A failed probe is not fatal: the listing stage can still recover the
    // channel's identity from the entries themselves.
    if (exitCode == 0 && !m_probeStdout.isEmpty()) {
        const QJsonDocument doc = QJsonDocument::fromJson(m_probeStdout);
        if (doc.isObject()) {
            const QJsonObject root = doc.object();
            m_channel = YtDlp::parseChannelHeader(root);

            const QJsonValue count = root.value(QStringLiteral("playlist_count"));
            if (!count.isNull() && !count.isUndefined() && count.toInt(0) > 0)
                m_expectedTotal = count.toInt(0);
        }
    }

    startListing();
}

// ------------------------------------------------------------ stage two ----

void ChannelSync::startListing()
{
    m_stage = Stage::Listing;
    m_stderr.clear();

    m_process = makeProcess();
    m_process->setArguments(YtDlp::channelListArgs(m_url, m_settings));

    connect(m_process, &QProcess::readyReadStandardOutput,
            this, &ChannelSync::handleListingReadyRead);
    connect(m_process, &QProcess::finished, this,
            [this](int code, QProcess::ExitStatus) { handleListingFinished(code); });

    emit statusChanged(m_channel.title.isEmpty()
                           ? tr("Loading video list…")
                           : tr("Loading videos from %1…").arg(m_channel.title));
    emit progress(0, m_expectedTotal);
    m_process->start();
}

void ChannelSync::handleListingReadyRead()
{
    if (!m_process)
        return;

    m_stdoutBuffer += m_process->readAllStandardOutput();

    // yt-dlp emits one complete JSON object per line. Anything after the last
    // newline is a partial record and must wait for the next read.
    int newline;
    while ((newline = m_stdoutBuffer.indexOf('\n')) >= 0) {
        const QByteArray line = m_stdoutBuffer.left(newline);
        m_stdoutBuffer.remove(0, newline + 1);
        consumeEntryLine(line);
    }

    emit progress(m_videos.size(), m_expectedTotal);
}

void ChannelSync::consumeEntryLine(const QByteArray &line)
{
    const QByteArray trimmed = line.trimmed();
    if (trimmed.isEmpty() || !trimmed.startsWith('{'))
        return;

    const QJsonDocument doc = QJsonDocument::fromJson(trimmed);
    if (!doc.isObject())
        return;

    const QJsonObject entry = doc.object();

    VideoInfo v = YtDlp::parseFlatEntry(entry);
    if (v.videoId.isEmpty())
        return;
    m_videos.append(v);

    // If the probe told us nothing, fill in the channel from the first entry
    // that carries identifying fields.
    if (m_channel.channelId.isEmpty())
        YtDlp::mergeChannelFromEntry(m_channel, entry);
}

void ChannelSync::handleListingFinished(int exitCode)
{
    if (m_cancelRequested)
        return;

    // Flush a final line that arrived without a trailing newline.
    if (!m_stdoutBuffer.isEmpty()) {
        consumeEntryLine(m_stdoutBuffer);
        m_stdoutBuffer.clear();
    }

    const QByteArray stderrCopy = m_stderr;
    const ChannelInfo channel = m_channel;
    const QVector<VideoInfo> videos = m_videos;

    teardown();

    if (videos.isEmpty()) {
        const QString detail = YtDlp::extractError(QString::fromUtf8(stderrCopy));
        if (!detail.isEmpty())
            emit failed(detail);
        else if (exitCode != 0)
            emit failed(tr("yt-dlp exited with code %1.").arg(exitCode));
        else
            emit failed(tr("No videos were listed for this channel."));
        return;
    }

    if (channel.channelId.isEmpty()) {
        emit failed(tr("The response did not identify a channel."));
        return;
    }

    ChannelInfo resolved = channel;
    if (resolved.url.isEmpty())
        resolved.url = m_url;
    resolved.lastSynced = QDateTime::currentDateTimeUtc();

    emit progress(videos.size(), videos.size());
    emit finished(resolved, videos);
}

// ---------------------------------------------------------------- misc ----

void ChannelSync::cancel()
{
    if (!isRunning())
        return;

    m_cancelRequested = true;

    if (m_process) {
        m_process->disconnect(this);
        m_process->kill();
        m_process->waitForFinished(2000);
        m_process->deleteLater();
        m_process = nullptr;
    }

    m_stage = Stage::Idle;
    m_videos.clear();
    m_stdoutBuffer.clear();
    emit cancelled();
}

void ChannelSync::teardown()
{
    if (m_process) {
        m_process->disconnect(this);
        m_process->deleteLater();
        m_process = nullptr;
    }
    m_stage = Stage::Idle;
}
