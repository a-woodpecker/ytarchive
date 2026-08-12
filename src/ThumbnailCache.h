#pragma once

#include <QCache>
#include <QHash>
#include <QNetworkAccessManager>
#include <QObject>
#include <QPixmap>
#include <QSet>
#include <QUrl>

// Two-level thumbnail store: an in-memory pixmap cache in front of a folder of
// downloaded JPEGs. Requests are deduplicated so a fast scroll does not fire
// hundreds of redundant network calls.
class ThumbnailCache : public QObject
{
    Q_OBJECT
public:
    explicit ThumbnailCache(QObject* parent = nullptr);

    void setCacheDir(const QString& dir);

    // Returns a ready pixmap, or a null pixmap after starting a fetch.
    // `ready` fires later for the same key.
    QPixmap thumbnail(const QString& videoId, const QString& url);

    // True when Qt can actually do HTTPS. A Qt install missing its TLS backend
    // plugin fails every thumbnail fetch while everything else keeps working,
    // because yt-dlp does its own networking in a separate process.
    static bool httpsAvailable();
    static QString tlsDiagnostic();
    // Lists the formats Qt can actually decode, and flags a missing JPEG plugin.
    static QString imageFormatDiagnostic();

    int failureCount() const { return m_failed.size(); }

signals:
    void ready(const QString& videoId);
    // Emitted for every fetch that does not produce an image, so failures are
    // visible instead of silently leaving a blank card.
    void fetchFailed(const QString& videoId, const QString& url, const QString& error);

private:
    QString diskPath(const QString& videoId) const;
    void startFetch(const QString& videoId, const QUrl& url);

    QNetworkAccessManager m_net;
    QCache<QString, QPixmap> m_memory;
    QSet<QString> m_inFlight;
    QSet<QString> m_failed;
    QHash<QString, int> m_attempts;   // bounded retry, rather than one strike
    QString m_cacheDir;
};