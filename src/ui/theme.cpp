#include "ui/theme.h"
#include <QApplication>
#include <QFontDatabase>
#include <QPalette>
#include <QStyleFactory>

namespace EditorTheme {
const Colors &colors()
{
    static const Colors palette{
        "#f1f5f9", "#ffffff", "#e2e8f0", "#0f172a", "#475569", "#94a3b8",
        "#1d4ed8", "#1e40af", "#1e3a8a", "#ffffff", "#475569", "#e2e8f0",
        "#f8fafc", "#e2e8f0", "#cbd5e1", "#ffffff", "#64748b", "#dbeafe", "#1e3a8a",
        "#2563eb", "#eff6ff", "#0369a1", "#6d28d9", "#166534", "#dcfce7",
        "#475569", "#1d4ed8", "#dcfce7", "#fee2e2"
    };
    return palette;
}

QFont codeFont() { return QFontDatabase::systemFont(QFontDatabase::FixedFont); }

void apply(QApplication &application)
{
    // Reapplication is deliberately stable (also useful for isolated widget tests).
    application.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    QFont font = application.font();
    if (font.pointSizeF() > 0 && font.pointSizeF() < 10)
        font.setPointSizeF(10);
    application.setFont(font);
    const auto &c = colors();
    QPalette palette;
    palette.setColor(QPalette::Window, c.window);
    palette.setColor(QPalette::WindowText, c.text);
    palette.setColor(QPalette::Base, c.surface);
    palette.setColor(QPalette::AlternateBase, c.window);
    palette.setColor(QPalette::Text, c.text);
    palette.setColor(QPalette::Button, c.surface);
    palette.setColor(QPalette::ButtonText, c.text);
    palette.setColor(QPalette::Highlight, c.accent);
    palette.setColor(QPalette::HighlightedText, c.onAccent);
    palette.setColor(QPalette::ToolTipBase, c.surface);
    palette.setColor(QPalette::ToolTipText, c.text);
    palette.setColor(QPalette::PlaceholderText, c.textMuted);
    palette.setColor(QPalette::Link, c.accent);
    for (auto role : {QPalette::Text, QPalette::WindowText, QPalette::ButtonText})
        palette.setColor(QPalette::Disabled, role, c.disabledText);
    palette.setColor(QPalette::Disabled, QPalette::Button, c.disabledSurface);
    application.setPalette(palette);
    QString style = QStringLiteral(R"(
QToolBar { background: %1; border: 0; border-bottom: 1px solid %2; spacing: 4px; padding: 4px; }
QDockWidget::title { background: %3; color: %4; padding: 4px 8px; font-weight: bold; }
QPushButton, QToolButton { background: %1; color: %4; border: 1px solid %2; border-radius: 4px; padding: 4px 8px; }
QPushButton:hover, QToolButton:hover { background: %3; border-color: %5; }
QPushButton:pressed, QToolButton:pressed, QToolButton:checked { background: %6; }
QPushButton:focus, QToolButton:focus { border: 2px solid %5; padding: 3px 7px; }
QPushButton[role="primary"], QToolButton[role="primary"] { background: %5; color: %7; border-color: %5; }
QPushButton[role="primary"]:hover, QToolButton[role="primary"]:hover { background: %8; }
QPushButton[role="primary"]:pressed, QToolButton[role="primary"]:pressed { background: %9; }
QPushButton[role="primary"]:focus, QToolButton[role="primary"]:focus { border-color: %4; }
QPushButton:disabled, QToolButton:disabled, QPushButton[role="primary"]:disabled, QToolButton[role="primary"]:disabled { background: %3; color: %10; border-color: %2; }
QLineEdit, QComboBox, QPlainTextEdit, QTextEdit, QAbstractItemView { background: %1; color: %4; border: 1px solid %2; border-radius: 3px; selection-background-color: %5; selection-color: %7; }
QLineEdit, QComboBox { padding: 4px; }
QLineEdit:focus, QComboBox:focus, QPlainTextEdit:focus, QTextEdit:focus, QAbstractItemView:focus { border-color: %5; }
QLineEdit:disabled, QComboBox:disabled { background: %3; color: %10; }
QHeaderView::section { background: %3; color: %4; border: 0; border-right: 1px solid %2; border-bottom: 1px solid %2; padding: 4px; }
QGroupBox { border: 1px solid %2; border-radius: 4px; margin-top: 10px; padding-top: 8px; }
QGroupBox::title { subcontrol-origin: margin; left: 8px; padding: 0 4px; color: %4; }
QMenu { background: %1; color: %4; border: 1px solid %2; }
QMenuBar { background: %1; color: %4; padding: 4px; }
QMenuBar::item:selected { background: %3; }
QMenuBar::item:pressed { background: %5; color: %7; }
QMenuBar::item:disabled { color: %10; }
QMenu::item:selected { background: %5; color: %7; }
QMenu::item:disabled { color: %10; }
QStatusBar { background: %3; color: %4; }
)");
    const QList<QColor> tokens{c.surface,c.border,c.surfaceAlt,c.text,c.accent,c.nodeHeader,
                              c.onAccent,c.accentHover,c.accentPressed,c.disabledText};
    for (int i = tokens.size(); i > 0; --i)
        style.replace(QStringLiteral("%") + QString::number(i), tokens[i - 1].name());
    application.setStyleSheet(style);
}
}
