// SPDX-License-Identifier: GPL-3.0-or-later
#ifdef _WIN32
#include <windows.h>
#endif

#include <QApplication>
#include <QCommandLineParser>
#include <QSurfaceFormat>

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
    QApplication::setOrganizationName("clivus");
    clivus::style::apply(app);

    QCommandLineParser parser;
    const QCommandLineOption bench("bench", "Render N frames headlessly and report timings.", "frames");
    parser.addOption(bench);
    parser.addPositionalArgument("folder", "DICOM folder to open", "[folder]");
    parser.process(app);

    MainWindow window;
    window.show();
    const QStringList args = parser.positionalArguments();
    if (!args.isEmpty()) window.openFolder(args.first());
    if (parser.isSet(bench)) window.runBenchmark(parser.value(bench).toInt());
    return app.exec();
}
