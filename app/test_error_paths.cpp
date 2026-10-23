// SPDX-License-Identifier: GPL-3.0-or-later
// Roadmap 3.5 error-recovery harness: the offscreen UI drives the REAL scan/decode paths and
// asserts recovery rather than success. QApplication + QWidget only — the canvas never paints
// here, so ctest can run it anywhere; GL behaviour is not what is being tested, the error
// plumbing around it is.
//
// Scenarios:
//   A. An empty folder scans to a plain "No DICOM images" state with progress hidden — no stuck
//      indicators, no crash.
//   B. A folder whose objects are corrupted AFTER the first decode lands produces slice decode
//      failures that stay readable: the failure message replaces the timing/status text, the
//      progress indicators never stay up, and selecting further series still loads cleanly —
//      the decode pump survives a broken object instead of wedging on it.
//   C. Cancel after failures returns the status bar to "Ready" with the cancel button gone.
//   D. Recovery is demonstrable without a restart: reopening the untouched corpus loads again.
//
// Run: QT_QPA_PLATFORM=offscreen ./app/clivus_test_error_paths <corpus-folder>

#include "main_window.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QLabel>
#include <QListWidget>
#include <QProgressBar>
#include <QPushButton>
#include <QSettings>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QThread>
#include <QDir>
#include <QEventLoop>
#include <algorithm>
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

// Pump the event loop until a predicate holds (or give up after timeoutMs). Decode results and
// scan completions all travel through the UI event loop, so this is what wait-time means here.
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
    for (QLabel* label : w.findChildren<QLabel*>()) {
        if (label->parentWidget() == w.statusBar()) return label;
    }
    return nullptr;
}

QProgressBar* progressBarIn(const MainWindow& w)
{
    for (QProgressBar* bar : w.findChildren<QProgressBar*>()) return bar;
    return nullptr;
}

QListWidget* seriesListIn(const MainWindow& w)
{
    // The series browser is the widest list in the app; the Help window's nav list is created
    // lazily and only when the manual is opened, so there is exactly one here.
    const QList<QListWidget*> lists = w.findChildren<QListWidget*>();
    return lists.isEmpty() ? nullptr : lists.first();
}

bool statusStartsWith(const MainWindow& w, const char* prefix)
{
    const QLabel* status = statusOf(w);
    return status && status->text().startsWith(QLatin1String(prefix));
}

}  // namespace

