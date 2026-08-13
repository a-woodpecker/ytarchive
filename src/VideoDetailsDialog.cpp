#include "VideoDetailsDialog.h"

#include "Theme.h"
#include "YtDlp.h"

#include <QApplication>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QRegularExpression>
#include <QTabWidget>
#include <QTextBrowser>
#include <QScrollBar>
#include <QVBoxLayout>
#include <algorithm>

namespace {

// Escapes text for HTML, then turns bare URLs into links. Order matters: doing
// it the other way round would mangle the markup we just inserted.
QString escapeAndLinkify(const QString &plain)
{
    QString html = plain.toHtmlEscaped();
    static const QRegularExpression url(
        QStringLiteral(R"((https?://[^\s<>"]+))"));
    html.replace(url, QStringLiteral(R"(<a href="\1">\1</a>)"));
    html.replace(QChar('\n'), QStringLiteral("<br>"));
    return html;
}

QString formatWhen(const QDateTime &dt)
{
    if (!dt.isValid())
        return QString();
    return dt.toLocalTime().toString(QStringLiteral("d MMM yyyy"));
}

} // namespace

// ------------------------------------------------------------ file lookup --

QString VideoDetailsDialog::infoJsonPath(const VideoInfo &video)
{
    if (!video.infoJsonPath.isEmpty() && QFileInfo::exists(video.infoJsonPath))
        return video.infoJsonPath;
    if (!video.filePath.isEmpty()) {
        const QString derived = YtDlp::infoJsonPathFor(video.filePath);
        if (QFileInfo::exists(derived))
            return derived;
    }
    return QString();
}

QString VideoDetailsDialog::descriptionPath(const VideoInfo &video)
{
    if (video.filePath.isEmpty())
        return QString();
    const QFileInfo fi(video.filePath);
    const QString path =
        fi.absoluteDir().filePath(fi.completeBaseName() + QStringLiteral(".description"));
    return QFileInfo::exists(path) ? path : QString();
}

bool VideoDetailsDialog::descriptionAvailable(const VideoInfo &video)
{
    // The .description sidecar is preferred, but the same text is inside
    // .info.json, so either one is enough.
    return !descriptionPath(video).isEmpty()
           || !infoJsonPath(video).isEmpty()
           || !video.description.isEmpty();
}

bool VideoDetailsDialog::infoJsonAvailable(const VideoInfo &video)
{
    return !infoJsonPath(video).isEmpty();
}

// ------------------------------------------------------------------- ctor --

VideoDetailsDialog::VideoDetailsDialog(const VideoInfo &video, Page page, QWidget *parent)
    : QDialog(parent)
    , m_video(video)
{
    setWindowTitle(video.title.isEmpty() ? tr("Video details") : video.title);
    setAttribute(Qt::WA_DeleteOnClose);
    resize(880, 680);

    buildUi();
    loadDescription();

    // Parsing is deferred until the comments page is actually wanted, so
    // opening the description of a video with a huge comment file is instant.
    if (page == Page::Comments) {
        m_tabs->setCurrentIndex(1);
        loadComments();
        renderComments();
    } else {
        m_tabs->setCurrentIndex(0);
    }

    connect(m_tabs, &QTabWidget::currentChanged, this, [this](int index) {
        if (index == 1 && m_comments.isEmpty() && m_commentsError.isEmpty()) {
            loadComments();
            renderComments();
        }
    });
}

