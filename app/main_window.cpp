// SPDX-License-Identifier: GPL-3.0-or-later
#include "main_window.h"

#include <QAction>
#include <QDir>
#include <QFileDialog>
#include <QIcon>
#include <QImage>
#include <QLabel>
#include <QListWidget>
#include <QMenuBar>
#include <QPixmap>
#include <QProgressBar>
#include <QPushButton>
#include <QSplitter>
#include <QStatusBar>
#include <QTimer>
#include <QtConcurrent/QtConcurrentMap>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <cstdio>

#include "canvas_widget.h"

namespace {

// Worker thread: decode a series' first object and squeeze it to list-icon size. QImage is safe to
// build off the GUI thread; only the QPixmap/QIcon step has to happen back on it.
QImage thumbnailImage(const QString& file)
{
    clivus::ImageFrame frame;
    if (file.isEmpty() || !clivus::loadFrame(file.toStdString(), frame)) return {};
    std::vector<uint8_t> bytes(frame.byteCount());
    double c = 0.0, w = 1.0;
    frame.fullWindow(c, w);
    frame.render(bytes.data(), c, w);
    // A colour frame is three bytes per pixel; a grey one is a single byte.
    const QImage::Format format =
        frame.isColour() ? QImage::Format_RGB888 : QImage::Format_Grayscale8;
    // Qt keeps ownership of the buffer via the deleter, so copy the row bytes.
    QImage img(bytes.data(), frame.width, frame.height, frame.width * frame.channels, format);
    return img.copy().scaled(72, 72, Qt::KeepAspectRatio, Qt::SmoothTransformation);
}

QString seriesLabel(const clivus::SeriesInfo& s)
{
    // Frames only when they say something the object count does not: a 15-frame RT Dose is one object
    // and fifteen slices, and the list should not pretend otherwise.
    const int frames = s.totalFrames();
    const QString count = frames > int(s.files.size())
                              ? QString("%1 objects · %2 frames").arg(s.files.size()).arg(frames)
                              : QString("%1 objects").arg(s.files.size());
    return QString("%1 · %2\n%3 · %4×%5")
        .arg(QString::fromStdString(s.modality))
        .arg(QString::fromStdString(s.seriesDescription.empty() ? s.studyDescription
                                                              : s.seriesDescription))
        .arg(count)
        .arg(s.width)
        .arg(s.height);
}

}  // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle("Clivus — DICOM viewer (research use only)");
    resize(1280, 860);

    m_seriesList = new QListWidget(this);
    m_seriesList->setIconSize(QSize(72, 72));
    m_seriesList->setUniformItemSizes(false);
    m_canvas = new CanvasWidget(this);

    auto* splitter = new QSplitter(Qt::Horizontal, this);
    splitter->addWidget(m_seriesList);
    splitter->addWidget(m_canvas);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({320, 900});
    setCentralWidget(splitter);

    m_status = new QLabel("Open a folder of DICOM files (Ctrl+O)", this);
    statusBar()->addWidget(m_status, 1);

    m_progress = new QProgressBar(this);
    m_progress->setMaximumWidth(180);
    m_progress->setAlignment(Qt::AlignCenter);
    m_progress->hide();
    statusBar()->addPermanentWidget(m_progress);

    m_cancel = new QPushButton("Cancel", this);
    m_cancel->setToolTip("Stop scanning and stop decoding thumbnails (Esc)");
    m_cancel->setShortcut(Qt::Key_Escape);
    m_cancel->hide();
    statusBar()->addPermanentWidget(m_cancel);
    connect(m_cancel, &QPushButton::clicked, this, &MainWindow::cancelLoading);

    // One thread for the work the user is waiting on. Reading it from a timer, not from the worker,
    // keeps the scan from having to talk to widgets at all.
    m_foreground.setMaxThreadCount(1);
    m_scanTicker = new QTimer(this);
    m_scanTicker->setInterval(150);
    connect(m_scanTicker, &QTimer::timeout, this, &MainWindow::updateScanProgress);

    m_scanWatcher = new QFutureWatcher<ScanResult>(this);
    connect(m_scanWatcher, &QFutureWatcherBase::finished, this, &MainWindow::scanFinished);

    m_thumbWatcher = new QFutureWatcher<QImage>(this);
    connect(m_thumbWatcher, &QFutureWatcherBase::resultReadyAt, this,
            [this](int) { fillReadyThumbnails(); });
    connect(m_thumbWatcher, &QFutureWatcherBase::finished, this, [this] {
        fillReadyThumbnails();
        setIdleProgress();
        if (!m_sliceInFlight) updateInfo();   // do not talk over a decode in progress
    });

    m_sliceWatcher = new QFutureWatcher<SliceResult>(this);
    connect(m_sliceWatcher, &QFutureWatcherBase::finished, this, &MainWindow::sliceFinished);

    auto* open = new QAction("&Open folder…", this);
    open->setShortcut(QKeySequence::Open);
    connect(open, &QAction::triggered, this, [this] {
        const QString dir = QFileDialog::getExistingDirectory(this, "DICOM folder");
        if (!dir.isEmpty()) openFolder(dir);
    });
    auto* quit = new QAction("&Quit", this);
    quit->setShortcut(QKeySequence::Quit);
    connect(quit, &QAction::triggered, this, &QWidget::close);
    menuBar()->addMenu("&File")->addActions({open, quit});

    connect(m_seriesList, &QListWidget::currentRowChanged, this, &MainWindow::showSeries);
    connect(m_canvas, &CanvasWidget::scrollRequested, this, &MainWindow::showSlice);
    connect(m_canvas, &CanvasWidget::windowChanged, this, [this](double c, double w) {
        if (m_currentSeries < 0) return;
        m_windowLevel = QString("WC %1  WW %2").arg(c, 0, 'f', 1).arg(w, 0, 'f', 1);
        updateInfo();
    });
    connect(m_canvas, &CanvasWidget::frameTiming, this,
            [this](double windowMs, double paintMs, double fps) {
                m_timing = QString("window %1 ms | paint %2 ms | %3 fps")
                               .arg(windowMs, 0, 'f', 2)
                               .arg(paintMs, 0, 'f', 2)
                               .arg(fps, 0, 'f', 0);
                updateInfo();
            });
}

