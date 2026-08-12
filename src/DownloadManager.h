#pragma once

#include "Models.h"
#include "Settings.h"

#include <QHash>
#include <QObject>
#include <QQueue>

class QProcess;
class Database;

// Runs a bounded number of concurrent yt-dlp downloads, reports progress, and
// stamps every finished file with its upload date before marking it archived.
class DownloadManager : public QObject
{
    Q_OBJECT
public:
    struct Progress {
        qint64  videoPk = -1;
        QString title;
        QString channelTitle;
        double  fraction = 0.0;    // -1 when the total size is unknown
        qint64  downloadedBytes = 0;
        qint64  totalBytes = 0;
        double  speedBytesPerSec = 0.0;
        qint64  etaSeconds = -1;
        QString stage;             // "Waiting", "Downloading", "Merging", ...
    };

    explicit DownloadManager(Database *db, QObject *parent = nullptr);
    ~DownloadManager() override;

    void setSettings(const Settings &s) { m_settings = s; }

    void enqueue(const QVector<VideoInfo> &videos, const QString &channelTitle,
                 const QString &channelId);
    void cancel(qint64 videoPk);
    void cancelAll();

    int  activeCount() const { return m_active.size(); }
    int  queuedCount() const { return m_queue.size(); }
    bool isBusy() const { return activeCount() > 0 || queuedCount() > 0; }
    QVector<Progress> snapshot() const;

signals:
    void queueChanged();
    void progress(const DownloadManager::Progress &p);
    void videoStateChanged(qint64 videoPk, DownloadState state);
    void videoFinished(qint64 videoPk, bool success, const QString &message);
    void logMessage(const QString &line);
    void allFinished();

private:
    struct Job {
        VideoInfo video;
        QString   channelTitle;
        QString   channelId;
        QString   destinationDir;
        QString   printFilePath;
        QProcess *process = nullptr;
        QString   stderrBuffer;
        Progress  progress;
    };

    void pumpQueue();
    void startJob(Job *job);
    void handleStdout(Job *job);
    void handleFinished(Job *job, int exitCode);
    void finalize(Job *job, bool success, const QString &message);
    void parseProgressLine(Job *job, const QString &line);

    Database          *m_db;
    Settings           m_settings;
    QQueue<Job *>      m_queue;
    QHash<qint64, Job *> m_active;
    QHash<qint64, Job *> m_pendingByPk;   // everything not yet finished
};
