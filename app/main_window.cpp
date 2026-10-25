// SPDX-License-Identifier: GPL-3.0-or-later
#include "main_window.h"
#ifdef CLIVUS_HAVE_VTK
#include "vtk_mpr_dialog.h"
#endif

#include "about_dialog.h"
#include "annotation_store.h"
#include "deid_dialog.h"
#include "export_images.h"
#include "server_dialog.h"
#include "series_summary.h"
#include "branding.h"

#include "clivus/version.h"

#include <QAction>
#include <QActionGroup>
#include <QDir>
#include <QFileDialog>
#include <QPointer>
#include <QProgressDialog>
#include <QSignalBlocker>
#include <QTextStream>
#include <QTimer>
#include <QtConcurrent/QtConcurrentRun>

#include <atomic>
#include <memory>
#include <QIcon>
#include <QImage>
#include <QHBoxLayout>
#include <QLabel>
#include <QEvent>
#include <QElapsedTimer>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QLineEdit>
#include <QListWidget>
#include <QPointer>
#include <QSignalBlocker>
#include <QMenuBar>
#include <QMessageBox>
#include <QPixmap>
#include <QProgressBar>
#include <QPushButton>
#include <QScreen>
#include <QDropEvent>
#include <QMimeData>
#include <QSettings>
#include <QUrl>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QApplication>
#include <QToolBar>
#include <QVBoxLayout>
#include <QTimer>
#include <QtConcurrent/QtConcurrentRun>

#include <atomic>
#include <memory>
#include <QtConcurrent/QtConcurrentMap>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <cstdio>

#include "browser_model.h"
#include "canvas_widget.h"
#include "help_window.h"
#include "style.h"
#include "view_controls.h"

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

}  // namespace

// A group header carries the label twice: the text on screen (which includes the expand/collapse
// chevron) and the plain label, so toggling can redraw the row without rebuilding it.
inline constexpr int kGroupLabelRole = clivus::ui::kGroupKeyRole + 1;

