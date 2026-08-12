#pragma once

#include <QCache>
#include <QHash>
#include <QNetworkAccessManager>
#include <QObject>
#include <QPixmap>
#include <QSet>

// Two-level thumbnail store: an in-memory pixmap cache in front of a folder of
// downloaded JPEGs. Requests are deduplicated so a fast scroll does not fire
// hundreds of redundant network calls.
class ThumbnailCache : public QObject
{
    Q_OBJECT
public:
    explicit ThumbnailCache(QObject *parent = nullptr);

    void setCacheDir(const QString &dir);

    // Returns a ready pixmap, or a null pixmap after starting a fetch.
    // `ready` fires later for the same key.
    QPixmap thumbnail(const QString &videoId, const QString &url);

signals:
    void ready(const QString &videoId);

private:
    QString diskPath(const QString &videoId) const;

    QNetworkAccessManager m_net;
    QCache<QString, QPixmap> m_memory;
    QSet<QString> m_inFlight;
    QSet<QString> m_failed;
    QString m_cacheDir;
};
