#include "ThumbnailCache.h"

// Defined by the build from project(VERSION). The fallback only matters when
// this file is compiled outside the application target, such as in a test.
#ifndef YTA_VERSION
#  define YTA_VERSION "0.0.0"
#endif

#include <QBuffer>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QImageReader>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSslSocket>
#include <QStringList>
#include <QUrlQuery>

ThumbnailCache::ThumbnailCache(QObject *parent)
    : QObject(parent)
{
    m_memory.setMaxCost(600);   // roughly 600 cards held in RAM
    m_net.setRedirectPolicy(QNetworkRequest::NoLessSafeRedirectPolicy);
}

bool ThumbnailCache::httpsAvailable()
{
    return QSslSocket::supportsSsl();
}

QString ThumbnailCache::tlsDiagnostic()
{
    QStringList lines;
    lines << QCoreApplication::translate("ThumbnailCache",
              "Qt cannot make HTTPS connections, so no thumbnail can be downloaded.");
    lines << QCoreApplication::translate("ThumbnailCache",
              "Build-time TLS support: %1").arg(QSslSocket::sslLibraryBuildVersionString());
    const QString runtime = QSslSocket::sslLibraryVersionString();
    lines << QCoreApplication::translate("ThumbnailCache",
              "Runtime TLS library: %1").arg(runtime.isEmpty()
                                                 ? QCoreApplication::translate("ThumbnailCache",
                                                       "none found")
                                                 : runtime);
    lines << QCoreApplication::translate("ThumbnailCache",
              "On Windows this usually means the tls\\qschannelbackend.dll plugin was not "
              "copied next to the executable. Re-run windeployqt.");
    return lines.join(QChar('\n'));
}

QString ThumbnailCache::imageFormatDiagnostic()
{
    QStringList formats;
    const QList<QByteArray> supported = QImageReader::supportedImageFormats();
    for (const QByteArray &f : supported)
        formats << QString::fromLatin1(f);

    QString text = QCoreApplication::translate("ThumbnailCache",
                       "Image formats Qt can decode: %1").arg(formats.join(QStringLiteral(", ")));

    // JPEG lives in a plugin (imageformats/qjpeg). Without it Qt handles only
    // the handful of formats compiled into QtGui, and every YouTube thumbnail
    // downloads correctly and then fails to decode.
    if (!supported.contains("jpeg") && !supported.contains("jpg")) {
        text += QChar('\n');
        text += QCoreApplication::translate("ThumbnailCache",
                    "JPEG support is MISSING. Copy the imageformats plugin folder next to "
                    "the executable by running windeployqt.");
    }
    return text;
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

// YouTube's sized thumbnail URLs carry sqp/rs query parameters. Those variants
// are served in whatever format the CDN prefers, which is often WebP - a format
// Qt only decodes when the separate qtimageformats module is installed.
// Dropping the query yields the plain, always-JPEG original.
static QUrl withoutQuery(const QUrl &url)
{
    QUrl bare = url;
    bare.setQuery(QString());
    return bare;
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

    if (url.isEmpty()) {
        if (!m_failed.contains(videoId)) {
            m_failed.insert(videoId);
            emit fetchFailed(videoId, QString(),
                             QCoreApplication::translate(
                                 "ThumbnailCache",
                                 "no thumbnail URL in the channel listing"));
        }
        return QPixmap();
    }

    if (m_inFlight.contains(videoId) || m_failed.contains(videoId))
        return QPixmap();

    startFetch(videoId, QUrl(url));
    return QPixmap();
}

void ThumbnailCache::startFetch(const QString &videoId, const QUrl &url)
{
    m_inFlight.insert(videoId);

    QNetworkRequest request{ url };
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute,
                         QNetworkRequest::PreferCache);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("YtArchive/" YTA_VERSION));

    QNetworkReply *reply = m_net.get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, videoId, url] {
        reply->deleteLater();
        m_inFlight.remove(videoId);

        auto giveUp = [this, videoId, url](const QString &reason) {
            m_failed.insert(videoId);
            emit fetchFailed(videoId, url.toString(), reason);
        };

        if (reply->error() != QNetworkReply::NoError) {
            // One retry covers a transient hiccup without blanking the card
            // for the rest of the session.
            if (++m_attempts[videoId] < 2)
                return;
            giveUp(reply->errorString());
            return;
        }

        const QByteArray data = reply->readAll();

        QBuffer buffer;
        buffer.setData(data);
        buffer.open(QIODevice::ReadOnly);
        QImageReader reader(&buffer);
        reader.setAutoTransform(true);
        const QImage image = reader.read();

        if (image.isNull()) {
            // The bytes arrived but Qt could not decode them. Fall back to the
            // unparameterised URL, which YouTube always serves as JPEG.
            if (url.hasQuery()) {
                m_attempts[videoId] = 0;
                startFetch(videoId, withoutQuery(url));
                return;
            }

            const QString contentType =
                reply->header(QNetworkRequest::ContentTypeHeader).toString();
            const QString detected = QString::fromLatin1(reader.format());
            giveUp(QCoreApplication::translate("ThumbnailCache",
                       "could not decode the image (%1 bytes, Content-Type \"%2\", "
                       "detected format \"%3\"): %4")
                       .arg(data.size())
                       .arg(contentType.isEmpty()
                                ? QCoreApplication::translate("ThumbnailCache", "unknown")
                                : contentType)
                       .arg(detected.isEmpty()
                                ? QCoreApplication::translate("ThumbnailCache", "unrecognised")
                                : detected)
                       .arg(reader.errorString()));
            return;
        }

        const QPixmap pm = QPixmap::fromImage(image);
        if (pm.isNull()) {
            giveUp(QCoreApplication::translate("ThumbnailCache",
                                               "the decoded image was empty"));
            return;
        }

        if (!m_cacheDir.isEmpty()) {
            // Re-encode rather than storing the raw bytes, so the disk cache is
            // always a format we have just proven we can read back.
            QFile f(diskPath(videoId));
            if (f.open(QIODevice::WriteOnly))
                image.save(&f, "PNG");
        }

        m_memory.insert(videoId, new QPixmap(pm));
        emit ready(videoId);
    });
}