namespace {

// The keys the collapse state is stored under. They are built from the identifiers the tree was
// grouped by, so a rescan or a mid-scan batch produces the same keys and an expansion survives it.
QString patientGroupKey(const std::string& patientKey)
{
    return QStringLiteral("P:") + QString::fromStdString(patientKey);
}

QString studyGroupKey(const std::string& patientKey, const std::string& studyUid)
{
    // U+001F (unit separator) cannot appear in a DICOM UID or a patient ID, so two studies under one
    // patient can never collide on this key even when their descriptions match.
    return QStringLiteral("S:") + QString::fromStdString(patientKey) + QChar(0x1f) +
           QString::fromStdString(studyUid);
}

// The chevron is text, not an icon: it inherits the row's font and colour, scales with the UI text
// size, and needs no resource file. It is the affordance that says "this row folds".
QString groupRowText(bool collapsed, const QString& label)
{
    return (collapsed ? QStringLiteral("\u25B8 ") : QStringLiteral("\u25BE ")) + label;
}

}  // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle("Clivus — DICOM viewer (research use only)");
    resize(1280, 860);

    m_seriesList = new QListWidget(this);
    m_seriesList->setIconSize(QSize(clivus::style::kThumbSize, clivus::style::kThumbSize));
    m_seriesList->setUniformItemSizes(false);
    m_seriesList->setWordWrap(true);                 // long descriptions wrap instead of clipping
    m_seriesList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_seriesList->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);

    // Roadmap 3.2: search over the series browser. Mechanism: the rows are already built (with
    // thumbnails), so the filter only hides and shows them — it never rebuilds, re-sorts or touches
    // the background thumbnail/slice loading. Matching is case-insensitive substring against the
    // row's own visible text, which is exactly the description + modality/count/dimensions label the
    // user is reading. When everything is filtered out one disabled "(no matching series)" row
    // appears; when the box is cleared the list returns to all rows with the selection unchanged.
    // The line edit sits above the list inside the same splitter pane, so it is in the natural tab
    // focus order (search box -> list) and Ctrl+F jumps straight to it.
    m_seriesSearch = new QLineEdit(this);
    m_seriesSearch->setPlaceholderText("Search series…");
    m_seriesSearch->setClearButtonEnabled(true);
    m_seriesSearch->setToolTip("Filter the series list by description or modality (Ctrl+F)");
    // Roadmap 3.4: Escape inside the box is a two-step undo — first clear the query, then hand
    // focus back to the list. Handled by an event filter rather than a shortcut so it can't fire
    // while the user is in ANY other widget.
    m_seriesSearch->installEventFilter(this);
    // Ctrl+F focus shortcut: QLineEdits carry no shortcuts of their own; the shortcut lives on a
    // window-level QAction that just puts keyboard focus into the box.
    auto* findSeries = new QAction(this);
    findSeries->setShortcut(QKeySequence::Find);   // Ctrl+F
    connect(findSeries, &QAction::triggered, m_seriesSearch,
            [this]() { m_seriesSearch->setFocus(Qt::ShortcutFocusReason); });
    addAction(findSeries);
    connect(m_seriesSearch, &QLineEdit::textChanged, this, [this](const QString&) {
        // Placeholder auto-clears on typing-empty: an empty query shows every row again.
        applySeriesFilter();
    });

    m_canvas = new CanvasWidget(this);
    // The gestures are fixed regardless of the highlighted tool, so they are stated once here
    // (roadmap 2.1) instead of being implied by whichever action happens to be checked.
    connect(m_canvas, &CanvasWidget::pixelInfoChanged, this,
            [this](const CanvasWidget::PixelInfo& info) {
                if (!m_pixelReadout) return;
                if (!info.valid) {
                    m_pixelReadout->clear();
                    return;
                }
                QString text = QString("r%1 c%2").arg(info.row).arg(info.col);
                if (info.storedKnown)
                    text += QString("   %1").arg(QString::number(info.stored, 'g', 6));
                text += QString("   %1").arg(QString::number(info.modality, 'g', 6));
                if (!info.unit.isEmpty()) text += " " + info.unit;
                m_pixelReadout->setText(text);
            });

    m_canvas->setToolTip("Left-drag = window/level · Right-drag = pan · Double-click or R = reset view "
                         "· Wheel = slices · Ctrl+wheel = zoom");
    m_sliceBar = new SliceBar(this);
    m_windowBar = new WindowLevelBar(this);
    m_presetBar = new WindowPresetBar(this);

    // One options bar: numeric window/level, then the preset selector, on the line above the image.
    auto* optionsBar = new QWidget(this);
    auto* optionsLayout = new QHBoxLayout(optionsBar);
    optionsLayout->setContentsMargins(4, 0, 8, 0);
    optionsLayout->setSpacing(clivus::style::kSpacing);
    optionsLayout->addWidget(m_windowBar, 0);
    optionsLayout->addWidget(m_presetBar, 0);
    optionsLayout->addStretch(1);

    // Canvas plus the slice strip under it: one viewer page inside the splitter, so the sidebar
    // splitter still divides browser and image as before.
    auto* viewer = new QWidget(this);
    auto* viewerLayout = new QVBoxLayout(viewer);
    viewerLayout->setContentsMargins(0, 0, 0, 0);
    viewerLayout->setSpacing(0);
    // Options bar above the image, slice strip below it: the two controls the user reaches for most
    // often sit next to the picture rather than in a dialog (roadmap 2.2/2.5).
    viewerLayout->addWidget(optionsBar, 0);
    viewerLayout->addWidget(m_canvas, 1);
    viewerLayout->addWidget(m_sliceBar, 0);

    m_splitter = new QSplitter(Qt::Horizontal, this);
    // The search box and the list are one browser pane (roadmap 3.2): the box filters the list
    // below it, and the splitter divides that pane from the viewer as before.
    auto* sidebarPane = new QWidget(this);
    auto* sidebarLayout = new QVBoxLayout(sidebarPane);
    sidebarLayout->setContentsMargins(0, 0, 0, 0);
    sidebarLayout->setSpacing(0);
    sidebarLayout->addWidget(m_seriesSearch, 0);
    sidebarLayout->addWidget(m_seriesList, 1);
    m_splitter->addWidget(sidebarPane);
    m_splitter->addWidget(viewer);
    m_splitter->setStretchFactor(1, 1);
    m_splitter->setSizes({320, 900});

    m_status = new QLabel("Ready", this);
    statusBar()->addWidget(m_status, 1);

    // Pixel readout (roadmap: pixel pointer). Row/column, then the numbers the file actually supports:
    // stored value, modality value, and a unit only when the object earned one.
    m_pixelReadout = new QLabel(this);
    m_pixelReadout->setTextFormat(Qt::PlainText);
    m_pixelReadout->setToolTip("Pointer position and pixel values under the cursor");
    statusBar()->addPermanentWidget(m_pixelReadout);

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
    // And one thread for speculation, separate so it can never sit in front of a requested frame.
    // The cost is that a prefetch and a requested decode can run at the same time; that is a brief
    // second core of work, against a user-visible stall on every direction reversal.
    m_prefetch.setMaxThreadCount(1);
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

    // The prefetch slot's own watcher: smooth scroll (see presentSlice). Never presents anything
    // itself without the user's target agreeing (onPrefetchLanded checks); shares the foreground
    // pool, so the app stays one decode at a time.
    m_prefetchWatcher = new QFutureWatcher<clivus::ImageFrame>(this);
    connect(m_prefetchWatcher, &QFutureWatcherBase::finished, this,
            &MainWindow::onPrefetchLanded);

    auto* open = new QAction("&Open folder…", this);
    open->setIcon(QIcon::fromTheme("folder-open"));
    open->setToolTip("Choose a folder of DICOM files to scan");
    open->setShortcut(QKeySequence::Open);
    connect(open, &QAction::triggered, this, [this] {
        const QString dir = QFileDialog::getExistingDirectory(this, "DICOM folder");
        if (!dir.isEmpty()) openFolder(dir);
    });
    // ROI edits are written through immediately: a measurement the user can see must survive a
    // crash, and the file is small. The canvas announces the change; the window knows the series.
    connect(m_canvas, &CanvasWidget::roisChanged, this,
            [this] { saveAnnotationsForSlice(m_currentSlice); });

    // Network access (roadmap D1): a DICOMweb study comes down into the cache and then opens
    // through the ordinary scan path — one decode route, not two.
    auto* openFromServer = new QAction("Open from &server…", this);
    openFromServer->setToolTip("Search a DICOMweb (QIDO-RS/WADO-RS) server and download a study");
    connect(openFromServer, &QAction::triggered, this, [this] {
        ServerDialog dialog(this);
        connect(&dialog, &ServerDialog::studyReady, this,
                [this](const QString& folder) { openFolder(folder); });
        dialog.exec();
    });
    auto* quit = new QAction("E&xit", this);
    quit->setShortcut(QKeySequence::Quit);
    connect(quit, &QAction::triggered, this, &QWidget::close);

    // Menu and toolbar share one QAction per command, so a shortcut cannot drift from the button
    // wired to the same thing (roadmap 6.3). Each action drives behaviour that already exists.
    auto* fit = new QAction("Fit &image", this);
    fit->setIcon(QIcon::fromTheme("zoom-fit-best"));
    fit->setToolTip("Scale the image to fit the canvas, keeping the aspect ratio (F)");
    fit->setShortcut(Qt::Key_F);
    connect(fit, &QAction::triggered, m_canvas, &CanvasWidget::fitView);

    // Deliberately no application shortcut on Reset view: the canvas already consumes R
    // (canvas_widget.cpp keyPressEvent) while it has focus, and a second handler at window scope
    // would make the boundary between "canvas focused" and "anywhere" ambiguous (roadmap 3.4).
    // Keyboard users reach reset from this menu via its mnemonic, or R on the canvas, or double-click.
    auto* reset = new QAction("&Reset view", this);
    reset->setIcon(QIcon::fromTheme("view-refresh"));
    reset->setToolTip("Restore default zoom, pan and window/level (double-click or R on the canvas)");
    connect(reset, &QAction::triggered, m_canvas, &CanvasWidget::resetView);

    // Tools (roadmap 2.1/2.6). Both gestures are always live on the canvas; these actions only say
    // which one the UI is presenting, and they are checkable and exclusive so the toolbar, the menu and
    // the cursor can never disagree about the active tool.
    m_windowAction = new QAction("&Window/Level", this);
    m_windowAction->setIcon(QIcon::fromTheme("contrast"));
    m_windowAction->setToolTip("Window/level tool (cross cursor). Left-drag on the image: horizontal = "
                               "width, vertical = centre. Right-drag still pans.");
    m_windowAction->setCheckable(true);
    m_windowAction->setChecked(true);

    m_panAction = new QAction("&Pan", this);
    m_panAction->setIcon(QIcon::fromTheme("transform-move"));
    m_panAction->setToolTip("Pan tool (open-hand cursor). Right-drag on the image to move it; "
                            "left-drag still adjusts window/level.");
    m_panAction->setCheckable(true);

    m_pointerAction = new QAction("&Browse", this);
    auto* pointer = m_pointerAction;
    pointer->setIcon(QIcon::fromTheme("cursor-arrow"));
    pointer->setToolTip("Browse tool (B) — the plain arrow: no adjustment is highlighted, but both "
                        "drags keep working (left = window/level, right = pan) as does the wheel "
                        "and double-click reset");
    pointer->setCheckable(true);

    // The measurement tool (M): its stated purpose takes over the LEFT drag while it is the
    // active tool — one deliberate interaction change, spelled out in the hint text. Everything
    // else (right-drag pan, wheel, keyboard) keeps behaving.
    m_measureAction = new QAction("&Measure", this);
    auto* measure = m_measureAction;
    measure->setIcon(QIcon::fromTheme("applications-graphics"));
    measure->setToolTip("Measurement tool (M) — left-drag draws distance; Shift+click three points "
                        "for an angle (second click = vertex); right-drag still pans");
    measure->setCheckable(true);
    measure->setShortcut(QKeySequence("M"));   // single key like Fit's F; the noting fields never see it

    // ROI tools (roadmap D2). Same one-tool-at-a-time model as Measure: while active, the LEFT drag
    // defines the region; right-drag still pans. Statistics appear on release.
    m_roiRectAction = new QAction("&Rectangle ROI", this);
    m_roiRectAction->setToolTip("Rectangle ROI — left-drag a region to read mean, SD, min, max and "
                                "area; saved with the series");
    m_roiRectAction->setCheckable(true);
    m_roiRectAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+R")));
    m_roiEllipseAction = new QAction("&Ellipse ROI", this);
    m_roiEllipseAction->setToolTip("Ellipse ROI — left-drag an ellipse to read mean, SD, min, max "
                                   "and area; saved with the series");
    m_roiEllipseAction->setCheckable(true);

    m_toolGroup = new QActionGroup(this);
    m_toolGroup->setExclusive(true);
    for (auto* a : {m_windowAction, m_panAction, pointer, measure, m_roiRectAction, m_roiEllipseAction}) {
        a->setActionGroup(m_toolGroup);
        m_toolGroup->addAction(a);
    }
    connect(m_windowAction, &QAction::triggered, this,
            [this] { setActiveTool(ActiveTool::WindowLevel); });
    connect(m_panAction, &QAction::triggered, this, [this] { setActiveTool(ActiveTool::Pan); });
    connect(pointer, &QAction::triggered, this, [this] { setActiveTool(ActiveTool::Pointer); });
    connect(measure, &QAction::triggered, this, [this] { setActiveTool(ActiveTool::Measure); });
    connect(m_roiRectAction, &QAction::triggered, this,
            [this] { setActiveTool(ActiveTool::RoiRect); });
    connect(m_roiEllipseAction, &QAction::triggered, this,
            [this] { setActiveTool(ActiveTool::RoiEllipse); });

    // Cine (roadmap D3). Space plays and pauses; the rate is adjustable without a dialog.
    m_cineAction = new QAction("&Play / pause cine", this);
    m_cineAction->setShortcut(QKeySequence(Qt::Key_Space));
    m_cineAction->setCheckable(true);
    m_cineAction->setToolTip("Loop the current series at 12 frames per second (Space). "
                             "[ and ] change the rate");
    connect(m_cineAction, &QAction::triggered, this, [this] { toggleCine(); });
    auto* slower = new QAction("Slower playback", this);
    slower->setShortcut(QKeySequence(Qt::Key_BracketLeft));
    connect(slower, &QAction::triggered, this, [this] {
        m_cineFps = std::max(2, m_cineFps - 2);
        if (m_cineTimer->isActive()) { m_cineTimer->setInterval(1000 / m_cineFps); }
        statusBar()->showMessage(QString("Cine rate: %1 fps").arg(m_cineFps), 3000);
    });
    auto* faster = new QAction("Faster playback", this);
    faster->setShortcut(QKeySequence(Qt::Key_BracketRight));
    connect(faster, &QAction::triggered, this, [this] {
        m_cineFps = std::min(30, m_cineFps + 2);
        if (m_cineTimer->isActive()) { m_cineTimer->setInterval(1000 / m_cineFps); }
        statusBar()->showMessage(QString("Cine rate: %1 fps").arg(m_cineFps), 3000);
    });
    addAction(slower);
    addAction(faster);

    m_cineTimer = new QTimer(this);
    m_cineTimer->setInterval(1000 / m_cineFps);
    connect(m_cineTimer, &QTimer::timeout, this, &MainWindow::cineTick);

    auto* sidebar = new QAction("&Series sidebar", this);
    sidebar->setToolTip("Show or hide the series browser (Ctrl+B)");
    sidebar->setCheckable(true);
    sidebar->setChecked(true);
    sidebar->setShortcut(QKeySequence("Ctrl+B"));
    connect(sidebar, &QAction::toggled, this, [this](bool on) {
        m_seriesList->setVisible(on);
        // Give the reclaimed space to the canvas: the image is the point.
        m_splitter->setSizes(on ? QList<int>{320, 900} : QList<int>{0, 1220});
    });

    // Roadmap 1.5: timings are development information, not primary UI. Session-only by design —
    // nothing is persisted, so a fresh session starts clean.
    auto* diagnostics = new QAction("Show &diagnostics", this);
    m_diagnosticsAction = diagnostics;   // kept for save/restore/reset of layout (roadmap 3.1)
    diagnostics->setToolTip("Show render timing details in the image overlay (remembered across sessions) (Ctrl+D)");
    diagnostics->setCheckable(true);
    diagnostics->setShortcut(QKeySequence("Ctrl+D"));
    connect(diagnostics, &QAction::toggled, this, [this](bool on) {
        m_showDiagnostics = on;
        updateInfo();
    });

    auto* manual = new QAction("&User manual", this);
    manual->setToolTip("Open the Clivus user manual");
    manual->setShortcut(QKeySequence::HelpContents);   // F1
    connect(manual, &QAction::triggered, this, &MainWindow::showManual);

    auto* about = new QAction("&About Clivus", this);
    about->setToolTip("Version, licence and research-use notice");
    connect(about, &QAction::triggered, this, &MainWindow::showAbout);

    auto* file = menuBar()->addMenu("&File");
    file->addAction(open);
    file->addAction(openFromServer);
    auto* exportImage = new QAction("Export &image as PNG…", this);
    exportImage->setToolTip("Write what the canvas is showing, at the current window/level and zoom, "
                            "with no overlay text");
    connect(exportImage, &QAction::triggered, this, &MainWindow::exportCurrentImage);
    file->addAction(exportImage);

    auto* exportSeries = new QAction("Export &series as PNGs…", this);
    exportSeries->setToolTip("Write every slice of the open series as a PNG at the current "
                             "window/level");
    connect(exportSeries, &QAction::triggered, this, &MainWindow::exportSeriesImages);
    file->addAction(exportSeries);

    auto* exportDeid = new QAction("Export &de-identified copy…", this);
    exportDeid->setToolTip("Write a copy of the open folder with patient identity removed from the "
                           "metadata, then verify it");
    connect(exportDeid, &QAction::triggered, this, &MainWindow::exportDeidentifiedCopy);
    file->addAction(exportDeid);

    auto* exportAction = new QAction("&Export measurements…", this);
    exportAction->setToolTip("Write the open series' saved ROIs to a CSV file");
    connect(exportAction, &QAction::triggered, this, &MainWindow::exportMeasurements);
    file->addAction(exportAction);
    file->addSeparator();
    file->addAction(quit);

    auto* view = menuBar()->addMenu("&View");
    view->addAction(fit);
    view->addAction(reset);
    view->addSeparator();
    view->addAction(sidebar);
    view->addSeparator();
    view->addAction(diagnostics);
    view->addSeparator();
    // Roadmap 3.1: an escape hatch from a layout the user saved by accident. It restores the
    // built-in defaults from the same constants construction uses, then stores them so a later
    // close cannot resurrect the layout the user just threw away.
    auto* resetLayoutAction = new QAction("Re&set layout", this);
    resetLayoutAction->setToolTip("Restore the built-in window layout (size, splitter, sidebar, diagnostics)");
    connect(resetLayoutAction, &QAction::triggered, this, &MainWindow::resetLayout);
    view->addAction(resetLayoutAction);
    view->addSeparator();
    // The same QAction instances the toolbar shows, in one exclusive group (roadmap 6.3).
    auto* tools = view->addMenu("&Tools");
    tools->addAction(m_windowAction);
    tools->addAction(m_panAction);
    tools->addAction(pointer);
    tools->addAction(measure);

    auto* clearMeasure = new QAction("Clear &measurements", this);
    clearMeasure->setToolTip("Remove every drawn measurement (this slice and slice re-decodes always start clean)");
    connect(clearMeasure, &QAction::triggered, this, [this] { m_canvas->clearMeasurements(); });
    tools->addAction(clearMeasure);

    // First-round MPR: opt-in, labelled experimental, disabled honestly when the study reports no
    // geometry (the entry knows the settled series rather than promising a broken window).
    m_mprAction = new QAction("MPR (&research/experimental)", this);
    m_mprAction->setEnabled(false);
    m_mprAction->setToolTip("Three orthogonal planes (axial, coronal, sagittal) resliced through "
                            "the scanner's own geometry. First round: nearest-neighbour, "
                            "no interpolation across slices. Needs per-slice geometry.");
    connect(m_mprAction, &QAction::triggered, this, &MainWindow::openMprView);
    view->addAction(m_mprAction);
