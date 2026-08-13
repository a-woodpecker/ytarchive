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
        QString assetUrl;     // the asset for *this* platform, when there is one
        QString assetName;
        QString notes;
    };

    // What counts as the right download here. A release carries builds for
    // every platform, so picking the first one attached would offer a Windows
    // installer to a Linux user.
    struct AssetPreference {
        QStringList suffixes;    // ".deb", ".exe", ...
        QStringList archTokens;  // "amd64", "x86_64", ...
        QString     codename;    // "bookworm", when the system reports one
    };

    static AssetPreference currentPreference();

    // Chooses among a release's assets. Kept separate from the platform
    // detection above so the rules can be tested for every platform at once.
    static QString chooseAsset(const QVector<QPair<QString, QString>> &nameAndUrl,
                               const AssetPreference &preference,
                               QString *chosenName = nullptr);

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

    // Interprets a GitHub "releases/latest" payload. Separated from the network
    // call so the decisions it makes - which asset counts, whether a draft is
    // ignored - can be tested directly against real response shapes.
    // Returns false and sets `error` on malformed input; sets `ignored` when
    // the release exists but should not be offered.
    static bool parseLatestRelease(const QByteArray &json, Release *release,
                                   bool *ignored, QString *error);

    // Base URL of the API, "https://api.github.com" unless YTA_UPDATE_API_BASE
    // is set. Overridable for testing and for GitHub Enterprise hosts.
    static QString apiBase();

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
