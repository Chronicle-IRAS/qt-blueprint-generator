#pragma once
#include <QColor>
#include <QFont>
class QApplication;

namespace EditorTheme {
inline constexpr int SpaceSmall = 4;
inline constexpr int SpaceMedium = 8;
inline constexpr int SpaceLarge = 16;
struct Colors {
    QColor window, surface, surfaceAlt, text, textMuted, border;
    QColor accent, accentHover, accentPressed, onAccent, disabledText, disabledSurface;
    QColor canvas, gridMinor, gridMajor, nodeBody, nodeBorder, nodeHeader, nodeHeaderText;
    QColor nodeHover, nodeSelectedBody, portInput, portOutput, portCompatible, portCompatibleFill;
    QColor edge, selection, diffAdded, diffRemoved;
};
const Colors &colors();
void apply(QApplication &application);
QFont codeFont();
}