#ifdef CLIVUS_HAVE_VTK
    // VTK view: the Phase 3/4 dialog (three VTK planes + volume rendering)
    // for the same series. Same eligibility rule as MPR — no usable
    // geometry, no window that would render positions it does not have.
    m_vtkAction = new QAction("VTK view (&research/experimental)", this);
    m_vtkAction->setEnabled(false);
    m_vtkAction->setToolTip("Three VTK planes with a linked crosshair plus GPU volume rendering, "
                            "for the current series. Experimental: the planes use VTK's window/level "
                            "mapping, and the main canvas stays the legacy one until pixel parity "
                            "is proven (see docs/VTK-MIGRATION.md).");
    connect(m_vtkAction, &QAction::triggered, this, &MainWindow::openVtkView);
    view->addAction(m_vtkAction);
#endif
    view->addSeparator();

    // Cache is a convenience the user can drop at will (roadmap Phase 1): clearing it only costs a
    // rescan, never data.
    auto* clearCache = new QAction("Clear index &cache", this);
    clearCache->setToolTip("Forget this folder's cached metadata (the next open re-reads the files)");
    connect(clearCache, &QAction::triggered, this, [this] {
        if (m_folder.isEmpty()) return;
        if (clivus::IndexStore::clearCache(m_folder.toStdString()))
            statusBar()->showMessage("Index cache cleared for this folder; the next open rescans it", 5000);
        else
            statusBar()->showMessage("The index cache could not be removed (it may be in use); "
                                     "reopen the folder or close Clivus and delete it from the cache directory", 8000);
    });
    view->addAction(clearCache);

    auto* help = menuBar()->addMenu("&Help");
    help->addAction(manual);
    help->addSeparator();
    help->addAction(about);

    auto* toolbar = addToolBar("main");
    toolbar->setMovable(false);
    toolbar->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    toolbar->setIconSize(QSize(clivus::style::kIconSize, clivus::style::kIconSize));
    for (auto* a : {open, fit}) toolbar->addAction(a);
    toolbar->addSeparator();
    for (auto* a : {m_windowAction, m_panAction, pointer, measure}) toolbar->addAction(a);   // exclusive tool set
    toolbar->addSeparator();
    for (auto* a : {reset}) toolbar->addAction(a);

    setMinimumSize(720, 480);   // narrower windows clip the toolbar and sidebar
    // Empty state (roadmap 1.6): what Clivus does, a prominent way in, and a route to the manual.
    // It is one page of a stack, so the main window never blocks waiting on it.
    m_emptyState = new QWidget(this);
    auto* emptyLayout = new QVBoxLayout(m_emptyState);
    emptyLayout->addStretch(1);
    // Branded welcome: the mark (or the built-in wordmark fallback), then one honest line about
    // what the app is for, then the single obvious way in. The logo is an asset swap-point —
    // see branding.h; nothing here needs touching when the artwork lands.
    emptyLayout->addWidget(clivus::branding::makeBrandHeader(132, m_emptyState), 0,
                           Qt::AlignHCenter);
    auto* tagline = new QLabel(QStringLiteral("DICOM viewing for research and education"),
                               m_emptyState);
    tagline->setAlignment(Qt::AlignCenter);
    tagline->setStyleSheet(QStringLiteral("color:#8a94a0;"));
    emptyLayout->addWidget(tagline);
    auto* blurb = new QLabel(
        "Open a folder of DICOM files to scan it: browse its series on the left, and examine each "
        "slice on the right — window/level, zoom, measurements and multiplanar views included. "
        "You can also drop a folder anywhere on this window.", m_emptyState);
    blurb->setAlignment(Qt::AlignCenter);
    blurb->setWordWrap(true);
    auto* openButton = new QPushButton("Open Folder…", m_emptyState);
    openButton->setToolTip("Choose a folder of DICOM files to scan (Ctrl+O)");
    openButton->setMinimumWidth(180);
    connect(openButton, &QPushButton::clicked, open, &QAction::trigger);
    auto* manualLink = new QLabel(
        QString("<a href=\"#manual\">User manual</a> (F1) explains the interface in detail"),
        m_emptyState);
    manualLink->setAlignment(Qt::AlignCenter);
    connect(manualLink, &QLabel::linkActivated, this, [this] { showManual(); });
    emptyLayout->addSpacing(clivus::style::kSpacing);
    emptyLayout->addWidget(blurb);
    emptyLayout->addSpacing(clivus::style::kSpacing);
    emptyLayout->addWidget(openButton, 0, Qt::AlignCenter);
    emptyLayout->addSpacing(clivus::style::kSpacing);
    emptyLayout->addWidget(manualLink);
    emptyLayout->addStretch(2);

    m_centralStack = new QStackedWidget(this);
    m_centralStack->addWidget(m_emptyState);   // page 0: nothing open yet
    m_centralStack->addWidget(m_splitter);     // page 1: folder open
    setCentralWidget(m_centralStack);
    setAcceptDrops(true);   // dropping a folder or a DICOM file opens it (File > Open equivalence)

    restoreLayout();   // roadmap 3.1: geometry, splitter, sidebar and diagnostics from QSettings

    // The row number is not the series index once a filter hides rows, so the row's UserRole carries
    // the index it stands for. Without this, clicking the first row of a filtered list opened whichever
    // series happened to be index 0.
    connect(m_seriesList, &QListWidget::currentRowChanged, this, [this](int row) {
        // A collapse moves the caret onto the header it just folded. That is a navigation move, not a
        // request to open anything: without this the fold would start a decode of the group's first
        // series and throw away the slice the user was reading.
        if (m_suppressRowActivation) return;
        if (!m_seriesList || row < 0) { showSeries(-1); return; }
        const QListWidgetItem* item = m_seriesList->item(row);
        if (!item) { showSeries(-1); return; }
        int index = item->data(Qt::UserRole).toInt();
        if (index < 0 && !item->data(Qt::UserRole + 1).toBool()) {
            // A group header, not the placeholder: open the first series under it. Selecting the
            // header itself is what a click on the group means.
            for (int i = row + 1; i < m_seriesList->count(); ++i) {
                const int below = m_seriesList->item(i)->data(Qt::UserRole).toInt();
                if (below >= 0) { index = below; break; }
            }
        }
        if (index < 0) { showSeries(-1); return; }
        showSeries(index);
    });
    // Roadmap 3.4: keyboard navigation — arrows move currentRow (loading as it goes, since
    // currentRowChanged drives showSeries), and Return/Enter activates the row under the caret even
    // when it is already current (e.g. after tabbing in with the mouse having selected elsewhere).
    connect(m_seriesList, &QListWidget::activated, this, [this](const QModelIndex& index) {
        if (index.isValid() && m_seriesList->currentRow() != index.row())
            m_seriesList->setCurrentRow(index.row());
    });
    // The viewport receives the mouse, so the fold gesture is watched there; the list receives the
    // keys, so Left and Right can fold and unfold whatever header the caret is on. Neither path goes
    // through the selection: folding a group must not start a decode.
    m_seriesList->viewport()->installEventFilter(this);
    m_seriesList->installEventFilter(this);
    connect(m_canvas, &CanvasWidget::scrollRequested, this, &MainWindow::showSlice);
    connect(m_sliceBar, &SliceBar::sliceRequested, this, &MainWindow::goToSlice);
    // The canvas is the one that changes values by dragging; it reports them here, this is where they
    // become the canonical state, and the fields are refreshed from it (roadmap 2.2/6.3).
    connect(m_canvas, &CanvasWidget::windowChanged, this, [this](double c, double w) {
        if (m_currentSeries < 0) return;
        m_center = c;
        m_width = w;
        m_windowLevel = QString("WC %1  WW %2").arg(c, 0, 'f', 1).arg(w, 0, 'f', 1);
        m_windowBar->setValues(c, w);
        m_presetBar->syncTo(c, w);   // names the preset only while the numbers still match it
        updateInfo();
    });
    connect(m_windowBar, &WindowLevelBar::windowLevelEdited, this, &MainWindow::setWindowLevel);
    connect(m_presetBar, &WindowPresetBar::presetChosen, this, &MainWindow::setWindowLevel);
    connect(m_canvas, &CanvasWidget::viewChanged, this, [this] { updateInfo(); });
    connect(m_canvas, &CanvasWidget::frameTiming, this,
            [this](double windowMs, double paintMs, double fps) {
                m_timing = QString("window %1 ms | paint %2 ms | %3 fps")
                               .arg(windowMs, 0, 'f', 2)
                               .arg(paintMs, 0, 'f', 2)
                               .arg(fps, 0, 'f', 0);
                updateInfo();
            });
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    saveLayout();   // roadmap 3.1: the window arrangement survives a restart
    QMainWindow::closeEvent(event);
}

