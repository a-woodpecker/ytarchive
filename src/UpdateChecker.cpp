#include "UpdateChecker.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>

namespace {

    // Splits "1.2.3-beta.1" into ({1,2,3}, "beta.1").
    void parseVersion(const QString& raw, QList<int>* numbers, QString* suffix)
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
        for (const QString& part : parts) {
            // Stop at the first non-numeric segment rather than guessing.
            bool ok = false;
            const int n = part.toInt(&ok);
            if (!ok)
                break;
            numbers->append(n);
        }
    }

} // namespace

UpdateChecker::UpdateChecker(QObject* parent)
    : QObject(parent)
    , m_net(new QNetworkAccessManager(this))
{
}

int UpdateChecker::compareVersions(const QString& a, const QString& b)
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

    QNetworkRequest request(QUrl(QStringLiteral("https://api.github.com/repos/%1/releases/latest")
        .arg(m_repo)));
    // GitHub rejects API requests without a User-Agent.
    request.setHeader(QNetworkRequest::UserAgentHeader,
        QStringLiteral("YtArchive/%1").arg(m_currentVersion));
    request.setRawHeader("Accept", "application/vnd.github+json");
    request.setRawHeader("X-GitHub-Api-Version", "2022-11-28");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
        QNetworkRequest::NoLessSafeRedirectPolicy);

    QNetworkReply* reply = m_net->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, userInitiated] {
        reply->deleteLater();
        m_inFlight = false;

        if (reply->error() != QNetworkReply::NoError) {
            const int status =
                reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            if (status == 404) {
                emit checkFailed(tr("No published releases were found for %1.").arg(m_repo),
                    userInitiated);
            }
            else if (status == 403) {
                // 60 requests/hour for unauthenticated clients, shared per IP.
                emit checkFailed(tr("GitHub's rate limit was reached. Try again later."),
                    userInitiated);
            }
            else {
                emit checkFailed(reply->errorString(), userInitiated);
            }
            return;
        }

        const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        if (!doc.isObject()) {
            emit checkFailed(tr("GitHub returned an unreadable response."), userInitiated);
            return;
        }

        const QJsonObject root = doc.object();
        if (root.value(QStringLiteral("draft")).toBool() ||
            root.value(QStringLiteral("prerelease")).toBool()) {
            emit upToDate(userInitiated);
            return;
        }

        Release release;
        release.tag = root.value(QStringLiteral("tag_name")).toString();
        release.pageUrl = root.value(QStringLiteral("html_url")).toString();
        release.notes = root.value(QStringLiteral("body")).toString();
        release.version = release.tag;
        if (release.version.startsWith(QLatin1Char('v'), Qt::CaseInsensitive))
            release.version.remove(0, 1);

        if (release.tag.isEmpty()) {
            emit checkFailed(tr("The latest release has no version tag."), userInitiated);
            return;
        }

        // Prefer a direct link to the installer when the release ships one.
        const QJsonArray assets = root.value(QStringLiteral("assets")).toArray();
        for (const QJsonValue& value : assets) {
            const QJsonObject asset = value.toObject();
            const QString name = asset.value(QStringLiteral("name")).toString();
            if (name.endsWith(QStringLiteral(".exe"), Qt::CaseInsensitive)) {
                release.installerUrl =
                    asset.value(QStringLiteral("browser_download_url")).toString();
                break;
            }
        }

        if (compareVersions(release.version, m_currentVersion) > 0)
            emit updateAvailable(release, userInitiated);
        else
            emit upToDate(userInitiated);
        });
}