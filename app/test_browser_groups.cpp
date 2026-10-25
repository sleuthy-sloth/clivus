// SPDX-License-Identifier: GPL-3.0-or-later
// The series browser's groups: folding and unfolding a patient or a study (v0.4.1, radiology report:
// "folders and subfolders load into groups, but the groups cannot be collapsed").
//
// The rules this file pins, in the order the report asked about them:
//   - a group header folds and unfolds, by mouse click and by Left/Right on the keyboard;
//   - folding moves no series, starts no decode and never resets the slice being read;
//   - a folded group's series are hidden, its own header is not (and says how much is inside);
//   - a query reveals what it matches even inside a folded group, and clearing it folds them back;
//   - a mid-scan batch (which rebuilds every row) keeps both the fold state and the selection.
//
// Run: QT_QPA_PLATFORM=offscreen ./app/clivus_test_browser_groups <dicom-folder>

#include "browser_model.h"
#include "main_window.h"

#include <QApplication>
#include <QDir>
#include <QEventLoop>
#include <QKeyEvent>
#include <QLineEdit>
#include <QListWidget>
#include <QMouseEvent>
#include <QTemporaryDir>
#include <QTimer>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <vector>

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);      \
            std::exit(1);                                                    \
        }                                                                    \
    } while (false)

