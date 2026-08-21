#include "ChecksumService.h"

#include "FileTime.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutexLocker>
#include <QTimer>

ChecksumService::ChecksumService(QObject *parent)
    : QObject(parent)
    , m_worker(new QObject)
{
    m_thread.start(QThread::LowPriority);   // never compete with a download
    m_worker->moveToThread(&m_thread);
}

ChecksumService::~ChecksumService()
{
    cancelAll();
    m_worker->deleteLater();
    m_thread.quit();
    m_thread.wait(5000);
}

QString ChecksumService::sidecarPath(const QString &mediaPath)
{
    const QFileInfo fi(mediaPath);
    return fi.absoluteDir().filePath(fi.completeBaseName() + QStringLiteral(".sha256"));
}

bool ChecksumService::writeSidecar(const QString &mediaPath, const QString &digest)
{
    const QString path = sidecarPath(mediaPath);
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;

    // Two spaces between digest and name is what sha256sum -c expects.
    const QByteArray line = digest.toUtf8() + "  "
                            + QFileInfo(mediaPath).fileName().toUtf8() + "\n";
    const bool ok = f.write(line) == line.size();
    f.close();

    // Carry the media file's timestamp, so the sidecar sorts with the record
    // it describes rather than with the day it happened to be hashed.
    if (ok) {
        const QDateTime stamp = QFileInfo(mediaPath).lastModified();
        if (stamp.isValid())
            setFileModificationTime(path, stamp);
    }
    return ok;
}

void ChecksumService::enqueue(qint64 videoPk, const QString &path, const QString &expected)
{
    {
        QMutexLocker lock(&m_mutex);
        for (const Task &task : std::as_const(m_queue)) {
            if (task.videoPk == videoPk)
                return;                       // already waiting
        }
        m_queue.enqueue({ videoPk, path, expected });
    }
    m_abort = false;
    // Always ask; processNext decides whether anything should start, so two
    // enqueues in a row cannot put two tasks in flight.
    QTimer::singleShot(0, this, [this] { processNext(); });
}

void ChecksumService::cancelAll()
{
    m_abort = true;
    QMutexLocker lock(&m_mutex);
    m_queue.clear();
}

int ChecksumService::pending() const
{
    QMutexLocker lock(&m_mutex);
    return m_queue.size();
}

bool ChecksumService::isBusy() const
{
    QMutexLocker lock(&m_mutex);
    return m_running || !m_queue.isEmpty();
}

void ChecksumService::processNext()
{
    Task task;
    {
        QMutexLocker lock(&m_mutex);
        if (m_running)
            return;                    // one at a time, by design
        if (m_queue.isEmpty()) {
            lock.unlock();
            emit queueDrained();
            return;
        }
        task = m_queue.dequeue();
        m_running = true;
    }

    emit started(task.videoPk, task.path);

    // The hash runs on the worker thread; the result returns queued.
    QMetaObject::invokeMethod(m_worker, [this, task] {
        QString digest;
        QString error;

        QFile file(task.path);
        if (!file.exists()) {
            error = tr("The file is no longer on disk.");
        } else if (!file.open(QIODevice::ReadOnly)) {
            error = tr("Could not open the file: %1").arg(file.errorString());
        } else {
            QCryptographicHash hash(QCryptographicHash::Sha256);
            // Reads in chunks rather than loading the file, which matters when
            // a single video can be several gigabytes.
            if (hash.addData(&file))
                digest = QString::fromLatin1(hash.result().toHex());
            else
                error = tr("The file could not be read to the end.");
            file.close();
        }

        if (m_abort)
            error = tr("Cancelled.");

        QMetaObject::invokeMethod(this, [this, task, digest, error] {
            {
                QMutexLocker lock(&m_mutex);
                m_running = false;
            }

            if (!error.isEmpty()) {
                emit failed(task.videoPk, task.path, error);
            } else if (task.expected.isEmpty()) {
                emit computed(task.videoPk, task.path, digest);
            } else {
                emit verified(task.videoPk, task.path,
                              digest.compare(task.expected, Qt::CaseInsensitive) == 0,
                              digest, task.expected);
            }
            processNext();
        }, Qt::QueuedConnection);
    }, Qt::QueuedConnection);
}