void MainWindow::openFolder(const QString& dir)
{
    // Everything already in flight belongs to the folder we are leaving: the scan is told to stop,
    // the thumbnails are cancelled before they start, and m_epoch retires any slice decode.
    ++m_epoch;
    if (m_scanControl) m_scanControl->cancel = true;
    if (m_thumbWatcher->isRunning()) m_thumbWatcher->cancel();

    m_folder = dir;
    m_series.clear();
    m_seriesList->clear();
    m_currentSeries = -1;
    m_currentSlice = -1;
    m_desiredSlice = -1;
    m_timing.clear();
    m_windowLevel.clear();
    m_canvas->setFrame(clivus::ImageFrame{});   // black, rather than the old folder's image
    startScan();
}

void MainWindow::startScan()
{
    auto control = std::make_shared<clivus::ScanControl>();
    m_scanControl = control;

    m_progress->setRange(0, 0);          // indeterminate: a scan has no total until it is done
    m_progress->setFormat(QString());
    m_progress->show();
    m_cancel->show();
    updateScanProgress();
    m_scanTicker->start();

    const std::string folder = m_folder.toStdString();
    m_scanWatcher->setFuture(QtConcurrent::run(&m_foreground, [folder, control] {
        ScanResult r;
        r.series = clivus::scanDirectoryCached(folder, control.get());
        r.cancelled = control->cancel.load(std::memory_order_relaxed);
        return r;
    }));
}

void MainWindow::updateScanProgress()
{
    if (!m_scanControl) return;
    m_status->setText(QString("Scanning %1… %2 entries")
                          .arg(QDir::toNativeSeparators(m_folder))
                          .arg(m_scanControl->examined.load(std::memory_order_relaxed)));
}

void MainWindow::scanFinished()
{
    m_scanTicker->stop();
    ScanResult r = m_scanWatcher->result();
    if (r.cancelled) {
        setIdleProgress();
        m_status->setText(QString("Scan cancelled — %1").arg(QDir::toNativeSeparators(m_folder)));
        return;
    }

    m_series = std::move(r.series);
    m_seriesList->clear();
    for (const auto& s : m_series) {
        auto* item = new QListWidgetItem(seriesLabel(s), m_seriesList);
        item->setToolTip(QString::fromStdString(s.patientName));
    }

    if (m_series.empty()) {
        setIdleProgress();
        m_status->setText(
            QString("No DICOM pixel data found in %1").arg(QDir::toNativeSeparators(m_folder)));
        return;
    }

    // Thumbnails go first, but nothing waits for them: the selected series' slice is decoded on the
    // one-thread foreground pool, so it cannot end up behind a folder's worth of thumbnail jobs.
    startThumbnails();
    const int row = m_benchFrames > 0 ? std::min(m_benchRow, int(m_series.size()) - 1) : 0;
    m_seriesList->setCurrentRow(row);
}