// Roadmap 3.4: the search box reacts to Escape before QLineEdit's own handling, so the user can
// back out of filtering without no-touching the mouse: first press clears the query, further
// presses return focus to the series list, where arrows continue.
bool MainWindow::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_seriesSearch && event->type() == QEvent::KeyPress) {
        auto* key = static_cast<QKeyEvent*>(event);
        if (key->key() == Qt::Key_Escape) {
            if (!m_seriesSearch->text().isEmpty()) {
                m_seriesSearch->clear();
            } else {
                m_seriesList->setFocus();
            }
            return true;
        }
    }
    // A click on a group header folds or unfolds it, and does nothing else: the selection never moves,
    // so the series on screen (and its slice) stays exactly where it was. Swallowing the press is what
    // keeps QListWidget from making the header current, which would open the group's first series.
    if (watched == m_seriesList->viewport() && event->type() == QEvent::MouseButtonPress) {
        auto* mouse = static_cast<QMouseEvent*>(event);
        if (mouse->button() == Qt::LeftButton) {
            const QListWidgetItem* item = m_seriesList->itemAt(mouse->position().toPoint());
            if (item && item->data(clivus::ui::kHeaderRole).toInt() != 0) {
                toggleGroupCollapse(m_seriesList->row(item));
                return true;
            }
        }
    }
    // Standard disclosure behaviour on the keyboard: Left folds the header under the caret, Right
    // unfolds it. On a series row both keys fall through to QListWidget, so ordinary navigation is
    // untouched.
    if (watched == m_seriesList && event->type() == QEvent::KeyPress) {
        auto* key = static_cast<QKeyEvent*>(event);
        const QListWidgetItem* item = m_seriesList->currentItem();
        const bool onHeader = item && item->data(clivus::ui::kHeaderRole).toInt() != 0;
        if (onHeader && (key->key() == Qt::Key_Left || key->key() == Qt::Key_Right)) {
            const QString groupKey = item->data(clivus::ui::kGroupKeyRole).toString();
            const bool collapsed = m_collapsedGroups.contains(groupKey);
            const bool wantCollapsed = key->key() == Qt::Key_Left;
            if (collapsed != wantCollapsed) toggleGroupCollapse(m_seriesList->row(item));
            return true;
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

// Roadmap 3.1. Only layout is written here, and only the four keys in settings_names.h. Recent
// folders, file paths, and anything derived from patient data are deliberately NOT persisted: a
// viewer of medical images must not leave a trail of what was opened on shared machines. If that
// ever changes, the decision — not just the feature — must be revisited.
void MainWindow::saveLayout()
{
    QSettings s = clivus::settings::clivusSettings();
    s.setValue(clivus::settings::kGeometry, saveGeometry());
    s.setValue(clivus::settings::kSplitter, m_splitter->saveState());
    s.setValue(clivus::settings::kSidebarVisible, m_seriesList->isVisible());
    s.setValue(clivus::settings::kDiagnostics, m_diagnosticsAction->isChecked());
}

namespace {
}  // namespace

void MainWindow::restoreLayout()
{
    QSettings s = clivus::settings::clivusSettings();
    const bool haveAny = s.contains(clivus::settings::kGeometry) ||
                         s.contains(clivus::settings::kSplitter);
    if (haveAny) {
        bool geometryRestored = false;
        const QByteArray geometryBytes = s.value(clivus::settings::kGeometry).toByteArray();
        if (!geometryBytes.isEmpty()) {
            // restoreGeometry() itself validates the byte layout; the post-check below keeps a
            // syntactically valid but degenerate/offscreen rectangle (e.g. saved on a monitor that
            // has since been unplugged) from taking the window where the user cannot reach it.
            const QRect before = geometry();
            if (restoreGeometry(geometryBytes)) {
                const QRect g = frameGeometry();   // includes the frame: compare what the WM gets
                const QList<QScreen*> screens = QGuiApplication::screens();
                QRect joined;
                for (const QScreen* screen : screens)
                    joined = joined.isNull() ? screen->geometry() : joined.united(screen->geometry());
                // Usable means: at least the application's own minimum size, and reasonably inside
                // the attached desktop. A rectangle smaller than that (or parked offscreen, e.g.
                // saved on a monitor that has since been unplugged) is degenerate.
                if (g.width() >= std::max(minimumWidth(), 600) &&
                    g.height() >= std::max(minimumHeight(), 500) &&
                    !screens.isEmpty() && g.intersects(joined))
                    geometryRestored = true;
                else
                    setGeometry(before);   // undo the restore, fall back below
            }
        }
        if (!geometryRestored) resize(clivus::settings::kDefaultWindowSize);

        if (!m_splitter->restoreState(s.value(clivus::settings::kSplitter).toByteArray())) {
            m_splitter->setSizes(clivus::settings::kDefaultSplitterSizes);
        }
        // The persisted sidebar key decides whether the browser pane starts open; the splitter
        // state already carries the pane sizes, so this only completes the collapsed case.
        m_seriesList->setVisible(s.value(clivus::settings::kSidebarVisible, true).toBool());
    } else {
        resize(clivus::settings::kDefaultWindowSize);
    }
    // Diagnostics: persisted as an explicit bool; a missing/corrupt entry falls back to the
    // session default (off), matching the pre-3.1 behaviour.
    m_diagnosticsAction->setChecked(s.value(clivus::settings::kDiagnostics, false).toBool());
}

void MainWindow::resetLayout()
{
    // The same constants construction uses, so the menu and a fresh start agree by construction.
    resize(clivus::settings::kDefaultWindowSize);
    m_splitter->setSizes(clivus::settings::kDefaultSplitterSizes);
    m_seriesList->setVisible(true);
    m_diagnosticsAction->setChecked(false);
    // Store immediately: the reset must not be undone by the close handler writing the pre-reset
    // state back (the closeEvent runs after this and will re-save whatever is now on screen).
    saveLayout();
    statusBar()->showMessage("Layout restored to the built-in default", 5000);
}

// One place that knows the active tool. The checked action is already the single truth in the UI - this
// mirrors it into the cursor and the one-line hint so the three cannot drift (roadmap 2.1/2.6).
// Switching tools changes feedback only; it never disables a gesture, so the user cannot get stuck in a
// mode that does not do what they expect.
bool MainWindow::selectSeriesForCapture(const QString& modality)
{
    for (size_t i = 0; i < m_series.size(); ++i) {
        if (QString::fromStdString(m_series[i].modality).compare(modality, Qt::CaseInsensitive) != 0)
            continue;
        const int row = rowForSeriesIndex(int(i));
        if (row < 0) continue;
        m_seriesList->setCurrentRow(row);   // the normal selection path: shows and decodes it
        return true;
    }
    return false;
}

QImage MainWindow::captureCanvasImage()
{
    return m_canvas ? m_canvas->grabFramebuffer() : QImage();
}

QPoint MainWindow::captureCanvasPosition() const
{
    return m_canvas ? m_canvas->mapTo(this, QPoint(0, 0)) : QPoint();
}

bool MainWindow::canvasReady() const
{
    return m_canvas != nullptr && m_canvas->hasValidFrame();
}

int MainWindow::addRoiForCapture()
{
    if (!m_canvas->hasValidFrame()) return 0;
    // A drag in image space: the middle 40% of the frame, which lands on anatomy in the corpus CT.
    // buildRoi does the statistics against the real pixels, so this is the same code path a mouse
    // drag uses — only the pointer motion is simulated.
    const int rows = m_canvas->frameHeightForCapture();
    const int cols = m_canvas->frameWidthForCapture();
    if (rows <= 0 || cols <= 0) return 0;
    const int r0 = rows / 5, r1 = rows - rows / 5;
    const int c0 = cols / 5, c1 = cols - cols / 5;
    m_canvas->addRoiForCapture(r0, c0, r1, c1);
    saveAnnotationsForSlice(m_currentSlice);
    return int(m_canvas->rois().size());
}

bool MainWindow::captureMprImage(const QString& path)
{
    if (m_currentSeries < 0) return false;
    MprView view(m_series[size_t(m_currentSeries)], this);
    view.show();
    QElapsedTimer clock;
    clock.start();
    while (!view.haveVolumeForTest() && clock.elapsed() < 30000) {
        QApplication::processEvents(QEventLoop::AllEvents, 100);
        QThread::msleep(100);
    }
    if (!view.haveVolumeForTest()) return false;
    // Centre the planes first: a reformat shot pinned at position 1 shows an empty edge plane and
    // reads as a broken window. Then linear + MIP so the new controls are visibly doing something.
    view.centerPlanesForCapture();
    view.setReformatForTest(true, 1, 5.0);
    QApplication::processEvents();
    QThread::msleep(500);
    QApplication::processEvents();
    const bool saved = view.compositeImage().save(path);
    view.close();
    return saved;
}

void MainWindow::toggleCine()
{
    if (m_cineTimer->isActive()) {
        stopCine();
        return;
    }
    if (m_currentSeries < 0) {
        m_cineAction->setChecked(false);
        return;
    }
    const int total = m_series[size_t(m_currentSeries)].totalFrames();
    if (total < 2) {
        statusBar()->showMessage("A single image has nothing to play", 4000);
        m_cineAction->setChecked(false);
        return;
    }
    m_cineTimer->setInterval(1000 / std::max(2, m_cineFps));
    m_cineTimer->start();
    m_cineAction->setChecked(true);
    statusBar()->showMessage(QString("Cine: playing at %1 fps (Space stops)").arg(m_cineFps), 4000);
}

void MainWindow::stopCine(const QString& reason)
{
    const bool wasRunning = m_cineTimer && m_cineTimer->isActive();
    if (m_cineTimer) m_cineTimer->stop();
    if (m_cineAction) m_cineAction->setChecked(false);
    if (wasRunning && !reason.isEmpty()) statusBar()->showMessage(reason, 4000);
}

void MainWindow::cineTick()
{
    if (m_currentSeries < 0) {
        stopCine();
        return;
    }
    const int total = m_series[size_t(m_currentSeries)].totalFrames();
    if (total < 2) {
        stopCine();
        return;
    }
    m_cineAdvancing = true;
    // Advance from what is on screen, so a slow decode stretches the loop instead of skipping frames:
    // cine is a view of the data, and dropping slices would hide them.
    const int next = (m_currentSlice + 1 >= total) ? 0 : m_currentSlice + 1;
    goToSlice(next);
    m_cineAdvancing = false;
}

void MainWindow::openMprView()
{
    if (m_currentSeries < 0 || m_currentSeries >= int(m_series.size())) return;
    auto* view = new MprView(m_series[size_t(m_currentSeries)], this);
    view->show();
    view->raise();
    view->activateWindow();
}

#ifdef CLIVUS_HAVE_VTK
void MainWindow::openVtkView()
{
    if (m_currentSeries < 0 || m_currentSeries >= int(m_series.size())) return;
    auto* view = new VtkMprDialog(m_series[size_t(m_currentSeries)], this);
    view->show();
    view->raise();
    view->activateWindow();
}
#endif

QString MainWindow::currentSeriesUid() const
{
    if (m_currentSeries < 0 || m_currentSeries >= int(m_series.size())) return {};
    return QString::fromStdString(m_series[size_t(m_currentSeries)].seriesUid);
}

void MainWindow::loadAnnotationsForSlice(const int slice)
{
    m_canvas->setRois({});
    if (slice < 0) return;
    const QString uid = currentSeriesUid();
    if (uid.isEmpty()) return;   // no Series Instance UID: nothing can be keyed, so nothing loads
    QString warning;
    const auto stored = AnnotationStore::load(uid, &warning);
    if (!warning.isEmpty()) statusBar()->showMessage(warning, 8000);
    for (const auto& entry : stored) {
        if (entry.slice != slice) continue;
        std::vector<CanvasWidget::RoiAnnotation> rois;
        rois.reserve(size_t(entry.rois.size()));
        for (const auto& roi : entry.rois) rois.push_back(roi);
        m_canvas->setRois(std::move(rois));
        m_canvas->refreshRoiStats();   // numbers come from the pixels, never from the file
        break;
    }
}

void MainWindow::saveAnnotationsForSlice(const int slice)
{
    if (slice < 0) return;
    const QString uid = currentSeriesUid();
    if (uid.isEmpty()) return;
    const auto& rois = m_canvas->rois();
    QVector<CanvasWidget::RoiAnnotation> copy;
    copy.reserve(int(rois.size()));
    for (const auto& roi : rois) copy.append(roi);
    QString error;
    if (!AnnotationStore::setSliceRois(uid, slice, copy, &error))
        statusBar()->showMessage(error, 8000);
}

// One CSV for the open series: every ROI the sidecar holds, with its statistics recomputed against
// the slice's own pixels when that slice is loaded, or left as geometry only when it is not. The file
// carries no patient identifiers — series UID and slice numbers only.
void MainWindow::exportMeasurements()
{
    if (m_currentSeries < 0) return;
    const QString uid = currentSeriesUid();
    const auto stored = AnnotationStore::load(uid, nullptr);
    if (stored.isEmpty()) {
        statusBar()->showMessage("This series has no saved measurements to export", 5000);
        return;
    }
    const QString path = QFileDialog::getSaveFileName(this, "Export measurements", "measurements.csv",
                                                     "CSV (*.csv)");
    if (path.isEmpty()) return;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        statusBar()->showMessage("Could not write the CSV file: " + file.errorString(), 8000);
        return;
    }
    QTextStream out(&file);
    out << "series_uid,slice,shape,row0,col0,row1,col1,pixels,mean,sd,min,max,area_mm2\n";
    int rows = 0;
    for (const auto& entry : stored) {
        for (const auto& roi : entry.rois) {
            // Recompute against the live canvas only for the slice on screen; a stored geometry
            // without pixels is written with empty statistics rather than invented ones.
            QString mean, sd, minimum, maximum, area, pixels;
            if (entry.slice == m_currentSlice) {
                for (const auto& live : m_canvas->rois()) {
                    if (live.row0 == roi.row0 && live.col0 == roi.col0 && live.row1 == roi.row1 &&
                        live.col1 == roi.col1) {
                        mean = QString::number(live.mean, 'f', 2);
                        sd = QString::number(live.stddev, 'f', 2);
                        minimum = QString::number(live.min, 'f', 2);
                        maximum = QString::number(live.max, 'f', 2);
                        area = live.areaKnown ? QString::number(live.areaMm2, 'f', 2) : QString();
                        pixels = QString::number(live.pixels);
                        break;
                    }
                }
            }
            out << QStringLiteral("%1,%2,%3,%4,%5,%6,%7,%8,%9,%10,%11,%12,%13\n")
                       .arg(uid)
                       .arg(entry.slice)
                       .arg(roi.ellipse ? "ellipse" : "rectangle")
                       .arg(roi.row0).arg(roi.col0).arg(roi.row1).arg(roi.col1)
                       .arg(pixels, mean, sd, minimum, maximum, area);
            ++rows;
        }
    }
    file.close();
    statusBar()->showMessage(QString("Exported %1 ROIs to %2").arg(rows).arg(path), 6000);
}

void MainWindow::exportCurrentImage()
{
    if (!m_canvas->hasValidFrame()) {
        statusBar()->showMessage("Nothing to export yet: open a study and wait for an image", 5000);
        return;
    }
    const QString path = QFileDialog::getSaveFileName(
        this, "Export image", QDir(m_folder).filePath("slice.png"), "PNG image (*.png)");
    if (path.isEmpty()) return;
    const QImage image = m_canvas->captureCanvasImageForExport();
    QString error;
    if (exportDisplayedPng(image, path, &error)) {
        statusBar()->showMessage(
            QString("Wrote %1 (%2 x %3, window/level applied, no overlays)")
                .arg(path).arg(image.width()).arg(image.height()), 6000);
    } else {
        statusBar()->showMessage(error, 8000);
    }
}

void MainWindow::exportSeriesImages()
{
    if (m_currentSeries < 0) {
        statusBar()->showMessage("Open a series first", 4000);
        return;
    }
    const QString folder = QFileDialog::getExistingDirectory(this, "Folder for the PNG series");
    if (folder.isEmpty()) return;

    const clivus::SeriesInfo series = m_series[size_t(m_currentSeries)];
    const double center = m_canvas->windowCenter();
    const double width = m_canvas->windowWidth();
    auto cancelled = std::make_shared<std::atomic<bool>>(false);

    auto* progress = new QProgressDialog(QString("Exporting %1 slices…").arg(series.totalFrames()),
                                        "Cancel", 0, 0, this);
    progress->setWindowModality(Qt::WindowModal);
    progress->setMinimumDuration(0);
    progress->show();

    auto* watcher = new QFutureWatcher<SeriesExportResult>(this);
    connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher, progress, folder] {
        const SeriesExportResult result = watcher->result();
        watcher->deleteLater();
        progress->close();
        progress->deleteLater();
        if (!result.error.isEmpty()) {
            statusBar()->showMessage(result.error, 8000);
            return;
        }
        QString message = QString("Wrote %1 PNGs to %2").arg(result.written).arg(folder);
        if (result.failed > 0)
            message += QString("; %1 slices could not be decoded (%2)")
                           .arg(result.failed)
                           .arg(result.failures.value(0));
        if (result.cancelled) message = QString("Export cancelled after %1 files").arg(result.written);
        statusBar()->showMessage(message, 8000);
    });
    connect(progress, &QProgressDialog::canceled, this, [cancelled] { *cancelled = true; });
    watcher->setFuture(QtConcurrent::run([series, center, width, folder, cancelled] {
        return exportSeriesPngs(series, center, width, folder,
                                [cancelled] { return cancelled->load(); });
    }));
}

