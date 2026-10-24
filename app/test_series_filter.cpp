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

// Rows are grouped now: a patient header and a study header sit above their series, and neither is a
// series the user can open. Every count here is about series rows, which are the ones that carry a
// series index; the placeholder carries -1 and headers carry -1 as well.
int visibleCount(const QListWidget* list)
{
    int n = 0;
    for (int i = 0; i < list->count(); ++i) {
        const QListWidgetItem* item = list->item(i);
        if (item->data(Qt::UserRole).toInt() < 0) continue;
        if (!item->isHidden()) ++n;
    }
    return n;
}

int seriesRowCount(const QListWidget* list)
{
    int n = 0;
    for (int i = 0; i < list->count(); ++i)
        if (list->item(i)->data(Qt::UserRole).toInt() >= 0 && !list->item(i)->isHidden()) ++n;
    return n;
}

int headerCount(const QListWidget* list)
{
    int n = 0;
    for (int i = 0; i < list->count(); ++i) {
        const int kind = list->item(i)->data(Qt::UserRole + 2).toInt();
        if (kind != 0 && !list->item(i)->isHidden()) ++n;
    }
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
    const int total = visibleCount(list);   // series rows, not headers
    CHECK(headerCount(list) > 0);           // the grouping is on screen, not just in the model
    CHECK(total > 0);

    QLineEdit* search = findSearchBox(w);
    CHECK(search != nullptr);
    CHECK(search->placeholderText().startsWith(QString::fromUtf8("Search series")));

    // 1. A modality substring from the first *series* row's own text filters down but keeps that row.
    // item(0) is a patient header now, so the series row has to be found rather than assumed.
    int firstSeriesRow = -1;
    for (int i = 0; i < list->count(); ++i)
        if (list->item(i)->data(Qt::UserRole).toInt() >= 0) { firstSeriesRow = i; break; }
    CHECK(firstSeriesRow >= 0);
    const QString firstText = list->item(firstSeriesRow)->text();
    search->setText(firstText.section('\n', 1, 1).split(' ').first());   // a modality token
    CHECK(visibleCount(list) >= 1);
    CHECK(visibleCount(list) < total || firstText.section('\n', 1, 1).split(' ').first().isEmpty());
    CHECK(!list->item(0)->isHidden() || !list->item(0)->text().contains(search->text(), Qt::CaseInsensitive));

    // 2. Nonsense query: exactly one placeholder row, not selectable.
    search->setText("zzz_no_such_series_zzz");
    CHECK(visibleCount(list) == 0);   // no series survives the query
    CHECK(headerCount(list) == 0);    // and no group header stays behind with nothing under it
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

    // 6. A mid-scan batch repopulates the list and keeps the selection rule intact. The corpus is
    // smaller than the progress interval, so this drives the batch path directly — the same entry
    // point the worker's queued signal calls.
    {
        std::vector<clivus::SeriesInfo> batch;
        for (int i = 0; i < total + 4; ++i) {
            clivus::SeriesInfo s;
            s.seriesUid = "1.2.826.0.1.3680043.9.B" + std::to_string(i);
            s.studyUid = "1.2.826.0.1.3680043.9.S" + std::to_string(i);
            s.seriesDescription = "BATCH SERIES " + std::to_string(i);
            s.modality = "CT";
            s.width = 64;
            s.height = 64;
            // A real object, so opening one of these rows actually decodes something: a series with no
            // files is deliberately refused by showSeries().
            s.files.push_back((QDir(argv[1]).filePath(QStringLiteral("CT_small.dcm"))).toStdString());
            s.frameCounts.push_back(1);
            batch.push_back(s);
        }
        search->clear();
        const quint64 epoch = w.folderEpochForTest();
        w.applyScanBatchForTest(epoch, batch);
        CHECK(seriesRowCount(list) == int(batch.size()));
        CHECK(visibleCount(list) == int(batch.size()));
        CHECK(headerCount(list) > 0);   // the batch arrives grouped, not flat
        // The first row is the patient header, so the selection lands on the first series below it.
        CHECK(list->currentRow() > 0);
        CHECK(list->currentItem()->data(Qt::UserRole).toInt() >= 0);

        // Activating a group header opens its first series: a click on "Patient A" means "show me this".
        list->setCurrentRow(0);
        CHECK(!w.seriesUidForTest().empty());

        // A batch from a folder the user has already left must change nothing.
        std::vector<clivus::SeriesInfo> stale = batch;
        for (auto& s : stale) s.seriesDescription = "STALE";
        w.applyScanBatchForTest(epoch + 7, stale);
        CHECK(seriesRowCount(list) == int(batch.size()));
        CHECK(!list->item(0)->text().contains(QStringLiteral("STALE")));

        // Filtering still works on rows that arrived as a batch.
        search->setText(QStringLiteral("BATCH SERIES 3"));
        CHECK(visibleCount(list) >= 1);
        search->clear();
    }

    std::printf("series filter: all checks passed (%d series)\n", total);
    return 0;
}