void VideoDetailsDialog::buildUi()
{
    auto *layout = new QVBoxLayout(this);

    auto *header = new QLabel(this);
    header->setObjectName(QStringLiteral("headerTitle"));
    header->setText(m_video.title);
    header->setWordWrap(true);
    layout->addWidget(header);

    QStringList meta;
    if (m_video.uploadDate.isValid()) {
        meta << (m_video.dateIsApproximate ? QStringLiteral("~") : QString())
                    + formatWhen(m_video.uploadDate);
    }
    if (m_video.viewCount >= 0)
        meta << tr("%1 views").arg(formatCount(m_video.viewCount));
    if (m_video.durationSecs > 0)
        meta << formatDuration(m_video.durationSecs);
    auto *metaLabel = new QLabel(meta.join(QStringLiteral("  \u00b7  ")), this);
    metaLabel->setObjectName(QStringLiteral("headerMeta"));
    layout->addWidget(metaLabel);

    m_tabs = new QTabWidget(this);

    // ---- description ----
    m_description = new QTextBrowser(this);
    m_description->setOpenExternalLinks(true);
    m_tabs->addTab(m_description, tr("Description"));

    // ---- comments ----
    auto *commentsPage = new QWidget(this);
    auto *commentsLayout = new QVBoxLayout(commentsPage);
    commentsLayout->setContentsMargins(0, 8, 0, 0);

    auto *bar = new QHBoxLayout;
    m_filter = new QLineEdit(commentsPage);
    m_filter->setPlaceholderText(tr("Filter comments"));
    m_filter->setClearButtonEnabled(true);
    connect(m_filter, &QLineEdit::textChanged, this, [this] { renderComments(); });
    bar->addWidget(m_filter, 1);

    m_sort = new QComboBox(commentsPage);
    m_sort->addItem(tr("Most liked"));
    m_sort->addItem(tr("Newest first"));
    m_sort->addItem(tr("Oldest first"));
    m_sort->addItem(tr("As downloaded"));
    connect(m_sort, &QComboBox::currentIndexChanged, this, [this] { renderComments(); });
    bar->addWidget(m_sort);

    commentsLayout->addLayout(bar);

    m_commentCount = new QLabel(commentsPage);
    m_commentCount->setObjectName(QStringLiteral("headerMeta"));
    commentsLayout->addWidget(m_commentCount);

    m_commentsView = new QTextBrowser(commentsPage);
    m_commentsView->setOpenExternalLinks(true);
    commentsLayout->addWidget(m_commentsView, 1);

    m_tabs->addTab(commentsPage, tr("Comments"));
    layout->addWidget(m_tabs, 1);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::close);
    layout->addWidget(buttons);
}

// ------------------------------------------------------------ description --

void VideoDetailsDialog::loadDescription()
{
    QString text;

    const QString sidecar = descriptionPath(m_video);
    if (!sidecar.isEmpty()) {
        QFile f(sidecar);
        if (f.open(QIODevice::ReadOnly | QIODevice::Text))
            text = QString::fromUtf8(f.readAll());
    }

    if (text.trimmed().isEmpty()) {
        const QString json = infoJsonPath(m_video);
        if (!json.isEmpty()) {
            QFile f(json);
            if (f.open(QIODevice::ReadOnly)) {
                const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
                if (doc.isObject())
                    text = doc.object().value(QStringLiteral("description")).toString();
            }
        }
    }

    if (text.trimmed().isEmpty())
        text = m_video.description;

    const ThemePalette &t = Theme::palette();
    if (text.trimmed().isEmpty()) {
        m_description->setHtml(
            QStringLiteral("<div style='color:%1'>%2</div>")
                .arg(t.meta.name(),
                     tr("No description was saved for this video.<br><br>"
                        "Descriptions are written alongside the media when "
                        "<i>Description</i> is enabled under "
                        "Preferences &gt; What to keep.")));
        return;
    }

    m_description->setHtml(
        QStringLiteral("<div style='color:%1; font-size:10pt; line-height:145%%'>%2</div>")
            .arg(t.title.name(), escapeAndLinkify(text)));
}

// --------------------------------------------------------------- comments --

