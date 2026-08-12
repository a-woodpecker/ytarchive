#include "MainWindow.h"

#include "DownloadsPanel.h"
#include "PreferencesDialog.h"
#include "ThumbnailCache.h"
#include "VideoCardDelegate.h"
#include "VideoModel.h"
#include "YtDlp.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QDockWidget>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QListWidget>
#include <QMap>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollBar>
#include <QSettings>
#include <QStatusBar>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

namespace {
    constexpr qint64 kAllChannels = -1;

    // QListView scrolls by blitting the pixels it already has and repainting only
    // the strip that just became visible. Any row erased but left outside that
    // strip survives as a hairline across the grid. Repainting the whole viewport
    // costs nothing at this item count and removes the entire class of artefact.
    class SeamlessListView : public QListView
    {
    public:
        using QListView::QListView;

    protected:
        void scrollContentsBy(int dx, int dy) override
        {
            QListView::scrollContentsBy(dx, dy);
            viewport()->update();
        }
    };
} // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    m_settings = Settings::load();

    m_thumbs = new ThumbnailCache(this);
    m_thumbs->setCacheDir(m_settings.thumbnailCacheDir());

    m_model = new VideoModel(m_thumbs, this);
    m_proxy = new VideoFilterProxy(this);
    m_proxy->setSourceModel(m_model);

    m_sync = new ChannelSync(this);
    m_sync->setSettings(m_settings);

    m_downloads = new DownloadManager(&m_db, this);
    m_downloads->setSettings(m_settings);

    m_updates = new UpdateChecker(this);
    m_updates->setRepository(QStringLiteral(YTA_GITHUB_REPO));
    m_updates->setCurrentVersion(QStringLiteral(YTA_VERSION));

    buildUi();
    buildActions();
    applyStyle();

    connect(m_sync, &ChannelSync::finished, this, &MainWindow::onSyncFinished);
    connect(m_sync, &ChannelSync::failed, this, &MainWindow::onSyncFailed);
    connect(m_sync, &ChannelSync::statusChanged, this, [this](const QString& msg) {
        statusBar()->showMessage(msg);
        if (m_syncBanner->isVisible())
            m_syncLabel->setText(syncScopeLabel() + msg);
        });
    connect(m_sync, &ChannelSync::progress, this, &MainWindow::onSyncProgress);
    connect(m_sync, &ChannelSync::cancelled, this, [this] {
        hideSyncBanner();
        setBusy(false);
        statusBar()->showMessage(tr("Channel sync cancelled."), 5000);
        });

    connect(m_downloads, &DownloadManager::progress, this,
        [this](const DownloadManager::Progress& p) {
            m_downloadsPanel->onProgress(p);
            QString text = p.stage;
            if (p.totalBytes > 0) {
                text = tr("%1 of %2").arg(formatBytes(p.downloadedBytes),
                    formatBytes(p.totalBytes));
                if (p.speedBytesPerSec > 0)
                    text += QStringLiteral("  ·  ") +
                    formatBytes(static_cast<qint64>(p.speedBytesPerSec)) +
                    QStringLiteral("/s");
            }
            m_model->updateProgress(p.videoPk, p.fraction, text);
        });
    connect(m_downloads, &DownloadManager::videoStateChanged, this,
        [this](qint64 pk, DownloadState state) { m_model->updateState(pk, state); });
    connect(m_downloads, &DownloadManager::videoFinished, this,
        [this](qint64 pk, bool ok, const QString& message) {
            m_downloadsPanel->onFinished(pk, ok, message);
            if (ok)
                m_model->refreshVideo(m_db.video(pk));
            updateCounters();
            reloadChannels(currentChannelPk());
        });
    connect(m_downloads, &DownloadManager::logMessage, this, [this](const QString& line) {
        m_log->appendPlainText(line);
        });
    connect(m_downloads, &DownloadManager::allFinished, this, [this] {
        statusBar()->showMessage(tr("Queue finished."), 8000);
        });
    connect(m_downloadsPanel, &DownloadsPanel::cancelRequested, m_downloads,
        &DownloadManager::cancel);

    connect(m_thumbs, &ThumbnailCache::fetchFailed, this,
        [this](const QString& videoId, const QString& url, const QString& error) {
            // Report the first handful only: one broken sync could otherwise write
            // thousands of identical lines.
            static int reported = 0;
            if (reported == 0) {
                // Printed once, alongside the first failure: if JPEG is absent the
                // downloads are fine and the decoder is the whole problem.
                m_log->appendPlainText(ThumbnailCache::imageFormatDiagnostic());
            }
            if (reported < 8) {
                m_log->appendPlainText(tr("Thumbnail failed for %1: %2").arg(videoId, error));
                if (!url.isEmpty())
                    m_log->appendPlainText(QStringLiteral("  ") + url);
            }
            else if (reported == 8) {
                m_log->appendPlainText(tr("Further thumbnail errors suppressed."));
            }
            ++reported;
        });

    connect(m_updates, &UpdateChecker::updateAvailable, this, &MainWindow::onUpdateAvailable);
    connect(m_updates, &UpdateChecker::upToDate, this, [this](bool userInitiated) {
        m_settings.lastUpdateCheck = QDateTime::currentSecsSinceEpoch();
        m_settings.save();
        if (userInitiated) {
            QMessageBox::information(this, tr("No update available"),
                tr("You are running version %1, which is the latest release.")
                .arg(QStringLiteral(YTA_VERSION)));
        }
        });
    connect(m_updates, &UpdateChecker::checkFailed, this,
        [this](const QString& message, bool userInitiated) {
            m_log->appendPlainText(tr("Update check failed: %1").arg(message));
            if (userInitiated)
                QMessageBox::warning(this, tr("Could not check for updates"), message);
        });

    connect(m_model, &VideoModel::checkedCountChanged, this, [this](int n) {
        m_selectionLabel->setText(n == 0 ? tr("Nothing selected")
            : tr("%n video(s) selected", "", n));
        m_downloadButton->setEnabled(n > 0);
        });

    if (openCatalog()) {
        m_db.reconcileWithDisk();
        reloadChannels();
    }

    // Checked once, up front: without TLS every thumbnail fetch fails while
    // downloads keep working, because yt-dlp does its own networking.
    if (!ThumbnailCache::httpsAvailable()) {
        m_log->appendPlainText(ThumbnailCache::tlsDiagnostic());
        statusBar()->showMessage(
            tr("Thumbnails are unavailable: Qt has no working HTTPS support. "
                "See the yt-dlp output tab."), 15000);
    }

    QSettings ui;
    const QByteArray savedGeometry = ui.value(QStringLiteral("window/geometry")).toByteArray();
    const QByteArray savedState = ui.value(QStringLiteral("window/state")).toByteArray();
    if (!savedGeometry.isEmpty())
        restoreGeometry(savedGeometry);
    if (!savedState.isEmpty()) {
        restoreState(savedState);
    }
    else {
        // First run: the grid is the main event, the transfer list needs a few
        // rows. Deferred, because resizing docks mid-construction leaves the
        // layout settling while the first paint is already happening.
        QTimer::singleShot(0, this, [this] {
            resizeDocks({ m_downloadsDock }, { 200 }, Qt::Vertical);
            });
    }

    // At most one background check per day, and never before the window is up.
    const qint64 dayAgo = QDateTime::currentSecsSinceEpoch() - 24 * 60 * 60;
    if (m_settings.checkUpdatesOnStartup && m_settings.lastUpdateCheck < dayAgo)
        QTimer::singleShot(2500, this, [this] { m_updates->check(false); });
}

