#include "DownloadManager.h"

#include "Database.h"
#include "FileTime.h"
#include "YtDlp.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPointer>
#include <QQueue>
#include <QProcess>
#include <QStandardPaths>
#include <QTimer>
#include <QTemporaryFile>
#include <QUuid>

namespace {
// How long yt-dlp is given to shut down cleanly before it is killed outright.
constexpr int kTerminateGraceMs = 4000;

// Failures the service is reporting as final. Retrying these achieves nothing
// and buries the real reason under repeated attempts.
const char *kPermanentMarkers[] = {
    "private video", "video unavailable", "removed by the uploader",
    "members-only", "join this channel", "sign in to confirm your age",
    "age-restricted", "copyright", "not available in your country",
    "account associated with this video has been terminated",
    "this live event will begin", "premieres in",
};
} // namespace

DownloadManager::DownloadManager(Database *db, QObject *parent)
    : QObject(parent)
    , m_db(db)
{
}

DownloadManager::~DownloadManager()
{
    cancelAll();
    // Let anything still winding down finish writing its resume state.
    for (Job *job : std::as_const(m_active)) {
        if (job->process)
            job->process->waitForFinished(kTerminateGraceMs);
    }
    qDeleteAll(m_queue);
    m_queue.clear();
}

void DownloadManager::enqueue(const QVector<VideoInfo> &videos,
                              const QString &channelTitle,
                              const QString &channelId)
{
    const QString dir = m_settings.channelDir(channelTitle, channelId);
    QDir().mkpath(dir);

    const QString scratch = QStandardPaths::writableLocation(QStandardPaths::TempLocation);

    int added = 0;
    for (const VideoInfo &v : videos) {
        if (v.pk < 0 || m_pendingByPk.contains(v.pk))
            continue;

        auto *job = new Job;
        job->video = v;
        job->channelTitle = channelTitle;
        job->channelId = channelId;
        job->destinationDir = dir;
        job->printFilePath = QDir(scratch).filePath(
            QStringLiteral("ytarchive-%1.path")
                .arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));

        job->progress.videoPk = v.pk;
        job->progress.title = v.title;
        job->progress.channelTitle = channelTitle;
        // Sending cookies to a request that does not need them makes the
        // service offer formats that cannot be downloaded, so they are held
        // back until a failure asks for them.
        job->withCookies = cookiesConfigured() && !m_settings.cookiesOnlyWhenNeeded;

        job->progress.stage = tr("Waiting");

        m_queue.enqueue(job);
        m_pendingByPk.insert(v.pk, job);

        if (m_db)
            m_db->setVideoState(v.pk, DownloadState::Queued);
        emit videoStateChanged(v.pk, DownloadState::Queued);
        emit progress(job->progress);
        ++added;
    }

    if (added > 0) {
        emit queueChanged();
        pumpQueue();
    }
}

void DownloadManager::pumpQueue()
{
    while (m_active.size() < qMax(1, m_settings.maxConcurrent) && !m_queue.isEmpty()) {
        Job *job = m_queue.dequeue();
        m_active.insert(job->video.pk, job);
        startJob(job);
    }
    emit queueChanged();

    if (m_active.isEmpty() && m_queue.isEmpty())
        emit allFinished();
}

void DownloadManager::startJob(Job *job)
{
    QDir().mkpath(job->destinationDir);

    auto *proc = new QProcess(this);
    job->process = proc;
    // Widened PATH, so a child yt-dlp can find a JS runtime that the desktop
    // session did not put on PATH.
    proc->setProcessEnvironment(toolProcessEnvironment());
    proc->setProgram(m_settings.ytDlpPath);
    proc->setArguments(YtDlp::downloadArgs(job->video, job->destinationDir,
                                           job->printFilePath, m_settings,
                                           job->withCookies));

    job->progress.stage = tr("Starting");
    emit progress(job->progress);

    connect(proc, &QProcess::readyReadStandardOutput, this, [this, job] { handleStdout(job); });
    connect(proc, &QProcess::readyReadStandardError, this, [this, job] {
        const QString chunk = QString::fromUtf8(job->process->readAllStandardError());
        job->stderrBuffer += chunk;
        for (const QString &line : chunk.split(QChar('\n'), Qt::SkipEmptyParts))
            emit logMessage(line.trimmed());
    });
    connect(proc, &QProcess::errorOccurred, this, [this, job](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart) {
            finalize(job, Outcome::Failed,
                     tr("Could not run yt-dlp (looked for \"%1\"). Set its full path in "
                        "Preferences.").arg(m_settings.ytDlpPath));
        }
    });
    connect(proc, &QProcess::finished, this,
            [this, job](int code, QProcess::ExitStatus) { handleFinished(job, code); });

    if (m_settings.verboseLogging) {
        // Quoted, so the line can be pasted into a shell and run as-is.
        QStringList quoted;
        for (const QString &a : proc->arguments())
            quoted << (a.contains(QChar(' ')) ? QStringLiteral("\"%1\"").arg(a) : a);
        emit logMessage(QStringLiteral("$ %1 %2")
                            .arg(proc->program(), quoted.join(QChar(' '))));
    }

    if (m_db)
        m_db->setVideoState(job->video.pk, DownloadState::Downloading);
    emit videoStateChanged(job->video.pk, DownloadState::Downloading);

    proc->start();
}

