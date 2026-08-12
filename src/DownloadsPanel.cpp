#include "DownloadsPanel.h"

#include "Models.h"

#include <QCoreApplication>

#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMenu>
#include <QProgressBar>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace {
constexpr int kColTitle = 0;
constexpr int kColProgress = 1;
constexpr int kColStatus = 2;

QString rateText(double bytesPerSecond)
{
    if (bytesPerSecond <= 0)
        return QString();
    return formatBytes(static_cast<qint64>(bytesPerSecond)) + QStringLiteral("/s");
}

QString etaText(qint64 seconds)
{
    if (seconds < 0)
        return QString();
    if (seconds < 60)
        return QCoreApplication::translate("DownloadsPanel", "%1s left").arg(seconds);
    if (seconds < 3600)
        return QCoreApplication::translate("DownloadsPanel", "%1m left").arg(seconds / 60);
    return QCoreApplication::translate("DownloadsPanel", "%1h %2m left")
        .arg(seconds / 3600).arg((seconds % 3600) / 60);
}
} // namespace

DownloadsPanel::DownloadsPanel(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 6, 8, 8);
    layout->setSpacing(6);

    auto *header = new QHBoxLayout;
    m_summary = new QLabel(tr("Nothing in the queue"), this);
    m_summary->setObjectName(QStringLiteral("panelSummary"));
    header->addWidget(m_summary);
    header->addStretch();

    auto *clearButton = new QPushButton(tr("Clear finished"), this);
    clearButton->setFlat(true);
    connect(clearButton, &QPushButton::clicked, this, &DownloadsPanel::clearCompleted);
    header->addWidget(clearButton);
    layout->addLayout(header);

    m_tree = new QTreeWidget(this);
    m_tree->setColumnCount(3);
    m_tree->setHeaderLabels({ tr("Video"), tr("Progress"), tr("Status") });
    m_tree->setRootIsDecorated(false);
    m_tree->setAlternatingRowColors(false);
    m_tree->setUniformRowHeights(true);
    m_tree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    m_tree->header()->setSectionResizeMode(kColTitle, QHeaderView::Stretch);
    m_tree->header()->setSectionResizeMode(kColProgress, QHeaderView::Fixed);
    m_tree->header()->setSectionResizeMode(kColStatus, QHeaderView::Fixed);
    m_tree->setColumnWidth(kColProgress, 220);
    m_tree->setColumnWidth(kColStatus, 260);
    layout->addWidget(m_tree);

    connect(m_tree, &QTreeWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        QTreeWidgetItem *item = m_tree->itemAt(pos);
        if (!item)
            return;
        QMenu menu(this);
        QAction *cancelAction = menu.addAction(tr("Cancel this download"));
        if (menu.exec(m_tree->viewport()->mapToGlobal(pos)) == cancelAction)
            emit cancelRequested(item->data(kColTitle, Qt::UserRole).toLongLong());
    });
}

void DownloadsPanel::onProgress(const DownloadManager::Progress &p)
{
    QTreeWidgetItem *item = m_rows.value(p.videoPk, nullptr);
    if (!item) {
        item = new QTreeWidgetItem(m_tree);
        item->setData(kColTitle, Qt::UserRole, p.videoPk);
        m_rows.insert(p.videoPk, item);

        auto *bar = new QProgressBar(m_tree);
        bar->setRange(0, 1000);
        bar->setTextVisible(true);
        bar->setFormat(QStringLiteral("%p%"));
        m_tree->setItemWidget(item, kColProgress, bar);
    }

    QString label = p.title;
    if (!p.channelTitle.isEmpty())
        label = p.channelTitle + QStringLiteral(" — ") + p.title;
    item->setText(kColTitle, label);
    item->setToolTip(kColTitle, label);

    if (auto *bar = qobject_cast<QProgressBar *>(m_tree->itemWidget(item, kColProgress))) {
        if (p.fraction >= 0.0) {
            bar->setRange(0, 1000);
            bar->setValue(static_cast<int>(p.fraction * 1000));
        } else {
            bar->setRange(0, 0);   // busy indicator when the size is unknown
        }
    }

    QStringList status;
    status << p.stage;
    if (p.totalBytes > 0)
        status << formatBytes(p.downloadedBytes) + QStringLiteral(" / ") + formatBytes(p.totalBytes);
    else if (p.downloadedBytes > 0)
        status << formatBytes(p.downloadedBytes);
    const QString rate = rateText(p.speedBytesPerSec);
    if (!rate.isEmpty())
        status << rate;
    const QString eta = etaText(p.etaSeconds);
    if (!eta.isEmpty())
        status << eta;

    item->setText(kColStatus, status.join(QStringLiteral("  ·  ")));
    updateSummary();
}

void DownloadsPanel::onFinished(qint64 videoPk, bool success, const QString &message)
{
    QTreeWidgetItem *item = m_rows.value(videoPk, nullptr);
    if (!item)
        return;

    if (auto *bar = qobject_cast<QProgressBar *>(m_tree->itemWidget(item, kColProgress))) {
        bar->setRange(0, 1000);
        bar->setValue(success ? 1000 : bar->value());
        bar->setEnabled(false);
    }

    item->setText(kColStatus, success ? tr("Saved to the archive") : message);
    item->setToolTip(kColStatus, success ? message : message);
    item->setData(kColStatus, Qt::UserRole, success);
    updateSummary();
}

void DownloadsPanel::clearCompleted()
{
    const QList<qint64> keys = m_rows.keys();
    for (qint64 pk : keys) {
        QTreeWidgetItem *item = m_rows.value(pk);
        auto *bar = qobject_cast<QProgressBar *>(m_tree->itemWidget(item, kColProgress));
        const bool finished = bar && !bar->isEnabled();
        if (finished) {
            m_rows.remove(pk);
            delete item;
        }
    }
    updateSummary();
}

void DownloadsPanel::updateSummary()
{
    int active = 0;
    int done = 0;
    for (QTreeWidgetItem *item : std::as_const(m_rows)) {
        auto *bar = qobject_cast<QProgressBar *>(m_tree->itemWidget(item, kColProgress));
        if (bar && bar->isEnabled())
            ++active;
        else
            ++done;
    }

    if (m_rows.isEmpty())
        m_summary->setText(tr("Nothing in the queue"));
    else
        m_summary->setText(tr("%n in progress", "", active) +
                           QStringLiteral("  ·  ") + tr("%n finished", "", done));
}
