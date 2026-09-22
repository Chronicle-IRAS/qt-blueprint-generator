#pragma once
#include <QColor>
#include <QFont>
class QApplication;

namespace EditorTheme {
inline constexpr int SpaceSmall = 4;
inline constexpr int SpaceMedium = 8;
inline constexpr int SpaceLarge = 16;

enum class Theme {
    Light,
    Dark,
};

struct Colors {
    QColor window, surface, surfaceAlt, text, textMuted, border;
    QColor accent, accentHover, accentPressed, onAccent, disabledText, disabledSurface;
    QColor canvas, gridMinor, gridMajor, nodeBody, nodeBorder, nodeHeader, nodeHeaderText;
    QColor nodeHover, nodeSelectedBody, portInput, portOutput, portCompatible, portCompatibleFill;
    QColor edge, selection, diffAdded, diffRemoved;
};

// Colours of the theme that is currently applied. Custom drawn items read them while painting,
// so switching themes does not have to push colours into item state.
const Colors &colors();
// Colours of a named theme. The light and dark sets are checked against each other by tests.
const Colors &colors(Theme theme);
Theme theme();
// Makes theme active and applies its palette, stylesheet and font to the whole application.
void setTheme(QApplication &application, Theme theme);
// Applies the active theme again. Reapplying it is stable, which isolated widget tests rely on.
void apply(QApplication &application);
QFont codeFont();
}
