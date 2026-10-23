// SPDX-License-Identifier: GPL-3.0-or-later
// MPR harness (first round): drives the real window offscreen and asserts the contract:
//   - the entry enables for a series whose every object reports geometry, and disables for one
//     that does not (both fixtures are committed: testdata/volume and a corpus object without
//     IPP/IOP);
//   - opening on the eligible series yields three planes, each holding a resliced frame;
//   - moving one plane's position changes that plane's index, leaves the others alone, and clamps
//     out-of-range requests instead of breaking.
//
// Run: QT_QPA_PLATFORM=offscreen ./app/clivus_test_mpr <volume-fixture> <geometry-less-dicom>

#include "main_window.h"

#include "canvas_widget.h"
#include "mpr_view.h"
#include "view_controls.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QListWidget>
#include <QTemporaryDir>
#include <QThread>
#include <cstdio>
#include <functional>

#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
            std::exit(1);                                                  \
        }                                                                  \
    } while (false)

namespace {

bool waitFor(const std::function<bool()>& condition, int timeoutMs)
{
    QElapsedTimer clock;
    clock.start();
    while (!condition()) {
        if (clock.elapsed() > timeoutMs) return false;
        QApplication::processEvents(QEventLoop::AllEvents, 50);
        QThread::msleep(20);
    }
    return true;
}

QListWidget* seriesListIn(const MainWindow& w)
{
    const QList<QListWidget*> lists = w.findChildren<QListWidget*>();
    return lists.isEmpty() ? nullptr : lists.first();
}

}  // namespace

int main(int argc, char** argv)
{
    QCoreApplication::setOrganizationName(QStringLiteral("sleuthy-sloth"));
    QCoreApplication::setApplicationName(QStringLiteral("clivus-mpr-test"));
    QApplication app(argc, argv);
    QSettings().clear();
    CHECK(argc > 2);

    // --- Ineligible: a geometry-less object (no IPP/IOP). The entry must say no.
    {
        QTemporaryDir dir;
        CHECK(dir.isValid());
        CHECK(QFile::copy(argv[2], dir.path() + '/' + QFileInfo(argv[2]).fileName()));

        MainWindow w;
        w.show();
        w.openFolder(dir.path());
        QListWidget* list = seriesListIn(w);
        CHECK(list != nullptr);
        CHECK(waitFor([&] { return list->count() > 0; }, 30000));
        list->setCurrentRow(0);
        QApplication::processEvents();
        CHECK(!w.mprAvailable());   // no geometry, no promise of an MPR window
    }

    // --- Eligible: the synthetic 4-slice volume fixture (16x16, geometry on every slice).
    QTemporaryDir volDir;
    CHECK(volDir.isValid());
    const QDir volumeSource(argv[1]);
    for (const QString& f : volumeSource.entryList(QDir::Files | QDir::NoDotAndDotDot))
        CHECK(QFile::copy(volumeSource.filePath(f), volDir.path() + '/' + f));

    MainWindow w;
    w.show();
    w.openFolder(volDir.path());
    QListWidget* list = seriesListIn(w);
    CHECK(list != nullptr);
    CHECK(waitFor([&] { return list->count() > 0; }, 30000));
    list->setCurrentRow(0);
    QApplication::processEvents();
    CHECK(w.mprAvailable());

    w.openMprForTest();
    MprView* view = w.findChild<MprView*>();
    CHECK(view != nullptr);
    CHECK(view->planesShown() == 3);
    const QList<CanvasWidget*> canvases = view->findChildren<CanvasWidget*>();
    CHECK(canvases.size() == 3);

    const bool allValid = waitFor(
        [&] {
            for (CanvasWidget* c : canvases)
                if (!c->hasValidFrame()) return false;
            return true;
        },
        60000);
    if (!allValid) {
        for (CanvasWidget* c : canvases) std::printf("DBG canvas valid=%d\n", int(c->hasValidFrame()));
        for (QLabel* l : view->findChildren<QLabel*>())
            std::printf("DBG label: %s\n", l->text().left(100).toStdString().c_str());
    }
    CHECK(allValid);

    // All planes start at position 0; moving the coronal keeps the other two where they were.
    CHECK(view->planeIndex(0) == 0 && view->planeIndex(1) == 0 && view->planeIndex(2) == 0);
    view->setPositionForTest(1, 2);
    QApplication::processEvents();
    CHECK(view->planeIndex(1) == 2);
    CHECK(view->planeIndex(0) == 0);
    CHECK(view->planeIndex(2) == 0);

    // Out-of-range clamps to the plane's own extent (the fixture is 16 rows: coronal max = 15),
    // and below range clamps to 0.
    view->setPositionForTest(1, 100000);
    QApplication::processEvents();
    CHECK(view->planeIndex(1) == 15);
    view->setPositionForTest(1, -19);
    QApplication::processEvents();
    CHECK(view->planeIndex(1) == 0);

    view->close();
    QSettings().clear();
    std::printf("mpr: all checks passed (eligibility both ways, three planes, navigation)\n");
    return 0;
}
