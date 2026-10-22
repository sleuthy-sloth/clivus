// SPDX-License-Identifier: GPL-3.0-or-later
#include "main_window.h"

#include <QAction>
#include <QDir>
#include <QFileDialog>
#include <QImage>
#include <QLabel>
#include <QListWidget>
#include <QMenuBar>
#include <QSplitter>
#include <QStatusBar>
#include <QTimer>

#include <algorithm>
#include <cstdio>

#include "canvas_widget.h"

namespace {
QIcon thumbnailFor(const clivus::SeriesInfo& series)
{
    clivus::ImageFrame frame;
    if (series.files.empty() || !clivus::loadFrame(series.files.front(), frame)) return {};
    std::vector<uint8_t> bytes(frame.pixels.size());
    double c = 0.0, w = 1.0;
    frame.fullWindow(c, w);
    frame.render(bytes.data(), c, w);
    // Qt keeps ownership of the buffer via the deleter, so copy the row bytes.
    QImage img(bytes.data(), frame.width, frame.height, frame.width, QImage::Format_Grayscale8);
    return QIcon(QPixmap::fromImage(img.copy().scaled(72, 72, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
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
    // shortcut: synchronous scan on the UI thread; move to a worker when folders get big.
    setCursor(Qt::WaitCursor);
    m_series = clivus::scanDirectoryCached(dir.toStdString());
    m_seriesList->clear();
    for (const auto& s : m_series) {
        const QString label = QString("%1 · %2\n%3 objects · %4×%5")
                                  .arg(QString::fromStdString(s.modality))
                                  .arg(QString::fromStdString(s.seriesDescription.empty() ? s.studyDescription
                                                                                         : s.seriesDescription))
                                  .arg(s.files.size())
                                  .arg(s.width)
                                  .arg(s.height);
        auto* item = new QListWidgetItem(thumbnailFor(s), label, m_seriesList);
        item->setToolTip(QString::fromStdString(s.patientName));
    }
    unsetCursor();

    if (m_series.empty()) {
        m_status->setText(QString("No DICOM pixel data found in %1").arg(QDir::toNativeSeparators(dir)));
        return;
    }
    m_seriesList->setCurrentRow(0);
}

// Drives the canvas through N repaints and reports the distribution, so the fps claim
// can be checked without a human dragging the mouse.
void MainWindow::runBenchmark(int frames, int seriesRow)
{
    if (m_seriesList->count() == 0) return;
    m_seriesList->setCurrentRow(std::min(seriesRow, m_seriesList->count() - 1));

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
    m_currentSeries = index;
    m_currentSlice = 0;

    clivus::ImageFrame frame;
    if (!clivus::loadFrame(m_series[index].files.front(), frame)) {
        m_status->setText("Failed to decode the first object of this series");
        return;
    }
    m_canvas->setFrame(std::move(frame));
    m_timing.clear();
    updateInfo();
}

void MainWindow::showSlice(int delta)
{
    if (m_currentSeries < 0) return;
    const auto& files = m_series[size_t(m_currentSeries)].files;
    const int next = std::clamp(m_currentSlice + delta, 0, int(files.size()) - 1);
    if (next == m_currentSlice) return;

    clivus::ImageFrame frame;
    if (!clivus::loadFrame(files[size_t(next)], frame)) return;
    m_currentSlice = next;
    m_canvas->setFrame(std::move(frame));
    updateInfo();
}

void MainWindow::updateInfo(const QString& extra)
{
    if (m_currentSeries < 0) return;
    const auto& s = m_series[size_t(m_currentSeries)];
    QString text = QString("%1 — %2 / %3  [%4]   %5   %6")
                       .arg(QString::fromStdString(s.patientName))
                       .arg(m_currentSlice + 1)
                       .arg(s.files.size())
                       .arg(QString::fromStdString(s.seriesDescription))
                       .arg(m_windowLevel)
                       .arg(m_timing);
    if (!extra.isEmpty()) text += "   " + extra;
    m_status->setText(text);
}