void DownloadManager::handleStdout(Job *job)
{
    const QString chunk = QString::fromUtf8(job->process->readAllStandardOutput());
    for (const QString &raw : chunk.split(QChar('\n'), Qt::SkipEmptyParts)) {
        const QString line = raw.trimmed();
        if (line.isEmpty())
            continue;

        if (line.startsWith(QStringLiteral("@P@|"))) {
            parseProgressLine(job, line);
            continue;
        }

        // Post-processing steps have no percentage, so surface them as stages.
        if (line.contains(QStringLiteral("[Merger]")))
            job->progress.stage = tr("Merging streams");
        else if (line.contains(QStringLiteral("[Metadata]")) ||
                 line.contains(QStringLiteral("[ThumbnailsConvertor]")))
            job->progress.stage = tr("Writing metadata");
        else if (line.contains(QStringLiteral("[ExtractAudio]")))
            job->progress.stage = tr("Extracting audio");
        else
            continue;

        emit progress(job->progress);
        emit logMessage(line);
    }
}

void DownloadManager::parseProgressLine(Job *job, const QString &line)
{
    // Format: @P@|downloaded|total|speed|eta   ("NA" for anything unknown.)
    const QStringList parts = line.split(QChar('|'));
    if (parts.size() < 5)
        return;

    auto toNumber = [](const QString &s) -> double {
        if (s.isEmpty() || s == QLatin1String("NA") || s == QLatin1String("None"))
            return -1.0;
        bool ok = false;
        const double d = s.toDouble(&ok);
        return ok ? d : -1.0;
    };

    const double downloaded = toNumber(parts.at(1));
    const double total      = toNumber(parts.at(2));
    const double speed      = toNumber(parts.at(3));
    const double eta        = toNumber(parts.at(4));

    Progress &p = job->progress;
    p.downloadedBytes = downloaded > 0 ? static_cast<qint64>(downloaded) : 0;
    p.totalBytes      = total > 0 ? static_cast<qint64>(total) : 0;
    p.speedBytesPerSec = speed > 0 ? speed : 0.0;
    p.etaSeconds      = eta >= 0 ? static_cast<qint64>(eta) : -1;
    p.fraction = (total > 0 && downloaded >= 0)
                     ? qBound(0.0, downloaded / total, 1.0)
                     : -1.0;   // indeterminate
    p.stage = tr("Downloading");

    emit progress(p);
}

bool DownloadManager::looksTransient(const QString &message)
{
    const QString lower = message.toLower();
    for (const char *marker : kPermanentMarkers) {
        if (lower.contains(QLatin1String(marker)))
            return false;
    }

    // A 403 on a media URL is the common case: the extraction succeeded and
    // the transfer was refused, which a fresh extraction often resolves.
    static const char *transient[] = {
        "403", "unable to download video data", "timed out", "timeout",
        "connection reset", "connection aborted", "transporterror",
        "temporary failure", "unable to connect", "read error",
        "incomplete", "500", "502", "503", "504", "429",
    };
    for (const char *marker : transient) {
        if (lower.contains(QLatin1String(marker)))
            return true;
    }
    return false;
}

bool DownloadManager::cookiesConfigured() const
{
    return !m_settings.cookiesFromBrowser.trimmed().isEmpty()
           || !m_settings.cookiesFile.trimmed().isEmpty();
}