void MainWindow::exportDeidentifiedCopy()
{
    if (m_folder.isEmpty()) {
        statusBar()->showMessage("Open a folder first", 4000);
        return;
    }
    DeidDialog dialog(m_folder, this);
    dialog.exec();
}

// A folder dropped anywhere on the window opens it; a single DICOM file opens its folder (the scan
// reads every object in the folder; one file alone is not a study).
void MainWindow::dragEnterEvent(QDragEnterEvent* event)
{
    if (event->mimeData()->hasUrls()) event->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent* event)
{
    const QList<QUrl> urls = event->mimeData()->urls();
    if (urls.isEmpty()) return;
    event->acceptProposedAction();
    QString target;
    for (const QUrl& url : urls) {
        const QString local = url.toLocalFile();
        if (QFileInfo(local).isDir()) {
            target = local;
            break;   // a dropped folder is what the user pointed at
        }
        if (QFileInfo(local).isFile() && target.isEmpty())
            target = QFileInfo(local).absolutePath();   // remember the first file's folder
    }
    if (!target.isEmpty()) openFolder(target);
}

void MainWindow::setActiveTool(ActiveTool tool)
{
    m_activeTool = tool;
    QString hint;
    switch (tool) {
    case ActiveTool::Pan:
        if (!m_panAction->isChecked()) m_panAction->setChecked(true);
        m_canvas->setToolHint(CanvasWidget::ToolHint::Pan);
        hint = "Pan tool — right-drag on the image to move it (left-drag still adjusts window/level)";
        break;
    case ActiveTool::Pointer:
        if (!m_pointerAction->isChecked()) m_pointerAction->setChecked(true);
        m_canvas->setToolHint(CanvasWidget::ToolHint::Pointer);
        hint = "Pointer tool — no adjustment highlighted; left-drag = window/level, right-drag = pan";
        break;
    case ActiveTool::Measure:
        if (!m_measureAction->isChecked()) m_measureAction->setChecked(true);
        m_canvas->setToolHint(CanvasWidget::ToolHint::Measure);
        hint = "Measurement tool — left-drag draws a distance; Shift+click three points (second = "
               "vertex) draws an angle. The measurement tool takes over the LEFT drag until you "
               "switch back";
        break;
    case ActiveTool::RoiRect:
        if (!m_roiRectAction->isChecked()) m_roiRectAction->setChecked(true);
        m_canvas->setToolHint(CanvasWidget::ToolHint::RoiRect);
        hint = "Rectangle ROI — left-drag a region for mean, SD, min, max and area; right-drag pans. "
               "Kept with the series";
        break;
    case ActiveTool::RoiEllipse:
        if (!m_roiEllipseAction->isChecked()) m_roiEllipseAction->setChecked(true);
        m_canvas->setToolHint(CanvasWidget::ToolHint::RoiEllipse);
        hint = "Ellipse ROI — left-drag an ellipse for mean, SD, min, max and area; right-drag pans. "
               "Kept with the series";
        break;
    case ActiveTool::WindowLevel:
    default:
        if (!m_windowAction->isChecked()) m_windowAction->setChecked(true);
        m_canvas->setToolHint(CanvasWidget::ToolHint::WindowLevel);
        hint = "Window/level tool — left-drag on the image: horizontal = width, vertical = centre "
               "(right-drag still pans)";
        break;
    }
    // Transient hint, not the persistent state line: the status bar keeps reporting the study.
    statusBar()->showMessage(hint, 5000);
}

void MainWindow::openFolder(const QString& dir)
{
    stopCine();
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
    m_windowBar->setWindowingAvailable(true);
    m_sliceBar->setSliceCount(0);
    m_centralStack->setCurrentIndex(1);         // off the empty state: the folder is being opened
    m_status->setText("Loading series…");
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
    // Every open gets a new epoch: a batch queued by a folder the user has left must not paint over
    // the folder they are looking at now.
    const quint64 epoch = ++m_folderEpoch;
    QPointer<MainWindow> self(this);
    m_scanWatcher->setFuture(QtConcurrent::run(&m_foreground, [folder, control, self, epoch] {
        ScanResult r;
        clivus::ScanProgress progress;
        if (self) {
            // The worker keeps its own records; the UI gets a copy, handed over through the event
            // loop so nothing touches widgets from this thread.
            progress = [self, epoch](const std::vector<clivus::SeriesInfo>& snapshot) {
                std::vector<clivus::SeriesInfo> copy = snapshot;
                QMetaObject::invokeMethod(
                    self,
                    [self, epoch, copy = std::move(copy)]() {
                        if (self) self->applyScanBatch(epoch, copy);
                    },
                    Qt::QueuedConnection);
            };
        }
        r.series = clivus::scanDirectoryCached(folder, control.get(), progress);
        r.cancelled = control->cancel.load(std::memory_order_relaxed);
        return r;
    }));
}

void MainWindow::updateScanProgress()
{
    if (!m_scanControl) return;
    m_status->setText(QString("Loading series… scanning %1 (%2 entries examined so far)")
                          .arg(QDir::toNativeSeparators(m_folder))
                          .arg(m_scanControl->examined.load(std::memory_order_relaxed)));
}

void MainWindow::scanFinished()
{
    m_scanTicker->stop();
    ScanResult r = m_scanWatcher->result();
    if (r.cancelled) {
        setIdleProgress();
        // Batches already shown stay shown: throwing away a nearly finished scan because the user
        // stopped it was the old behaviour and it helped nobody.
        if (!m_series.empty()) {
            m_status->setText(
                QString("Scan cancelled — showing the %1 series found before you stopped it.")
                    .arg(m_series.size()));
        } else {
            m_status->setText(
                QString("Scan cancelled — the folder was left unread; you can open it again or pick "
                        "another one. The application is still usable."));
        }
        return;
    }

    const QString keepUid = currentSeriesUid();
    m_series = std::move(r.series);
    rebuildSeriesRows(keepUid);

    if (m_series.empty()) {
        setIdleProgress();
        // Informative, not terminal: what happened, what to try, and the app is fine (roadmap 1.6).
        m_status->setText(
            QString("No DICOM images were found in %1 — check that this is the right folder and "
                    "that the files are DICOM objects with pixel data. You can open another folder "
                    "at any time.")
                .arg(QDir::toNativeSeparators(m_folder)));
        return;
    }

    // Thumbnails go first, but nothing waits for them: the selected series' slice is decoded on the
    // one-thread foreground pool, so it cannot end up behind a folder's worth of thumbnail jobs.
    startThumbnails();
    const int row = m_benchFrames > 0 ? std::min(m_benchRow, int(m_series.size()) - 1) : 0;
    m_seriesList->setCurrentRow(row);
}

