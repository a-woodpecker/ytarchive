#pragma once

#include <QListView>

// The card grid.
//
// Two behaviours that QListView does not give us:
//
//  * Scrolling repaints the whole viewport. The default blits the pixels it
//    already has and repaints only the newly exposed strip, which can leave a
//    row erased but never redrawn - a hairline across the grid.
//
//  * The wheel moves a sensible distance. With pixel scrolling, Qt's single
//    step for an icon-mode view is roughly one item, so a single notch moves
//    wheelScrollLines items - about three whole rows of cards, which reads as
//    the list jumping rather than scrolling.
class VideoGridView : public QListView
{
    Q_OBJECT
public:
    explicit VideoGridView(QWidget *parent = nullptr);

    // Pixels moved per wheel line. The system's wheelScrollLines multiplies
    // this, so the platform preference is still respected.
    void setPixelsPerWheelLine(int pixels) { m_pixelsPerLine = pixels; }
    int  pixelsPerWheelLine() const { return m_pixelsPerLine; }

protected:
    void scrollContentsBy(int dx, int dy) override;
    void wheelEvent(QWheelEvent *event) override;

private:
    int    m_pixelsPerLine = 45;
    double m_carry = 0.0;   // sub-pixel remainder, so fine wheels do not stall
};
