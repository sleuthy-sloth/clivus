// SPDX-License-Identifier: GPL-3.0-or-later
// Roadmap 3.2 offscreen harness: series search/filter. A real folder is opened through the public
// API (the scan runs its normal background path), then the search box is driven like a user would:
// filter, assert visibility/selection/placeholder, clear, assert full list back.
//
// Run: QT_QPA_PLATFORM=offscreen ./app/clivus_test_series_filter <dicom-folder>

#include "main_window.h"

#include <QAction>
#include <QApplication>
#include <QDir>
#include <QLineEdit>
#include <QListWidget>
#include <QTimer>
#include <cstdio>

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);      \
            std::exit(1);                                                    \
        }                                                                    \
    } while (false)

namespace {

int visibleCount(const QListWidget* list)
{
    int n = 0;
    for (int i = 0; i < list->count(); ++i)
        if (!list->item(i)->isHidden()) ++n;
    return n;
}

QLineEdit* findSearchBox(MainWindow& w)
{
    const QList<QLineEdit*> edits = w.findChildren<QLineEdit*>();
    for (QLineEdit* e : edits)
        if (e->placeholderText().contains("Search series")) return e;
    return nullptr;
}

}  // namespace

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    // Tests must never touch the user's real cache: point the index at a throwaway directory.
    qputenv("CLIVUS_CACHE_DIR",
            (QDir::tempPath() + QStringLiteral("/clivus-test-cache-%1")
                                   .arg(QCoreApplication::applicationPid()))
                .toUtf8());

    if (argc < 2) {
        std::printf("usage: clivus_test_series_filter <dicom-folder>\n");
        return 2;
    }

    MainWindow w;
    w.openFolder(QString::fromLocal8Bit(argv[1]));

    // The scan is asynchronous; the list fills when scanFinished() lands. Wait up to 10 s.
    QListWidget* list = w.findChildren<QListWidget*>().value(0, nullptr);
    CHECK(list != nullptr);
    for (int i = 0; i < 200 && list->count() == 0; ++i) {
        QEventLoop loop;
        QTimer::singleShot(50, &loop, &QEventLoop::quit);
        loop.exec();
    }
    const int total = list->count();
    CHECK(total > 0);

    QLineEdit* search = findSearchBox(w);
    CHECK(search != nullptr);
    CHECK(search->placeholderText().startsWith(QString::fromUtf8("Search series")));

    // 1. A modality substring from the first row's own text filters down but keeps that row.
    const QString firstText = list->item(0)->text();
    search->setText(firstText.section('\n', 1, 1).split(' ').first());   // a modality token
    CHECK(visibleCount(list) >= 1);
    CHECK(visibleCount(list) < total || firstText.section('\n', 1, 1).split(' ').first().isEmpty());
    CHECK(!list->item(0)->isHidden() || !list->item(0)->text().contains(search->text(), Qt::CaseInsensitive));

    // 2. Nonsense query: exactly one placeholder row, not selectable.
    search->setText("zzz_no_such_series_zzz");
    CHECK(visibleCount(list) == 1);
    CHECK(list->item(0)->text() == QString("(no matching series)"));
    CHECK(list->item(0)->flags() == Qt::NoItemFlags);

    // 3. Clearing restores everything and clears the placeholder.
    search->clear();   // the placeholder text auto-clears: empty query = unfiltered list
    CHECK(search->text().isEmpty());
    CHECK(visibleCount(list) == total);
    for (int i = 0; i < list->count(); ++i) CHECK(!list->item(i)->isHidden());

    // 4. Selection stays visible when it matches: select a row, filter to its text, it stays.
    list->setCurrentRow(std::min(1, total - 1));
    QListWidgetItem* selected = list->currentItem();
    search->setText(selected->text().section('\n', 0, 0).left(6));   // description prefix
    CHECK(list->currentItem() == selected);
    CHECK(!selected->isHidden());

    // 5. Ctrl+F focuses the search box (window-level QAction, roadmap 3.2 keyboard operability).
    bool found = false;
    const QList<QAction*> actions = w.actions();
    for (QAction* a : actions)
        if (a->shortcut() == QKeySequence::Find) { found = true; break; }
    CHECK(found);

    std::printf("series filter: all checks passed (%d series)\n", total);
    return 0;
}