// One builder for the rows, shared by the mid-scan batches and the finished scan so the two cannot
// drift. The selection is restored by series UID: a batch can legitimately add a series that sorts
// above the current one, and losing the selection because a row moved is a bug the user sees.
void MainWindow::rebuildSeriesRows(const QString& keepUid)
{
    {
        // Blocked only while the rows are rebuilt: clearing the list emits currentRowChanged(-1), and
        // acting on that mid-rebuild would start a decode for a series that is about to be replaced.
        // The blocker must NOT cover setCurrentRow below, or the new selection never reaches showSeries.
        const QSignalBlocker blocker(m_seriesList);
        m_seriesList->clear();
        // Grouped, not flat: a patient header and a study header above their series. The flat list is
        // how two patients with identically described series became indistinguishable — the boundary
        // between them was simply not on screen. Headers are not selectable; activating one opens its
        // first series (see the currentRowChanged handler), because a person who clicks a group means
        // "show me this".
        const clivus::ui::BrowserTree tree = clivus::ui::buildBrowserTree(m_series);
        for (const auto& patient : tree.patients) {
            const QString patientKey = patientGroupKey(patient.patientKey);
            const bool patientCollapsed = m_collapsedGroups.contains(patientKey);
            auto* header = new QListWidgetItem(groupRowText(patientCollapsed, patient.label),
                                               m_seriesList);
            // Selectable on purpose: Qt will not make a non-selectable item current, so a header that
            // could not be selected could not open its first series either. The selection handler
            // treats the header as "show me this group"; folding is the mouse/keyboard path (see
            // eventFilter) and deliberately does not move the selection.
            header->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            header->setData(Qt::UserRole, -1);
            header->setData(clivus::ui::kHeaderRole, int(clivus::ui::HeaderKind::Patient));
            header->setData(clivus::ui::kGroupKeyRole, patientKey);
            header->setData(kGroupLabelRole, patient.label);
            for (const auto& study : patient.studies) {
                QString studyText = study.label;
                if (study.series.size() > 1)
                    studyText += QStringLiteral("  (%1 series)").arg(study.series.size());
                const QString studyKey = studyGroupKey(patient.patientKey, study.studyUid);
                const bool studyCollapsed = m_collapsedGroups.contains(studyKey);
                auto* studyHeader = new QListWidgetItem(
                    groupRowText(studyCollapsed, QStringLiteral("    ") + studyText), m_seriesList);
                studyHeader->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
                studyHeader->setData(Qt::UserRole, -1);
                studyHeader->setData(clivus::ui::kHeaderRole, int(clivus::ui::HeaderKind::Study));
                studyHeader->setData(clivus::ui::kGroupKeyRole, studyKey);
                studyHeader->setData(kGroupLabelRole, QStringLiteral("    ") + studyText);
                for (const auto& entry : study.series) {
                    auto* item = new QListWidgetItem(clivus::ui::seriesLabel(m_series[size_t(entry.seriesIndex)]),
                                                     m_seriesList);
                    // Deliberately not the patient name: ordinary hover tooltips stay free of
                    // identifying metadata (roadmap 1.3 / 3.5). The description and modality line is
                    // enough to pick one.
                    item->setToolTip(clivus::ui::seriesLabel(m_series[size_t(entry.seriesIndex)]));
                    item->setData(Qt::UserRole, entry.seriesIndex);
                }
            }
        }
        applySeriesFilter();   // roadmap 3.2: a query typed before the scan finished still applies
    }

    int row = -1;
    bool keptHidden = false;
    if (!keepUid.isEmpty()) {
        for (int i = 0; i < m_seriesList->count(); ++i) {
            const QListWidgetItem* item = m_seriesList->item(i);
            if (!item) continue;
            const int index = item->data(Qt::UserRole).toInt();
            if (index >= 0 && index < int(m_series.size()) &&
                QString::fromStdString(m_series[size_t(index)].seriesUid) == keepUid) {
                row = i;
                // A rebuild must not unfold a group the user folded, and must not lose the caret
                // because the row it stands on is hidden: both would move the user's attention (and,
                // through the selection handler, start a decode they did not ask for).
                keptHidden = item->isHidden();
                break;
            }
        }
    }
    if (row < 0) {
        for (int i = 0; i < m_seriesList->count(); ++i) {
            const QListWidgetItem* item = m_seriesList->item(i);
            if (item && !item->isHidden() && item->data(Qt::UserRole).toInt() >= 0) { row = i; break; }
        }
    }
    if (row >= 0) {
        const QListWidgetItem* item = m_seriesList->item(row);
        const int mapped = item ? item->data(Qt::UserRole).toInt() : -2;
        // A rebuild is not a navigation: when the row it restores is the series already open (or the
        // row it restores is hidden inside a folded group), the handler must not re-run showSeries().
        // That would drop the slice being read and decode slice 0 again on every scan batch. A click
        // from the user still goes through the handler untouched, because it does not come from here.
        m_suppressRowActivation = keptHidden || (mapped >= 0 && mapped == m_currentSeries);
        m_seriesList->setCurrentRow(row);
        m_suppressRowActivation = false;
    }
}

int MainWindow::rowForSeriesIndex(int seriesIndex) const
{
    if (!m_seriesList || seriesIndex < 0) return -1;
    for (int i = 0; i < m_seriesList->count(); ++i) {
        const QListWidgetItem* item = m_seriesList->item(i);
        if (!item) continue;
        if (item->data(Qt::UserRole + 1).toBool()) continue;   // the placeholder
        if (item->data(Qt::UserRole).toInt() == seriesIndex) return i;
    }
    return -1;
}

// A snapshot from a scan that is still running. Only ever grows the list: a snapshot with nothing new
// would rebuild every row and drop the selection for no reason, and the finished scan supersedes all
// of this anyway.
void MainWindow::applyScanBatch(quint64 folderEpoch, std::vector<clivus::SeriesInfo> batch)
{
    if (folderEpoch != m_folderEpoch) return;   // a folder the user has already left
    if (batch.size() <= m_series.size()) return;

    const QString keepUid = currentSeriesUid();
    m_series = std::move(batch);
    rebuildSeriesRows(keepUid);
    m_status->setText(QString("Scanning… %1 series found so far — any of them can be opened now")
                          .arg(m_series.size()));
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
    int ready = 0, failed = 0;
    for (int i = 0; i < count; ++i) {
        if (!future.isResultReadyAt(i)) continue;
        ++ready;
        // i is a series index (one thumbnail task per series), not a row.
        QListWidgetItem* item = m_seriesList->item(rowForSeriesIndex(i));
        if (!item) continue;
        const QImage image = future.resultAt(i);
        if (image.isNull()) {
            if (!item->icon().isNull()) continue;
            // A thumbnail that cannot be decoded is a state the user should see, not a silent gap:
            // say so on the row, plainly, without echoing any decoder's raw error text (roadmap 1.3).
            item->setIcon(QIcon::fromTheme("image-missing"));
            item->setData(Qt::UserRole, true);
            ++failed;
            continue;
        }
        item->setIcon(QIcon(QPixmap::fromImage(image)));
    }
    if (failed > 0)
        statusBar()->showMessage(QString("%1 series could not be previewed — you can still open them")
                                     .arg(failed),
                                 8000);
    if (m_progress->maximum() > 0) m_progress->setValue(ready);
}

// Decodes are strictly one at a time and always chase m_desiredSlice, so spinning the wheel while a
// decode runs costs one more decode after it, not a queue of them. A slice index is a frame index
// across the whole series, so one 15-frame object is 15 steps rather than one.
void MainWindow::pumpSlice()
{
    // A transient fresh decode of the same slice skips straight to the decode; but a parked
    // prefetch for exactly this index presents instantly (that is the smooth-scroll payoff).
    if (!m_sliceInFlight && m_currentSeries >= 0 && m_prefetchFrame.valid() &&
        m_prefetchIndex == m_desiredSlice && m_failedSlice != m_desiredSlice) {
        m_prefetchIndex = -1;
        ++m_scrollStats.prefetchServed;
        presentSlice(m_desiredSlice, std::move(m_prefetchFrame));
        return;
    }
    if (m_sliceInFlight || m_currentSeries < 0 || m_desiredSlice < 0) return;
    // The speculative pool may already be fetching exactly this slice. Starting a second decode of it on
    // the foreground pool was pure waste — two reads of the same file, and the second one lands after the
    // first has already been presented — so wait for that one instead.
    if (m_prefetchIndex == m_desiredSlice && m_prefetchWatcher->isRunning()) return;
    // Roadmap 3.5: a slice that already failed is not retried on its own — that would spin the
    // decode pool (pump → fail → pump) on a permanently broken file. The failure is cleared only
    // when the user picks a slice that previously succeeded, i.e. by moving away first.
    if (m_failedSlice == m_desiredSlice) return;
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
    m_status->setText(QString("Loading slice %1 / %2…")
                          .arg(m_desiredSlice + 1)
                          .arg(m_series[size_t(m_currentSeries)].totalFrames()));
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
        // What happened, what to try, and the app is fine: other slices and series still work.
        m_status->setText("This slice could not be decoded — the file may be unsupported or "
                          "damaged. Try another slice or series; the rest of the application "
                          "remains usable.");
        m_failedSlice = r.index;
        // Roadmap 3.5: a failed decode must not retry itself. Only chase a NEW target: re-pumping
        // the same index would spin the decode pool forever on a permanently broken file, and
        // pumpSlice() refuses an index the previous attempt failed on (m_failedSlice), so the next
        // wheel step / slider move / selection change is what restarts it.
        if (m_desiredSlice != r.index) pumpSlice();
        return;
    }
    // A decoded frame that is no longer what the wheel points at is NOT presented. Presenting it flashed
    // a slice the user had already scrolled away from — one repaint of the wrong image, then another of
    // the right one — which is the stutter the report describes. The pump is restarted for the slice that
    // is current now; what is presented is always the frame it says it is, so nothing is mislabelled.
    if (r.index != m_desiredSlice) {
        pumpSlice();
        return;
    }
    presentSlice(r.index, std::move(r.frame));
}