bool DownloadManager::retryWithOppositeCookies(Job *job, const QString &reason)
{
    if (!cookiesConfigured() || job->cancelled || job->cookieSwitchTried)
        return false;

    const bool wantCookies = !job->withCookies && YtDlp::needsAuthentication(reason);
    // The reverse case: cookies were sent, and the service answered with
    // formats that cannot be fetched. That is what sending them cost.
    const bool dropCookies = job->withCookies && YtDlp::formatUnavailable(reason);

    if (!wantCookies && !dropCookies)
        return false;

    job->cookieSwitchTried = true;
    job->withCookies = wantCookies;

    if (job->process) {
        job->process->disconnect(this);
        job->process->deleteLater();
        job->process = nullptr;
    }
    QFile::remove(job->printFilePath);

    const qint64 pk = job->video.pk;
    m_active.remove(pk);

    if (m_db)
        m_db->setVideoState(pk, DownloadState::Queued);
    emit videoStateChanged(pk, DownloadState::Queued);
    emit logMessage(wantCookies
                        ? tr("\"%1\" needs an account; retrying with cookies.")
                              .arg(job->video.title)
                        : tr("\"%1\" offered no downloadable format with cookies; "
                             "retrying without them.").arg(job->video.title));

    m_queue.prepend(job);   // straight back to the front: nothing to wait for
    pumpQueue();
    return true;
}

bool DownloadManager::scheduleRetry(Job *job, const QString &reason)
{
    if (m_settings.autoRetryAttempts <= 0 || job->cancelled)
        return false;
    if (job->attempt >= m_settings.autoRetryAttempts)
        return false;
    if (!looksTransient(reason))
        return false;

    ++job->attempt;
    const int delay = qMax(1, m_settings.autoRetryDelay) * job->attempt;

    if (job->process) {
        job->process->disconnect(this);
        job->process->deleteLater();
        job->process = nullptr;
    }
    QFile::remove(job->printFilePath);

    const qint64 pk = job->video.pk;
    m_active.remove(pk);          // frees the slot for another video meanwhile

    if (m_db)
        m_db->setVideoState(pk, DownloadState::Queued);
    emit videoStateChanged(pk, DownloadState::Queued);
    emit logMessage(tr("Retrying \"%1\" in %2s (attempt %3 of %4): %5")
                        .arg(job->video.title)
                        .arg(delay)
                        .arg(job->attempt + 1)
                        .arg(m_settings.autoRetryAttempts + 1)
                        .arg(reason));

    job->progress.stage = tr("Waiting to retry");
    emit progress(job->progress);

    // Re-queued rather than restarted immediately: an instant repeat tends to
    // be refused the same way, and the delay grows with each attempt.
    QPointer<DownloadManager> self(this);
    QTimer::singleShot(delay * 1000, this, [self, job] {
        if (!self)
            return;
        if (!self->m_pendingByPk.contains(job->video.pk))
            return;               // cancelled while waiting
        self->m_queue.enqueue(job);
        self->pumpQueue();
    });

    pumpQueue();
    return true;
}

void DownloadManager::handleFinished(Job *job, int exitCode)
{
    // Killing the process makes it exit non-zero. Without this check the user's
    // own cancellation would be reported back to them as a download failure.
    if (job->cancelled) {
        finalize(job, Outcome::Cancelled, tr("Cancelled"));
        return;
    }

    if (exitCode != 0) {
        const QString raw = YtDlp::extractError(job->stderrBuffer);

        // Before treating this as a failure, consider whether the cookie
        // decision was simply wrong for this video.
        if (retryWithOppositeCookies(job, raw))
            return;

        // Classification reads the original text: the rewritten form is shorter
        // and would lose the markers it looks for.
        if (scheduleRetry(job, raw))
            return;

        const QString message = raw.isEmpty()
                                    ? tr("yt-dlp exited with code %1.").arg(exitCode)
                                    : YtDlp::friendlyError(raw);
        finalize(job, Outcome::Failed, message);
        return;
    }

    // Recover the final path yt-dlp printed after its last move.
    QString mediaPath;
    QFile pathFile(job->printFilePath);
    if (pathFile.open(QIODevice::ReadOnly)) {
        const QStringList lines = QString::fromUtf8(pathFile.readAll())
                                      .split(QChar('\n'), Qt::SkipEmptyParts);
        if (!lines.isEmpty())
            mediaPath = lines.last().trimmed();
        pathFile.close();
    }
    QFile::remove(job->printFilePath);

    if (mediaPath.isEmpty() || !QFileInfo::exists(mediaPath)) {
        finalize(job, Outcome::Failed,
                 tr("The download reported success but no file was found on disk."));
        return;
    }

    if (job->attempt > 0) {
        emit logMessage(tr("\"%1\" succeeded on attempt %2.")
                            .arg(job->video.title).arg(job->attempt + 1));
    }

    const QString infoJson = YtDlp::infoJsonPathFor(mediaPath);

    // The upload date from info.json is authoritative; the listing's date was
    // only an estimate.
    QDateTime uploadDate = QFileInfo::exists(infoJson)
                               ? YtDlp::uploadDateFromInfoJson(infoJson)
                               : QDateTime();
    if (!uploadDate.isValid())
        uploadDate = job->video.uploadDate;

    if (uploadDate.isValid()) {
        // Stamp the media file and every sidecar that shares its basename, so
        // the whole record carries the same date.
        setFileModificationTime(mediaPath, uploadDate);

        const QFileInfo fi(mediaPath);
        const QDir dir = fi.absoluteDir();
        const QString prefix = fi.completeBaseName() + QLatin1Char('.');

        // Deliberately a literal prefix comparison rather than a name filter:
        // the default filename template embeds the video id in square brackets,
        // and QDir's glob syntax would read "[dQw4w9WgXcQ]" as a character
        // class, silently matching nothing.
        const QStringList names = dir.entryList(QDir::Files);
        for (const QString &name : names) {
            if (name.startsWith(prefix, Qt::CaseSensitive))
                setFileModificationTime(dir.filePath(name), uploadDate);
        }
    }

    const qint64 size = QFileInfo(mediaPath).size();
    qint64 likeCount = -1;
    qint64 viewCount = -1;
    if (QFileInfo::exists(infoJson))
        YtDlp::countsFromInfoJson(infoJson, &likeCount, &viewCount);

    if (m_db) {
        m_db->recordDownload(job->video.pk, mediaPath,
                             QFileInfo::exists(infoJson) ? infoJson : QString(),
                             size, uploadDate, likeCount, viewCount);
    }

    finalize(job, Outcome::Success, mediaPath);
}

