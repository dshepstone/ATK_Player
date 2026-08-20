#include "ui/Theme.h"

namespace atk::ui::theme {
namespace {

/// Substitutes %NAME% placeholders in the stylesheet template with the palette
/// colours, so a colour is never written twice.
QString expand(QString sheet)
{
    const auto put = [&sheet](const char* token, const QColor& color) {
        sheet.replace(QLatin1StringView(token), color.name(QColor::HexRgb));
    };

    put("%WINDOW%",          windowBackground());
    put("%PANEL%",           panelBackground());
    put("%BORDER%",          panelBorder());
    put("%CONTROL%",         controlBackground());
    put("%CONTROL_HOVER%",   controlHover());
    put("%CONTROL_PRESSED%", controlPressed());
    put("%TEXT_SECONDARY%",  textSecondary());
    put("%TEXT_DISABLED%",   textDisabled());
    put("%ACCENT_MUTED%",    accentMuted());
    put("%ACCENT%",          accent());
    put("%TEXT%",            textPrimary());

    return sheet;
}

} // namespace

QString styleSheet()
{
    static const QString sheet = expand(QStringLiteral(R"CSS(
QWidget {
    background-color: %WINDOW%;
    color: %TEXT%;
    font-size: 12px;
}

QMainWindow::separator {
    background-color: %BORDER%;
    width: 1px;
    height: 1px;
}

QMenuBar {
    background-color: %PANEL%;
    border-bottom: 1px solid %BORDER%;
    padding: 2px 4px;
}
QMenuBar::item {
    padding: 4px 10px;
    background: transparent;
    border-radius: 3px;
}
QMenuBar::item:selected { background-color: %CONTROL_HOVER%; }
QMenuBar::item:pressed  { background-color: %ACCENT_MUTED%; }

QMenu {
    background-color: %PANEL%;
    border: 1px solid %BORDER%;
    padding: 4px;
}
QMenu::item {
    padding: 5px 28px 5px 22px;
    border-radius: 3px;
}
QMenu::item:selected { background-color: %ACCENT_MUTED%; }
QMenu::item:disabled { color: %TEXT_DISABLED%; }
QMenu::separator {
    height: 1px;
    background: %BORDER%;
    margin: 4px 8px;
}

QDockWidget {
    titlebar-close-icon: none;
    titlebar-normal-icon: none;
}
QDockWidget::title {
    background-color: %PANEL%;
    border-bottom: 1px solid %BORDER%;
    padding: 6px 8px;
    font-weight: 600;
    letter-spacing: 1px;
}

QListWidget {
    background-color: %PANEL%;
    border: none;
    outline: none;
    padding: 2px;
}
QListWidget::item {
    padding: 5px 8px;
    border-radius: 3px;
}
QListWidget::item:hover    { background-color: %CONTROL_HOVER%; }
QListWidget::item:selected { background-color: %ACCENT_MUTED%; }

QToolButton {
    background-color: %CONTROL%;
    border: 1px solid %BORDER%;
    border-radius: 4px;
    padding: 5px 9px;
    min-width: 30px;
}
QToolButton:hover    { background-color: %CONTROL_HOVER%; }
QToolButton:pressed  { background-color: %CONTROL_PRESSED%; }
QToolButton:checked  { background-color: %ACCENT_MUTED%; border-color: %ACCENT%; }
QToolButton:disabled { color: %TEXT_DISABLED%; }

QLabel[atkRole="statusValue"] {
    color: %TEXT%;
    font-family: "Consolas", "DejaVu Sans Mono", monospace;
}
QLabel[atkRole="statusCaption"] {
    color: %TEXT_SECONDARY%;
    font-size: 11px;
    letter-spacing: 1px;
}
QLabel[atkRole="panelHeading"] {
    color: %TEXT_SECONDARY%;
    font-size: 11px;
    font-weight: 600;
    letter-spacing: 2px;
    padding: 8px 10px 6px 10px;
}
QLabel[atkRole="placeholder"] {
    color: %TEXT_DISABLED%;
    padding: 10px;
}

QStatusBar {
    background-color: %PANEL%;
    border-top: 1px solid %BORDER%;
}
QStatusBar::item { border: none; }

QScrollBar:vertical {
    background: %WINDOW%;
    width: 10px;
    margin: 0;
}
QScrollBar::handle:vertical {
    background: %CONTROL%;
    border-radius: 5px;
    min-height: 24px;
}
QScrollBar::handle:vertical:hover { background: %CONTROL_HOVER%; }
QScrollBar::add-line, QScrollBar::sub-line { height: 0; width: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: none; }

QSplitter::handle { background-color: %BORDER%; }
QSplitter::handle:horizontal { width: 1px; }
QSplitter::handle:vertical   { height: 1px; }

QToolTip {
    background-color: %PANEL%;
    color: %TEXT%;
    border: 1px solid %BORDER%;
    padding: 4px 6px;
}

)CSS"));
    return sheet;
}

} // namespace atk::ui::theme
