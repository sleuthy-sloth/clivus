// SPDX-License-Identifier: GPL-3.0-or-later
// Measurement-tool harness (roadmap: measurement tools): the offscreen UI drives the REAL canvas
// through synthetic mouse events and asserts the numbers:
//   (a) distance in mm from a known PixelSpacing (MR_small: 0.3125 mm/px both axes);
//   (b) an unknown spacing reports the unknown state — never an invented mm;
//   (c) an L-shaped angle through the widget mapping reads ~90 degrees;
//   (d) with the measure tool OFF, a left drag is still window/level (no gesture regression);
//   (e) switching series clears the slice's measurements.
//
// Run: QT_QPA_PLATFORM=offscreen ./app/clivus_test_measurements <corpus-folder>

#include "main_window.h"

#include "canvas_widget.h"

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QListWidget>
#include <QMouseEvent>
#include <QTemporaryDir>
#include <QThread>
#include <cmath>
#include <cstdio>
#include <functional>

#define CHECK(cond)                                                         \
    do {                                                                    \
        if (!(cond)) {                                                      \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);     \
            std::exit(1);                                                   \
        }                                                                   \
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

void click(CanvasWidget* canvas, QPoint at, const Qt::KeyboardModifiers mods,
           const QEvent::Type type = QEvent::MouseButtonPress)
{
    QMouseEvent e(type, QPointF(at), canvas->mapToGlobal(at), Qt::LeftButton,
                  type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton, mods);
    QApplication::sendEvent(canvas, &e);
    QApplication::processEvents();
}

void drag(CanvasWidget* canvas, QPoint from, QPoint to)
{
    click(canvas, from, Qt::NoModifier);                    // press
    QMouseEvent move(QEvent::MouseMove, QPointF(to), canvas->mapToGlobal(to),
                     Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(canvas, &move);
    QApplication::processEvents();
    click(canvas, to, Qt::NoModifier, QEvent::MouseButtonRelease);   // release
}

void anglePoints(CanvasWidget* canvas, QPoint a, QPoint vertex, QPoint c)
{
    click(canvas, a, Qt::ShiftModifier);
    click(canvas, a, Qt::NoModifier, QEvent::MouseButtonRelease);
    click(canvas, vertex, Qt::ShiftModifier);
    click(canvas, vertex, Qt::NoModifier, QEvent::MouseButtonRelease);
    click(canvas, c, Qt::ShiftModifier);
    click(canvas, c, Qt::NoModifier, QEvent::MouseButtonRelease);
}

}  // namespace

int main(int argc, char** argv)
{
    QCoreApplication::setOrganizationName(QStringLiteral("sleuthy-sloth"));
    QCoreApplication::setApplicationName(QStringLiteral("clivus-measure-test"));
    QApplication app(argc, argv);
    // Tests must never touch the user's real cache: point the index at a throwaway directory.
    qputenv("CLIVUS_CACHE_DIR",
            (QDir::tempPath() + QStringLiteral("/clivus-test-cache-%1")
                                   .arg(QCoreApplication::applicationPid()))
                .toUtf8());

    QSettings().clear();
    CHECK(argc > 1);

    QTemporaryDir dir;
    CHECK(dir.isValid());
    const QDir corpusSource(argv[1]);
    const QString folder = dir.path();
    for (const QString& f : corpusSource.entryList(QDir::Files | QDir::NoDotAndDotDot))
        QFile::copy(corpusSource.filePath(f), folder + QLatin1Char('/') + f);

    MainWindow w;
    w.show();
    w.openFolder(folder);
    QListWidget* list = seriesListIn(w);
    CHECK(list != nullptr);
    CHECK(waitFor([&] { return list->count() > 0; }, 30000));
    CanvasWidget* canvas = w.findChild<CanvasWidget*>();
    CHECK(canvas != nullptr);
    list->setCurrentRow(0);
    CHECK(waitFor([&] { return canvas->hasValidFrame(); }, 30000));

    // --- (a) known spacing: the committed mm ratio between a 100-px and a 40-px horizontal drag
    // must be 2.5 (the mapping is the same fit for both, so only the mm math is under test).
    canvas->clearMeasurements();
    w.setActiveTool(MainWindow::ActiveTool::Measure);
    drag(canvas, QPoint(20, 40), QPoint(120, 40));
    CHECK(canvas->distanceCount() == 1);
    CHECK(canvas->distanceAt(0).spacing_known);          // MR_small carries Pixel Spacing
    const double longMm = canvas->distanceAt(0).distance_mm;
    CHECK(longMm > 0.0);
    drag(canvas, QPoint(20, 50), QPoint(60, 50));
    CHECK(canvas->distanceCount() == 2);
    const double shortMm = canvas->distanceAt(1).distance_mm;
    CHECK(shortMm > 0.0);
    CHECK(std::abs(shortMm * 2.5 - longMm) < longMm * 0.05);

    // --- (b) unknown spacing stays unknown — never an invented number.
    canvas->setPixelSpacing(0.0, 0.0);
    canvas->clearMeasurements();
    drag(canvas, QPoint(20, 30), QPoint(70, 30));
    CHECK(canvas->distanceCount() == 1);
    CHECK(!canvas->distanceAt(0).spacing_known);
    CHECK(canvas->distanceAt(0).distance_mm == 0.0);

    // --- (c) a 90-degree L reads ~90 (isotropic spacing; the widget mapping is invertible).
    w.setActiveTool(MainWindow::ActiveTool::Measure);
    canvas->clearMeasurements();
    canvas->setPixelSpacing(0.3125, 0.3125);
    anglePoints(canvas, QPoint(20, 60), QPoint(20, 20), QPoint(70, 20));
    CHECK(canvas->measurementCount() == 1);
    CHECK(std::abs(canvas->angleAt(0).angle_deg - 90.0) < 1.0);
    // Degenerate input (a repeated vertex) is dropped, not committed as a fake angle.
    anglePoints(canvas, QPoint(40, 40), QPoint(40, 40), QPoint(40, 40));
    CHECK(canvas->measurementCount() == 1);

    // --- (d) with the measure tool off, a left drag is window/level — no committed measurement.
    w.setActiveTool(MainWindow::ActiveTool::WindowLevel);
    const int committedBefore = canvas->measurementCount();
    drag(canvas, QPoint(40, 40), QPoint(90, 40));
    CHECK(canvas->measurementCount() == committedBefore);

    // --- (e) switching series clears the slice's measurements (even a row that fails to decode:
    // the clear happens on selection, not on the next successful decode).
    w.setActiveTool(MainWindow::ActiveTool::Measure);
    canvas->clearMeasurements();
    drag(canvas, QPoint(20, 20), QPoint(80, 20));
    CHECK(canvas->distanceCount() == 1);
    CHECK(waitFor([&] {
        if (list->count() < 2) return true;   // single-row corpus: nothing to switch to
        list->setCurrentRow(1);
        return canvas->distanceCount() == 0;
    }, 15000));

    // Smooth scroll: with the display settled, the prefetch slot chases the neighbour in the
    // wheel's direction (default forward), so the next step can present without a decode wait.
    w.setActiveTool(MainWindow::ActiveTool::WindowLevel);
    CHECK(waitFor([&] { return w.prefetchIndex() == 1; }, 15000));

    canvas->clearMeasurements();
    QSettings().clear();
    std::printf("measurements: all checks passed\n");
    return 0;
}
