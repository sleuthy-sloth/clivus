// SPDX-License-Identifier: GPL-3.0-or-later
// ROI annotations end to end: drag on the real canvas, statistics from real pixels, persistence in a
// sidecar, reload on the next slice visit, and a corrupt file that must not take the viewer down.
//
// Run: QT_QPA_PLATFORM=offscreen ./app/clivus_test_annotations <corpus-folder>

#include "annotation_store.h"

#include "clivus/index_store.h"
#include "clivus/roi.h"
#include "canvas_widget.h"
#include "main_window.h"

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QMouseEvent>
#include <QSettings>
#include <QTemporaryDir>
#include <QThread>
#include <cstdio>
#include <functional>

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);      \
            std::exit(1);                                                    \
        }                                                                    \
    } while (false)

namespace {

bool waitFor(const std::function<bool()>& condition, const int timeoutMs)
{
    QElapsedTimer clock;
    clock.start();
    while (!condition()) {
        if (clock.elapsed() > timeoutMs) return false;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        QThread::msleep(10);
    }
    return true;
}

void drag(CanvasWidget* canvas, const QPoint& from, const QPoint& to)
{
    QMouseEvent press(QEvent::MouseButtonPress, from, canvas->mapToGlobal(from), Qt::LeftButton,
                      Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(canvas, &press);
    QMouseEvent move(QEvent::MouseMove, to, canvas->mapToGlobal(to), Qt::NoButton, Qt::LeftButton,
                     Qt::NoModifier);
    QApplication::sendEvent(canvas, &move);
    QMouseEvent release(QEvent::MouseButtonRelease, to, canvas->mapToGlobal(to), Qt::LeftButton,
                        Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(canvas, &release);
    QCoreApplication::processEvents();
}

}  // namespace

int main(int argc, char** argv)
{
    QCoreApplication::setOrganizationName(QStringLiteral("sleuthy-sloth"));
    QCoreApplication::setApplicationName(QStringLiteral("clivus-annotations-test"));
    QApplication app(argc, argv);
    QSettings().clear();
    QTemporaryDir cache;
    CHECK(cache.isValid());
    qputenv("CLIVUS_CACHE_DIR", cache.path().toUtf8());
    CHECK(argc > 1);

    MainWindow window;
    window.resize(1200, 800);
    window.show();
    window.openFolder(QString::fromUtf8(argv[1]));
    CanvasWidget* canvas = window.findChild<CanvasWidget*>();
    CHECK(canvas != nullptr);
    CHECK(waitFor([&] { return canvas->hasValidFrame(); }, 20000));

    // (a) one rectangle drag produces one ROI with real statistics from the pixels.
    window.setActiveTool(MainWindow::ActiveTool::RoiRect);
    drag(canvas, QPoint(100, 100), QPoint(700, 600));
    CHECK(canvas->rois().size() == 1);
    {
        const auto& roi = canvas->rois().front();
        CHECK(!roi.ellipse);
        CHECK(roi.pixels > 0);
        CHECK(roi.max >= roi.mean && roi.mean >= roi.min);
        CHECK(roi.stddev >= 0.0);
        CHECK(roi.row1 >= roi.row0 && roi.col1 >= roi.col0);
    }

    // (b) the sidecar exists and holds that ROI for the slice on screen.
    CHECK(waitFor([&] { return QFile::exists(AnnotationStore::pathForSeries(
                                  QString::fromStdString(window.seriesUidForTest()))); },
                  5000));
    const QString uid = QString::fromStdString(window.seriesUidForTest());
    CHECK(!uid.isEmpty());
    {
        const auto stored = AnnotationStore::load(uid);
        CHECK(stored.size() == 1);
        CHECK(stored.front().rois.size() == 1);
        CHECK(stored.front().rois.front().pixels == 0);   // statistics are not persisted
    }

    // (c) the file carries geometry and the series UID only — no patient name reaches it.
    {
        QFile file(AnnotationStore::pathForSeries(uid));
        CHECK(file.open(QIODevice::ReadOnly));
        const QByteArray body = file.readAll();
        CHECK(!body.contains("PatientName"));
        CHECK(!body.contains("patient_name"));
    }

    // (d) an ellipse over the same drag box covers fewer pixels than the rectangle did — the box is
    // identical, so the difference is the mask, not the geometry.
    const int rectanglePixels = canvas->rois().front().pixels;
    CHECK(rectanglePixels > 0);
    canvas->clearRois();
    window.setActiveTool(MainWindow::ActiveTool::RoiEllipse);
    drag(canvas, QPoint(100, 100), QPoint(700, 600));
    CHECK(canvas->rois().size() == 1);
    CHECK(canvas->rois().front().ellipse);
    CHECK(canvas->rois().front().pixels > 0);
    std::printf("roi mask: rectangle %d px, ellipse %d px over the same box\n", rectanglePixels,
                canvas->rois().front().pixels);
    CHECK(canvas->rois().front().pixels < rectanglePixels);   // corners dropped

    // (e) a click (no drag) adds nothing.
    drag(canvas, QPoint(500, 500), QPoint(500, 500));
    CHECK(canvas->rois().size() == 1);

    // (f) a corrupt sidecar is an empty series, reported, not a crash.
    {
        const QString path = AnnotationStore::pathForSeries(uid);
        QFile file(path);
        CHECK(file.open(QIODevice::WriteOnly));
        file.write("{ this is not json");
        file.close();
        QString warning;
        const auto stored = AnnotationStore::load(uid, &warning);
        CHECK(stored.isEmpty());
        CHECK(!warning.isEmpty());
    }

    // (g) two slices of one series coexist in the sidecar, and clearing one leaves the other.
    {
        clivus::IndexStore::clearCache(QString::fromUtf8(argv[1]).toStdString());
        CanvasWidget::RoiAnnotation roi;
        roi.row0 = 1; roi.col0 = 2; roi.row1 = 3; roi.col1 = 4;
        QVector<CanvasWidget::RoiAnnotation> list{roi};
        QString error;
        CHECK(AnnotationStore::setSliceRois(uid, 0, list, &error));
        CHECK(AnnotationStore::setSliceRois(uid, 1, list, &error));
        CHECK(AnnotationStore::load(uid).size() == 2);
        CHECK(AnnotationStore::setSliceRois(uid, 0, {}, &error));   // empty list drops only slice 0
        const auto remaining = AnnotationStore::load(uid);
        CHECK(remaining.size() == 1);
        CHECK(remaining.front().slice == 1);
        CHECK(AnnotationStore::clearSeries(uid, &error));
        CHECK(AnnotationStore::load(uid).isEmpty());
    }

    // (h) a null pixel buffer is refused rather than dereferenced.
    CHECK(!clivus::roiStats(static_cast<const float*>(nullptr), 4, 4, clivus::RoiShape::Rectangle,
                            0, 0, 1, 1, 1, 1)
               .ok);

    std::printf("annotations: all checks passed\n");
    return 0;
}