void VideoDetailsDialog::loadComments()
{
    m_comments.clear();
    m_commentsError.clear();

    const QString path = infoJsonPath(m_video);
    if (path.isEmpty()) {
        m_commentsError = tr("No metadata file was saved for this video, so there "
                             "are no comments to show.");
        return;
    }

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        m_commentsError = tr("Could not open %1").arg(QFileInfo(path).fileName());
        return;
    }

    // A comment-bearing info.json can be several megabytes, so the wait cursor
    // is set for the parse rather than leaving the window looking frozen.
    QGuiApplication::setOverrideCursor(Qt::WaitCursor);
    const QByteArray raw = f.readAll();
    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(raw, &parseError);
    QGuiApplication::restoreOverrideCursor();

    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        m_commentsError = tr("The metadata file could not be read: %1")
                              .arg(parseError.errorString());
        return;
    }

    const QJsonArray array = doc.object().value(QStringLiteral("comments")).toArray();
    if (array.isEmpty()) {
        m_commentsError = tr("No comments were downloaded for this video.<br><br>"
                             "Comments are off by default because fetching them can add "
                             "several minutes per video. Enable <i>Comments</i> under "
                             "Preferences &gt; What to keep, then download the video "
                             "again to capture them.");
        return;
    }

    m_comments.reserve(array.size());
    for (const QJsonValue &value : array) {
        const QJsonObject o = value.toObject();
        Comment c;
        c.id       = o.value(QStringLiteral("id")).toString();
        c.parentId = o.value(QStringLiteral("parent")).toString(QStringLiteral("root"));
        c.author   = o.value(QStringLiteral("author")).toString();
        c.text     = o.value(QStringLiteral("text")).toString();

        const QJsonValue likes = o.value(QStringLiteral("like_count"));
        c.likeCount = likes.isNull() || likes.isUndefined()
                          ? -1 : static_cast<qint64>(likes.toDouble(-1));

        const QJsonValue ts = o.value(QStringLiteral("timestamp"));
        if (!ts.isNull() && ts.toDouble(0) > 0)
            c.posted = QDateTime::fromSecsSinceEpoch(static_cast<qint64>(ts.toDouble(0)),
                                                     Qt::UTC);

        c.pinned     = o.value(QStringLiteral("is_pinned")).toBool();
        c.byUploader = o.value(QStringLiteral("author_is_uploader")).toBool();
        c.hearted    = o.value(QStringLiteral("is_favorited")).toBool();

        if (!c.text.isEmpty())
            m_comments.append(c);
    }
}