MainWindow::~MainWindow() = default;

void MainWindow::buildUi()
{
    setWindowTitle(tr("YouTube Archive"));
    resize(1360, 860);

    // ---- navigation panel -------------------------------------------------
    auto* navDock = new QDockWidget(tr("Channels"), this);
    navDock->setObjectName(QStringLiteral("navDock"));
    navDock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
    navDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);

    auto* navWidget = new QWidget(navDock);
    auto* navLayout = new QVBoxLayout(navWidget);
    navLayout->setContentsMargins(8, 8, 8, 8);
    navLayout->setSpacing(8);

    m_channelList = new QListWidget(navWidget);
    m_channelList->setObjectName(QStringLiteral("channelList"));
    m_channelList->setIconSize(QSize(28, 28));
    m_channelList->setSpacing(1);
    connect(m_channelList, &QListWidget::currentItemChanged, this,
        &MainWindow::onChannelSelectionChanged);
    navLayout->addWidget(m_channelList, 1);

    auto* addButton = new QPushButton(tr("Add channel"), navWidget);
    addButton->setObjectName(QStringLiteral("primaryButton"));
    connect(addButton, &QPushButton::clicked, this, &MainWindow::addChannel);
    navLayout->addWidget(addButton);

    navDock->setWidget(navWidget);
    navDock->setMinimumWidth(240);
    addDockWidget(Qt::LeftDockWidgetArea, navDock);

    // ---- central area -----------------------------------------------------
    auto* central = new QWidget(this);
    auto* centralLayout = new QVBoxLayout(central);
    centralLayout->setContentsMargins(0, 0, 0, 0);
    centralLayout->setSpacing(0);

    // Channel header
    auto* header = new QWidget(central);
    header->setObjectName(QStringLiteral("channelHeader"));
    auto* headerLayout = new QVBoxLayout(header);
    headerLayout->setContentsMargins(20, 16, 20, 12);
    headerLayout->setSpacing(2);

    m_headerTitle = new QLabel(tr("All videos"), header);
    m_headerTitle->setObjectName(QStringLiteral("headerTitle"));
    headerLayout->addWidget(m_headerTitle);

    m_headerMeta = new QLabel(QString(), header);
    m_headerMeta->setObjectName(QStringLiteral("headerMeta"));
    headerLayout->addWidget(m_headerMeta);

    centralLayout->addWidget(header);

    // Update notice - hidden unless a newer release exists.
    m_updateBanner = new QWidget(central);
    m_updateBanner->setObjectName(QStringLiteral("updateBanner"));
    auto* updateLayout = new QHBoxLayout(m_updateBanner);
    updateLayout->setContentsMargins(20, 8, 20, 8);
    updateLayout->setSpacing(12);

    m_updateLabel = new QLabel(QString(), m_updateBanner);
    m_updateLabel->setObjectName(QStringLiteral("updateLabel"));
    updateLayout->addWidget(m_updateLabel, 1);
    m_updateBanner->hide();
    centralLayout->addWidget(m_updateBanner);

    // Sync progress banner - hidden until a channel listing is running.
    m_syncBanner = new QWidget(central);
    m_syncBanner->setObjectName(QStringLiteral("syncBanner"));
    auto* bannerLayout = new QHBoxLayout(m_syncBanner);
    bannerLayout->setContentsMargins(20, 8, 20, 8);
    bannerLayout->setSpacing(12);

    m_syncLabel = new QLabel(QString(), m_syncBanner);
    m_syncLabel->setObjectName(QStringLiteral("syncLabel"));
    m_syncLabel->setMinimumWidth(260);
    bannerLayout->addWidget(m_syncLabel);

    m_syncBar = new QProgressBar(m_syncBanner);
    m_syncBar->setObjectName(QStringLiteral("syncBar"));
    m_syncBar->setRange(0, 0);          // indeterminate until a total is known
    m_syncBar->setTextVisible(false);
    m_syncBar->setFixedHeight(6);
    bannerLayout->addWidget(m_syncBar, 1);

    auto* syncCancel = new QToolButton(m_syncBanner);
    syncCancel->setText(tr("Cancel"));
    connect(syncCancel, &QToolButton::clicked, this, [this] {
        m_syncingAll = false;
        m_pendingSyncQueue.clear();
        m_sync->cancel();
        });
    bannerLayout->addWidget(syncCancel);

    m_syncBanner->hide();
    centralLayout->addWidget(m_syncBanner);

    // Toolbar row
    auto* bar = new QWidget(central);
    bar->setObjectName(QStringLiteral("filterBar"));
    auto* barLayout = new QHBoxLayout(bar);
    barLayout->setContentsMargins(20, 8, 20, 10);
    barLayout->setSpacing(8);

    m_search = new QLineEdit(bar);
    m_search->setPlaceholderText(tr("Search titles in this channel"));
    m_search->setClearButtonEnabled(true);
    m_search->setMinimumWidth(220);
    m_search->setMaximumWidth(340);
    connect(m_search, &QLineEdit::textChanged, m_proxy, &VideoFilterProxy::setSearchText);
    barLayout->addWidget(m_search);

    m_stateFilter = new QComboBox(bar);
    m_stateFilter->addItem(tr("All videos"), VideoFilterProxy::AnyState);
    m_stateFilter->addItem(tr("Not in archive"), VideoFilterProxy::OnlyNotDownloaded);
    m_stateFilter->addItem(tr("In archive"), VideoFilterProxy::OnlyDownloaded);
    m_stateFilter->addItem(tr("Failed or missing"), VideoFilterProxy::OnlyFailed);
    connect(m_stateFilter, &QComboBox::currentIndexChanged, this, [this](int i) {
        m_proxy->setStateFilter(
            static_cast<VideoFilterProxy::StateFilter>(m_stateFilter->itemData(i).toInt()));
        });
    barLayout->addWidget(m_stateFilter);

    auto* selectAll = new QToolButton(bar);
    selectAll->setText(tr("Select all"));
    connect(selectAll, &QToolButton::clicked, this, [this] { m_model->setAllChecked(true); });
    barLayout->addWidget(selectAll);

    auto* selectNone = new QToolButton(bar);
    selectNone->setText(tr("Clear selection"));
    connect(selectNone, &QToolButton::clicked, this, [this] { m_model->setAllChecked(false); });
    barLayout->addWidget(selectNone);

    auto* selectMissing = new QToolButton(bar);
    selectMissing->setText(tr("Select not archived"));
    connect(selectMissing, &QToolButton::clicked, this, [this] {
        m_model->checkOnly(DownloadState::NotDownloaded);
        });
    barLayout->addWidget(selectMissing);

    barLayout->addStretch();

    m_selectionLabel = new QLabel(tr("Nothing selected"), bar);
    m_selectionLabel->setObjectName(QStringLiteral("selectionLabel"));
    barLayout->addWidget(m_selectionLabel);

    m_downloadButton = new QPushButton(tr("Download selected"), bar);
    m_downloadButton->setObjectName(QStringLiteral("primaryButton"));
    m_downloadButton->setEnabled(false);
    connect(m_downloadButton, &QPushButton::clicked, this, &MainWindow::downloadChecked);
    barLayout->addWidget(m_downloadButton);

    m_downloadAllButton = new QPushButton(tr("Download everything missing"), bar);
    connect(m_downloadAllButton, &QPushButton::clicked, this,
        &MainWindow::downloadEverythingMissing);
    barLayout->addWidget(m_downloadAllButton);

    centralLayout->addWidget(bar);

    // Video grid
    m_grid = new SeamlessListView(central);
    m_grid->setObjectName(QStringLiteral("videoGrid"));
    m_grid->setModel(m_proxy);
    m_grid->setItemDelegate(new VideoCardDelegate(m_grid));
    m_grid->setViewMode(QListView::IconMode);
    m_grid->setResizeMode(QListView::Adjust);
    m_grid->setMovement(QListView::Static);
    m_grid->setUniformItemSizes(true);

    // An explicit grid with the gutters baked in, rather than setSpacing().
    // Icon-mode layout with Adjust resizing places items itself, and letting it
    // derive cell size from the size hint plus spacing leaves the geometry
    // slightly indeterminate - which is where repaint seams come from.
    m_grid->setSpacing(0);
    m_grid->setGridSize(QSize(VideoCardDelegate::kCardWidth + 12,
        VideoCardDelegate::kCardHeight + 12));
    m_grid->setMouseTracking(true);
    m_grid->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_grid->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_grid->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_grid->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_grid, &QListView::customContextMenuRequested, this,
        &MainWindow::onVideoContextMenu);
    connect(m_grid, &QListView::doubleClicked, this, &MainWindow::onVideoActivated);
    centralLayout->addWidget(m_grid, 1);

    setCentralWidget(central);

    // ---- downloads dock ---------------------------------------------------
    m_downloadsDock = new QDockWidget(tr("Downloads"), this);
    m_downloadsDock->setObjectName(QStringLiteral("downloadsDock"));
    m_downloadsPanel = new DownloadsPanel(m_downloadsDock);
    m_downloadsDock->setWidget(m_downloadsPanel);
    addDockWidget(Qt::BottomDockWidgetArea, m_downloadsDock);

    auto* logDock = new QDockWidget(tr("yt-dlp output"), this);
    logDock->setObjectName(QStringLiteral("logDock"));
    m_log = new QPlainTextEdit(logDock);
    m_log->setReadOnly(true);
    m_log->setMaximumBlockCount(5000);
    logDock->setWidget(m_log);
    addDockWidget(Qt::BottomDockWidgetArea, logDock);
    tabifyDockWidget(m_downloadsDock, logDock);
    m_downloadsDock->raise();

    statusBar()->showMessage(tr("Ready."));
}

