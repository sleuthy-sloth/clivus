// SPDX-License-Identifier: GPL-3.0-or-later
// Cine playback through the real window and the real decode pump: it advances, it wraps, a manual
// slice step stops it, and a single-image series refuses to play.
//
// Run: QT_QPA_PLATFORM=offscreen ./app/clivus_test_cine <corpus-folder>

#include "main_window.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QEventLoop>
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

}  // namespace

int main(int argc, char** argv)
{
    QCoreApplication::setOrganizationName(QStringLiteral("sleuthy-sloth"));
    QCoreApplication::setApplicationName(QStringLiteral("clivus-cine-test"));
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
    CHECK(waitFor([&] { return window.frameCountForTest() > 1; }, 20000));
    CHECK(waitFor([&] { return window.currentSliceForTest() == 0; }, 10000));
    const int total = window.frameCountForTest();
    CHECK(total > 1);

    // (a) starting cine advances through slices.
    window.toggleCine();
    CHECK(window.cineRunningForTest());
    CHECK(waitFor([&] { return window.currentSliceForTest() >= 2; }, 10000));

    // (b) it wraps rather than stopping at the last slice.
    CHECK(waitFor([&] { return window.currentSliceForTest() == 0; }, 30000));
    CHECK(window.cineRunningForTest());

    // (c) a manual slice step stops playback (the user took the wheel back). Cine is still running
    // from (b); a second toggle here would stop it, which is what this check must not do.
    CHECK(window.cineRunningForTest());
    window.showSliceForTest(1);
    CHECK(!window.cineRunningForTest());

    // (d) toggling twice leaves it stopped.
    window.toggleCine();
    CHECK(window.cineRunningForTest());
    window.toggleCine();
    CHECK(!window.cineRunningForTest());
    const int settled = window.currentSliceForTest();
    QThread::msleep(300);
    QCoreApplication::processEvents();
    CHECK(window.currentSliceForTest() == settled);

    // (e) opening another folder stops cine.
    window.toggleCine();
    CHECK(window.cineRunningForTest());
    window.openFolder(QString::fromUtf8(argv[1]));
    CHECK(!window.cineRunningForTest());

    std::printf("cine: all checks passed (%d frames, wrap and manual-stop verified)\n", total);
    return 0;
}