void VideoDetailsDialog::renderComments()
{
    const ThemePalette &t = Theme::palette();

    if (!m_commentsError.isEmpty()) {
        m_commentCount->clear();
        m_commentsView->setHtml(QStringLiteral("<div style='color:%1'>%2</div>")
                                    .arg(t.meta.name(), m_commentsError));
        return;
    }

    const QString needle = m_filter->text().trimmed();

    // Index of every comment that matches the filter, plus any ancestor needed
    // to keep a matching reply attached to the thread it belongs to.
    QVector<int> topLevel;
    QHash<QString, QVector<int>> repliesByParent;
    QHash<QString, int> indexById;
    for (int i = 0; i < m_comments.size(); ++i)
        indexById.insert(m_comments.at(i).id, i);

    auto matches = [&](const Comment &c) {
        if (needle.isEmpty())
            return true;
        return c.text.contains(needle, Qt::CaseInsensitive)
               || c.author.contains(needle, Qt::CaseInsensitive);
    };

    for (int i = 0; i < m_comments.size(); ++i) {
        const Comment &c = m_comments.at(i);
        if (c.parentId == QLatin1String("root") || !indexById.contains(c.parentId)) {
            topLevel.append(i);
        } else {
            repliesByParent[c.parentId].append(i);
        }
    }

    // A thread is kept when the parent matches or any of its replies do.
    QVector<int> visibleThreads;
    for (int index : topLevel) {
        const Comment &c = m_comments.at(index);
        bool keep = matches(c);
        if (!keep) {
            for (int reply : repliesByParent.value(c.id))
                if (matches(m_comments.at(reply))) { keep = true; break; }
        }
        if (keep)
            visibleThreads.append(index);
    }

    const int mode = m_sort->currentIndex();
    std::stable_sort(visibleThreads.begin(), visibleThreads.end(),
                     [&](int a, int b) {
        const Comment &x = m_comments.at(a);
        const Comment &y = m_comments.at(b);
        if (x.pinned != y.pinned)
            return x.pinned;               // pinned always leads
        switch (mode) {
        case 0: return x.likeCount > y.likeCount;
        case 1: return x.posted > y.posted;
        case 2: return x.posted < y.posted;
        default: return false;             // as downloaded
        }
    });

    // Build the HTML.
    QString html;
    html.reserve(visibleThreads.size() * 400);
    int rendered = 0;
    bool truncated = false;

    auto renderOne = [&](const Comment &c, bool isReply) {
        QStringList badges;
        if (c.pinned)     badges << tr("Pinned");
        if (c.byUploader) badges << tr("Creator");
        if (c.hearted)    badges << tr("Hearted");

        QString badgeHtml;
        if (!badges.isEmpty()) {
            badgeHtml = QStringLiteral(
                " <span style='color:%1; font-size:8pt'>[%2]</span>")
                    .arg(t.accent.name(), badges.join(QStringLiteral(", ")));
        }

        QStringList sub;
        const QString when = formatWhen(c.posted);
        if (!when.isEmpty())
            sub << when;
        if (c.likeCount > 0)
            sub << tr("%1 likes").arg(formatCount(c.likeCount));

        // Qt's rich text engine ignores margin-bottom on a div, so vertical
        // separation comes from an explicit spacer. Without it the reader
        // cannot tell which text belongs to which author.
        html += QStringLiteral(
            "<div style='margin-left:%1px'>"
            "<span style='color:%2; font-weight:bold'>%3</span>%4"
            "<span style='color:%5; font-size:8pt'>&nbsp;&nbsp;%6</span>"
            "</div>"
            "<div style='margin-left:%1px; color:%7'>%8</div>"
            "<div style='height:11px; line-height:11px'>&nbsp;</div>")
            .arg(isReply ? 34 : 0)
            .arg(t.title.name(), c.author.toHtmlEscaped(), badgeHtml,
                 t.meta.name(), sub.join(QStringLiteral(" &middot; ")),
                 t.title.name(), escapeAndLinkify(c.text));
        ++rendered;
    };

    for (int index : visibleThreads) {
        if (rendered >= kRenderLimit) { truncated = true; break; }
        const Comment &c = m_comments.at(index);
        renderOne(c, false);

        QVector<int> replies = repliesByParent.value(c.id);
        std::stable_sort(replies.begin(), replies.end(), [&](int a, int b) {
            return m_comments.at(a).posted < m_comments.at(b).posted;
        });
        for (int reply : replies) {
            if (rendered >= kRenderLimit) { truncated = true; break; }
            if (!needle.isEmpty() && !matches(m_comments.at(reply))
                && !matches(c))
                continue;
            renderOne(m_comments.at(reply), true);
        }

        html += QStringLiteral(
            "<div style='border-top:1px solid %1; height:1px; margin-bottom:6px'></div>")
                    .arg(t.cardHover.name());
    }

    m_renderedCount = rendered;

    QString summary = tr("%1 of %2 comments")
                          .arg(formatCount(rendered), formatCount(m_comments.size()));
    if (truncated) {
        summary += QStringLiteral("  \u00b7  ")
                   + tr("display limited to %1; use the filter to narrow")
                         .arg(formatCount(kRenderLimit));
    }
    m_commentCount->setText(summary);

    if (html.isEmpty()) {
        html = QStringLiteral("<div style='color:%1'>%2</div>")
                   .arg(t.meta.name(), tr("Nothing matches that filter."));
    }
    m_commentsView->setHtml(
        QStringLiteral("<div style='font-size:10pt'>%1</div>").arg(html));
    m_commentsView->verticalScrollBar()->setValue(0);
}
