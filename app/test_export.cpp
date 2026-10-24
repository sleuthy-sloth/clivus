// SPDX-License-Identifier: GPL-3.0-or-later
// Export paths: PNG of the current view, a whole series as PNGs, and the de-identified copy through
// the real dialog. The point is that the files exist, have the right shape, and that the copy's
// report says "verified" only when verification actually passed.
//
// Run: QT_QPA_PLATFORM=offscreen ./app/clivus_test_export <corpus-folder>

#include "deid_dialog.h"
#include "export_images.h"

#include "clivus/series_index.h"

#include <QApplication>
#include <QCheckBox>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QImage>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
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
        QThread::msleep(20);
    }
    return true;
}

}  // namespace

int main(int argc, char** argv)
{
    QCoreApplication::setOrganizationName(QStringLiteral("sleuthy-sloth"));
    QCoreApplication::setApplicationName(QStringLiteral("clivus-export-test"));
    QApplication app(argc, argv);
    QTemporaryDir work;
    CHECK(work.isValid());
    qputenv("CLIVUS_CACHE_DIR", work.path().toUtf8());
    CHECK(argc > 1);
    const QString corpus = QString::fromUtf8(argv[1]);

    // (a) a PNG written from an image reads back with the same size and pixel content.
    {
        QImage image(64, 48, QImage::Format_Grayscale8);
        image.fill(0);
        for (int y = 0; y < 48; ++y)
            for (int x = 0; x < 64; ++x) image.setPixel(x, y, (x + y) % 256);
        const QString path = work.path() + "/single.png";
        QString error;
        CHECK(exportDisplayedPng(image, path, &error));
        QImage reread;
        CHECK(reread.load(path));
        CHECK(reread.size() == image.size());
        CHECK(reread.convertToFormat(QImage::Format_Grayscale8) == image.convertToFormat(QImage::Format_Grayscale8));
    }

    // (b) an empty image is refused with a reason, not written as a black file.
    {
        QString error;
        CHECK(!exportDisplayedPng(QImage(), work.path() + "/empty.png", &error));
        CHECK(!error.isEmpty());
        CHECK(!QFile::exists(work.path() + "/empty.png"));
    }

    // (c) a whole series exports one PNG per frame, at the requested size.
    {
        const auto series = clivus::scanDirectory(corpus.toStdString());
        CHECK(!series.empty());
        const clivus::SeriesInfo* target = nullptr;
        for (const auto& s : series)
            if (s.totalFrames() > 1) { target = &s; break; }
        if (target == nullptr) target = &series.front();
        const QString folder = work.path() + "/series";
        const SeriesExportResult result =
            exportSeriesPngs(*target, 135.0, 2063.0, folder, [] { return false; });
        CHECK(result.error.isEmpty());
        CHECK(result.written == target->totalFrames());
        CHECK(result.failed == 0);
        const QStringList files = QDir(folder).entryList({"*.png"}, QDir::Files, QDir::Name);
        CHECK(files.size() == target->totalFrames());
        QImage first;
        CHECK(first.load(folder + "/" + files.first()));
        CHECK(first.width() == target->width && first.height() == target->height);
    }

    // (d) cancellation stops early and says so.
    {
        const auto series = clivus::scanDirectory(corpus.toStdString());
        CHECK(!series.empty());
        const QString folder = work.path() + "/series-cancelled";
        int calls = 0;
        const SeriesExportResult result =
            exportSeriesPngs(series.front(), 100.0, 400.0, folder, [&calls] { return ++calls > 1; });
        CHECK(result.cancelled);
        CHECK(result.written <= 1);
    }

    // (e) the de-identified copy through the dialog: report says verified, and the files are there.
    {
        DeidDialog dialog(corpus);
        dialog.show();
        const QString destination = work.path() + "/deid";
        QDir().mkpath(destination);

        const auto edits = dialog.findChildren<QLineEdit*>();
        CHECK(!edits.isEmpty());
        edits.first()->setText(destination);   // the only line edit: the destination

        QPushButton* run = nullptr;
        for (QPushButton* button : dialog.findChildren<QPushButton*>())
            if (button->text().contains("Export de-identified")) run = button;
        CHECK(run != nullptr);
        run->click();

        QPlainTextEdit* report = dialog.findChild<QPlainTextEdit*>();
        CHECK(report != nullptr);
        CHECK(waitFor([&] { return report->toPlainText().contains("Verification"); }, 30000));
        const QString text = report->toPlainText();
        CHECK(text.contains("Verification: PASSED"));
        CHECK(!text.contains("FAILED"));
        const QStringList written = QDir(destination).entryList(QDir::Files);
        CHECK(!written.isEmpty());
        // The report must carry the two limits, not only the success line.
        CHECK(text.contains("burned-in", Qt::CaseInsensitive));
        CHECK(text.contains("not a legal", Qt::CaseInsensitive));
    }

    std::printf("export: all checks passed (single PNG, series PNGs, cancellation, de-identified copy)\n");
    return 0;
}