void MainWindow::buildActions()
{
    QMenu* fileMenu = menuBar()->addMenu(tr("&File"));
    fileMenu->addAction(tr("&Add channel…"), QKeySequence(QStringLiteral("Ctrl+N")),
        this, &MainWindow::addChannel);
    fileMenu->addAction(tr("Open archive &folder"), this, &MainWindow::openArchiveFolder);
    fileMenu->addSeparator();
    fileMenu->addAction(tr("&Preferences…"), QKeySequence::Preferences,
        this, &MainWindow::openPreferences);
    fileMenu->addSeparator();
    fileMenu->addAction(tr("&Quit"), QKeySequence::Quit, this, &QWidget::close);

    QMenu* channelMenu = menuBar()->addMenu(tr("&Channel"));
    channelMenu->addAction(tr("&Refresh video list"), QKeySequence::Refresh,
        this, &MainWindow::syncCurrentChannel);
    channelMenu->addAction(tr("Refresh &all channels"), this, &MainWindow::syncAllChannels);
    channelMenu->addSeparator();
    channelMenu->addAction(tr("&Remove from catalog…"), this, &MainWindow::removeCurrentChannel);

    QMenu* downloadMenu = menuBar()->addMenu(tr("&Downloads"));
    downloadMenu->addAction(tr("Download &selected"), QKeySequence(QStringLiteral("Ctrl+D")),
        this, &MainWindow::downloadChecked);
    downloadMenu->addAction(tr("Download everything &missing"), this,
        &MainWindow::downloadEverythingMissing);
    downloadMenu->addSeparator();
    downloadMenu->addAction(tr("&Cancel all"), this, [this] {
        m_downloads->cancelAll();
        statusBar()->showMessage(tr("Cancelled the queue."), 5000);
        });

    QMenu* helpMenu = menuBar()->addMenu(tr("&Help"));
    helpMenu->addAction(tr("Check for &updates…"), this, &MainWindow::checkForUpdates);
    helpMenu->addSeparator();
    helpMenu->addAction(tr("&About"), this, [this] {
        QMessageBox::about(this, tr("About YouTube Archive"),
            tr("<h3>YouTube Archive</h3>"
                "<p>A local archiving front-end for yt-dlp. Video files are stored on disk in "
                "per-channel folders and indexed by a SQLite catalog, so the archive stays "
                "readable with or without this program.</p>"
                "<p>Every file is stamped with its upload date rather than its download date.</p>"
                "<p>Requires <b>yt-dlp</b> and <b>ffmpeg</b> on your system.</p>"
                "<p>Version %1</p>").arg(QStringLiteral(YTA_VERSION)));
        });
}

