// SPDX-License-Identifier: GPL-3.0-or-later
// Stack scrolling (v0.4.1, radiology report: "scrolling through the image stacks feels jerky").
//
// This is a scheduling probe, not a frame-rate measurement: the offscreen platform on a headless build
// host cannot create a GL context, so nothing here says anything about paint cost or fps. What it does
// measure is the part that decides whether the image on screen is the one the wheel points at:
//
//   A. a burst of steps while one decode is already in flight — only the slice the user asked for LAST
//      may be presented; landing frames for slices already scrolled past are dropped, not flashed;
//   B. a step into a slice the prefetch pool is already decoding — it is presented once, not twice
//      (the request path used to start a second, redundant decode of the same file);
//   C. reverse direction — the last requested slice still wins;
//   D. a sequential walk with a pause between steps — the request -> present latency distribution, which
//      is what a user feels as "smooth" or "jerky" when nothing is cached.
//
// Run: QT_QPA_PLATFORM=offscreen ./app/clivus_test_scroll <dicom-folder>

#include "main_window.h"

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QListWidget>
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

constexpr int kSlices = 40;

void spin(int ms)
{
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
}

// Waits until the pipeline is quiet: the canvas holds a frame and nothing new lands for a moment.
int settle(MainWindow& w)
{
    int last = -1;
    for (int i = 0; i < 200; ++i) {
        spin(20);
        const int presents = w.scrollStatsForTest().presents;
        if (last == presents && w.currentSliceForTest() >= 0) return presents;
        last = presents;
    }
    return last;
}

double percentile(std::vector<double> v, double p)
{
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    const size_t i = std::min(v.size() - 1, size_t(p / 100.0 * double(v.size())));
    return v[i];
}

int countPresented(const MainWindow& w, int slice)
{
    int n = 0;
    for (int s : w.presentedSlicesForTest())
        if (s == slice) ++n;
    return n;
}

