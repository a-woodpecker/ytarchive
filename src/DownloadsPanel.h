#pragma once

#include "DownloadManager.h"

#include <QHash>
#include <QWidget>

class QTreeWidget;
class QTreeWidgetItem;
class QLabel;
class QPushButton;

// Live view of the transfer queue: one row per video, with speed, size and ETA.
class DownloadsPanel : public QWidget
{
    Q_OBJECT
public:
    explicit DownloadsPanel(QWidget* parent = nullptr);

public slots:
    void onProgress(const DownloadManager::Progress& p);
    void onFinished(qint64 videoPk, bool success, const QString& message);
    void clearCompleted();

public slots:
    // Greys the button out when there is nothing running to cancel.
    void setQueueActive(bool active);

signals:
    void cancelRequested(qint64 videoPk);
    void cancelAllRequested();

private:
    void updateSummary();

    QTreeWidget* m_tree;
    QLabel* m_summary;
    QPushButton* m_cancelAll;
    QHash<qint64, QTreeWidgetItem*> m_rows;
};