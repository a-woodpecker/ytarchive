#pragma once

#include <QObject>
#include <QQueue>
#include <QMutex>
#include <QString>
#include <QThread>
#include <atomic>

// Computes and verifies SHA-256 digests of archived media.
//
// The service being downloaded from publishes no hash of its own, and the
// files here are assembled locally from separate video and audio streams, so
// nothing upstream could describe them anyway. What a digest is good for is
// fixity: proving months later that a file is byte-for-byte what was written.
//
// Hashing runs on one worker thread, one file at a time. Reading several
// multi-gigabyte files at once from the same disk is slower than reading them
// in turn, and this must never block the interface.
class ChecksumService : public QObject
{
    Q_OBJECT
public:
    explicit ChecksumService(QObject *parent = nullptr);
    ~ChecksumService() override;

    // `expected` empty records a new digest; otherwise the result is compared
    // against it and reported as a verification.
    void enqueue(qint64 videoPk, const QString &path, const QString &expected = QString());
    void cancelAll();
    int  pending() const;
    bool isBusy() const;

    // Sidecar path: "<media basename>.sha256", in the format sha256sum reads,
    // so the archive can be checked without this program.
    static QString sidecarPath(const QString &mediaPath);
    static bool writeSidecar(const QString &mediaPath, const QString &digest);

signals:
    void started(qint64 videoPk, const QString &path);
    void computed(qint64 videoPk, const QString &path, const QString &digest);
    void verified(qint64 videoPk, const QString &path, bool matches,
                  const QString &digest, const QString &expected);
    void failed(qint64 videoPk, const QString &path, const QString &error);
    void queueDrained();

private:
    struct Task {
        qint64  videoPk;
        QString path;
        QString expected;
    };

    void processNext();

    QThread          m_thread;
    // One worker living on the thread for the life of the service. Creating one
    // per task made it possible for two to be in flight at once.
    QObject         *m_worker = nullptr;
    QQueue<Task>     m_queue;
    mutable QMutex   m_mutex;
    std::atomic<bool> m_abort{ false };
    bool             m_running = false;   // guarded by m_mutex
};