namespace {

void spin(int ms)
{
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
}

QListWidget* browser(MainWindow& w)
{
    return w.findChildren<QListWidget*>().value(0, nullptr);
}

int visibleSeries(const QListWidget* list)
{
    int n = 0;
    for (int i = 0; i < list->count(); ++i) {
        const QListWidgetItem* item = list->item(i);
        if (item->data(Qt::UserRole).toInt() < 0) continue;
        if (!item->isHidden()) ++n;
    }
    return n;
}

int visibleHeaders(const QListWidget* list)
{
    int n = 0;
    for (int i = 0; i < list->count(); ++i) {
        const QListWidgetItem* item = list->item(i);
        if (item->data(clivus::ui::kHeaderRole).toInt() == 0 || item->isHidden()) continue;
        ++n;
    }
    return n;
}

int firstHeaderRow(const QListWidget* list, clivus::ui::HeaderKind kind)
{
    for (int i = 0; i < list->count(); ++i)
        if (list->item(i)->data(clivus::ui::kHeaderRole).toInt() == int(kind)) return i;
    return -1;
}

// Series rows that sit under the group whose header is at `headerRow`, up to the next header of the
// same or a higher level. This is what a fold is supposed to hide, so the test states it explicitly.
std::vector<int> seriesRowsUnder(const QListWidget* list, int headerRow)
{
    std::vector<int> rows;
    const int kind = list->item(headerRow)->data(clivus::ui::kHeaderRole).toInt();
    for (int i = headerRow + 1; i < list->count(); ++i) {
        const QListWidgetItem* item = list->item(i);
        const int k = item->data(clivus::ui::kHeaderRole).toInt();
        if (k != 0 && k <= kind) break;
        if (item->data(Qt::UserRole).toInt() >= 0) rows.push_back(i);
    }
    return rows;
}

bool allHidden(const QListWidget* list, const std::vector<int>& rows)
{
    for (int r : rows)
        if (!list->item(r)->isHidden()) return false;
    return !rows.empty();
}

// A series that decodes (borrowed object from the corpus) so opening one is a real load, and whose
// identity is a chosen patient/study, so the grouping can be driven from the test.
clivus::SeriesInfo makeSeries(const QString& corpus, const std::string& patientId,
                              const std::string& patientName, const std::string& studyUid,
                              const std::string& description, int instance)
{
    clivus::SeriesInfo s;
    s.patientId = patientId;
    s.patientName = patientName;
    s.studyUid = studyUid;
    s.seriesUid = studyUid + ".1." + std::to_string(instance);
    s.seriesDescription = description;
    s.modality = "CT";
    s.width = 128;
    s.height = 128;
    s.files.push_back(QDir(corpus).filePath(QStringLiteral("CT_small.dcm")).toStdString());
    s.frameCounts.push_back(1);
    return s;
}

QLineEdit* searchBox(MainWindow& w)
{
    const QList<QLineEdit*> edits = w.findChildren<QLineEdit*>();
    for (QLineEdit* e : edits)
        if (e->placeholderText().contains(QStringLiteral("Search series"))) return e;
    return nullptr;
}

}  // namespace

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    qputenv("CLIVUS_CACHE_DIR",
            (QDir::tempPath() + QStringLiteral("/clivus-test-cache-%1")
                                   .arg(QCoreApplication::applicationPid()))
                .toUtf8());
    if (argc < 2) {
        std::printf("usage: clivus_test_browser_groups <dicom-folder>\n");
        return 2;
    }
    const QString corpus = QString::fromLocal8Bit(argv[1]);

    MainWindow w;
    w.resize(1100, 700);
    w.show();   // the fold gesture is a mouse click, so the rows need real geometry
    w.openFolder(corpus);

    QListWidget* list = browser(w);
    CHECK(list != nullptr);
    for (int i = 0; i < 200 && list->count() == 0; ++i) spin(50);
    CHECK(list->count() > 0);

    const int totalSeries = visibleSeries(list);
    const int totalHeaders = visibleHeaders(list);
    CHECK(totalSeries > 0);
    CHECK(totalHeaders > 0);

    // 0. Everything starts expanded: a 0.4.0 user sees no change until they fold something.
    CHECK(visibleSeries(list) == totalSeries);

    // 1. A real click on a patient header folds the group. The click is the gesture, so it goes through
    // the viewport exactly as Qt delivers it, not through a private setter.
    const int patientRow = firstHeaderRow(list, clivus::ui::HeaderKind::Patient);
    CHECK(patientRow >= 0);
    const std::vector<int> underPatient = seriesRowsUnder(list, patientRow);
    CHECK(!underPatient.empty());
    const QString groupKey = w.groupKeyAtRowForTest(patientRow);
    CHECK(!groupKey.isEmpty());
    const QString uidBefore = QString::fromStdString(w.seriesUidForTest());
    const int sliceBefore = w.currentSliceForTest();
    const int caretBefore = list->currentRow();

    auto clickHeader = [&](int row) {
        const QRect rect = list->visualItemRect(list->item(row));
        CHECK(!rect.isEmpty());
        QMouseEvent press(QEvent::MouseButtonPress, rect.center(), list->viewport()->mapToGlobal(rect.center()),
                          Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(list->viewport(), &press);
    };

    clickHeader(patientRow);
    CHECK(w.rowIsGroupHeaderForTest(patientRow));
    CHECK(allHidden(list, underPatient));            // the series went away
    CHECK(!list->item(patientRow)->isHidden());      // the patient header did not
    CHECK(list->item(patientRow)->text().startsWith(QString::fromUtf8("\u25B8")));   // folded chevron
    // No series was opened, no slice was reset: folding is navigation, not a decode.
    CHECK(QString::fromStdString(w.seriesUidForTest()) == uidBefore);
    CHECK(w.currentSliceForTest() == sliceBefore);
    CHECK(list->currentRow() == caretBefore);

    // 2. Clicking again unfolds it and every series comes back.
    clickHeader(patientRow);
    CHECK(visibleSeries(list) == totalSeries);
    CHECK(list->item(patientRow)->text().startsWith(QString::fromUtf8("\u25BE")));

    // 3. Keyboard: Left folds, Right unfolds, on the header the caret is on.
    list->setCurrentRow(patientRow);
    QKeyEvent left(QEvent::KeyPress, Qt::Key_Left, Qt::NoModifier);
    QApplication::sendEvent(list, &left);
    CHECK(allHidden(list, underPatient));
    QKeyEvent right(QEvent::KeyPress, Qt::Key_Right, Qt::NoModifier);
    QApplication::sendEvent(list, &right);
    CHECK(visibleSeries(list) == totalSeries);

    // 4. A row that is not a header is not a fold target.
    const int seriesRow = *std::min_element(underPatient.begin(), underPatient.end());
    CHECK(!w.toggleGroupAtRowForTest(seriesRow));
    CHECK(!w.toggleGroupAtRowForTest(-1));
    CHECK(!w.toggleGroupAtRowForTest(list->count() + 5));
    CHECK(visibleSeries(list) == totalSeries);

    // 5. Patient and study levels both fold, with two studies under one patient and two series under
    // one of them — the shape a real folder has. This window starts from an empty folder so the batch
    // below is the whole list (applyScanBatch refuses a batch that adds nothing).
    QTemporaryDir scratch;
    CHECK(scratch.isValid());
    MainWindow wb;
    wb.resize(1100, 700);
    wb.show();
    wb.openFolder(scratch.path());
    spin(300);
    QListWidget* lb = browser(wb);
    CHECK(lb != nullptr);
    {
        std::vector<clivus::SeriesInfo> batch;
        batch.push_back(makeSeries(corpus, "PID-ONE", "ONE^PATIENT", "1.2.3.1", "STUDY A SERIES 1", 1));
        batch.push_back(makeSeries(corpus, "PID-ONE", "ONE^PATIENT", "1.2.3.1", "STUDY A SERIES 2", 2));
        batch.push_back(makeSeries(corpus, "PID-ONE", "ONE^PATIENT", "1.2.3.2", "STUDY B SERIES 1", 3));
        batch.push_back(makeSeries(corpus, "PID-TWO", "TWO^PATIENT", "1.2.3.9", "OTHER PATIENT", 4));
        wb.applyScanBatchForTest(wb.folderEpochForTest(), batch);
        CHECK(visibleSeries(lb) == int(batch.size()));

        const int p = firstHeaderRow(lb, clivus::ui::HeaderKind::Patient);
        const int studyA = p + 1;
        CHECK(lb->item(studyA)->data(clivus::ui::kHeaderRole).toInt() ==
              int(clivus::ui::HeaderKind::Study));
        const std::vector<int> underA = seriesRowsUnder(lb, studyA);
        CHECK(underA.size() == 2);
        const int studyB = studyA + 3;
        const std::vector<int> underB = seriesRowsUnder(lb, studyB);
        CHECK(underB.size() == 1);

        // Folding study A takes its two series away and leaves study B's one alone.
        CHECK(wb.toggleGroupAtRowForTest(studyA));
        CHECK(allHidden(lb, underA));
        CHECK(!lb->item(underB.front())->isHidden());
        CHECK(!lb->item(studyA)->isHidden());

        // Folding the patient takes both studies and all three series away, and keeps its own header.
        CHECK(wb.toggleGroupAtRowForTest(p));
        CHECK(allHidden(lb, underB));
        CHECK(lb->item(studyB)->isHidden());
        CHECK(!lb->item(p)->isHidden());
        CHECK(visibleSeries(lb) == 1);   // only patient two's series is still on screen

        // Unfolding the patient brings study B back, and study A is still folded: nested state is kept
        // per group, not squashed into one flag.
        CHECK(wb.toggleGroupAtRowForTest(p));
        CHECK(!lb->item(studyB)->isHidden());
        CHECK(allHidden(lb, underA));
        CHECK(!lb->item(underB.front())->isHidden());

        // 6. A query reveals what it matches inside a folded group, and clearing folds it back.
        QLineEdit* search = searchBox(wb);
        CHECK(search != nullptr);
        search->setText(QStringLiteral("STUDY A SERIES 1"));
        CHECK(visibleSeries(lb) == 1);
        CHECK(!lb->item(underA.front())->isHidden());   // the match, inside the folded study
        search->clear();
        CHECK(allHidden(lb, underA));
        // Study B's series and patient two's are on screen; study A's two stay folded away.
        CHECK(visibleSeries(lb) == 2);

        // 7. A rebuild (the mid-scan batch path) keeps the fold state and the selection.
        lb->setCurrentRow(underB.front());
        const QString uidOpen = QString::fromStdString(wb.seriesUidForTest());
        CHECK(!uidOpen.isEmpty());

        std::vector<clivus::SeriesInfo> grown = batch;
        grown.push_back(makeSeries(corpus, "PID-TWO", "TWO^PATIENT", "1.2.3.9", "OTHER PATIENT 2", 5));
        wb.applyScanBatchForTest(wb.folderEpochForTest(), grown);
        CHECK(visibleSeries(lb) == 3);   // study B + patient two's two; study A is still folded
        CHECK(QString::fromStdString(wb.seriesUidForTest()) == uidOpen);   // selection did not move
        const int p2 = firstHeaderRow(lb, clivus::ui::HeaderKind::Patient);
        const int studyA2 = p2 + 1;
        CHECK(allHidden(lb, seriesRowsUnder(lb, studyA2)));

        // 8. Folding the group that holds the open series moves the caret to the header and keeps that
        // series open: the caret lands on a header, never on another series (which would decode it).
        CHECK(wb.toggleGroupAtRowForTest(p2));
        CHECK(QString::fromStdString(wb.seriesUidForTest()) == uidOpen);
        CHECK(wb.rowIsGroupHeaderForTest(lb->currentRow()));
    }

    // 9. An empty folder: no groups, nothing to fold, and the browser stays usable.
    {
        QTemporaryDir empty;
        CHECK(empty.isValid());
        MainWindow w2;
        w2.openFolder(empty.path());
        spin(300);
        QListWidget* list2 = browser(w2);
        CHECK(list2 != nullptr);
        CHECK(!w2.toggleGroupAtRowForTest(0));   // no rows at all
        CHECK(visibleSeries(list2) == 0);
    }

    std::printf("browser groups: all checks passed\n");
    return 0;
}