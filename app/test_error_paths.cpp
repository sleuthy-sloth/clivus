// SPDX-License-Identifier: GPL-3.0-or-later
// Roadmap 3.5 error-recovery harness: the offscreen UI drives the REAL scan/decode paths and
// asserts recovery. QApplication + QWidget only — the canvas never paints here, so ctest runs it
// anywhere; the plumbing under test is the error handling, not the GL rendering.
//
// Scenarios:
//   A. Empty folder → plain "No DICOM images" state, progress and cancel hidden.
//   B. Intact folder opened once (scan cache populated), then the RT Dose object is RENAMED:
//      the folder fingerprint is unchanged (count, bytes, newest mtime), the cached scan serves
//      the intact-looking series list, and decoding the dose series fails on a missing file —
//      the one failure every parser agrees to report. Other series keep loading: the pump
//      survives, no indicator sticks.
//   C. After failures, Cancel returns the status bar to Ready with the controls gone.
//
// Run: QT_QPA_PLATFORM=offscreen ./app/clivus_test_error_paths <corpus-folder>

#include "main_window.h"

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QLabel>
#include <QListWidget>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QThread>
#include <cstdio>
#include <filesystem>
#include <functional>

namespace fs = std::filesystem;

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);      \
            std::exit(1);                                                   \
            std::exit(1);                                                    \
        }                                                                    \
    } while (false)

namespace {

int failures = 0;

void expect(bool ok, const char* what)
{
    if (!ok) {
        std::printf("FAIL %s\n", what);
        ++failures;
    }
}

bool waitFor(const std::function<bool()>& condition, int timeoutMs)
{
    QElapsedTimer clock;
    clock.start();
    while (!condition()) {
        if (clock.elapsed() > timeoutMs) return false;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        QThread::msleep(20);
    }
    return true;
}

QLabel* statusOf(const MainWindow& w)
{
    QLabel* fallback = nullptr;
    for (QLabel* label : w.findChildren<QLabel*>()) {
        if (label->parentWidget() != w.statusBar()) continue;
        // The status bar also carries patient/study/particulars labels ("no description", file
        // counts). The slice-status label is the one that reads Ready/Loading/Slice, so match it
        // positively instead of grabbing the first child.
        const QString t = label->text();
        if (t.startsWith(QStringLiteral("Slice")) || t == QStringLiteral("Ready") ||
            t.startsWith(QStringLiteral("Loading")) || t.contains(QStringLiteral("No DICOM")))
            return label;
        if (fallback == nullptr) fallback = label;
    }
    return fallback;
}

QProgressBar* progressBarIn(const MainWindow& w)
{
    const QList<QProgressBar*> bars = w.findChildren<QProgressBar*>();
    return bars.isEmpty() ? nullptr : bars.first();
}

QListWidget* seriesListIn(const MainWindow& w)
{
    const QList<QListWidget*> lists = w.findChildren<QListWidget*>();
    return lists.isEmpty() ? nullptr : lists.first();
}

// A DECODED state reads "Slice <n> of <m>". The swing state before anything lands reads
// "Slice … of <m>" — startsWith("Slice") matches it too, so match the digits, not the prefix.
bool decodedState(const MainWindow& w)
{
    const QLabel* status = statusOf(w);
    return status &&
           QRegularExpression(QStringLiteral("Slice \\d+ of \\d+")).match(status->text()).hasMatch();
}

QString statusText(const MainWindow& w)
{
    const QLabel* status = statusOf(w);
    return status ? status->text() : QString();
}

}  // namespace

int main(int argc, char** argv)
{
    QCoreApplication::setOrganizationName(QStringLiteral("sleuthy-sloth"));
    QCoreApplication::setApplicationName(QStringLiteral("clivus-error-test"));
    QApplication app(argc, argv);
    // Tests must never touch the user's real cache: point the index at a throwaway directory.
    qputenv("CLIVUS_CACHE_DIR",
            (QDir::tempPath() + QStringLiteral("/clivus-test-cache-%1")
                                   .arg(QCoreApplication::applicationPid()))
                .toUtf8());

    QSettings().clear();
    CHECK(argc > 1);

    // --- Scenario A: empty folder.
    {
        QTemporaryDir empty;
        CHECK(empty.isValid());
        MainWindow w;
        w.openFolder(empty.path());
        CHECK(waitFor([&] { return statusText(w).contains("No DICOM images"); }, 10000));
        CHECK(progressBarIn(w)->isHidden());
        expect(statusText(w).contains("check that this is the right folder"),
               "the empty-folder message says what to check (roadmap 1.6)");
    }

    // --- Scenario B/C: intact copy, then the dose object deleted via rename.
    {
        QTemporaryDir dir;
        CHECK(dir.isValid());
        const QString copy = dir.path();
        QDir source(argv[1]);
        for (const QString& f : source.entryList(QDir::Files))
            QFile::copy(source.filePath(f), copy + QLatin1Char('/') + f);

        // Pass 1: fully intact, left with a populated scan cache behind.
        {
            MainWindow w;
            w.openFolder(copy);
            CHECK(waitFor([&] { return decodedState(w); }, 15000));
        }

        // Pass 2: rename the RT Dose object away. Everything the fingerprint keys on stays the
        // same — count, total bytes, newest mtime — so the cache must serve the full list.
        const fs::path doseCopy =
            (copy + QLatin1Char('/') + QStringLiteral("rtdose_rle.dcm")).toStdString();
        const fs::path doseMoved =
            (copy + QLatin1Char('/') + QStringLiteral("rtdose_moved.dcm")).toStdString();
        std::error_code renameEc;
        fs::rename(doseCopy, doseMoved, renameEc);
        CHECK(!renameEc);

        MainWindow w;
        w.openFolder(copy);

        QListWidget* list = seriesListIn(w);
        CHECK(list != nullptr);
        CHECK(waitFor([&] { return list->count() > 0; }, 30000));
        // The dose series fails loudly; the others settle into decoded states on their own (J2K
        // series fail to decode everywhere the codec set is the same, so keep the wait general).
        CHECK(waitFor([&] {
            const QString text = statusText(w);
            return decodedState(w) || text.contains("could not be decoded") ||
                   text.contains("has no image objects");
        }, 30000));
        int seenFailure = 0, seenLoaded = 0;
        for (int i = 0; i < list->count(); ++i) {
            list->setCurrentRow(i);
            CHECK(waitFor([&] {
                const QString text = statusText(w);
                return decodedState(w) || text.contains("could not be decoded") ||
                       text.contains("has no image objects");
            }, 15000));
            const QString text = statusText(w);
            if (text.contains("could not be decoded")) ++seenFailure;
            if (decodedState(w)) ++seenLoaded;
            // No STUCK indicators after any selection: thumbnails may still be in flight when the
            // slice settles, so the bar hiding on its own is the proof it never wedges.
            CHECK(waitFor([&] { return progressBarIn(w)->isHidden(); }, 20000));
        }
        expect(seenFailure > 0, "the dose series' failure was readable");
        expect(seenLoaded > 0, "other series still decode after the failure");

        // Scenario C: cancel after failures lands the status bar on Ready.
        for (QPushButton* cancel : w.findChildren<QPushButton*>())
            if (cancel->text() == QStringLiteral("Cancel")) cancel->click();
        CHECK(waitFor([&] { return statusText(w) == QStringLiteral("Ready"); }, 5000));
        CHECK(progressBarIn(w)->isHidden());
    }

    QSettings().clear();
    if (failures == 0) std::printf("error paths: all checks passed\n");
    return failures == 0 ? 0 : 1;
}
