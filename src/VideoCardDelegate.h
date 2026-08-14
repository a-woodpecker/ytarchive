#pragma once

#include <QStyledItemDelegate>

// Draws a YouTube-style card: 16:9 thumbnail with a duration badge, a
// two-line title, and a metadata line. A checkbox in the thumbnail's corner
// selects the video for download; downloaded videos get a badge instead.
class VideoCardDelegate : public QStyledItemDelegate
{
    Q_OBJECT
public:
    explicit VideoCardDelegate(QObject *parent = nullptr);

    static constexpr int kCardWidth  = 320;
    static constexpr int kCardHeight = 258;
    // Extra height for the channel line, added only when it is being shown.
    static constexpr int kChannelLineHeight = 17;

    void setShowChannel(bool show) { m_showChannel = show; }
    bool showChannel() const { return m_showChannel; }

    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override;
    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override;

protected:
    bool editorEvent(QEvent *event, QAbstractItemModel *model,
                     const QStyleOptionViewItem &option, const QModelIndex &index) override;

private:
    static QRect thumbRect(const QRect &card);
    bool m_showChannel = false;
    static QRect checkRect(const QRect &card);
};