void MainWindow::applyStyle()
{
    QFile f(QStringLiteral(":/resources/style.qss"));
    if (f.open(QIODevice::ReadOnly))
        qApp->setStyleSheet(QString::fromUtf8(f.readAll()));
}

bool MainWindow::openCatalog()
{
    QDir().mkpath(m_settings.archiveRoot);

    QString error;
    if (!m_db.open(m_settings.databasePath(), &error)) {
        QMessageBox::critical(this, tr("Cannot open the catalog"),
            tr("The catalog database at\n%1\ncould not be opened.\n\n%2\n\n"
                "Pick a different archive folder in Preferences.")
            .arg(m_settings.databasePath(), error));
        return false;
    }
    return true;
}

// ------------------------------------------------------------- channels ----

void MainWindow::reloadChannels(qint64 selectPk)
{
    if (selectPk == -2)
        selectPk = currentChannelPk();

    const QSignalBlocker blocker(m_channelList);
    m_channelList->clear();

    auto* allItem = new QListWidgetItem(tr("All videos"), m_channelList);
    allItem->setData(Qt::UserRole, QVariant::fromValue<qint64>(kAllChannels));
    allItem->setSizeHint(QSize(0, 36));

    const QVector<ChannelInfo> channels = m_db.channels();
    for (const ChannelInfo& c : channels) {
        auto* item = new QListWidgetItem(m_channelList);
        item->setText(c.title.isEmpty() ? c.channelId : c.title);
        item->setData(Qt::UserRole, QVariant::fromValue<qint64>(c.pk));
        item->setSizeHint(QSize(0, 40));
        item->setToolTip(tr("%1\n%2 videos, %3 in the archive")
            .arg(c.url).arg(c.videoCount).arg(c.downloadedCount));
    }

    // Restore the previous selection where possible.
    int rowToSelect = 0;
    for (int i = 0; i < m_channelList->count(); ++i) {
        if (m_channelList->item(i)->data(Qt::UserRole).toLongLong() == selectPk) {
            rowToSelect = i;
            break;
        }
    }
    m_channelList->setCurrentRow(rowToSelect);

    onChannelSelectionChanged();
}

