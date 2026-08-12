#include "Theme.h"

#include <QApplication>
#include <QFile>
#include <QPalette>
#include <QStyleHints>

namespace {

ThemeMode g_current = ThemeMode::Dark;
ThemePalette g_palette;

ThemePalette darkPalette()
{
    ThemePalette p;
    p.cardHover          = QColor(0xFF, 0xFF, 0xFF, 18);
    p.cardSelected       = QColor(0x3E, 0xA6, 0xFF, 40);
    p.thumbBackground    = QColor(0x18, 0x18, 0x18);
    p.thumbPlaceholder   = QColor(0x55, 0x55, 0x55);
    p.title              = QColor(0xF1, 0xF1, 0xF1);
    p.meta               = QColor(0xAA, 0xAA, 0xAA);
    p.accent             = QColor(0xFF, 0x00, 0x33);
    p.durationBadge      = QColor(0x00, 0x00, 0x00, 200);
    p.durationBadgeText  = Qt::white;
    p.archived           = QColor(0x2B, 0xA6, 0x40);
    p.failed             = QColor(0xCC, 0x33, 0x33);
    p.queued             = QColor(0x60, 0x60, 0x60);
    p.badgeText          = Qt::white;
    p.progressTrack      = QColor(0xFF, 0xFF, 0xFF, 60);
    p.checkboxBackground = QColor(0, 0, 0, 140);
    p.checkboxBorder     = QColor(0xDD, 0xDD, 0xDD);
    p.checkboxTick       = Qt::white;
    p.errorText          = QColor(0xE5, 0x73, 0x73);
    return p;
}

ThemePalette lightPalette()
{
    ThemePalette p;
    p.cardHover          = QColor(0x00, 0x00, 0x00, 14);
    p.cardSelected       = QColor(0x06, 0x5F, 0xD8, 34);
    p.thumbBackground    = QColor(0xE5, 0xE5, 0xE5);
    p.thumbPlaceholder   = QColor(0xB0, 0xB0, 0xB0);
    p.title              = QColor(0x0F, 0x0F, 0x0F);
    p.meta               = QColor(0x60, 0x60, 0x60);
    p.accent             = QColor(0xCC, 0x00, 0x00);
    // The duration badge always sits over the thumbnail image, so it stays
    // dark in both themes - a light badge would vanish on pale artwork.
    p.durationBadge      = QColor(0x00, 0x00, 0x00, 200);
    p.durationBadgeText  = Qt::white;
    p.archived           = QColor(0x1E, 0x8E, 0x3E);
    p.failed             = QColor(0xC5, 0x22, 0x1F);
    p.queued             = QColor(0x70, 0x70, 0x70);
    p.badgeText          = Qt::white;
    p.progressTrack      = QColor(0x00, 0x00, 0x00, 60);
    p.checkboxBackground = QColor(0xFF, 0xFF, 0xFF, 210);
    p.checkboxBorder     = QColor(0x60, 0x60, 0x60);
    p.checkboxTick       = Qt::white;
    p.errorText          = QColor(0xC5, 0x22, 0x1F);
    return p;
}

QString stylesheetFor(ThemeMode mode)
{
    const QString path = mode == ThemeMode::Light
                             ? QStringLiteral(":/resources/style-light.qss")
                             : QStringLiteral(":/resources/style.qss");
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return QString();
    return QString::fromUtf8(f.readAll());
}

} // namespace

namespace Theme {

bool systemDetectionAvailable()
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    return true;
#else
    return false;
#endif
}

ThemeMode resolve(ThemeMode mode)
{
    if (mode != ThemeMode::System)
        return mode;

#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    if (auto *hints = QGuiApplication::styleHints()) {
        if (hints->colorScheme() == Qt::ColorScheme::Light)
            return ThemeMode::Light;
        if (hints->colorScheme() == Qt::ColorScheme::Dark)
            return ThemeMode::Dark;
    }
#endif
    // Older Qt cannot report the desktop preference; dark is this app's default.
    return ThemeMode::Dark;
}

void apply(ThemeMode mode)
{
    const ThemeMode effective = resolve(mode);
    g_current = effective;
    g_palette = effective == ThemeMode::Light ? lightPalette() : darkPalette();

    // Keep QPalette roughly in step with the stylesheet. Widgets drawn by the
    // style rather than by our rules - tooltips, some dialogs - read from it.
    QPalette qp = qApp->palette();
    if (effective == ThemeMode::Light) {
        qp.setColor(QPalette::Window,          QColor(0xFF, 0xFF, 0xFF));
        qp.setColor(QPalette::WindowText,      QColor(0x0F, 0x0F, 0x0F));
        qp.setColor(QPalette::Base,            QColor(0xFF, 0xFF, 0xFF));
        qp.setColor(QPalette::AlternateBase,   QColor(0xF2, 0xF2, 0xF2));
        qp.setColor(QPalette::Text,            QColor(0x0F, 0x0F, 0x0F));
        qp.setColor(QPalette::Button,          QColor(0xF2, 0xF2, 0xF2));
        qp.setColor(QPalette::ButtonText,      QColor(0x0F, 0x0F, 0x0F));
        qp.setColor(QPalette::ToolTipBase,     QColor(0xFF, 0xFF, 0xFF));
        qp.setColor(QPalette::ToolTipText,     QColor(0x0F, 0x0F, 0x0F));
        qp.setColor(QPalette::Highlight,       QColor(0x06, 0x5F, 0xD8));
        qp.setColor(QPalette::HighlightedText, Qt::white);
    } else {
        qp.setColor(QPalette::Window,          QColor(0x0F, 0x0F, 0x0F));
        qp.setColor(QPalette::WindowText,      QColor(0xF1, 0xF1, 0xF1));
        qp.setColor(QPalette::Base,            QColor(0x12, 0x12, 0x12));
        qp.setColor(QPalette::AlternateBase,   QColor(0x1C, 0x1C, 0x1C));
        qp.setColor(QPalette::Text,            QColor(0xF1, 0xF1, 0xF1));
        qp.setColor(QPalette::Button,          QColor(0x27, 0x27, 0x27));
        qp.setColor(QPalette::ButtonText,      QColor(0xF1, 0xF1, 0xF1));
        qp.setColor(QPalette::ToolTipBase,     QColor(0x21, 0x21, 0x21));
        qp.setColor(QPalette::ToolTipText,     QColor(0xF1, 0xF1, 0xF1));
        qp.setColor(QPalette::Highlight,       QColor(0x3E, 0xA6, 0xFF));
        qp.setColor(QPalette::HighlightedText, Qt::black);
    }
    qApp->setPalette(qp);
    qApp->setStyleSheet(stylesheetFor(effective));
}

const ThemePalette &palette() { return g_palette; }
ThemeMode current() { return g_current; }

QString toString(ThemeMode mode)
{
    switch (mode) {
    case ThemeMode::Light:  return QStringLiteral("light");
    case ThemeMode::System: return QStringLiteral("system");
    case ThemeMode::Dark:   break;
    }
    return QStringLiteral("dark");
}

ThemeMode fromString(const QString &text)
{
    const QString t = text.trimmed().toLower();
    if (t == QLatin1String("light"))  return ThemeMode::Light;
    if (t == QLatin1String("system")) return ThemeMode::System;
    return ThemeMode::Dark;
}

} // namespace Theme
