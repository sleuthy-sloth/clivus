// SPDX-License-Identifier: GPL-3.0-or-later
#ifdef _WIN32
#include <windows.h>
#endif

#include <QApplication>
#include <QIcon>
#include <QCommandLineParser>
#include <QDir>
#include <QPainter>
#include <QStandardPaths>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QThread>
#include <QSurfaceFormat>

#include "about_dialog.h"
#include "branding.h"

#include "clivus/version.h"

#include "main_window.h"

#include "server_dialog.h"
#include "style.h"

int main(int argc, char** argv)
{
#ifdef _WIN32
    // The app is a WIN32 subsystem executable, so it has no console of its own and --bench output would
    // disappear. Reattach to the terminal that launched us; double-clicked launches fail harmlessly.
    if (AttachConsole(ATTACH_PARENT_PROCESS)) {
        freopen("CONOUT$", "w", stdout);
        freopen("CONOUT$", "w", stderr);
    }
#endif

    // Ask for a compatibility profile explicitly: the canvas draws a textured quad with fixed-function
    // GL, which a core profile refuses. On Windows this also decides whether Qt uses the GPU driver or
    // falls back to software rendering, so it must be set before QApplication exists.
    QSurfaceFormat format;
    format.setVersion(2, 1);
    format.setProfile(QSurfaceFormat::CompatibilityProfile);
    format.setSwapInterval(1);
    QSurfaceFormat::setDefaultFormat(format);

    QApplication app(argc, argv);
    QApplication::setApplicationName("Clivus");
    QApplication::setApplicationVersion(CLIVUS_VERSION);
    // Window/taskbar icon from the shipped asset set; branding::appIcon falls back to the mark if
    // the file is absent (a source build with no assets still starts).
    QApplication::setWindowIcon(QIcon(clivus::branding::appIcon(256)));
    QApplication::setOrganizationName("sleuthy-sloth");   // matches app/settings_names.h
    // The index cache lives in the application cache directory, never beside the DICOM data (the
    // old in-folder index carried patient names and travelled with copies of a study). QStandardPaths
    // knows the right place per platform; the core reads it from here.
    {
        const QString cacheBase = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
        if (!cacheBase.isEmpty())
            qputenv("CLIVUS_CACHE_DIR", (cacheBase + QStringLiteral("/index")).toUtf8());
    }
    clivus::style::apply(app);

    QCommandLineParser parser;
    const QCommandLineOption bench("bench", "Render N frames headlessly and report timings.", "frames");
    parser.addOption(bench);
    // Documentation capture: save the branded widget surfaces (welcome page, About) as PNGs from
    // the real application, then exit. Widgets only — the GL canvas needs a real context, so a
    // host without one still captures these truthfully instead of shipping hand-made mockups.
    const QCommandLineOption capture("capture", "Save welcome/About PNGs to this folder and exit.", "dir");
    parser.addOption(capture);
    parser.addPositionalArgument("folder", "DICOM folder to open", "[folder]");
    parser.process(app);

    MainWindow window;
    window.show();
    const QStringList args = parser.positionalArguments();
    if (parser.isSet(capture)) {
        const QString dir = parser.value(capture);
        QDir().mkpath(dir);
        QApplication::processEvents();   // let the layout settle before grabbing
        if (args.isEmpty()) {
            // Branded widget surfaces: no study loaded, no GL needed.
            window.grab().save(dir + QStringLiteral("/welcome-screen.png"));
        }
        AboutDialog about(&window);
        about.show();
        QApplication::processEvents();
        about.grab().save(dir + QStringLiteral("/about-screen.png"));
        about.close();

        if (!args.isEmpty()) {
            // A real viewer capture: open the study, wait for the first slice to decode and paint,
            // then grab the whole window. Needs a real GL context (a desktop session or a CI
            // runner with a window server) — on a headless box the canvas stays blank and the
            // grab says so instead of pretending.
            window.openFolder(args.first());
            // The scan is asynchronous: wait for the series list to exist before choosing one.
            {
                QElapsedTimer scanClock;
                scanClock.start();
                while (!window.hasSeriesForCapture() && scanClock.elapsed() < 20000) {
                    QApplication::processEvents(QEventLoop::AllEvents, 100);
                    QThread::msleep(50);
                }
            }
            // A capture exists to show the viewer doing its job on real anatomy: prefer CT then MR
            // over whatever sorts first (the corpus's RT Dose is a 10x10 grid at WC 1 / WW 1 —
            // technically correct, visually a black rectangle).
            window.selectSeriesForCapture(QStringLiteral("CT")) ||
                window.selectSeriesForCapture(QStringLiteral("MR"));
            QElapsedTimer clock;
            clock.start();
            while (clock.elapsed() < 20000) {
                QApplication::processEvents(QEventLoop::AllEvents, 100);
                QThread::msleep(50);
                if (window.canvasReady()) break;
            }
            // One more pass so the textured frame is painted before the grab.
            for (int i = 0; i < 20; ++i) {
                QApplication::processEvents(QEventLoop::AllEvents, 50);
                QThread::msleep(30);
            }
            QImage shot = window.grab().toImage();
            // The canvas is a QOpenGLWidget: its pixels live in a GL framebuffer that a widget
            // grab does not include. Ask it for its framebuffer and paste it at the widget's
            // position — both images are this process's own render, nothing synthesised.
            const QImage canvas = window.captureCanvasImage();
            if (!canvas.isNull() && !shot.isNull()) {
                QPainter painter(&shot);
                painter.drawImage(window.captureCanvasPosition(), canvas);
                painter.end();
            }
            if (!shot.save(dir + QStringLiteral("/viewer.png"))) return 2;

            // A region of interest on the same slice, drawn by the same code the mouse uses: the
            // README should show the measurement tools that exist now, not a picture of them.
            if (window.addRoiForCapture() > 0) {
                for (int i = 0; i < 10; ++i) {
                    QApplication::processEvents(QEventLoop::AllEvents, 40);
                    QThread::msleep(20);
                }
                QImage roiShot = window.grab().toImage();
                const QImage roiCanvas = window.captureCanvasImage();
                if (!roiCanvas.isNull() && !roiShot.isNull()) {
                    QPainter painter(&roiShot);
                    painter.drawImage(window.captureCanvasPosition(), roiCanvas);
                    painter.end();
                }
                roiShot.save(dir + QStringLiteral("/viewer-roi.png"));
            }

            // MPR with the reformat row (linear interpolation + MIP) composited from its own
            // framebuffers. Skipped, not faked, when the volume cannot load.
            if (!window.captureMprImage(dir + QStringLiteral("/mpr.png")))
                qWarning("MPR capture unavailable for this study");

            // The DICOMweb dialog in its empty state: real widgets, no server contacted, no PHI.
            {
                ServerDialog server(&window);
                server.show();
                QApplication::processEvents();
                QThread::msleep(150);
                QApplication::processEvents();
                server.grab().save(dir + QStringLiteral("/server-dialog.png"));
                server.close();
            }
        }
        return 0;
    }
    if (!args.isEmpty()) window.openFolder(args.first());
    if (parser.isSet(bench)) window.runBenchmark(parser.value(bench).toInt());
    return app.exec();
}