qint64 MainWindow::currentChannelPk() const
{
    QListWidgetItem* item = m_channelList->currentItem();
    return item ? item->data(Qt::UserRole).toLongLong() : kAllChannels;
}

ChannelInfo MainWindow::currentChannel() const
{
    const qint64 pk = currentChannelPk();
    if (pk == kAllChannels)
        return ChannelInfo();
    return const_cast<Database&>(m_db).channel(pk);
}

void MainWindow::onChannelSelectionChanged()
{
    reloadVideos();
    updateCounters();
}

void MainWindow::reloadVideos()
{
    if (!m_db.isOpen())
        return;
    m_model->setVideos(m_db.videos(currentChannelPk()));
    m_grid->scrollToTop();
}

void MainWindow::updateCounters()
{
    const qint64 pk = currentChannelPk();
    if (pk == kAllChannels) {
        m_headerTitle->setText(tr("All videos"));
        m_headerMeta->setText(tr("%1 videos catalogued  ·  %2 in the archive  ·  %3 on disk")
            .arg(m_db.totalVideos())
            .arg(m_db.totalDownloaded())
            .arg(formatBytes(m_db.totalBytes())));
        return;
    }

    const ChannelInfo c = m_db.channel(pk);
    m_headerTitle->setText(c.title.isEmpty() ? c.channelId : c.title);

    QStringList meta;
    meta << tr("%1 videos catalogued").arg(c.videoCount);
    meta << tr("%1 in the archive").arg(c.downloadedCount);
    if (c.lastSynced.isValid())
        meta << tr("list refreshed %1")
        .arg(c.lastSynced.toLocalTime().toString(QStringLiteral("d MMM yyyy, HH:mm")));
    m_headerMeta->setText(meta.join(QStringLiteral("  ·  ")));
}

void MainWindow::addChannel()
{
    bool ok = false;
    const QString input = QInputDialog::getText(
        this, tr("Add a channel"),
        tr("Paste a channel URL or handle:\n"
            "for example https://www.youtube.com/@channelname or @channelname"),
        QLineEdit::Normal, QString(), &ok);
    if (!ok || input.trimmed().isEmpty())
        return;

    m_syncingAll = false;
    m_syncTotalCount = 1;
    m_syncDoneCount = 0;
    showSyncBanner(tr("Contacting channel…"));
    setBusy(true, tr("Loading the video list. Large channels take a while."));
    m_sync->start(input.trimmed());
}

