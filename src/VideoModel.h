#pragma once

#include "Models.h"

#include <QAbstractListModel>
#include <QHash>
#include <QSortFilterProxyModel>

class ThumbnailCache;

class VideoModel : public QAbstractListModel
{
    Q_OBJECT
public:
    enum Roles {
        VideoPkRole = Qt::UserRole + 1,
        VideoIdRole,
        TitleRole,
        UploadDateRole,
        DateApproxRole,
        DurationRole,
        ViewCountRole,
        StateRole,
        ThumbnailRole,
        ProgressRole,
        ProgressTextRole,
        FilePathRole,
        ErrorRole,
        ChannelPkRole
    };

    explicit VideoModel(ThumbnailCache *thumbs, QObject *parent = nullptr);

    void setVideos(const QVector<VideoInfo> &videos);
    const VideoInfo *videoAt(int row) const;
    QVector<VideoInfo> checkedVideos() const;
    QVector<VideoInfo> allVideos() const { return m_videos; }
    int checkedCount() const;

    void setAllChecked(bool checked);
    void setCheckedForRows(const QModelIndexList &rows, bool checked);
    void checkOnly(DownloadState state);

    void updateState(qint64 videoPk, DownloadState state);
    void updateProgress(qint64 videoPk, double fraction, const QString &text);
    void refreshVideo(const VideoInfo &v);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    bool setData(const QModelIndex &index, const QVariant &value, int role) override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;

signals:
    void checkedCountChanged(int count);

private:
    void rebuildIndex();
    int rowForPk(qint64 pk) const;

    QVector<VideoInfo>  m_videos;
    QHash<qint64, int>  m_rowByPk;
    ThumbnailCache     *m_thumbs;
};

// Filters by free text and by download state.
class VideoFilterProxy : public QSortFilterProxyModel
{
    Q_OBJECT
public:
    enum StateFilter { AnyState, OnlyDownloaded, OnlyNotDownloaded, OnlyFailed };

    using QSortFilterProxyModel::QSortFilterProxyModel;

    void setSearchText(const QString &text);
    void setStateFilter(StateFilter f);

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override;

private:
    QString     m_search;
    StateFilter m_stateFilter = AnyState;
};
