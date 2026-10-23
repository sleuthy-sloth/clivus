// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QDialog>

class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QTextBrowser;

// In-app manual (UI/UX roadmap 5.2): two panes — navigation on the left, article on the right,
// a search box above the navigation. The manual's one authoritative source is the chapter table
// in help_window.cpp; it must describe only behaviour that exists in the code, and works fully
// offline.
class HelpWindow : public QDialog {
    Q_OBJECT
public:
    explicit HelpWindow(QWidget* parent = nullptr);

private:
    void showChapter(int row);
    void applySearch(const QString& query);

    QLineEdit* m_search = nullptr;
    QListWidget* m_nav = nullptr;
    QListWidgetItem* m_noMatches = nullptr;
    QTextBrowser* m_text = nullptr;
};
