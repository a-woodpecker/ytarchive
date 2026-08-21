#include "VideoCardDelegate.h"

#include "Models.h"
#include "Theme.h"
#include "VideoModel.h"

#include <QApplication>
#include <QDateTime>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

namespace {

// All card colours come from the active theme; see Theme.cpp.

constexpr int kPad        = 8;
constexpr int kThumbRadius = 10;
constexpr int kCheckSize  = 22;

// A stylesheet may specify font-size in pixels, in which case pointSizeF()
// returns -1. Scale whichever unit the base font actually uses.
QFont derivedFont(const QFont &base, double deltaPoints, bool bold)
{
    QFont f = base;
    if (f.pointSizeF() > 0.0)
        f.setPointSizeF(qMax(1.0, f.pointSizeF() + deltaPoints));
    else if (f.pixelSize() > 0)
        f.setPixelSize(qMax(1, qRound(f.pixelSize() + deltaPoints * 4.0 / 3.0)));
    f.setBold(bold);
    return f;
}

// Badges are painted rather than loaded: no image files to ship, and they
// follow the theme's colours without a second set for the light scheme.
void paintShortBadge(QPainter *painter, const QRectF &box, const ThemePalette &t)
{
    // A tall 9:16 frame with a play mark, echoing the format itself.
    const qreal w = box.height() * 0.62;
    const QRectF frame(box.center().x() - w / 2, box.top(), w, box.height());

    painter->setPen(Qt::NoPen);
    painter->setBrush(t.accent);
    painter->drawRoundedRect(frame, frame.width() * 0.28, frame.width() * 0.28);

    const qreal s = frame.width() * 0.42;
    const QPointF c = frame.center();
    QPolygonF play({ QPointF(c.x() - s * 0.4, c.y() - s * 0.6),
                     QPointF(c.x() + s * 0.7, c.y()),
                     QPointF(c.x() - s * 0.4, c.y() + s * 0.6) });
    painter->setBrush(Qt::white);
    painter->drawPolygon(play);
}

void paintLiveBadge(QPainter *painter, const QRectF &box, const ThemePalette &t)
{
    // A broadcast mark: a dot with two arcs radiating from it.
    const QPointF centre(box.left() + box.width() * 0.34, box.center().y());
    const qreal dot = box.height() * 0.17;

    painter->setPen(Qt::NoPen);
    painter->setBrush(t.accent);
    painter->drawEllipse(centre, dot, dot);

    QPen pen(t.accent);
    pen.setWidthF(qMax(1.4, box.height() * 0.11));
    pen.setCapStyle(Qt::RoundCap);
    painter->setPen(pen);
    painter->setBrush(Qt::NoBrush);

    for (int i = 1; i <= 2; ++i) {
        const qreal r = dot + i * box.height() * 0.20;
        const QRectF arc(centre.x() - r, centre.y() - r, r * 2, r * 2);
        // Two opposing wedges, so it reads as a signal rather than a target.
        painter->drawArc(arc, -50 * 16, 100 * 16);
        painter->drawArc(arc, 130 * 16, 100 * 16);
    }
}

QString metaLine(const QModelIndex &index)
{
    QStringList bits;

    const QDateTime uploaded = index.data(VideoModel::UploadDateRole).toDateTime();
    if (uploaded.isValid()) {
        QString date = uploaded.toLocalTime().toString(QStringLiteral("d MMM yyyy"));
        if (index.data(VideoModel::DateApproxRole).toBool())
            date.prepend(QStringLiteral("~"));
        bits << date;
    } else {
        bits << QCoreApplication::translate("VideoCardDelegate", "Date unknown");
    }

    const qint64 views = index.data(VideoModel::ViewCountRole).toLongLong();
    if (views >= 0)
        bits << QCoreApplication::translate("VideoCardDelegate", "%1 views")
                    .arg(formatCount(views));

    // Only known after download, and null when the uploader hides it, so this
    // is simply absent rather than shown as zero.
    const qint64 likes = index.data(VideoModel::LikeCountRole).toLongLong();
    if (likes >= 0)
        bits << QCoreApplication::translate("VideoCardDelegate", "%1 likes")
                    .arg(formatCount(likes));

    return bits.join(QStringLiteral("  ·  "));
}

} // namespace

