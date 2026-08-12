#include "DownloadManager.h"

#include "Database.h"
#include "FileTime.h"
#include "YtDlp.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryFile>
#include <QUuid>

DownloadManager::DownloadManager(Database* db, QObject* parent)
    : QObject(parent)
    , m_db(db)
{
}

DownloadManager::~DownloadManager()
{
    cancelAll();
    qDeleteAll(m_queue);
    m_queue.clear();
}

void DownloadManager::enqueue(const QVector<VideoInfo>& videos,
    const QString& channelTitle,
    const QString& channelId)
{
    const QString dir = m_settings.channelDir(channelTitle, channelId);
    QDir().mkpath(dir);

    const QString scratch = QStandardPaths::writableLocation(QStandardPaths::TempLocation);

    int added = 0;
    for (const VideoInfo& v : videos) {
        if (v.pk < 0 || m_pendingByPk.contains(v.pk))
            continue;

        auto* job = new Job;
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
        Job* job = m_queue.dequeue();
        m_active.insert(job->video.pk, job);
        startJob(job);
    }
    emit queueChanged();

    if (m_active.isEmpty() && m_queue.isEmpty())
        emit allFinished();
}

void DownloadManager::startJob(Job* job)
{
    QDir().mkpath(job->destinationDir);

    auto* proc = new QProcess(this);
    job->process = proc;
    proc->setProgram(m_settings.ytDlpPath);
    proc->setArguments(YtDlp::downloadArgs(job->video, job->destinationDir,
        job->printFilePath, m_settings));

    job->progress.stage = tr("Starting");
    emit progress(job->progress);

    connect(proc, &QProcess::readyReadStandardOutput, this, [this, job] { handleStdout(job); });
    connect(proc, &QProcess::readyReadStandardError, this, [this, job] {
        const QString chunk = QString::fromUtf8(job->process->readAllStandardError());
        job->stderrBuffer += chunk;
        for (const QString& line : chunk.split(QChar('\n'), Qt::SkipEmptyParts))
            emit logMessage(line.trimmed());
        });
    connect(proc, &QProcess::errorOccurred, this, [this, job](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart) {
            finalize(job, false,
                tr("Could not run yt-dlp (looked for \"%1\"). Set its full path in "
                    "Preferences.").arg(m_settings.ytDlpPath));
        }
        });
    connect(proc, &QProcess::finished, this,
        [this, job](int code, QProcess::ExitStatus) { handleFinished(job, code); });

    if (m_db)
        m_db->setVideoState(job->video.pk, DownloadState::Downloading);
    emit videoStateChanged(job->video.pk, DownloadState::Downloading);

    proc->start();
}

void DownloadManager::handleStdout(Job* job)
{
    const QString chunk = QString::fromUtf8(job->process->readAllStandardOutput());
    for (const QString& raw : chunk.split(QChar('\n'), Qt::SkipEmptyParts)) {
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

void DownloadManager::parseProgressLine(Job* job, const QString& line)
{
    // Format: @P@|downloaded|total|speed|eta   ("NA" for anything unknown.)
    const QStringList parts = line.split(QChar('|'));
    if (parts.size() < 5)
        return;

    auto toNumber = [](const QString& s) -> double {
        if (s.isEmpty() || s == QLatin1String("NA") || s == QLatin1String("None"))
            return -1.0;
        bool ok = false;
        const double d = s.toDouble(&ok);
        return ok ? d : -1.0;
        };

    const double downloaded = toNumber(parts.at(1));
    const double total = toNumber(parts.at(2));
    const double speed = toNumber(parts.at(3));
    const double eta = toNumber(parts.at(4));

    Progress& p = job->progress;
    p.downloadedBytes = downloaded > 0 ? static_cast<qint64>(downloaded) : 0;
    p.totalBytes = total > 0 ? static_cast<qint64>(total) : 0;
    p.speedBytesPerSec = speed > 0 ? speed : 0.0;
    p.etaSeconds = eta >= 0 ? static_cast<qint64>(eta) : -1;
    p.fraction = (total > 0 && downloaded >= 0)
        ? qBound(0.0, downloaded / total, 1.0)
        : -1.0;   // indeterminate
    p.stage = tr("Downloading");

    emit progress(p);
}

void DownloadManager::handleFinished(Job* job, int exitCode)
{
    if (exitCode != 0) {
        const QString detail = YtDlp::extractError(job->stderrBuffer);
        finalize(job, false, detail.isEmpty()
            ? tr("yt-dlp exited with code %1.").arg(exitCode)
            : detail);
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
        finalize(job, false, tr("The download reported success but no file was found on disk."));
        return;
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
        for (const QString& name : names) {
            if (name.startsWith(prefix, Qt::CaseSensitive))
                setFileModificationTime(dir.filePath(name), uploadDate);
        }
    }

    const qint64 size = QFileInfo(mediaPath).size();
    if (m_db) {
        m_db->recordDownload(job->video.pk, mediaPath,
            QFileInfo::exists(infoJson) ? infoJson : QString(),
            size, uploadDate);
    }

    finalize(job, true, mediaPath);
}

void DownloadManager::finalize(Job* job, bool success, const QString& message)
{
    const qint64 pk = job->video.pk;

    if (job->process) {
        job->process->disconnect(this);
        job->process->deleteLater();
        job->process = nullptr;
    }
    QFile::remove(job->printFilePath);

    if (!success && m_db)
        m_db->setVideoState(pk, DownloadState::Failed, message);

    m_active.remove(pk);
    m_pendingByPk.remove(pk);
    delete job;

    emit videoStateChanged(pk, success ? DownloadState::Downloaded : DownloadState::Failed);
    emit videoFinished(pk, success, message);

    pumpQueue();
}

void DownloadManager::cancel(qint64 videoPk)
{
    if (Job* job = m_active.value(videoPk, nullptr)) {
        if (job->process)
            job->process->kill();
        // handleFinished will report the non-zero exit; mark intent clearly.
        if (m_db)
            m_db->setVideoState(videoPk, DownloadState::NotDownloaded);
        return;
    }

    // Still waiting: drop it from the queue without touching the process pool.
    for (int i = 0; i < m_queue.size(); ++i) {
        if (m_queue.at(i)->video.pk == videoPk) {
            Job* job = m_queue.at(i);
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
        for (Job* j : m_queue)
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
    for (Job* j : m_active)
        out.append(j->progress);
    for (Job* j : m_queue)
        out.append(j->progress);
    return out;
}