// The one canonical presentation path (sliceFinished's ok-tail, shared by the prefetch slot's
// consume in pumpSlice). Whatever gets here has passed the series-epoch/stale checks where they
// belong (sliceFinished guards the pipeline; pumpSlice guards cached consumption against a changed
// target — because a consume that lost the race lands on stale lines).
void MainWindow::presentSlice(int index, clivus::ImageFrame frame)
{
    if (m_currentSeries < 0) return;
    ++m_scrollStats.presents;
    m_presentedSlices.push_back(index);
    if (index == m_desiredSlice) {
        if (m_requestClock.isValid())
            m_scrollStats.latencyMs.push_back(double(m_requestClock.nsecsElapsed()) / 1e6);
    } else {
        ++m_scrollStats.stalePresents;
    }
    m_currentSlice = index;
    m_currentIsColour = frame.isColour();
    // Colour frames ignore the window (render() passes their samples through), so the fields stay
    // visible but disabled, with the reason in the options bar as well as the overlay (roadmap 2.2).
    m_windowBar->setWindowingAvailable(
        !m_currentIsColour,
        m_currentIsColour ? QString("Window/level does not apply to colour images")
                          : QString());
    m_presetBar->setAvailable(
        !m_currentIsColour,
        m_currentIsColour ? QString("Presets need a grayscale image") : QString());
    m_sliceBar->setCurrentSlice(index);
    m_canvas->setFrame(std::move(frame));
    loadAnnotationsForSlice(index);
    updateInfo();

    // Smooth scroll (radiology report: the wheel stutters and skips): with the display settled,
    // quietly decode the neighbour in the wheel's travel direction into a separate prefetch slot.
    // The next step presents it instantly (pumpSlice consumes the slot) instead of sitting through
    // a decode. The speculative decode runs on its OWN single-thread pool: a requested frame must
    // never queue behind work the user did not ask for, and QtConcurrent gives no way to cancel a
    // task already handed to a pool — so on a direction reversal the old shared pool made the frame
    // the user wanted wait for the frame they had just scrolled past. One decode at a time still
    // holds within each pool, and a landing behind a series change is dropped by the epoch check.
    const int total = m_series[size_t(m_currentSeries)].totalFrames();
    const int ahead = m_currentSlice + (m_lastStep ? 1 : -1);
    if (ahead >= 0 && ahead < total && m_prefetchIndex != ahead) {
        const auto& series = m_series[size_t(m_currentSeries)];
        int fileIndex = 0, frameIndex = 0;
        if (series.locate(ahead, fileIndex, frameIndex)) {
            m_prefetchIndex = ahead;
            ++m_scrollStats.prefetchStarts;
            const std::string path = series.files[size_t(fileIndex)];
            const quint64 epoch = m_epoch;
            m_prefetchWatcher->setFuture(QtConcurrent::run(&m_prefetch, [path, epoch, frameIndex] {
                clivus::ImageFrame frame;
                frame.frameIndex = 0;
                const bool ok = clivus::loadFrame(path, frame, frameIndex);
                return ok ? frame : clivus::ImageFrame{};
            }));
        }
    }
    startBenchmark();
}

// The prefetch landing: cache only what still belongs to the open series (epoch), and present it
// straight away if the wheel is already pointing at it — that is the case where the user scrolled
// while the prefetch was in flight and the slot's frame is exactly what they asked for.
void MainWindow::onPrefetchLanded()
{
    const clivus::ImageFrame frame = m_prefetchWatcher->result();
    if (frame.valid() && m_currentSeries >= 0 && m_prefetchIndex >= 0 &&
        m_prefetchIndex == m_desiredSlice && !m_sliceInFlight) {
        m_prefetchIndex = -1;
        presentSlice(m_desiredSlice, std::move(frame));
        return;
    }
    if (frame.valid()) {
        m_prefetchFrame = std::move(frame);   // parked: the pump serves it when the wheel arrives
        // The wheel may have moved past this frame while it was decoding: the slice it points at now
        // still needs its own decode, and nothing else would start it.
        pumpSlice();
        return;
    }
    m_prefetchIndex = -1;   // a failed or stale prefetch: nothing parked
    pumpSlice();            // and the target still needs its own decode
}

void MainWindow::cancelLoading()
{
    // Roadmap 3.5: cancel is a first-class path, not an error. Everything queued is retired in one
    // move; the one decode/scan currently executing is left to finish and be dropped by the epoch
    // check in sliceFinished/scanFinished, so no result can land after the user cancelled.
    if (m_scanControl) m_scanControl->cancel = true;
    const bool scanRunning = m_scanWatcher->isRunning();
    const bool thumbsRunning = m_thumbWatcher->isRunning();
    if (thumbsRunning) m_thumbWatcher->cancel();
    setIdleProgress();   // no stuck indicators regardless of which runs unwind below

    if (scanRunning) {
        m_status->setText(
            "Cancelling — the scan stops after the object it is currently reading; meanwhile the "
            "application remains usable, and you can open any other folder.");
        return;
    }
    if (thumbsRunning) {
        m_status->setText(
            "Previews cancelled — series remain openable as before; the selected series keeps "
            "decoding normally.");
        updateInfo();
        return;
    }
    // Nothing is in flight: cancel must not lie about work by claiming it stopped any.
    m_status->setText("Ready");
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
    const int row = rowForSeriesIndex(seriesRow);
    if (row >= 0) m_seriesList->setCurrentRow(row);
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

// Roadmap 3.2. One writer of the list's visibility state: rows whose visible text (the description
// plus the modality/count/dimensions line) contains the query are shown, the rest are hidden. The
// rows and their thumbnails are never rebuilt or reordered, so background loading is unaffected;
// the current selection stays if it matches. An empty query is the unfiltered list. When no row
// matches, a single disabled placeholder row says so instead of leaving a silent empty pane.
void MainWindow::applySeriesFilter()
{
    if (!m_seriesList) return;
    const QString query = m_seriesSearch ? m_seriesSearch->text().trimmed() : QString();
    const bool filtering = !query.isEmpty();

    // Count first: the placeholder (marked with UserRole+1) is removed whenever it is no longer
    // needed — a non-empty query with matches, or a cleared query.
    int matches = 0;
    if (filtering) {
        for (int i = 0; i < m_seriesList->count(); ++i) {
            const QListWidgetItem* item = m_seriesList->item(i);
            if (item && !item->data(Qt::UserRole + 1).toBool() &&
                item->data(Qt::UserRole).toInt() >= 0 &&
                item->text().contains(query, Qt::CaseInsensitive)) ++matches;
        }
    }
    if (m_seriesList->item(0) && m_seriesList->item(0)->data(Qt::UserRole + 1).toBool() &&
        (!filtering || matches > 0)) {
        QListWidgetItem* stale = m_seriesList->takeItem(0);
        delete stale;
    }

    int matchesFound = matches;   // the pre-count above; recounted below over non-placeholder rows
    QListWidgetItem* current = m_seriesList->currentItem();
    for (int i = 0; i < m_seriesList->count(); ++i) {
        QListWidgetItem* item = m_seriesList->item(i);
        if (!item) continue;
        if (item->data(Qt::UserRole + 1).toBool()) { item->setHidden(!filtering); continue; }
        if (item->data(Qt::UserRole).toInt() < 0) continue;   // group headers: decided below
        const bool match = item->text().contains(query, Qt::CaseInsensitive);
        item->setHidden(filtering && !match);
    }

    // Group headers follow their children: a patient or study header is visible only when at least one
    // series below it survived the filter. Done after the series rows so it reads the real state and
    // cannot disagree with it.
    {
        int patientRow = -1;
        int studyRow = -1;
        bool patientHasVisible = false;
        bool studyHasVisible = false;
        const auto closeStudy = [&] {
            if (studyRow >= 0 && m_seriesList->item(studyRow))
                m_seriesList->item(studyRow)->setHidden(!studyHasVisible);
        };
        const auto closePatient = [&] {
            if (patientRow >= 0 && m_seriesList->item(patientRow))
                m_seriesList->item(patientRow)->setHidden(!patientHasVisible);
        };
        for (int i = 0; i < m_seriesList->count(); ++i) {
            const QListWidgetItem* item = m_seriesList->item(i);
            if (!item) continue;
            const auto kind = clivus::ui::HeaderKind(item->data(clivus::ui::kHeaderRole).toInt());
            if (kind == clivus::ui::HeaderKind::Patient) {
                closeStudy();
                closePatient();
                patientRow = i;
                studyRow = -1;
                patientHasVisible = false;
                studyHasVisible = false;
                continue;
            }
            if (kind == clivus::ui::HeaderKind::Study) {
                closeStudy();
                studyRow = i;
                studyHasVisible = false;
                continue;
            }
            if (item->data(Qt::UserRole + 1).toBool()) continue;   // placeholder
            if (item->isHidden()) continue;
            patientHasVisible = true;
            studyHasVisible = true;
        }
        closeStudy();
        closePatient();
    }

    // Fold state on top of the query result: same writer, so the two can never disagree.
    applyCollapseState(filtering);

    if (filtering && matchesFound == 0) {
        auto* placeholder = new QListWidgetItem("(no matching series)");
        placeholder->setData(Qt::UserRole, -1);         // not a series: nothing to select
        placeholder->setData(Qt::UserRole + 1, true);   // marks the row as the placeholder
        placeholder->setFlags(Qt::NoItemFlags);         // not selectable, not focusable
        placeholder->setForeground(palette().color(QPalette::Disabled, QPalette::Text));
        m_seriesList->insertItem(0, placeholder);
    }
    // The selection stays visible when it matches: setCurrentItem on a hidden item would jump
    // elsewhere, so only re-anchor when a *query* has hidden the current row. A fold is not a query:
    // it moves the caret to the header itself (see toggleGroupCollapse) and leaves this alone.
    if (filtering && current && current->isHidden() && matchesFound > 0) {
        for (int i = 0; i < m_seriesList->count(); ++i) {
            QListWidgetItem* item = m_seriesList->item(i);
            if (item && !item->isHidden()) { m_seriesList->setCurrentItem(item); break; }
        }
    }
}

// Folds or unfolds one group. Returns false when the row is not a group header, so both the mouse and
// the keyboard have one rule and a test can drive the same entry point.
//
// The identity of the group is the vendor-neutral key the model already carries (kGroupKeyRole), not
// the row number: a scan batch rebuilds every row, and a label can be shared by two patients.
bool MainWindow::toggleGroupCollapse(int row)
{
    if (!m_seriesList || row < 0 || row >= m_seriesList->count()) return false;
    QListWidgetItem* item = m_seriesList->item(row);
    if (!item) return false;
    if (clivus::ui::HeaderKind(item->data(clivus::ui::kHeaderRole).toInt()) ==
        clivus::ui::HeaderKind::None)
        return false;
    const QString key = item->data(clivus::ui::kGroupKeyRole).toString();
    if (key.isEmpty()) return false;

    bool nowCollapsed = false;
    if (m_collapsedGroups.contains(key)) {
        m_collapsedGroups.remove(key);
    } else {
        m_collapsedGroups.insert(key);
        nowCollapsed = true;
    }

    applySeriesFilter();   // the single writer of visibility re-applies the fold

    // Folding the group that holds the series on screen keeps showing that series: collapsing a group
    // is navigation, not closing a study. The caret moves to the header, with activation suppressed, so
    // no decode and no slice reset happen.
    const QListWidgetItem* current = m_seriesList->currentItem();
    if (nowCollapsed && current && current->isHidden()) {
        m_suppressRowActivation = true;
        m_seriesList->setCurrentRow(row);
        m_suppressRowActivation = false;
    }
    return true;
}

// The fold half of the single visibility writer (see applySeriesFilter's comment). Runs after the
// query pass, so it can never make a header look empty: a group header stays on screen whenever it has
// a series at all; only its children are hidden.
//
// While a query is active the groups are shown open. A search that finds a series inside a folded
// study and then hides it would be a worse bug than the one this fixes; the fold state is kept, so
// clearing the query puts the folded groups back exactly as they were.
void MainWindow::applyCollapseState(bool filtering)
{
    const bool folding = !filtering;
    bool patientCollapsed = false;
    bool studyCollapsed = false;
    for (int i = 0; i < m_seriesList->count(); ++i) {
        QListWidgetItem* item = m_seriesList->item(i);
        if (!item) continue;
        const auto kind = clivus::ui::HeaderKind(item->data(clivus::ui::kHeaderRole).toInt());
        if (kind == clivus::ui::HeaderKind::Patient) {
            const QString key = item->data(clivus::ui::kGroupKeyRole).toString();
            patientCollapsed = folding && m_collapsedGroups.contains(key);
            studyCollapsed = false;
            item->setText(groupRowText(patientCollapsed, item->data(kGroupLabelRole).toString()));
            continue;
        }
        if (kind == clivus::ui::HeaderKind::Study) {
            // Inside a folded patient everything below the patient header is out of the way, including
            // the study headers — but their own fold state is untouched and comes back on unfold.
            if (patientCollapsed) {
                item->setHidden(true);
                continue;
            }
            const QString key = item->data(clivus::ui::kGroupKeyRole).toString();
            studyCollapsed = folding && m_collapsedGroups.contains(key);
            item->setText(groupRowText(studyCollapsed, item->data(kGroupLabelRole).toString()));
            continue;
        }
        if (item->data(Qt::UserRole + 1).toBool()) continue;   // the placeholder
        if (item->data(Qt::UserRole).toInt() < 0) continue;    // not a series row
        if (patientCollapsed || studyCollapsed) item->setHidden(true);
    }
}

bool MainWindow::rowIsGroupHeaderForTest(int row) const
{
    if (!m_seriesList || row < 0 || row >= m_seriesList->count()) return false;
    const QListWidgetItem* item = m_seriesList->item(row);
    return item && item->data(clivus::ui::kHeaderRole).toInt() != 0;
}

QString MainWindow::groupKeyAtRowForTest(int row) const
{
    if (!m_seriesList || row < 0 || row >= m_seriesList->count()) return QString();
    const QListWidgetItem* item = m_seriesList->item(row);
    return item ? item->data(clivus::ui::kGroupKeyRole).toString() : QString();
}

// Test seam: the counters describe one scroll run, so a test clears them between runs.
void MainWindow::resetScrollStatsForTest()
{
    m_scrollStats = ScrollStats{};
    m_presentedSlices.clear();
    m_requestClock.invalidate();
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
        m_sliceBar->setSliceCount(0);
        m_status->setText("This series has no image objects in the index — try another series; the application remains usable.");
        return;
    }

    ++m_epoch;              // a decode still running belongs to the series we are leaving
    m_failedSlice = -1;     // new series, fresh failure tracking (roadmap 3.5)
    m_prefetchFrame = clivus::ImageFrame{};   // the parked prefetch belongs to the previous series
    m_prefetchIndex = -1;
    m_hasStep = false;      // nothing about the wheel is remembered across a series switch
    m_currentSeries = index;
    m_currentSlice = -1;    // nothing decoded for this series yet
    m_timing.clear();
    m_windowLevel.clear();
    m_canvas->setFrame(clivus::ImageFrame{});
    // The strip's range is the series' flat frame count, so a multi-frame object counts as the
    // slices it actually carries (the loader already flattens them).
    // First-round MPR is honest about eligibility: a study whose objects did not all report
    // IPP/IOP has no usable geometry, so the entry disables itself rather than opening a window
    // that would render positions it does not actually have.
    m_mprAction->setEnabled(clivus::hasUsableGeometry(m_series[size_t(index)]));
#ifdef CLIVUS_HAVE_VTK
    if (m_vtkAction != nullptr)
        m_vtkAction->setEnabled(clivus::hasUsableGeometry(m_series[size_t(index)]));
#endif
    m_sliceBar->setSliceCount(m_series[size_t(index)].totalFrames());
    m_sliceBar->setCurrentSlice(0);
    m_desiredSlice = 0;
    pumpSlice();
}

