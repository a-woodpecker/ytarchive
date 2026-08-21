#include "VideoModel.h"
#include "ThumbnailCache.h"

#include <QCoreApplication>

VideoModel::VideoModel(ThumbnailCache *thumbs, QObject *parent)
    : QAbstractListModel(parent)
    , m_thumbs(thumbs)
{
    if (m_thumbs) {
        connect(m_thumbs, &ThumbnailCache::ready, this, [this](const QString &videoId) {
            for (int row = 0; row < m_videos.size(); ++row) {
                if (m_videos.at(row).videoId == videoId) {
                    const QModelIndex idx = index(row);
                    emit dataChanged(idx, idx, { ThumbnailRole, Qt::DecorationRole });
                    return;
                }
            }
        });
    }
}

void VideoModel::setVideos(const QVector<VideoInfo> &videos)
{
    beginResetModel();
    m_videos = videos;
    rebuildIndex();
    endResetModel();
    emit checkedCountChanged(checkedCount());
}

void VideoModel::rebuildIndex()
{
    m_rowByPk.clear();
    m_rowByPk.reserve(m_videos.size());
    for (int i = 0; i < m_videos.size(); ++i)
        m_rowByPk.insert(m_videos.at(i).pk, i);
}

int VideoModel::rowForPk(qint64 pk) const
{
    return m_rowByPk.value(pk, -1);
}

const VideoInfo *VideoModel::videoAt(int row) const
{
    if (row < 0 || row >= m_videos.size())
        return nullptr;
    return &m_videos.at(row);
}

QVector<VideoInfo> VideoModel::checkedVideos() const
{
    QVector<VideoInfo> out;
    for (const VideoInfo &v : m_videos)
        if (v.checked)
            out.append(v);
    return out;
}

int VideoModel::checkedCount() const
{
    int n = 0;
    for (const VideoInfo &v : m_videos)
        if (v.checked)
            ++n;
    return n;
}

void VideoModel::setAllChecked(bool checked)
{
    if (m_videos.isEmpty())
        return;
    for (VideoInfo &v : m_videos)
        v.checked = checked;
    emit dataChanged(index(0), index(m_videos.size() - 1), { Qt::CheckStateRole });
    emit checkedCountChanged(checkedCount());
}

void VideoModel::setCheckedForRows(const QModelIndexList &rows, bool checked)
{
    for (const QModelIndex &idx : rows) {
        if (!idx.isValid() || idx.row() >= m_videos.size())
            continue;
        m_videos[idx.row()].checked = checked;
        emit dataChanged(index(idx.row()), index(idx.row()), { Qt::CheckStateRole });
    }
    emit checkedCountChanged(checkedCount());
}

void VideoModel::checkOnly(DownloadState state)
{
    if (m_videos.isEmpty())
        return;
    for (VideoInfo &v : m_videos)
        v.checked = (v.state == state);
    emit dataChanged(index(0), index(m_videos.size() - 1), { Qt::CheckStateRole });
    emit checkedCountChanged(checkedCount());
}

void VideoModel::updateState(qint64 videoPk, DownloadState state)
{
    const int row = rowForPk(videoPk);
    if (row < 0)
        return;
    m_videos[row].state = state;
    if (state == DownloadState::Downloaded) {
        m_videos[row].checked = false;
        m_videos[row].progress = 0.0;
        m_videos[row].progressText.clear();
    }
    const QModelIndex idx = index(row);
    emit dataChanged(idx, idx, { StateRole, ProgressRole, ProgressTextRole, Qt::CheckStateRole });
    emit checkedCountChanged(checkedCount());
}

void VideoModel::updateProgress(qint64 videoPk, double fraction, const QString &text)
{
    const int row = rowForPk(videoPk);
    if (row < 0)
        return;
    m_videos[row].progress = fraction;
    m_videos[row].progressText = text;
    const QModelIndex idx = index(row);
    emit dataChanged(idx, idx, { ProgressRole, ProgressTextRole });
}

void VideoModel::refreshVideo(const VideoInfo &v)
{
    const int row = rowForPk(v.pk);
    if (row < 0)
        return;
    const bool wasChecked = m_videos.at(row).checked;
    m_videos[row] = v;
    m_videos[row].checked = wasChecked;
    emit dataChanged(index(row), index(row));
}

