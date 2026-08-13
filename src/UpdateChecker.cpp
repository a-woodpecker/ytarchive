#include "UpdateChecker.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QFile>
#include <QNetworkRequest>
#include <QSysInfo>
#include <functional>
#include <QCoreApplication>
#include <QRegularExpression>

namespace {

// Splits "1.2.3-beta.1" into ({1,2,3}, "beta.1").
void parseVersion(const QString &raw, QList<int> *numbers, QString *suffix)
{
    QString v = raw.trimmed();
    if (v.startsWith(QLatin1Char('v'), Qt::CaseInsensitive))
        v.remove(0, 1);

    const int dash = v.indexOf(QLatin1Char('-'));
    if (dash >= 0) {
        *suffix = v.mid(dash + 1);
        v = v.left(dash);
    }

    const QStringList parts = v.split(QLatin1Char('.'), Qt::SkipEmptyParts);
    for (const QString &part : parts) {
        // Stop at the first non-numeric segment rather than guessing.
        bool ok = false;
        const int n = part.toInt(&ok);
        if (!ok)
            break;
        numbers->append(n);
    }
}

} // namespace

UpdateChecker::UpdateChecker(QObject *parent)
    : QObject(parent)
    , m_net(new QNetworkAccessManager(this))
{
}

int UpdateChecker::compareVersions(const QString &a, const QString &b)
{
    QList<int> na, nb;
    QString sa, sb;
    parseVersion(a, &na, &sa);
    parseVersion(b, &nb, &sb);

    const int count = qMax(na.size(), nb.size());
    for (int i = 0; i < count; ++i) {
        // A missing segment counts as zero, so 1.2 == 1.2.0.
        const int x = i < na.size() ? na.at(i) : 0;
        const int y = i < nb.size() ? nb.at(i) : 0;
        if (x != y)
            return x < y ? -1 : 1;
    }

    // Equal numerically: a pre-release is older than the final release.
    if (sa.isEmpty() && !sb.isEmpty())
        return 1;
    if (!sa.isEmpty() && sb.isEmpty())
        return -1;
    return QString::compare(sa, sb);
}

UpdateChecker::AssetPreference UpdateChecker::currentPreference()
{
    AssetPreference preference;

#if defined(Q_OS_WIN)
    preference.suffixes = { QStringLiteral(".exe"), QStringLiteral(".msi") };
#elif defined(Q_OS_MACOS)
    preference.suffixes = { QStringLiteral(".dmg"), QStringLiteral(".pkg") };
#else
    preference.suffixes = { QStringLiteral(".deb"), QStringLiteral(".appimage"),
                            QStringLiteral(".rpm") };
#endif

    const QString arch = QSysInfo::currentCpuArchitecture();
    if (arch == QLatin1String("x86_64")) {
        preference.archTokens = { QStringLiteral("amd64"), QStringLiteral("x86_64"),
                                  QStringLiteral("x64") };
    } else if (arch == QLatin1String("arm64")) {
        preference.archTokens = { QStringLiteral("arm64"), QStringLiteral("aarch64") };
    } else if (!arch.isEmpty()) {
        preference.archTokens = { arch };
    }

#if defined(Q_OS_LINUX)
    // Packages are tagged with the distribution they were built on, so prefer
    // one built for this system when the release offers several.
    QFile osRelease(QStringLiteral("/etc/os-release"));
    if (osRelease.open(QIODevice::ReadOnly | QIODevice::Text)) {
        const QStringList lines = QString::fromUtf8(osRelease.readAll())
                                      .split(QChar('\n'), Qt::SkipEmptyParts);
        for (const QString &line : lines) {
            if (!line.startsWith(QStringLiteral("VERSION_CODENAME=")))
                continue;
            preference.codename = line.section(QChar('='), 1).remove(QChar('"')).trimmed();
            break;
        }
    }
#endif

    return preference;
}

