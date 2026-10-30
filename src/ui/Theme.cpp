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
    put("%ACCENT_FILL_HOVER%", accentFillHover());
    put("%ACCENT_FILL%",     accentFill());
    put("%TEXT_ON_ACCENT%",  textOnAccent());
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

/* An explicit toolbar rule, border included, so the stylesheet paints toolbars
   itself. Without it macOS draws a top-area toolbar natively, in its light
   look, and the review header showed as a white band. */
QToolBar {
    background-color: %WINDOW%;
    border: none;
}
QToolBar::separator {
    background-color: %BORDER%;
    width: 1px;
    margin: 6px 4px;
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
QListWidget::item:selected {
    background-color: %ACCENT_MUTED%;
    border-left: 3px solid %ACCENT%;
}

QToolButton {
    background-color: %CONTROL%;
    border: 1px solid %BORDER%;
    border-radius: 4px;
    padding: 5px 9px;
    min-width: 30px;
}
QToolButton:hover    { background-color: %CONTROL_HOVER%; }
QToolButton:pressed  { background-color: %CONTROL_PRESSED%; }
QToolButton:checked  {
    background-color: %ACCENT_FILL%;
    border-color: %ACCENT_FILL%;
    color: %TEXT_ON_ACCENT%;
}
QToolButton:checked:hover { background-color: %ACCENT_FILL_HOVER%; }
QToolButton:disabled { color: %TEXT_DISABLED%; }

/* Icon-only buttons in tool rows: flat until hovered, a filled accent tile
   when checked. */
QToolButton[atkRole="iconButton"] {
    background-color: transparent;
    border: 1px solid transparent;
    padding: 4px;
    min-width: 0;
}
QToolButton[atkRole="iconButton"]:hover   { background-color: %CONTROL_HOVER%; }
QToolButton[atkRole="iconButton"]:pressed { background-color: %CONTROL_PRESSED%; }
QToolButton[atkRole="iconButton"]:checked {
    background-color: %ACCENT_FILL%;
    border-color: %ACCENT_FILL%;
}
QToolButton[atkRole="iconButton"][popupMode="1"] { padding-right: 12px; }
/* Play / pause: a raised tile, so the main control reads as one at a glance. */
QToolButton#TransportPlayPauseButton {
    background-color: %CONTROL%;
    border: 1px solid %BORDER%;
    border-radius: 6px;
    padding: 4px 12px;
}
QToolButton#TransportPlayPauseButton:hover   { background-color: %CONTROL_HOVER%; }
QToolButton#TransportPlayPauseButton:pressed { background-color: %CONTROL_PRESSED%; }
/* Set In [ and Set Out ]: compact pills beside the review range. */
QToolButton[atkRole="markButton"] {
    border-radius: 11px;
    padding: 2px 9px;
    font-weight: 600;
    min-width: 0;
}
QToolButton[atkRole="markButton"][markSet="true"] { color: %ACCENT%; border-color: %ACCENT%; }
QToolButton[atkRole="markButton"]:disabled { border-color: %CONTROL%; }
QToolButton[atkRole="markButton"]:checked { background-color: %ACCENT_FILL%; color: %TEXT_ON_ACCENT%; border-color: %ACCENT_FILL%; }
QToolButton[atkRole="iconButton"]::menu-button {
    background: transparent;
    border: none;
    width: 12px;
}
QToolButton[atkRole="iconButton"]::menu-button:hover { background-color: %CONTROL_HOVER%; }

QPushButton {
    background-color: %CONTROL%;
    border: 1px solid %BORDER%;
    border-radius: 4px;
    padding: 5px 14px;
}
QPushButton:hover    { background-color: %CONTROL_HOVER%; }
QPushButton:pressed  { background-color: %CONTROL_PRESSED%; }
QPushButton:disabled { color: %TEXT_DISABLED%; }
QPushButton:default  { border-color: %ACCENT%; }
QPushButton[atkRole="iconButton"] { padding: 4px; }
QPushButton[atkRole="primary"] {
    background-color: %ACCENT_FILL%;
    border-color: %ACCENT_FILL%;
    color: %TEXT_ON_ACCENT%;
    font-weight: 600;
}
QPushButton[atkRole="primary"]:hover    { background-color: %ACCENT_FILL_HOVER%; }
QPushButton[atkRole="primary"]:disabled { background-color: %CONTROL%; color: %TEXT_DISABLED%; }

QLineEdit {
    background-color: %CONTROL%;
    border: 1px solid %BORDER%;
    border-radius: 4px;
    padding: 4px 6px;
    selection-background-color: %ACCENT_MUTED%;
}
QLineEdit:focus    { border-color: %ACCENT%; }
QLineEdit:disabled { color: %TEXT_DISABLED%; }

QSlider::groove:horizontal {
    height: 4px;
    background: %CONTROL%;
    border-radius: 2px;
}
QSlider::sub-page:horizontal {
    background: %ACCENT_FILL%;
    border-radius: 2px;
}
QSlider::handle:horizontal {
    background: %ACCENT%;
    width: 12px;
    margin: -4px 0;
    border-radius: 6px;
}
QSlider::handle:horizontal:hover { background: %TEXT%; }
QSlider::groove:vertical {
    width: 4px;
    background: %CONTROL%;
    border-radius: 2px;
}
QSlider::add-page:vertical {
    background: %ACCENT_FILL%;
    border-radius: 2px;
}
QSlider::handle:vertical {
    background: %ACCENT%;
    height: 12px;
    margin: 0 -4px;
    border-radius: 6px;
}
QSlider::handle:vertical:hover { background: %TEXT%; }

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
QScrollBar:horizontal {
    background: %WINDOW%;
    height: 10px;
    margin: 0;
}
QScrollBar::handle:horizontal {
    background: %CONTROL%;
    border-radius: 5px;
    min-width: 24px;
}
QScrollBar::handle:horizontal:hover { background: %CONTROL_HOVER%; }
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