void VideoModel::setShowChannel(bool show)
{
    if (m_showChannel == show)
        return;
    m_showChannel = show;
    if (!m_videos.isEmpty())
        emit dataChanged(index(0), index(m_videos.size() - 1), { ChannelTitleRole });
}

int VideoModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_videos.size();
}

QVariant VideoModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_videos.size())
        return QVariant();

    const VideoInfo &v = m_videos.at(index.row());

    switch (role) {
    case Qt::DisplayRole:
    case TitleRole:         return v.title;
    case Qt::ToolTipRole: {
        QString tip = v.title;
        if (v.uploadDate.isValid()) {
            tip += QLatin1Char('\n') + v.uploadDate.toLocalTime().toString(Qt::TextDate);
            if (v.dateIsApproximate)
                tip += QCoreApplication::translate("VideoModel", " (approximate until downloaded)");
        }
        if (!v.filePath.isEmpty())
            tip += QLatin1Char('\n') + v.filePath;
        if (!v.errorText.isEmpty())
            tip += QLatin1Char('\n') + v.errorText;
        return tip;
    }
    case Qt::CheckStateRole: return v.checked ? Qt::Checked : Qt::Unchecked;
    case VideoPkRole:        return v.pk;
    case VideoIdRole:        return v.videoId;
    case UploadDateRole:     return v.uploadDate;
    case DateApproxRole:     return v.dateIsApproximate;
    case DurationRole:       return v.durationSecs;
    case ViewCountRole:      return v.viewCount;
    case LikeCountRole:      return v.likeCount;
    case ChannelTitleRole:   return m_showChannel ? v.channelTitle : QString();
    case KindRole:           return static_cast<int>(v.kind);
    case StateRole:          return static_cast<int>(v.state);
    case ProgressRole:       return v.progress;
    case ProgressTextRole:   return v.progressText;
    case FilePathRole:       return v.filePath;
    case ErrorRole:          return v.errorText;
    case ChannelPkRole:      return v.channelPk;
    case ThumbnailRole:
        return m_thumbs ? QVariant::fromValue(m_thumbs->thumbnail(v.videoId, v.thumbUrl))
                        : QVariant();
    default:
        return QVariant();
    }
}

bool VideoModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (!index.isValid() || index.row() >= m_videos.size())
        return false;

    if (role == Qt::CheckStateRole) {
        m_videos[index.row()].checked =
            static_cast<Qt::CheckState>(value.toInt()) == Qt::Checked;
        emit dataChanged(index, index, { Qt::CheckStateRole });
        emit checkedCountChanged(checkedCount());
        return true;
    }
    return false;
}

Qt::ItemFlags VideoModel::flags(const QModelIndex &index) const
{
    if (!index.isValid())
        return Qt::NoItemFlags;
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable;
}

// ---------------------------------------------------------------- proxy ----

void VideoFilterProxy::setSearchText(const QString &text)
{
    m_search = text.trimmed();
    invalidateFilter();
}

void VideoFilterProxy::setStateFilter(StateFilter f)
{
    m_stateFilter = f;
    invalidateFilter();
}

void VideoFilterProxy::setKindFilter(KindFilter f)
{
    m_kindFilter = f;
    invalidateFilter();
}

bool VideoFilterProxy::filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const
{
    const QModelIndex idx = sourceModel()->index(sourceRow, 0, sourceParent);
    if (!idx.isValid())
        return false;

    if (!m_search.isEmpty()) {
        const QString title = idx.data(VideoModel::TitleRole).toString();
        if (!title.contains(m_search, Qt::CaseInsensitive))
            return false;
    }

    if (m_kindFilter != AnyKind) {
        const auto kind = static_cast<VideoKind>(idx.data(VideoModel::KindRole).toInt());
        switch (m_kindFilter) {
        case OnlyVideos:      if (kind != VideoKind::Video) return false; break;
        case OnlyShorts:      if (kind != VideoKind::Short) return false; break;
        case OnlyLivestreams: if (kind != VideoKind::Livestream) return false; break;
        case AnyKind:         break;
        }
    }

    const auto state = static_cast<DownloadState>(idx.data(VideoModel::StateRole).toInt());
    switch (m_stateFilter) {
    case OnlyDownloaded:
        return state == DownloadState::Downloaded;
    case OnlyNotDownloaded:
        return state != DownloadState::Downloaded;
    case OnlyFailed:
        return state == DownloadState::Failed || state == DownloadState::Missing;
    case AnyState:
        break;
    }
    return true;
}