void MainWindow::startThumbnails()
{
    QList<QString> firstObjects;
    firstObjects.reserve(int(m_series.size()));
    for (const auto& s : m_series)
        firstObjects.push_back(s.files.empty() ? QString() : QString::fromStdString(s.files.front()));

    m_progress->setRange(0, int(firstObjects.size()));
    m_progress->setValue(0);
    m_progress->setFormat("%v / %m thumbnails");
    m_progress->show();
    m_cancel->show();

    // One task per series: the global pool runs as many at a time as there are cores, and the results
    // come back in list order as they land.
    m_thumbWatcher->setFuture(QtConcurrent::mapped(firstObjects, &thumbnailImage));
}

void MainWindow::fillReadyThumbnails()
{
    const QFuture<QImage> future = m_thumbWatcher->future();
    const int count = future.resultCount();
    int ready = 0;
    for (int i = 0; i < count; ++i) {
        if (!future.isResultReadyAt(i)) continue;
        ++ready;
        QListWidgetItem* item = m_seriesList->item(i);
        if (!item || !item->icon().isNull()) continue;   // filled already, or the decode failed
        const QImage image = future.resultAt(i);
        if (image.isNull()) continue;                    // nothing decoded: leave the row iconless
        item->setIcon(QIcon(QPixmap::fromImage(image)));
    }
    if (m_progress->maximum() > 0) m_progress->setValue(ready);
}

// Decodes are strictly one at a time and always chase m_desiredSlice, so spinning the wheel while a
// decode runs costs one more decode after it, not a queue of them. A slice index is a frame index
// across the whole series, so one 15-frame object is 15 steps rather than one.
void MainWindow::pumpSlice()
{
    if (m_sliceInFlight || m_currentSeries < 0 || m_desiredSlice < 0) return;
    const auto& series = m_series[size_t(m_currentSeries)];
    if (m_desiredSlice >= series.totalFrames() || m_desiredSlice == m_currentSlice) return;

    int fileIndex = 0, frameIndex = 0;
    if (!series.locate(m_desiredSlice, fileIndex, frameIndex)) return;
    if (fileIndex < 0 || fileIndex >= int(series.files.size())) return;

    const int index = m_desiredSlice;
    const std::string path = series.files[size_t(fileIndex)];
    const quint64 epoch = m_epoch;
    m_sliceInFlight = true;
    m_sliceWatcher->setFuture(QtConcurrent::run(&m_foreground, [path, index, frameIndex, epoch] {
        SliceResult r;
        r.index = index;
        r.epoch = epoch;
        r.ok = clivus::loadFrame(path, r.frame, frameIndex);
        return r;
    }));
    updateInfo("decoding…");
}

void MainWindow::sliceFinished()
{
    m_sliceInFlight = false;
    SliceResult r = m_sliceWatcher->result();
    // The user can pick another series while a decode is running; that frame belongs to the one we
    // left behind, so drop it and let the pump start the right one.
    if (r.epoch != m_epoch || m_currentSeries < 0) {
        pumpSlice();
        return;
    }
    if (!r.ok) {
        m_status->setText("Failed to decode this slice");
        return;
    }
    // A fast spin leaves the wheel ahead of what is being decoded; painting this one would flash a
    // slice the user has already scrolled past, so let the pump fetch the one they are on.
    if (r.index != m_desiredSlice) {
        pumpSlice();
        return;
    }
    m_currentSlice = r.index;
    m_currentIsColour = r.frame.isColour();
    m_canvas->setFrame(std::move(r.frame));
    updateInfo();
    pumpSlice();     // the wheel may have moved while this decode was running
    startBenchmark();
}

void MainWindow::cancelLoading()
{
    if (m_scanControl) m_scanControl->cancel = true;
    if (m_thumbWatcher->isRunning()) m_thumbWatcher->cancel();   // queued thumbnails are dropped
    setIdleProgress();
    if (m_scanWatcher->isRunning()) {
        m_status->setText("Cancelling — the scan stops after the object it is reading");
        return;
    }
    if (m_series.empty()) {
        m_status->setText("Cancelled");
        return;
    }
    updateInfo();
}

void MainWindow::setIdleProgress()
{
    m_progress->hide();
    m_cancel->hide();
}

// Drives the canvas through N repaints and reports the distribution, so the fps claim
// can be checked without a human dragging the mouse. Called again when a decode lands, because the
// folder is scanned in the background and the first frame may not be up yet when --bench asks.
void MainWindow::runBenchmark(int frames, int seriesRow)
{
    if (frames <= 0) return;
    m_benchFrames = frames;
    m_benchRow = seriesRow;
    if (m_seriesList->count() > 0)
        m_seriesList->setCurrentRow(std::min(seriesRow, m_seriesList->count() - 1));
    startBenchmark();
}