void MainWindow::syncCurrentChannel()
{
    const ChannelInfo c = currentChannel();
    if (c.pk < 0) {
        syncAllChannels();
        return;
    }
    m_syncingAll = false;
    m_syncTotalCount = 1;
    m_syncDoneCount = 0;
    showSyncBanner(tr("Refreshing %1…").arg(c.title));
    setBusy(true, tr("Refreshing %1…").arg(c.title));
    m_sync->start(c.url.isEmpty() ? c.channelId : c.url);
}

void MainWindow::syncAllChannels()
{
    const QVector<ChannelInfo> channels = m_db.channels();
    if (channels.isEmpty()) {
        statusBar()->showMessage(tr("No channels yet. Add one first."), 5000);
        return;
    }

    m_pendingSyncQueue.clear();
    for (const ChannelInfo& c : channels)
        m_pendingSyncQueue << (c.url.isEmpty() ? c.channelId : c.url);

    m_syncingAll = true;
    m_syncTotalCount = channels.size();
    m_syncDoneCount = 0;
    const QString first = m_pendingSyncQueue.takeFirst();
    showSyncBanner(tr("Contacting channel…"));
    setBusy(true, tr("Refreshing all channels…"));
    m_sync->start(first);
}

void MainWindow::removeCurrentChannel()
{
    const ChannelInfo c = currentChannel();
    if (c.pk < 0)
        return;

    const auto answer = QMessageBox::question(
        this, tr("Remove channel"),
        tr("Remove \"%1\" from the catalog?\n\n"
            "Downloaded video files stay on disk in:\n%2")
        .arg(c.title, m_settings.channelDir(c.title, c.channelId)),
        QMessageBox::Cancel | QMessageBox::Yes, QMessageBox::Cancel);
    if (answer != QMessageBox::Yes)
        return;

    m_db.removeChannel(c.pk);
    reloadChannels();
}

void MainWindow::onSyncFinished(const ChannelInfo& channel, const QVector<VideoInfo>& videos)
{
    const qint64 pk = m_db.upsertChannel(channel);
    if (pk < 0) {
        onSyncFailed(tr("The channel could not be written to the catalog."));
        return;
    }

    int fresh = 0;
    m_db.upsertVideos(pk, videos, &fresh);

    reloadChannels(m_syncingAll ? currentChannelPk() : pk);

    const QString summary = fresh > 0
        ? tr("%1: %2 videos listed, %n new", "", fresh).arg(channel.title).arg(videos.size())
        : tr("%1: %2 videos listed, nothing new").arg(channel.title).arg(videos.size());
    statusBar()->showMessage(summary, 10000);
    m_log->appendPlainText(summary);

    ++m_syncDoneCount;

    if (m_syncingAll && !m_pendingSyncQueue.isEmpty()) {
        const QString next = m_pendingSyncQueue.takeFirst();
        showSyncBanner(tr("Contacting channel…"));
        m_sync->start(next);
        return;
    }

    m_syncingAll = false;
    hideSyncBanner();
    setBusy(false);
}

void MainWindow::onSyncFailed(const QString& message)
{
    m_log->appendPlainText(tr("Sync failed: %1").arg(message));
    ++m_syncDoneCount;

    if (m_syncingAll && !m_pendingSyncQueue.isEmpty()) {
        // One bad channel should not abort the whole pass.
        const QString next = m_pendingSyncQueue.takeFirst();
        showSyncBanner(tr("Contacting channel…"));
        m_sync->start(next);
        return;
    }

    m_syncingAll = false;
    hideSyncBanner();
    setBusy(false);
    QMessageBox::warning(this, tr("Could not load the channel"), message);
}

// ------------------------------------------------------------ downloads ----

void MainWindow::enqueue(const QVector<VideoInfo>& videos)
{
    if (videos.isEmpty()) {
        statusBar()->showMessage(tr("Nothing to download."), 5000);
        return;
    }

    // Videos may span several channels in the "All videos" view, and each
    // channel writes into its own folder.
    QMap<qint64, QVector<VideoInfo>> byChannel;
    for (const VideoInfo& v : videos) {
        if (v.state == DownloadState::Downloaded)
            continue;
        byChannel[v.channelPk].append(v);
    }

    int total = 0;
    for (auto it = byChannel.cbegin(); it != byChannel.cend(); ++it) {
        const ChannelInfo c = m_db.channel(it.key());
        m_downloads->enqueue(it.value(), c.title, c.channelId);
        total += it.value().size();
    }

    if (total == 0) {
        statusBar()->showMessage(tr("Everything selected is already in the archive."), 6000);
        return;
    }

    m_downloadsDock->show();
    m_downloadsDock->raise();
    statusBar()->showMessage(tr("Queued %n video(s).", "", total), 6000);
}

void MainWindow::downloadChecked()
{
    enqueue(m_model->checkedVideos());
}

