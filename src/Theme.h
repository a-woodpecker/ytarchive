#pragma once

#include <QColor>
#include <QString>

enum class ThemeMode { Dark, Light, System };

// Colours used by code that paints itself rather than being styled by the
// stylesheet - chiefly the video cards. Keeping them here means a theme is
// defined in exactly two places: this palette and the matching .qss.
struct ThemePalette {
    QColor cardHover;
    QColor cardSelected;
    QColor thumbBackground;
    QColor thumbPlaceholder;
    QColor title;
    QColor meta;
    QColor accent;
    QColor durationBadge;      // sits on top of the thumbnail, so dark in both themes
    QColor durationBadgeText;
    QColor archived;
    QColor failed;
    QColor queued;
    QColor badgeText;
    QColor progressTrack;
    QColor checkboxBackground;
    QColor checkboxBorder;
    QColor checkboxTick;
    QColor errorText;
};

namespace Theme {

// Applies the stylesheet and palette for `mode` to the whole application.
// System resolves to whatever the desktop reports, falling back to Dark.
void apply(ThemeMode mode);

// The palette for the theme currently applied. Never System.
const ThemePalette &palette();

ThemeMode current();
ThemeMode resolve(ThemeMode mode);

QString    toString(ThemeMode mode);
ThemeMode  fromString(const QString &text);

// True when this Qt build can read the desktop's light/dark preference.
bool systemDetectionAvailable();

} // namespace Theme