void report(const char* label, const MainWindow& w)
{
    const auto& s = w.scrollStatsForTest();
    std::vector<double> lat = s.latencyMs;
    std::printf("%-22s requests=%2d presents=%2d stale=%2d prefetch(start=%2d served=%2d) "
                "latency p50=%6.1fms p95=%6.1fms max=%6.1fms\n",
                label, s.requests, s.presents, s.stalePresents, s.prefetchStarts, s.prefetchServed,
                percentile(lat, 50), percentile(lat, 95), lat.empty() ? 0.0 : *std::max_element(lat.begin(), lat.end()));
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
        std::printf("usage: clivus_test_scroll <dicom-folder>\n");
        return 2;
    }
    const QString object = QDir(QString::fromLocal8Bit(argv[1])).filePath(QStringLiteral("CT_small.dcm"));
    if (!QFile::exists(object)) {
        std::printf("cannot run: %s is missing\n", qPrintable(object));
        return 2;
    }

    // A stack of 40 slices built from one committed object: the same file read 40 times is the decode
    // cost a real 40-slice series pays per slice, with no patient data and no fixture to maintain.
    clivus::SeriesInfo series;
    series.patientId = "PID-SCROLL";
    series.patientName = "SCROLL^SYNTHETIC";
    series.studyUid = "1.2.826.0.1.3680043.9.SCROLL";
    series.seriesUid = "1.2.826.0.1.3680043.9.SCROLL.1";
    series.seriesDescription = "SYNTHETIC 40 SLICE STACK";
    series.modality = "CT";
    series.width = 128;
    series.height = 128;
    for (int i = 0; i < kSlices; ++i) {
        series.files.push_back(object.toStdString());
        series.frameCounts.push_back(1);
    }

    QTemporaryDir scratch;
    CHECK(scratch.isValid());
    MainWindow w;
    w.resize(1100, 700);
    w.show();
    w.openFolder(scratch.path());   // nothing in it: the batch below is the whole browser
    spin(200);
    std::vector<clivus::SeriesInfo> batch{series};
    w.applyScanBatchForTest(w.folderEpochForTest(), batch);
    CHECK(w.frameCountForTest() == kSlices);
    settle(w);
    CHECK(w.currentSliceForTest() == 0);   // the first slice is on the canvas before measuring

    // A. Burst: the first step is served by the parked prefetch, the second lands while that slice is
    // still decoding, and the wheel then moves ten slices without letting anything land. Only slice 1
    // (which really was the target when it arrived) and the final target may be presented; the frame
    // that was decoding for an abandoned target is dropped, not flashed.
    {
        w.resetScrollStatsForTest();
        CHECK(w.currentSliceForTest() == 0);
        w.showSliceForTest(1);                 // 1, served from the parked prefetch
        w.showSliceForTest(1);                 // 2 starts decoding; the wheel keeps moving below
        for (int i = 0; i < 10; ++i) w.showSliceForTest(1);   // 3..12, all queued behind it
        settle(w);
        report("A burst(12 steps)", w);
        const auto& presented = w.presentedSlicesForTest();
        std::printf("A presented:");
        for (int s : presented) std::printf(" %d", s);
        std::printf("\n");
        CHECK(w.scrollStatsForTest().stalePresents == 0);
        CHECK(presented.size() == 2);
        CHECK(presented.front() == 1);
        CHECK(presented.back() == 12);
        CHECK(w.currentSliceForTest() == 12);
    }

    // B. A step into the slice the prefetch is already decoding lands once, not twice: the request path
    // used to start a second decode of the same file while the speculative one was in flight.
    {
        w.resetScrollStatsForTest();
        w.showSliceForTest(1);                 // 13
        settle(w);
        w.showSliceForTest(1);                 // 14 — the prefetch for it may already be running
        settle(w);
        report("B prefetch overlap", w);
        CHECK(countPresented(w, 14) == 1);
        CHECK(w.currentSliceForTest() == 14);
    }

    // C. Reverse: the last requested slice still wins, and nothing older is presented after it.
    {
        w.resetScrollStatsForTest();
        w.showSliceForTest(1);
        w.showSliceForTest(1);
        w.showSliceForTest(1);
        w.showSliceForTest(-1);
        settle(w);
        report("C direction change", w);
        // 14 -> 15 -> 16 -> 17 -> 16: the last request wins, and it is the frame on screen.
        CHECK(!w.presentedSlicesForTest().empty());
        CHECK(w.presentedSlicesForTest().back() == 16);
        CHECK(w.currentSliceForTest() == 16);
        CHECK(w.scrollStatsForTest().stalePresents == 0);
    }

    // D. Sequential walk with a pause between steps: the latency a user feels with nothing cached.
    {
        w.resetScrollStatsForTest();
        int steps = 0;
        for (int i = 0; i < 15; ++i) {
            w.showSliceForTest(1);
            ++steps;
            spin(40);   // long enough for the decode to land: this is the "one at a time" case
        }
        settle(w);
        report("D sequential walk", w);
        const auto& s = w.scrollStatsForTest();
        CHECK(s.requests == steps);
        CHECK(s.stalePresents == 0);
        CHECK(w.currentSliceForTest() == 31); // 16 + 15 steps
        CHECK(!s.latencyMs.empty());
    }

    // E. Walk back over slices already shown: every step must come from the decoded-frame cache,
    // not a second decode of the same file. This is the regression test for the radiology report
    // that scrolling jerks and skips frames — before the cache, the walk back re-decoded every
    // slice, and a fast spin dropped the intermediates outright.
    {
        w.resetScrollStatsForTest();
        CHECK(w.currentSliceForTest() == 31);
        for (int i = 0; i < 10; ++i) w.showSliceForTest(-1);
        settle(w);
        report("E cached walk back", w);
        const auto& s = w.scrollStatsForTest();
        CHECK(s.requests == 10);
        CHECK(s.presents == 10);
        CHECK(s.prefetchServed == 10);   // every present came from the cache, none re-decoded
        CHECK(s.stalePresents == 0);
        CHECK(w.presentedSlicesForTest().size() == 10);
        CHECK(w.presentedSlicesForTest().back() == 21);   // 31 - 10 steps
        CHECK(w.currentSliceForTest() == 21);
    }

    std::printf("scroll: all scheduling checks passed\n");
    return 0;
}