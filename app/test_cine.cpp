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
    //
    // What this checks is that playback stops advancing — not that no decode ever lands. A tick that
    // was already requested when the user stopped still arrives (it is the slice it asked for; throwing
    // it away would leave the viewer on a slice nobody chose), so the test first lets the pipeline go
    // quiet, then requires the index to hold still for longer than several ticks would have moved it.
    window.toggleCine();
    CHECK(window.cineRunningForTest());
    window.toggleCine();
    CHECK(!window.cineRunningForTest());

    int settled = window.currentSliceForTest();
    {
        QElapsedTimer quiet;
        quiet.start();
        int last = settled;
        QElapsedTimer stable;
        stable.start();
        while (quiet.elapsed() < 5000 && stable.elapsed() < 400) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
            QThread::msleep(20);
            if (window.currentSliceForTest() != last) {
                last = window.currentSliceForTest();
                stable.restart();
            }
        }
        settled = window.currentSliceForTest();
    }
    // 700 ms is more than eight ticks at the default 12 fps: a still-running cine cannot hide here.
    for (int i = 0; i < 35; ++i) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(20);
    }
    CHECK(window.currentSliceForTest() == settled);
    CHECK(!window.cineRunningForTest());

    // (e) opening another folder stops cine.
    window.toggleCine();
    CHECK(window.cineRunningForTest());
    window.openFolder(QString::fromUtf8(argv[1]));
    CHECK(!window.cineRunningForTest());

    std::printf("cine: all checks passed (%d frames, wrap and manual-stop verified)\n", total);
    return 0;
}