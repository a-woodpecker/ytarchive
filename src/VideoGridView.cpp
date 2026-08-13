#include "VideoGridView.h"

#include <QApplication>
#include <QScrollBar>
#include <QWheelEvent>
#include <cmath>

VideoGridView::VideoGridView(QWidget *parent)
    : QListView(parent)
{
}

void VideoGridView::scrollContentsBy(int dx, int dy)
{
    QListView::scrollContentsBy(dx, dy);
    viewport()->update();
}

void VideoGridView::wheelEvent(QWheelEvent *event)
{
    // Anything with a modifier held is somebody else's gesture.
    if (event->modifiers() != Qt::NoModifier || event->angleDelta().y() == 0) {
        QListView::wheelEvent(event);
        return;
    }

    QScrollBar *bar = verticalScrollBar();
    if (!bar || bar->maximum() == 0) {
        QListView::wheelEvent(event);
        return;
    }

    // A touchpad reports real pixels; use them unchanged so the content tracks
    // the fingers. Only a notched wheel needs a distance chosen for it.
    if (!event->pixelDelta().isNull()) {
        bar->setValue(bar->value() - event->pixelDelta().y());
        event->accept();
        return;
    }

    // 120 units is one notch by convention; high-resolution wheels send less.
    const int lines = qMax(1, QApplication::wheelScrollLines());
    const double pixels =
        (event->angleDelta().y() / 120.0) * lines * m_pixelsPerLine + m_carry;

    const int whole = static_cast<int>(std::trunc(pixels));
    m_carry = pixels - whole;   // keep the remainder rather than discarding it

    if (whole != 0)
        bar->setValue(bar->value() - whole);
    event->accept();
}