int main(int argc, char** argv)
{
    QCoreApplication::setOrganizationName(QStringLiteral("sleuthy-sloth"));
    QCoreApplication::setApplicationName(QStringLiteral("clivus-error-test"));
    QApplication app(argc, argv);
    QSettings().clear();
    CHECK(argc > 1);

    // Scenario A: an empty folder — must settle into an informative state with nothing stuck.
    {
        QTemporaryDir empty;
        CHECK(empty.isValid());
        MainWindow w;
        w.openFolder(empty.path());
        CHECK(waitFor([&] { return statusOf(w) && statusOf(w)->text().contains("No DICOM images"); },
                      10000));
        CHECK(progressBarIn(w)->isHidden());
        const QLabel* status = statusOf(w);
        CHECK(status != nullptr);
        // The message names the folder, the likely cause and what to try (roadmap 1.6).
        CHECK(status->text().contains("check that this is the right folder"));
    }

    // Scenario B/C: the copy is opened INTACT first (so the scan lists every series and its first
    // slice decodes), then corrupted IN PLACE with every file's mtime restored afterwards. The
    // folder fingerprint (count · newest-mtime · total bytes) therefore still matches, the cached
    // scan serves an intact-looking list, and the breakage shows up exactly where recovery has to
    // handle it: at slice-decode time, not at scan time.
    {
        QTemporaryDir dir;
        CHECK(dir.isValid());
        const QString copy = dir.path();
        QDir source(argv[1]);
        const QStringList names = source.entryList(QDir::Files);
        for (const QString& f : names)
            QFile::copy(source.filePath(f), copy + QLatin1Char('/') + f);

        // Pass 1: intact, and left with a cache behind.
        {
            MainWindow w;
            w.openFolder(copy);
            CHECK(waitFor([&] { return statusOf(w) && statusOf(w)->text().startsWith("Slice 1"); },
                          15000));
        }

        // Pass 2: poison in place. CT_small and MR_small are left alone so readable series still
        // exist; every other object's body is zeroed — SAME byte count, mtime restored afterwards,
        // so the fingerprint the index keys on does not change: count · newest-mtime · total bytes.
        namespace fs = std::filesystem;
        int poisoned = 0;
        for (const QString& f : names) {
            if (f == QStringLiteral("CT_small.dcm") || f == QStringLiteral("MR_small.dcm")) continue;
            const fs::path path = (copy + QLatin1Char('/') + f).toStdString();
            const auto originalMtime = fs::last_write_time(path);
            QFile file(copy + QLatin1Char('/') + f);
            CHECK(file.open(QIODevice::ReadWrite));
            const qint64 size = file.size();
            const QByteArray zeros(int(size), char(0));
            file.seek(0);
            CHECK(file.write(zeros) == size);
            file.close();
            fs::last_write_time(path, originalMtime);
            ++poisoned;
        }
        CHECK(poisoned > 0);

        // Deterministic DECODE-time failure on every platform: a zeroed body is still parseable
        // DICOM to some parsers (an MSVC run happily decodes it as black frames), so the RT Dose
        // object is additionally REPLACED with a same-size file of 0x7f bytes and the SAME mtime.
        // The fingerprint stays identical, so the cached scan serves the intact-looking row — and
        // then loadFrame fails outright, which every platform must report and survive.
        {
            QFile dose(copy + QLatin1Char('/') + QStringLiteral("rtdose_rle.dcm"));
            CHECK(dose.open(QIODevice::ReadOnly));
            const qint64 size = dose.size();
            dose.close();
            const auto keepMtime = fs::last_write_time(
                (copy + QLatin1Char('/') + QStringLiteral("rtdose_rle.dcm")).toStdString());
            CHECK(QFile::remove(copy + QLatin1Char('/') + QStringLiteral("rtdose_rle.dcm")));
            QFile replacement(copy + QLatin1Char('/') + QStringLiteral("rtdose_rle.dcm"));
            CHECK(replacement.open(QIODevice::WriteOnly));
            CHECK(replacement.write(QByteArray(int(size), char(0x7f))) == size);
            replacement.close();
            fs::last_write_time(
                (copy + QLatin1Char('/') + QStringLiteral("rtdose_rle.dcm")).toStdString(),
                keepMtime);
        }

        // Reopen: the cached list must come back intact-looking, then decode failures must be
        // readable, non-blocking, and followed by working series — the pump survives.
        MainWindow w;
        w.openFolder(copy);
        CHECK(waitFor([&] {
            const QString text = statusOf(w) ? statusOf(w)->text() : QString();
            return text.startsWith("Slice") || text.contains("could not be decoded") ||
                   text.contains("has no image objects");
        }, 15000));

        QListWidget* list = seriesListIn(w);
        CHECK(list != nullptr);
        int seenFailure = 0, seenLoaded = 0;
        for (int i = 0; i < list->count(); ++i) {
            list->setCurrentRow(i);
            CHECK(waitFor([&] {
                const QString text = statusOf(w) ? statusOf(w)->text() : QString();
                return text.startsWith("Slice") ||
                       text.contains("could not be decoded") ||
                       text.contains("has no image objects");
            }, 15000));
            const QString text = statusOf(w)->text();
            if (text.contains("could not be decoded")) ++seenFailure;
            if (text.startsWith("Slice")) ++seenLoaded;
            // No STUCK indicators after every selection: thumbnails may legitimately still be
            // filling while slices settle, so the bar may be up briefly — but it must hide on its
            // own well before the next selection.
            CHECK(waitFor([&] { return progressBarIn(w)->isHidden(); }, 20000));
        }
        // Both outcomes must occur: the failures are readable AND the decoder pump still lands
        // images afterwards, so recovery is proven within one session.
        CHECK(seenFailure > 0);
        CHECK(seenLoaded > 0);

        // Scenario C: cancel after failures returns to Ready, controls tucked away.
        for (QPushButton* cancel : w.findChildren<QPushButton*>()) {
            if (cancel->text() == QStringLiteral("Cancel")) cancel->click();
        }
        CHECK(waitFor([&] { return statusOf(w)->text() == QStringLiteral("Ready"); }, 5000));
        CHECK(progressBarIn(w)->isHidden());
    }

    // Scenario D: recovery without a restart — reopening the untouched corpus must load again.
    {
        MainWindow w;
        w.openFolder(QString(argv[1]));
        CHECK(waitFor([&] { return statusOf(w) && statusOf(w)->text().startsWith("Slice 1"); },
                      15000));
    }

    QSettings().clear();
    std::printf("error paths: all checks passed\n");
    return 0;
}
