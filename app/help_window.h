// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QDialog>

class QListWidget;
class QTextBrowser;

// In-app manual skeleton (UI/UX roadmap 5.2): two panes — navigation list on the left, article
// pane on the right. Chapters are placeholders for now; the real text lands with Phases 2–4 and
// must describe only behaviour that exists in the code.
class HelpWindow : public QDialog {
    Q_OBJECT
public:
    explicit HelpWindow(QWidget* parent = nullptr);

private:
    void showChapter(int row);

    QListWidget* m_nav = nullptr;
    QTextBrowser* m_text = nullptr;
};