void MainWindow::showSlice(int delta)
{
    if (m_currentSeries < 0) return;
    if (!m_cineAdvancing) stopCine();   // a manual scrub takes the wheel back
    const auto& series = m_series[size_t(m_currentSeries)];
    const int total = series.totalFrames();
    if (total <= 0) return;   // clamp() would give an upper bound of -1 here
    // Step from where the wheel last pointed, not from what is on screen, so a fast spin does not
    // keep restarting from the same slice. The target is the right base even when nothing is in flight:
    // a step can be waiting on the prefetch pool without a decode of its own, and measuring from the
    // displayed slice would then drop every second notch of a fast spin.
    const int from = std::max(m_desiredSlice >= 0 ? m_desiredSlice : m_currentSlice, 0);
    const int next = std::clamp(from + delta, 0, total - 1);
    if (next == m_desiredSlice && (m_sliceInFlight || next == m_currentSlice)) return;
    m_lastStep = next > from;
    m_hasStep = true;
    m_desiredSlice = next;
    ++m_scrollStats.requests;
    m_requestClock.start();   // latency is measured from the last slice the user asked for
    // The slider follows the slice the wheel is aiming at, not the one still decoding: during a fast
    // spin the strip and the decode pump both chase the same target, and the pump decides when the
    // decode happens, so this cannot outrun the coalescing in pumpSlice().
    m_sliceBar->setCurrentSlice(next);
    pumpSlice();
}

// Absolute jump from the slice slider (roadmap 2.5). It only sets the target and lets pumpSlice()
// coalesce it, so dragging across a long series issues one decode at a time and a late result for an
// intermediate position is discarded by the same index check that protects the wheel path.
void MainWindow::goToSlice(int index)
{
    if (m_currentSeries < 0) return;
    if (!m_cineAdvancing && m_cineTimer->isActive()) stopCine();
    const int total = m_series[size_t(m_currentSeries)].totalFrames();
    if (total <= 0) return;
    const int target = std::clamp(index, 0, total - 1);
    if (target == m_desiredSlice && (m_sliceInFlight || target == m_currentSlice)) return;
    m_desiredSlice = target;
    ++m_scrollStats.requests;
    m_requestClock.start();
    pumpSlice();
    if (!m_sliceInFlight) updateInfo();
}

// Single writer for the rendering state (roadmap 6.3). The canvas echoes the values back through
// windowChanged, so the fields, the overlay and the status line all end up showing exactly what
// ImageFrame::render() is being handed - none of them keeps its own copy to drift.
void MainWindow::setWindowLevel(double center, double width)
{
    if (m_currentSeries < 0) return;
    m_canvas->setWindowLevel(center, width);
}

void MainWindow::updateInfo(const QString& extra)
{
    if (m_currentSeries < 0) return;
    // Roadmap 3.5: while a slice's failure is the current state, its message IS the status — a
    // metadata refresh (thumbnails finishing, a diagnostics toggle, a window echo) must not
    // overwrite it with the neutral swing text, or the error the user reads disappears.
    if (m_failedSlice == m_desiredSlice && m_desiredSlice >= 0) return;
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

    // Roadmap 1.4: viewing information on the canvas overlays, diagnostics only under the toggle.
    // Top-left: what the series is. Top-right: how it is being viewed right now.
    QStringList left;
    const QString description = QString::fromStdString(
        !s.seriesDescription.empty() ? s.seriesDescription : s.studyDescription);
    if (!description.isEmpty()) left << description;
    if (!s.modality.empty()) left << QString::fromStdString(s.modality);
    if (s.width > 0 && s.height > 0) left << QString("%1 × %2").arg(s.width).arg(s.height);
    left << position + frameNote;
    
    // Add geometry info when available
    if (s.hasGeometry() && m_currentSlice >= 0 && fileIndex >= 0 && fileIndex < int(s.sliceGeometry.size())) {
        const auto& geom = s.sliceGeometry[size_t(fileIndex)];
        if (geom.hasGeometry) {
            std::array<double, 6> iop = {geom.orientRowX, geom.orientRowY, geom.orientRowZ,
                                          geom.orientColX, geom.orientColY, geom.orientColZ};
            std::string orientLabel = clivus::orientationLabel(iop);
            if (!orientLabel.empty()) {
                left << QString::fromStdString(orientLabel);
            }
            // Position along the slice normal. Shown relative to the first slice of the series
            // (physical mm; a real position label of a world-coordinate dot would be numerically
            // correct but meaningless to a human). Only when the geometry is genuinely there.
            auto alongDists = s.alongDistances();
            if (fileIndex < int(alongDists.size())) {
                const double rel = alongDists[size_t(fileIndex)] - alongDists.front();
                // Qt placeholders are %1, not printf's %+1 — the old string shipped the literal
                // "%+1" to the overlay. Sign explicitly: +3.5 / -7.5 reads as a position offset.
                const QString signedMm = (rel >= 0.0 ? QStringLiteral("+") : QString()) +
                                         QString::number(rel, 'f', 1);
                left << QStringLiteral("slice position %1 mm (rel. to first slice)").arg(signedMm);
            }
        }
    }
    
    if (!extra.isEmpty()) left << extra;

    QString right;
    if (m_currentIsColour) {
        right = "colour frame — window/level not applied";
    } else if (!m_windowLevel.isEmpty()) {
        right = m_windowLevel;
    }
    const double zoom = m_canvas->zoomFactor();
    if (zoom > 0.0) right += QString(right.isEmpty() ? "" : "   ") + QString("%1%").arg(int(zoom * 100.0));
    // The timing line (window/paint/fps) used to be appended to the corner block, where three numbers
    // and two units landed in the smallest text on screen next to clinical information. It is
    // diagnostic output, so it belongs in the status bar with the rest of the state.

    m_canvas->setOverlay(left.join(QLatin1String("\n")), right);

    // The status bar keeps only state, not a running technical string (roadmap 1.5).
    QString status = QString("Slice %1 of %2 — %3")
                         .arg(m_currentSlice < 0 ? QString("…") : QString::number(m_currentSlice + 1))
                         .arg(total)
                         .arg(description.isEmpty() ? QString("(no description)") : description);
    if (!extra.isEmpty()) status += " — " + extra;
    // Roadmap 2.2: colour frames are not a failure, but the missing window/level must be stated, not
    // left for the user to discover by dragging with no effect.
    // Zoom is viewport state, not series state: show it in the status bar only when it differs from
    // the fitted default, so the common case stays quiet (roadmap 2.4).
    if (zoom > 0.0 && std::abs(zoom - 1.0) > 0.001)
        status += QString(" — zoom %1%").arg(int(zoom * 100.0 + 0.5));
    if (m_currentIsColour) status += " — colour image: window/level is not applied";
    if (m_showDiagnostics && !m_timing.isEmpty()) status += " — " + m_timing;
    m_status->setText(status);
}

// Help and About arrive with the help-menu skeleton commit; the actions exist from the start so the
// menus are complete, and these slots are where they land.
void MainWindow::showManual()
{
    if (!m_help) m_help = new HelpWindow(this);
    m_help->show();
    m_help->raise();
    m_help->activateWindow();
}

void MainWindow::showAbout()
{
    // A proper identity home (branding): mark, version, repository, licence, acknowledgments and
    // the research-use disclaimer in one calm panel instead of a wall of modal text.
    AboutDialog dialog(this);
    dialog.exec();
}