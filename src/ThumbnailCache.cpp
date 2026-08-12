#include "ThumbnailCache.h"

#include <QDir>
#include <QFile>
#include <QNetworkReply>
#include <QNetworkRequest>

ThumbnailCache::ThumbnailCache(QObject *parent)
    : QObject(parent)
{
    m_memory.setMaxCost(600);   // roughly 600 cards held in RAM
    m_net.setRedirectPolicy(QNetworkRequest::NoLessSafeRedirectPolicy);
}

void ThumbnailCache::setCacheDir(const QString &dir)
{
    m_cacheDir = dir;
    QDir().mkpath(dir);
}

QString ThumbnailCache::diskPath(const QString &videoId) const
{
    return QDir(m_cacheDir).filePath(videoId + QStringLiteral(".jpg"));
}

QPixmap ThumbnailCache::thumbnail(const QString &videoId, const QString &url)
{
    if (videoId.isEmpty())
        return QPixmap();

    if (QPixmap *cached = m_memory.object(videoId))
        return *cached;

    // Second chance: the file may already be on disk from a previous session.
    const QString path = diskPath(videoId);
    if (QFile::exists(path)) {
        QPixmap pm;
        if (pm.load(path) && !pm.isNull()) {
            m_memory.insert(videoId, new QPixmap(pm));
            return pm;
        }
    }

    if (url.isEmpty() || m_inFlight.contains(videoId) || m_failed.contains(videoId))
        return QPixmap();

    m_inFlight.insert(videoId);

    QNetworkRequest request{ QUrl(url) };
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute,
                         QNetworkRequest::PreferCache);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("YtArchive/0.1"));

    QNetworkReply *reply = m_net.get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, videoId] {
        reply->deleteLater();
        m_inFlight.remove(videoId);

        if (reply->error() != QNetworkReply::NoError) {
            m_failed.insert(videoId);
            return;
        }

        const QByteArray data = reply->readAll();
        QPixmap pm;
        if (!pm.loadFromData(data) || pm.isNull()) {
            m_failed.insert(videoId);
            return;
        }

        if (!m_cacheDir.isEmpty()) {
            QFile f(diskPath(videoId));
            if (f.open(QIODevice::WriteOnly))
                f.write(data);
        }

        m_memory.insert(videoId, new QPixmap(pm));
        emit ready(videoId);
    });

    return QPixmap();
}
