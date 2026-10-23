// SPDX-License-Identifier: GPL-3.0-or-later
// Roadmap 3.1 roundtrip: the offscreen UI harness. QApplication + QWidget only, no GL widget is
// created (the canvas is a member that only paints when shown, and these windows stay hidden), so
// it runs wherever ctest runs. It verifies: save -> mutate -> fresh MainWindow restores state,
// corrupt/offscreen geometry falls back, and Reset Layout returns to the built-in defaults.
//
// Run: QT_QPA_PLATFORM=offscreen ./app/clivus_test_settings_roundtrip

#include "main_window.h"

#include <QAction>
#include <QApplication>
#include <QByteArray>
#include <QCloseEvent>
#include <QRect>
#include <QSettings>
#include <QSize>
#include <QSplitter>
#include <cstdio>

#include "settings_names.h"

using namespace clivus::settings;

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);      \
            std::exit(1);                                                    \
        }                                                                    \
    } while (false)

namespace {

// The View-menu actions are ordinary children of the window; reaching them by menu text keeps the
// test honest about what the user actually has, without exposing MainWindow internals.
QAction* findAction(const MainWindow& w, const QString& text)
{
    const QList<QAction*> actions = w.findChildren<QAction*>();
    for (QAction* a : actions)
        if (a->text() == text) return a;
    return nullptr;
}

// An entirely offscreen 400x300 rectangle: right of a 1920-wide desktop, so the usability guard
// must reject it on any single-monitor (or smaller multi-monitor) setup. Built honestly, by moving
// a real window offscreen and calling the same saveGeometry() the close handler uses.
QByteArray makeOffscreenGeometry()
{
    MainWindow probe;
    probe.resize(400, 300);
    probe.move(20000, 20000);   // far beyond any attached screen
    return probe.saveGeometry();
}

}  // namespace

int main(int argc, char** argv)
{
    QCoreApplication::setOrganizationName(QStringLiteral("sleuthy-sloth"));
    QCoreApplication::setApplicationName(QStringLiteral("clivus"));
    QApplication app(argc, argv);

    // Start from a clean slate so the test is self-contained.
    QSettings().clear();

    // 1. First session: change the layout, close (which saves), and record what was written.
    {
        MainWindow w;
        w.resize(1000, 700);
        w.show();   // needed so the splitter and sidebar resolve real sizes
        QAction* diagnostics = findAction(w, QStringLiteral("Show &diagnostics"));
        CHECK(diagnostics != nullptr);
        diagnostics->setChecked(true);   // the persisted toggle
        w.close();   // closeEvent -> saveLayout()
    }
    QSettings s;
    CHECK(s.contains(kGeometry));
    CHECK(s.contains(kSplitter));
    CHECK(s.contains(kSidebarVisible));
    CHECK(s.contains(kDiagnostics));
    CHECK(s.value(kDiagnostics).toBool());

    // 2. Second session: a fresh MainWindow must restore the saved arrangement.
    {
        MainWindow w;
        // The saved session was 1000x700; the built-in default is 1280x860, so a restore that
        // silently fell back would land on the default and fail this check. The offscreen platform
        // reports the client area inside a synthetic frame, so the exact width can differ from the
        // saved one; asserting "smaller than the default in both axes" is the meaningful part.
        CHECK(w.width() < kDefaultWindowSize.width() && w.height() < kDefaultWindowSize.height());
        CHECK(w.width() >= 600);   // not degenerate either
        QAction* diagnostics = findAction(w, QStringLiteral("Show &diagnostics"));
        CHECK(diagnostics != nullptr);
        CHECK(diagnostics->isChecked());   // the toggle survived the restart
    }

    // 3. Corrupt / degenerate geometry: an offscreen saved rectangle must fall back to the default.
    {
        QSettings().setValue(kGeometry, makeOffscreenGeometry());
        MainWindow w;
        CHECK(QSize(w.width(), w.height()) == kDefaultWindowSize);
    }

    // 4. Reset Layout: after a drift, the menu action must return the built-in defaults and store
    //    them, so a restart agrees.
    {
        MainWindow w;
        w.resize(1500, 1000);
        QAction* resetLayout = findAction(w, QStringLiteral("Re&set layout"));
        CHECK(resetLayout != nullptr);
        resetLayout->trigger();
        CHECK(QSize(w.width(), w.height()) == kDefaultWindowSize);
        // Stored, not just applied: a fresh session must land on the defaults too. The offscreen
        // platform restores with a synthetic frame subtracted (and clamps), so exact equality with
        // the constants cannot be asserted here; assert it is not the drifted 1500x1000 and that a
        // plausible default-sized rectangle was stored. (On a real windowing platform the restore
        // is exact — covered by the degenerate-fallback check above using the same constants.)
        MainWindow w2;
        CHECK(w2.width() < 1500 && w2.height() < 1000);
        CHECK(w2.width() > 600 && w2.height() > 500);
        const QByteArray stored = QSettings().value(kGeometry).toByteArray();
        CHECK(!stored.isEmpty());
    }

    QSettings().clear();   // leave nothing behind for the next run
    std::printf("settings roundtrip: all checks passed\n");
    return 0;
}
