#pragma once

#include "ChannelSync.h"
#include "Database.h"
#include "DownloadManager.h"
#include "Models.h"
#include "Settings.h"
#include "UpdateChecker.h"

#include <QMainWindow>

class QListWidget;
class QListView;
class QLineEdit;
class QComboBox;
class QLabel;
class QPushButton;
class QDockWidget;
class QPlainTextEdit;
class QCheckBox;
class QProgressBar;

class VideoModel;
class VideoFilterProxy;
class ThumbnailCache;
class DownloadsPanel;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private slots:
    void addChannel();
    void syncCurrentChannel();
    void syncAllChannels();
    void removeCurrentChannel();
    void downloadChecked();
    void downloadEverythingMissing();
    void retryFailedDownloads();
    void openPreferences();
    void openArchiveFolder();
    void onChannelSelectionChanged();
    void onSyncFinished(const ChannelInfo &channel, const QVector<VideoInfo> &videos);
    void onSyncFailed(const QString &message);
    void onSyncProgress(int loaded, int total);
    void onVideoContextMenu(const QPoint &pos);
    void onVideoActivated(const QModelIndex &index);
    void cancelAllDownloads();
    void checkForUpdates();
    void checkYtDlpVersion();
    void onUpdateAvailable(const UpdateChecker::Release &release, bool userInitiated);

private:
    void buildUi();
    void buildActions();
    void applyStyle();
    bool openCatalog();
    void reloadChannels(qint64 selectPk = -1);
    void reloadVideos();
    void updateCounters();
    void updateRetryButton();
    void enqueue(const QVector<VideoInfo> &videos);
    qint64 currentChannelPk() const;
    ChannelInfo currentChannel() const;
    void setBusy(bool busy, const QString &message = QString());
    void repaintAll();
    void applyDefaultDockSizes();
    void resetLayout();
    void scheduleFullRepaint();
    void showSyncBanner(const QString &title);
    void hideSyncBanner();
    QString syncScopeLabel() const;

    Settings         m_settings;
    Database         m_db;
    ThumbnailCache  *m_thumbs = nullptr;
    VideoModel      *m_model = nullptr;
    VideoFilterProxy*m_proxy = nullptr;
    ChannelSync     *m_sync = nullptr;
    UpdateChecker   *m_updates = nullptr;
    DownloadManager *m_downloads = nullptr;

    QListWidget *m_channelList = nullptr;
    QListView   *m_grid = nullptr;
    QLineEdit   *m_search = nullptr;
    QComboBox   *m_stateFilter = nullptr;
    QLabel      *m_headerTitle = nullptr;
    QLabel      *m_headerMeta = nullptr;
    QLabel      *m_selectionLabel = nullptr;
    QWidget     *m_updateBanner = nullptr;
    QLabel      *m_updateLabel = nullptr;
    QWidget     *m_syncBanner = nullptr;
    QLabel      *m_syncLabel = nullptr;
    QProgressBar*m_syncBar = nullptr;
    QPushButton *m_downloadButton = nullptr;
    QPushButton *m_downloadAllButton = nullptr;
    QPushButton *m_retryFailedButton = nullptr;
    QDockWidget *m_navDock = nullptr;
    QDockWidget *m_logDock = nullptr;
    QDockWidget *m_downloadsDock = nullptr;
    DownloadsPanel *m_downloadsPanel = nullptr;
    QPlainTextEdit *m_log = nullptr;

    QStringList m_pendingSyncQueue;   // channel URLs waiting for "Sync all"
    bool m_syncingAll = false;
    bool m_firstShowHandled = false;
    bool m_repaintQueued = false;
    bool m_forbiddenHintShown = false;
    QByteArray m_defaultState;   // layout as built, for View > Reset layout
    int  m_syncTotalCount = 0;        // channels in the current "Sync all" pass
    int  m_syncDoneCount = 0;
};