void MainWindow::downloadEverythingMissing()
{
    QVector<VideoInfo> missing;
    for (const VideoInfo& v : m_model->allVideos())
        if (v.state != DownloadState::Downloaded && v.state != DownloadState::Queued &&
            v.state != DownloadState::Downloading)
            missing.append(v);

    if (missing.isEmpty()) {
        statusBar()->showMessage(tr("Everything here is already archived."), 6000);
        return;
    }

    const auto answer = QMessageBox::question(
        this, tr("Download everything missing"),
        tr("Queue %n video(s) for download?\n\nThis can take a long time and a lot of disk space.",
            "", missing.size()),
        QMessageBox::Cancel | QMessageBox::Yes, QMessageBox::Yes);
    if (answer == QMessageBox::Yes)
        enqueue(missing);
}

// ----------------------------------------------------------------- misc ----

void MainWindow::onVideoContextMenu(const QPoint& pos)
{
    const QModelIndex proxyIndex = m_grid->indexAt(pos);
    if (!proxyIndex.isValid())
        return;

    const qint64 pk = proxyIndex.data(VideoModel::VideoPkRole).toLongLong();
    const auto state = static_cast<DownloadState>(proxyIndex.data(VideoModel::StateRole).toInt());
    const QString filePath = proxyIndex.data(VideoModel::FilePathRole).toString();
    const QString videoId = proxyIndex.data(VideoModel::VideoIdRole).toString();

    QMenu menu(this);
    QAction* downloadAction = menu.addAction(tr("Download now"));
    downloadAction->setEnabled(state != DownloadState::Downloaded &&
        state != DownloadState::Downloading);

    QAction* playAction = menu.addAction(tr("Open the archived file"));
    playAction->setEnabled(!filePath.isEmpty() && QFileInfo::exists(filePath));

    QAction* revealAction = menu.addAction(tr("Show the containing folder"));
    revealAction->setEnabled(playAction->isEnabled());

    menu.addSeparator();
    QAction* browserAction = menu.addAction(tr("Open on YouTube"));
    QAction* cancelAction = menu.addAction(tr("Cancel download"));
    cancelAction->setEnabled(state == DownloadState::Downloading ||
        state == DownloadState::Queued);

    menu.addSeparator();
    QAction* forgetAction = menu.addAction(tr("Forget the downloaded copy"));
    forgetAction->setEnabled(state == DownloadState::Downloaded ||
        state == DownloadState::Missing ||
        state == DownloadState::Failed);

    QAction* chosen = menu.exec(m_grid->viewport()->mapToGlobal(pos));
    if (!chosen)
        return;

    if (chosen == downloadAction) {
        enqueue({ m_db.video(pk) });
    }
    else if (chosen == playAction) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(filePath));
    }
    else if (chosen == revealAction) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(filePath).absolutePath()));
    }
    else if (chosen == browserAction) {
        QDesktopServices::openUrl(
            QUrl(QStringLiteral("https://www.youtube.com/watch?v=") + videoId));
    }
    else if (chosen == cancelAction) {
        m_downloads->cancel(pk);
    }
    else if (chosen == forgetAction) {
        m_db.clearDownload(pk);
        m_model->refreshVideo(m_db.video(pk));
        updateCounters();
    }
}

void MainWindow::onVideoActivated(const QModelIndex& index)
{
    const QString filePath = index.data(VideoModel::FilePathRole).toString();
    if (!filePath.isEmpty() && QFileInfo::exists(filePath)) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(filePath));
        return;
    }
    enqueue({ m_db.video(index.data(VideoModel::VideoPkRole).toLongLong()) });
}

void MainWindow::checkForUpdates()
{
    if (m_updates->isChecking())
        return;
    // A manual check overrides a previously skipped version.
    m_settings.skippedVersion.clear();
    m_settings.save();
    statusBar()->showMessage(tr("Checking for updates…"), 4000);
    m_updates->check(true);
}

void MainWindow::onUpdateAvailable(const UpdateChecker::Release& release, bool userInitiated)
{
    m_settings.lastUpdateCheck = QDateTime::currentSecsSinceEpoch();
    m_settings.save();

    if (!userInitiated && release.version == m_settings.skippedVersion)
        return;

    m_log->appendPlainText(tr("Version %1 is available.").arg(release.version));

    // Rebuild the banner's buttons each time so the URLs stay current.
    QLayout* layout = m_updateBanner->layout();
    while (layout->count() > 1) {
        QLayoutItem* item = layout->takeAt(1);
        if (QWidget* w = item->widget())
            w->deleteLater();
        delete item;
    }

    m_updateLabel->setText(tr("Version %1 is available. You have %2.")
        .arg(release.version, QStringLiteral(YTA_VERSION)));

    const QString target = release.installerUrl.isEmpty() ? release.pageUrl
        : release.installerUrl;

    auto* download = new QPushButton(release.installerUrl.isEmpty()
        ? tr("View release")
        : tr("Download installer"), m_updateBanner);
    download->setObjectName(QStringLiteral("primaryButton"));
    connect(download, &QPushButton::clicked, this, [target] {
        QDesktopServices::openUrl(QUrl(target));
        });
    layout->addWidget(download);

    auto* notes = new QToolButton(m_updateBanner);
    notes->setText(tr("Release notes"));
    connect(notes, &QToolButton::clicked, this, [release] {
        QDesktopServices::openUrl(QUrl(release.pageUrl));
        });
    layout->addWidget(notes);

    auto* skip = new QToolButton(m_updateBanner);
    skip->setText(tr("Skip this version"));
    connect(skip, &QToolButton::clicked, this, [this, release] {
        m_settings.skippedVersion = release.version;
        m_settings.save();
        m_updateBanner->hide();
        });
    layout->addWidget(skip);

    auto* dismiss = new QToolButton(m_updateBanner);
    dismiss->setText(QStringLiteral("\u2715"));
    dismiss->setToolTip(tr("Dismiss until next launch"));
    connect(dismiss, &QToolButton::clicked, this, [this] { m_updateBanner->hide(); });
    layout->addWidget(dismiss);

    m_updateBanner->show();
}