VideoCardDelegate::VideoCardDelegate(QObject *parent)
    : QStyledItemDelegate(parent)
{
}

QSize VideoCardDelegate::sizeHint(const QStyleOptionViewItem &, const QModelIndex &) const
{
    return { kCardWidth, kCardHeight + (m_showChannel ? kChannelLineHeight : 0) };
}

QRect VideoCardDelegate::thumbRect(const QRect &card)
{
    const int w = card.width() - 2 * kPad;
    const int h = w * 9 / 16;
    return { card.left() + kPad, card.top() + kPad, w, h };
}

QRect VideoCardDelegate::checkRect(const QRect &card)
{
    const QRect thumb = thumbRect(card);
    return { thumb.left() + 6, thumb.top() + 6, kCheckSize, kCheckSize };
}

void VideoCardDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option,
                              const QModelIndex &index) const
{
    const ThemePalette &t = Theme::palette();

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setRenderHint(QPainter::SmoothPixmapTransform, true);

    const QRect card = option.rect;

    // Card background: subtle, only on hover or selection.
    if (option.state & QStyle::State_Selected) {
        painter->setPen(Qt::NoPen);
        painter->setBrush(t.cardSelected);
        painter->drawRoundedRect(card.adjusted(2, 2, -2, -2), 12, 12);
    } else if (option.state & QStyle::State_MouseOver) {
        painter->setPen(Qt::NoPen);
        painter->setBrush(t.cardHover);
        painter->drawRoundedRect(card.adjusted(2, 2, -2, -2), 12, 12);
    }

    // --- thumbnail ---------------------------------------------------------
    const QRect thumb = thumbRect(card);
    QPainterPath clip;
    clip.addRoundedRect(thumb, kThumbRadius, kThumbRadius);
    painter->save();
    painter->setClipPath(clip);
    painter->fillRect(thumb, t.thumbBackground);

    const QPixmap pm = index.data(VideoModel::ThumbnailRole).value<QPixmap>();
    if (!pm.isNull()) {
        const QPixmap scaled = pm.scaled(thumb.size(), Qt::KeepAspectRatioByExpanding,
                                         Qt::SmoothTransformation);
        const QPoint topLeft(thumb.center().x() - scaled.width() / 2,
                             thumb.center().y() - scaled.height() / 2);
        painter->drawPixmap(topLeft, scaled);
    } else {
        painter->setPen(t.thumbPlaceholder);
        painter->drawText(thumb, Qt::AlignCenter, QStringLiteral("▶"));
    }
    painter->restore();

    const auto state = static_cast<DownloadState>(index.data(VideoModel::StateRole).toInt());
    const double progressFraction = index.data(VideoModel::ProgressRole).toDouble();

    // --- duration badge ----------------------------------------------------
    const QString duration = formatDuration(index.data(VideoModel::DurationRole).toLongLong());
    if (!duration.isEmpty()) {
        const QFont badgeFont = derivedFont(option.font, -0.5, true);
        painter->setFont(badgeFont);

        const QFontMetrics fm(badgeFont);
        const QRect textRect = fm.boundingRect(duration);
        QRect badge(0, 0, textRect.width() + 10, fm.height() + 2);
        badge.moveBottomRight(thumb.bottomRight() - QPoint(6, 6));

        painter->setPen(Qt::NoPen);
        painter->setBrush(t.durationBadge);
        painter->drawRoundedRect(badge, 4, 4);
        painter->setPen(t.durationBadgeText);
        painter->drawText(badge, Qt::AlignCenter, duration);
    }

    // --- kind badge ---------------------------------------------------------
    const auto kind = static_cast<VideoKind>(index.data(VideoModel::KindRole).toInt());
    if (kind != VideoKind::Video) {
        const int side = 22;
        const QRect plate(thumb.left() + 6, thumb.bottom() - side - 6, side, side);

        // A dark plate behind it, because the badge sits on the artwork and
        // has to stay legible over a pale thumbnail.
        painter->setPen(Qt::NoPen);
        painter->setBrush(t.durationBadge);
        painter->drawRoundedRect(plate, 5, 5);

        const QRectF inner = QRectF(plate).adjusted(5, 4, -5, -4);
        painter->save();
        if (kind == VideoKind::Short)
            paintShortBadge(painter, inner, t);
        else
            paintLiveBadge(painter, inner, t);
        painter->restore();
    }

    // --- state badge -------------------------------------------------------
    QString stateText;
    QColor  stateColor;
    if (state == DownloadState::Downloaded) {
        stateText = QStringLiteral("✓ ") + tr("Archived");
        stateColor = t.archived;
    } else if (state == DownloadState::Failed) {
        stateText = tr("Failed");
        stateColor = t.failed;
    } else if (state == DownloadState::Missing) {
        stateText = tr("File missing");
        stateColor = t.failed;
    } else if (state == DownloadState::Queued) {
        stateText = tr("Queued");
        stateColor = t.queued;
    }
    if (!stateText.isEmpty()) {
        const QFont badgeFont = derivedFont(option.font, -0.5, true);
        painter->setFont(badgeFont);

        const QFontMetrics fm(badgeFont);
        QRect badge(0, 0, fm.horizontalAdvance(stateText) + 12, fm.height() + 2);
        badge.moveTopRight(thumb.topRight() - QPoint(6, -6));

        painter->setPen(Qt::NoPen);
        painter->setBrush(stateColor);
        painter->drawRoundedRect(badge, 4, 4);
        painter->setPen(t.badgeText);
        painter->drawText(badge, Qt::AlignCenter, stateText);
    }

    // --- download progress bar across the bottom of the thumbnail ----------
    if (state == DownloadState::Downloading) {
        QRect bar(thumb.left(), thumb.bottom() - 4, thumb.width(), 4);
        painter->setPen(Qt::NoPen);
        painter->setBrush(t.progressTrack);
        painter->drawRect(bar);

        painter->setBrush(t.accent);
        if (progressFraction >= 0.0) {
            QRect filled = bar;
            filled.setWidth(static_cast<int>(bar.width() * progressFraction));
            painter->drawRect(filled);
        } else {
            // Unknown total size: show a fixed marker rather than a fake value.
            QRect filled = bar;
            filled.setWidth(bar.width() / 6);
            painter->drawRect(filled);
        }
    }

    // --- checkbox ----------------------------------------------------------
    const QRect check = checkRect(card);
    const bool isChecked = index.data(Qt::CheckStateRole).toInt() == Qt::Checked;

    painter->setPen(Qt::NoPen);
    painter->setBrush(t.checkboxBackground);
    painter->drawRoundedRect(check, 5, 5);
    painter->setPen(QPen(isChecked ? t.accent : t.checkboxBorder, 1.6));
    painter->setBrush(isChecked ? t.accent : QColor(0, 0, 0, 0));
    painter->drawRoundedRect(check.adjusted(3, 3, -3, -3), 3, 3);
    if (isChecked) {
        painter->setPen(QPen(t.checkboxTick, 2.0));
        const QRect inner = check.adjusted(6, 6, -6, -6);
        painter->drawLine(inner.left(), inner.center().y(),
                          inner.center().x() - 1, inner.bottom());
        painter->drawLine(inner.center().x() - 1, inner.bottom(),
                          inner.right(), inner.top());
    }

    // --- title -------------------------------------------------------------
    const QFont titleFont = derivedFont(option.font, 0.5, true);
    painter->setFont(titleFont);
    painter->setPen(t.title);

    const QFontMetrics titleFm(titleFont);
    QRect titleRect(card.left() + kPad, thumb.bottom() + 10,
                    card.width() - 2 * kPad, titleFm.height() * 2 + 2);

    // Manual two-line elision: Qt has no built-in multi-line elide.
    const QString title = index.data(VideoModel::TitleRole).toString();
    QString firstLine = title;
    QString secondLine;
    int split = -1;
    for (int i = 1; i <= title.length(); ++i) {
        if (titleFm.horizontalAdvance(title.left(i)) > titleRect.width()) {
            split = title.left(i).lastIndexOf(QChar(' '));
            if (split <= 0)
                split = i - 1;
            break;
        }
    }
    if (split > 0) {
        firstLine = title.left(split).trimmed();
        secondLine = titleFm.elidedText(title.mid(split).trimmed(), Qt::ElideRight,
                                        titleRect.width());
    }
    painter->drawText(QRect(titleRect.left(), titleRect.top(),
                            titleRect.width(), titleFm.height()),
                      Qt::AlignLeft | Qt::AlignVCenter, firstLine);
    if (!secondLine.isEmpty()) {
        painter->drawText(QRect(titleRect.left(), titleRect.top() + titleFm.height(),
                                titleRect.width(), titleFm.height()),
                          Qt::AlignLeft | Qt::AlignVCenter, secondLine);
    }

    // --- channel ------------------------------------------------------------
    int metaTop = titleRect.bottom() + 4;
    const QString channel = index.data(VideoModel::ChannelTitleRole).toString();
    if (m_showChannel && !channel.isEmpty()) {
        const QFont channelFont = derivedFont(option.font, -0.5, false);
        painter->setFont(channelFont);
        painter->setPen(t.meta);
        const QFontMetrics channelFm(channelFont);
        const QRect channelRect(titleRect.left(), metaTop,
                                titleRect.width(), channelFm.height());
        painter->drawText(channelRect, Qt::AlignLeft | Qt::AlignVCenter,
                          channelFm.elidedText(channel, Qt::ElideRight,
                                               channelRect.width()));
        metaTop = channelRect.bottom() + 2;
    }

    // --- metadata line -----------------------------------------------------
    const QFont metaFont = derivedFont(option.font, -0.5, false);
    painter->setFont(metaFont);
    painter->setPen(t.meta);

    const QFontMetrics metaFm(metaFont);
    QRect metaRect(titleRect.left(), metaTop, titleRect.width(), metaFm.height());

    QString meta = metaLine(index);
    if (state == DownloadState::Downloading) {
        const QString progressText = index.data(VideoModel::ProgressTextRole).toString();
        if (!progressText.isEmpty()) {
            meta = progressText;
            painter->setPen(t.title);
        }
    } else if (state == DownloadState::Failed) {
        const QString err = index.data(VideoModel::ErrorRole).toString();
        if (!err.isEmpty()) {
            meta = err;
            painter->setPen(t.errorText);
        }
    }
    painter->drawText(metaRect, Qt::AlignLeft | Qt::AlignVCenter,
                      metaFm.elidedText(meta, Qt::ElideRight, metaRect.width()));

    painter->restore();
}

bool VideoCardDelegate::editorEvent(QEvent *event, QAbstractItemModel *model,
                                    const QStyleOptionViewItem &option,
                                    const QModelIndex &index)
{
    if (event->type() == QEvent::MouseButtonRelease) {
        auto *me = static_cast<QMouseEvent *>(event);
        if (me->button() == Qt::LeftButton) {
            const QRect check = checkRect(option.rect);
            // A generous hit area: the visible box plus a few pixels.
            if (check.adjusted(-4, -4, 4, 4).contains(me->position().toPoint())) {
                const bool isChecked = index.data(Qt::CheckStateRole).toInt() == Qt::Checked;
                model->setData(index, isChecked ? Qt::Unchecked : Qt::Checked,
                               Qt::CheckStateRole);
                return true;
            }
        }
    }
    return QStyledItemDelegate::editorEvent(event, model, option, index);
}
