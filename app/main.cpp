// SPDX-License-Identifier: GPL-3.0-or-later
#ifdef _WIN32
#include <windows.h>
#endif

#include <QApplication>
#include <QIcon>
#include <QCommandLineParser>
#include <QDir>
#include <QSurfaceFormat>

#include "about_dialog.h"
#include "branding.h"

#include "clivus/version.h"

#include "main_window.h"
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
        window.grab().save(dir + QStringLiteral("/welcome-screen.png"));
        AboutDialog about(&window);
        about.show();
        QApplication::processEvents();
        about.grab().save(dir + QStringLiteral("/about-screen.png"));
        return 0;
    }
    if (!args.isEmpty()) window.openFolder(args.first());
    if (parser.isSet(bench)) window.runBenchmark(parser.value(bench).toInt());
    return app.exec();
}