QString MainWindow::syncScopeLabel() const
{
    if (!m_syncingAll || m_syncTotalCount <= 1)
        return QString();
    return tr("Channel %1 of %2  ·  ")
        .arg(qMin(m_syncDoneCount + 1, m_syncTotalCount))
        .arg(m_syncTotalCount);
}

void MainWindow::showSyncBanner(const QString& title)
{
    m_syncLabel->setText(syncScopeLabel() + title);
    m_syncBar->setRange(0, 0);
    m_syncBanner->show();
}

void MainWindow::hideSyncBanner()
{
    m_syncBanner->hide();
    m_syncBar->setRange(0, 0);
    m_syncLabel->clear();
}

void MainWindow::onSyncProgress(int loaded, int total)
{
    if (!m_syncBanner->isVisible())
        return;

    if (total > 0) {
        m_syncBar->setRange(0, total);
        m_syncBar->setValue(loaded);
        m_syncLabel->setText(syncScopeLabel() +
            tr("Loaded %1 of %2 videos").arg(loaded).arg(total));
    }
    else {
        // Channel size is unknown, which is the usual case. An honest busy bar
        // plus a live count beats a percentage we would have to invent.
        m_syncBar->setRange(0, 0);
        m_syncLabel->setText(syncScopeLabel() +
            (loaded > 0 ? tr("Found %n video(s)…", "", loaded)
                : tr("Contacting channel…")));
    }
}

void MainWindow::openPreferences()
{
    PreferencesDialog dialog(m_settings, this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    const Settings updated = dialog.result();
    const bool rootChanged = updated.archiveRoot != m_settings.archiveRoot;

    m_settings = updated;
    m_settings.save();
    m_updates->setCurrentVersion(QStringLiteral(YTA_VERSION));
    m_sync->setSettings(m_settings);
    m_downloads->setSettings(m_settings);
    m_thumbs->setCacheDir(m_settings.thumbnailCacheDir());

    if (rootChanged) {
        m_db.close();
        if (openCatalog()) {
            m_db.reconcileWithDisk();
            reloadChannels();
        }
    }
}

void MainWindow::openArchiveFolder()
{
    QDir().mkpath(m_settings.archiveRoot);
    QDesktopServices::openUrl(QUrl::fromLocalFile(m_settings.archiveRoot));
}

void MainWindow::setBusy(bool busy, const QString& message)
{
    m_channelList->setEnabled(!busy);
    if (busy)
        statusBar()->showMessage(message);
    else
        statusBar()->showMessage(tr("Ready."), 3000);
    QApplication::setOverrideCursor(busy ? Qt::BusyCursor : Qt::ArrowCursor);
    if (!busy)
        QApplication::restoreOverrideCursor();
}

void MainWindow::showEvent(QShowEvent* event)
{
    QMainWindow::showEvent(event);

    if (m_firstShowHandled)
        return;
    m_firstShowHandled = true;

    // The first paint runs while the dock layout is still settling, which can
    // leave a row of the backing store written but never repainted - visible as
    // a hairline wherever content should have been. Once the event loop has
    // drained, repaint everything once. QWidget::update() does not recurse into
    // children, so ask each of them directly.
    QTimer::singleShot(0, this, [this] {
        const QList<QWidget*> children = findChildren<QWidget*>();
        for (QWidget* w : children)
            w->update();
        update();
        });
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    if (m_downloads->isBusy()) {
        const auto answer = QMessageBox::question(
            this, tr("Downloads are running"),
            tr("%n download(s) are still running. Quit anyway?\n\n"
                "Partly downloaded files are kept and will resume next time.",
                "", m_downloads->activeCount() + m_downloads->queuedCount()),
            QMessageBox::Cancel | QMessageBox::Yes, QMessageBox::Cancel);
        if (answer != QMessageBox::Yes) {
            event->ignore();
            return;
        }
        m_downloads->cancelAll();
    }

    QSettings ui;
    ui.setValue(QStringLiteral("window/geometry"), saveGeometry());
    ui.setValue(QStringLiteral("window/state"), saveState());

    m_sync->cancel();
    QMainWindow::closeEvent(event);
}