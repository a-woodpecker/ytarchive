#pragma once

#include <QObject>
#include <QString>

class QNetworkAccessManager;

// Polls the GitHub Releases API and reports whether a newer version exists.
//
// This only ever *tells* you about an update - it never downloads or installs
// anything on its own. Silently replacing a running archiving tool is a bad
// trade for a program whose whole job is not losing data.
class UpdateChecker : public QObject
{
    Q_OBJECT
public:
    struct Release {
        QString version;      // normalised, e.g. "0.2.0"
        QString tag;          // as published, e.g. "v0.2.0"
        QString pageUrl;      // human-facing release page
        QString installerUrl; // .exe asset, when the release has one
        QString notes;
    };

    explicit UpdateChecker(QObject *parent = nullptr);

    // "owner/repo" — configured at build time via YTA_GITHUB_REPO.
    void setRepository(const QString &ownerAndRepo) { m_repo = ownerAndRepo; }
    void setCurrentVersion(const QString &version) { m_currentVersion = version; }

    // `userInitiated` is echoed back in the result signals so the caller can
    // stay quiet about routine background checks but report on manual ones.
    void check(bool userInitiated);
    bool isChecking() const { return m_inFlight; }

    // Returns <0, 0 or >0. Tolerates a leading "v" and differing segment
    // counts; a pre-release suffix sorts below the same version without one.
    static int compareVersions(const QString &a, const QString &b);

signals:
    void updateAvailable(const UpdateChecker::Release &release, bool userInitiated);
    void upToDate(bool userInitiated);
    void checkFailed(const QString &message, bool userInitiated);

private:
    QNetworkAccessManager *m_net;
    QString m_repo;
    QString m_currentVersion;
    bool    m_inFlight = false;
};