void DownloadManager::finalize(Job *job, Outcome outcome, const QString &message)
{
    const qint64 pk = job->video.pk;

    if (job->process) {
        job->process->disconnect(this);
        job->process->deleteLater();
        job->process = nullptr;
    }
    QFile::remove(job->printFilePath);

    DownloadState state = DownloadState::Downloaded;
    switch (outcome) {
    case Outcome::Success:
        break;                                  // already recorded by the caller
    case Outcome::Failed:
        state = DownloadState::Failed;
        if (m_db)
            m_db->setVideoState(pk, state, message);
        break;
    case Outcome::Cancelled:
        // Back to plain "not downloaded", so the card offers itself again
        // rather than sitting there looking broken. Anything already fetched
        // stays in .incomplete and resumes on the next attempt.
        state = DownloadState::NotDownloaded;
        if (m_db)
            m_db->setVideoState(pk, state);
        break;
    }

    m_active.remove(pk);
    m_pendingByPk.remove(pk);
    delete job;

    emit videoStateChanged(pk, state);
    emit videoFinished(pk, outcome == Outcome::Success, message);

    pumpQueue();
}

void DownloadManager::cancel(qint64 videoPk)
{
    if (Job *job = m_active.value(videoPk, nullptr)) {
        job->cancelled = true;
        if (job->process) {
            // Ask first, force second. A hard kill mid-fragment can leave the
            // partial file and yt-dlp's resume state disagreeing about how many
            // bytes are valid, and a later --continue then resumes from the
            // wrong offset - which shows up as corrupt audio or video in the
            // finished file rather than as a failed download.
            job->process->terminate();
            QPointer<QProcess> handle = job->process;
            QTimer::singleShot(kTerminateGraceMs, this, [handle] {
                if (handle && handle->state() != QProcess::NotRunning)
                    handle->kill();
            });
        }
        // handleFinished sees the flag and reports a cancellation, not a failure.
        return;
    }

    // Still waiting: drop it from the queue without touching the process pool.
    for (int i = 0; i < m_queue.size(); ++i) {
        if (m_queue.at(i)->video.pk == videoPk) {
            Job *job = m_queue.at(i);
            m_queue.removeAt(i);
            m_pendingByPk.remove(videoPk);
            delete job;
            if (m_db)
                m_db->setVideoState(videoPk, DownloadState::NotDownloaded);
            emit videoStateChanged(videoPk, DownloadState::NotDownloaded);
            emit queueChanged();
            return;
        }
    }
}

void DownloadManager::cancelAll()
{
    const QList<qint64> waiting = [this] {
        QList<qint64> ids;
        for (Job *j : m_queue)
            ids << j->video.pk;
        return ids;
    }();
    for (qint64 pk : waiting)
        cancel(pk);

    const QList<qint64> running = m_active.keys();
    for (qint64 pk : running)
        cancel(pk);
}

QVector<DownloadManager::Progress> DownloadManager::snapshot() const
{
    QVector<Progress> out;
    out.reserve(m_active.size() + m_queue.size());
    for (Job *j : m_active)
        out.append(j->progress);
    for (Job *j : m_queue)
        out.append(j->progress);
    return out;
}
