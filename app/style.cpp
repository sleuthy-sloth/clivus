// SPDX-License-Identifier: GPL-3.0-or-later
#include "style.h"

#include <QApplication>
#include <QPalette>

namespace clivus::style {

void apply(QApplication& app)
{
    // Charcoal/slate panels, near-white primary text, one restrained accent. Kept as a palette plus
    // a few structural QSS rules rather than a full stylesheet: less to fight with on each platform.
    QPalette p = app.palette();
    p.setColor(QPalette::Window, QColor(0x2a, 0x2e, 0x33));        // charcoal/slate panels
    p.setColor(QPalette::WindowText, QColor(0xe6, 0xe6, 0xe6));
    p.setColor(QPalette::Base, QColor(0x21, 0x24, 0x28));          // list/editor wells
    p.setColor(QPalette::AlternateBase, QColor(0x26, 0x2a, 0x2f));
    p.setColor(QPalette::Text, QColor(0xe6, 0xe6, 0xe6));
    p.setColor(QPalette::Button, QColor(0x32, 0x37, 0x3d));
    p.setColor(QPalette::ButtonText, QColor(0xe6, 0xe6, 0xe6));
    p.setColor(QPalette::ToolTipBase, QColor(0x1c, 0x1f, 0x23));
    p.setColor(QPalette::ToolTipText, QColor(0xe6, 0xe6, 0xe6));
    p.setColor(QPalette::Highlight, QColor(0x3d, 0x6b, 0x8f));     // single restrained accent
    p.setColor(QPalette::HighlightedText, QColor(0xf2, 0xf2, 0xf2));
    p.setColor(QPalette::PlaceholderText, QColor(0x9a, 0x9f, 0xa6));
    p.setColor(QPalette::Disabled, QPalette::Text, QColor(0x77, 0x7d, 0x84));
    p.setColor(QPalette::Disabled, QPalette::WindowText, QColor(0x77, 0x7d, 0x84));
    p.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(0x77, 0x7d, 0x84));
    app.setPalette(p);

    // Structural rules only: separators, hover/selected rows and consistent spacing/size. The image
    // canvas paints itself black; no gradient, shadow or animation anywhere.
    app.setStyleSheet(QString("QWidget { font-size: %1pt; } "
                              "QMainWindow::separator { width: 2px; height: 2px; "
                              "background: #464b52; } "
                              "QToolBar { spacing: %2px; padding: %2px; border: none; } "
                              "QToolButton { padding: 4px 8px; border-radius: 3px; } "
                              "QToolButton:hover { background: #3a4047; } "
                              "QToolButton:focus { border: 1px solid #3d6b8f; } "
                              "QListWidget { border: 1px solid #464b52; padding: 2px; } "
                              "QListWidget::item { padding: 4px; border-radius: 3px; } "
                              "QListWidget::item:hover { background: #3a4047; } "
                              "QListWidget::item:selected { background: #3d6b8f; color: #f2f2f2; } "
                              "QStatusBar { border-top: 1px solid #464b52; } "
                              "QMenu { border: 1px solid #464b52; }")
                          .arg(9)
                          .arg(kSpacing));
}

}  // namespace clivus::style