QString UpdateChecker::chooseAsset(const QVector<QPair<QString, QString>> &nameAndUrl,
                                   const AssetPreference &preference,
                                   QString *chosenName)
{
    auto matchesSuffix = [&preference](const QString &name) {
        for (const QString &suffix : preference.suffixes)
            if (name.endsWith(suffix, Qt::CaseInsensitive))
                return true;
        return false;
    };

    QVector<QPair<QString, QString>> candidates;
    for (const auto &asset : nameAndUrl) {
        if (!asset.first.isEmpty() && !asset.second.isEmpty() && matchesSuffix(asset.first))
            candidates.append(asset);
    }
    if (candidates.isEmpty())
        return QString();   // nothing for this platform; the caller falls back

    auto narrow = [&candidates](const std::function<bool(const QString &)> &keep) {
        QVector<QPair<QString, QString>> kept;
        for (const auto &asset : candidates)
            if (keep(asset.first))
                kept.append(asset);
        // Only narrow when something survives: a release that does not name
        // architectures at all should still offer its single build.
        if (!kept.isEmpty())
            candidates = kept;
    };

    if (!preference.archTokens.isEmpty()) {
        narrow([&preference](const QString &name) {
            for (const QString &token : preference.archTokens)
                if (name.contains(token, Qt::CaseInsensitive))
                    return true;
            return false;
        });
    }

    if (!preference.codename.isEmpty()) {
        const QString codename = preference.codename;
        narrow([&codename](const QString &name) {
            return name.contains(codename, Qt::CaseInsensitive);
        });
    }

    if (chosenName)
        *chosenName = candidates.first().first;
    return candidates.first().second;
}

QString UpdateChecker::apiBase()
{
    const QString override = qEnvironmentVariable("YTA_UPDATE_API_BASE");
    return override.isEmpty() ? QStringLiteral("https://api.github.com") : override;
}

bool UpdateChecker::parseLatestRelease(const QByteArray &json, Release *release,
                                       bool *ignored, QString *error)
{
    *ignored = false;

    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        *error = QCoreApplication::translate("UpdateChecker",
                                             "GitHub returned an unreadable response.");
        return false;
    }

    const QJsonObject root = doc.object();

    // Drafts are not public, and a prerelease should not be pushed at everyone.
    if (root.value(QStringLiteral("draft")).toBool()
        || root.value(QStringLiteral("prerelease")).toBool()) {
        *ignored = true;
        return true;
    }

    release->tag     = root.value(QStringLiteral("tag_name")).toString();
    release->pageUrl = root.value(QStringLiteral("html_url")).toString();
    release->notes   = root.value(QStringLiteral("body")).toString();
    release->version = release->tag;
    if (release->version.startsWith(QLatin1Char('v'), Qt::CaseInsensitive))
        release->version.remove(0, 1);

    if (release->tag.isEmpty()) {
        *error = QCoreApplication::translate("UpdateChecker",
                                             "The latest release has no version tag.");
        return false;
    }

    QVector<QPair<QString, QString>> candidates;
    const QJsonArray assets = root.value(QStringLiteral("assets")).toArray();
    for (const QJsonValue &value : assets) {
        const QJsonObject asset = value.toObject();
        candidates.append({ asset.value(QStringLiteral("name")).toString(),
                            asset.value(QStringLiteral("browser_download_url")).toString() });
    }
    release->assetUrl = chooseAsset(candidates, currentPreference(), &release->assetName);
    return true;
}

void UpdateChecker::check(bool userInitiated)
{
    if (m_inFlight)
        return;

    // Must look like "owner/repo"; anything else would build a URL that 404s
    // in a way that reads as "no releases yet" rather than "misconfigured".
    const QStringList repoParts = m_repo.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    if (repoParts.size() != 2) {
        emit checkFailed(tr("No GitHub repository is configured for this build."),
                         userInitiated);
        return;
    }

    m_inFlight = true;

    QNetworkRequest request(QUrl(QStringLiteral("%1/repos/%2/releases/latest")
                                     .arg(apiBase(), m_repo)));
    // GitHub rejects API requests without a User-Agent.
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("YtArchive/%1").arg(m_currentVersion));
    request.setRawHeader("Accept", "application/vnd.github+json");
    request.setRawHeader("X-GitHub-Api-Version", "2022-11-28");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);

    QNetworkReply *reply = m_net->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, userInitiated] {
        reply->deleteLater();
        m_inFlight = false;

        if (reply->error() != QNetworkReply::NoError) {
            const int status =
                reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            if (status == 404) {
                emit checkFailed(tr("No published releases were found for %1.").arg(m_repo),
                                 userInitiated);
            } else if (status == 403) {
                // 60 requests/hour for unauthenticated clients, shared per IP.
                emit checkFailed(tr("GitHub's rate limit was reached. Try again later."),
                                 userInitiated);
            } else {
                emit checkFailed(reply->errorString(), userInitiated);
            }
            return;
        }

        Release release;
        bool ignored = false;
        QString error;
        if (!parseLatestRelease(reply->readAll(), &release, &ignored, &error)) {
            emit checkFailed(error, userInitiated);
            return;
        }
        if (ignored) {
            emit upToDate(userInitiated);
            return;
        }

        if (compareVersions(release.version, m_currentVersion) > 0)
            emit updateAvailable(release, userInitiated);
        else
            emit upToDate(userInitiated);
    });
}