void MainWindow::startBenchmark()
{
    if (m_benchFrames <= 0 || m_currentSeries < 0 || m_currentSlice < 0 || m_sliceInFlight) return;
    const int frames = m_benchFrames;
    m_benchFrames = 0;   // one run per request

    auto windows = std::make_shared<std::vector<double>>();
    auto paints = std::make_shared<std::vector<double>>();
    auto fps = std::make_shared<std::vector<double>>();
    connect(m_canvas, &CanvasWidget::frameTiming, this,
            [windows, paints, fps](double w, double p, double f) {
                windows->push_back(w);
                paints->push_back(p);
                fps->push_back(f);
            });

    auto* timer = new QTimer(this);
    timer->setInterval(0);
    connect(timer, &QTimer::timeout, this, [this, timer, frames, windows, paints, fps] {
        if (int(paints->size()) < frames) {
            // Nudge the window every frame, so each repaint pays the real drag cost:
            // full re-window into 8 bit + texture upload + draw.
            const double w = m_series[size_t(m_currentSeries)].width;
            m_canvas->setWindowLevel(-400.0 + double(paints->size() % 2), w * 0.5);
            return;
        }
        timer->stop();
        auto median = [](std::vector<double> v) {
            if (v.empty()) return 0.0;
            std::sort(v.begin(), v.end());
            return v[v.size() / 2];
        };
        std::printf("bench %s %dx%d: %zu frames | window median %.2f ms | paint median %.2f ms | "
                    "fps median %.0f\n",
                    m_series[size_t(m_currentSeries)].seriesDescription.c_str(),
                    m_series[size_t(m_currentSeries)].width, m_series[size_t(m_currentSeries)].height,
                    paints->size(), median(*windows), median(*paints), median(*fps));
        std::fflush(stdout);
        close();
    });
    timer->start();
}

void MainWindow::showSeries(int index)
{
    if (index < 0 || index >= int(m_series.size())) return;
    const auto& files = m_series[size_t(index)].files;
    // The index is a LEFT JOIN, so a series row can come back with no files at all: front() on it
    // would be undefined behaviour, and there is nothing to show anyway.
    if (files.empty()) {
        m_currentSeries = -1;
        m_currentSlice = -1;
        m_desiredSlice = -1;
        m_status->setText("This series has no objects in the index");
        return;
    }

    ++m_epoch;              // a decode still running belongs to the series we are leaving
    m_currentSeries = index;
    m_currentSlice = -1;    // nothing decoded for this series yet
    m_timing.clear();
    m_windowLevel.clear();
    m_canvas->setFrame(clivus::ImageFrame{});
    m_desiredSlice = 0;
    pumpSlice();
}

void MainWindow::showSlice(int delta)
{
    if (m_currentSeries < 0) return;
    const auto& series = m_series[size_t(m_currentSeries)];
    const int total = series.totalFrames();
    if (total <= 0) return;   // clamp() would give an upper bound of -1 here
    // Step from where the wheel last pointed, not from what is on screen, so a fast spin does not
    // keep restarting from the same slice.
    const int from = std::max(m_sliceInFlight ? m_desiredSlice : m_currentSlice, 0);
    const int next = std::clamp(from + delta, 0, total - 1);
    if (next == m_desiredSlice && (m_sliceInFlight || next == m_currentSlice)) return;
    m_desiredSlice = next;
    pumpSlice();
}

void MainWindow::updateInfo(const QString& extra)
{
    if (m_currentSeries < 0) return;
    const auto& s = m_series[size_t(m_currentSeries)];
    const int total = s.totalFrames();
    const QString position = m_currentSlice < 0
                                 ? QString("… / %1").arg(total)
                                 : QString("%1 / %2").arg(m_currentSlice + 1).arg(total);
    // A slice inside a multi-frame object says which frame it is: "3 / 17" alone would hide that the
    // viewer is walking a cine rather than a stack of files.
    QString frameNote;
    int fileIndex = 0, frameIndex = 0;
    if (m_currentSlice >= 0 && s.locate(m_currentSlice, fileIndex, frameIndex)) {
        const int frames = fileIndex < int(s.frameCounts.size()) ? s.frameCounts[size_t(fileIndex)] : 1;
        if (frames > 1) frameNote = QString("   frame %1/%2").arg(frameIndex + 1).arg(frames);
    }
    QString text = QString("%1 — %2  [%3]   %4   %5")
                       .arg(QString::fromStdString(s.patientName))
                       .arg(position)
                       .arg(QString::fromStdString(s.seriesDescription))
                       .arg(m_windowLevel)
                       .arg(m_timing);
    text += frameNote;
    if (!extra.isEmpty()) text += "   " + extra;
    m_status->setText(text);
}
