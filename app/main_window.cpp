// SPDX-License-Identifier: GPL-3.0-or-later
#include "main_window.h"

#include "clivus/version.h"

#include <QAction>
#include <QActionGroup>
#include <QDir>
#include <QFileDialog>
#include <QIcon>
#include <QImage>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMenuBar>
#include <QMessageBox>
#include <QPixmap>
#include <QProgressBar>
#include <QPushButton>
#include <QScreen>
#include <QSettings>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QToolBar>
#include <QVBoxLayout>
#include <QTimer>
#include <QtConcurrent/QtConcurrentMap>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <cstdio>

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

// Series browser entry (roadmap 1.3): description prominent on the first line, then the
// modality · count · dimensions line. Any missing field gets an explicit fallback, never a guess.
QString seriesTitle(const clivus::SeriesInfo& s)
{
    const std::string& description =
        !s.seriesDescription.empty() ? s.seriesDescription : s.studyDescription;
    return description.empty() ? QString("(no description)")
                               : QString::fromStdString(description);
}

QString seriesDetails(const clivus::SeriesInfo& s)
{
    QStringList parts;
    if (!s.modality.empty()) parts << QString::fromStdString(s.modality);
    // Frames only when they say something the object count does not: a 15-frame RT Dose is one
    // object and fifteen slices, and the list should not pretend otherwise.
    const int frames = s.totalFrames();
    if (frames > int(s.files.size()))
        parts << QString("%1 objects · %2 frames").arg(s.files.size()).arg(frames);
    else
        parts << QString("%1 images").arg(s.files.size());
    if (s.width > 0 && s.height > 0) parts << QString("%1 × %2").arg(s.width).arg(s.height);
    return parts.join(QLatin1String(" · "));
}

QString seriesLabel(const clivus::SeriesInfo& s)
{
    return QString("%1\n%2").arg(seriesTitle(s), seriesDetails(s));
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
    m_canvas = new CanvasWidget(this);
    // The gestures are fixed regardless of the highlighted tool, so they are stated once here
    // (roadmap 2.1) instead of being implied by whichever action happens to be checked.
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
    m_splitter->addWidget(m_seriesList);
    m_splitter->addWidget(viewer);
    m_splitter->setStretchFactor(1, 1);
    m_splitter->setSizes({320, 900});

    m_status = new QLabel("Ready", this);
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
    open->setIcon(QIcon::fromTheme("folder-open"));
    open->setToolTip("Choose a folder of DICOM files to scan");
    open->setShortcut(QKeySequence::Open);
    connect(open, &QAction::triggered, this, [this] {
        const QString dir = QFileDialog::getExistingDirectory(this, "DICOM folder");
        if (!dir.isEmpty()) openFolder(dir);
    });
    auto* quit = new QAction("E&xit", this);
    quit->setShortcut(QKeySequence::Quit);
    connect(quit, &QAction::triggered, this, &QWidget::close);

    // Menu and toolbar share one QAction per command, so a shortcut cannot drift from the button
    // wired to the same thing (roadmap 6.3). Each action drives behaviour that already exists.
    auto* fit = new QAction("Fit &image", this);
    fit->setIcon(QIcon::fromTheme("zoom-fit-best"));
    fit->setToolTip("Scale the image to fit the canvas (keeps the aspect ratio)");
    connect(fit, &QAction::triggered, m_canvas, &CanvasWidget::fitView);

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

    m_pointerAction = new QAction("&Pointer", this);
    auto* pointer = m_pointerAction;
    pointer->setIcon(QIcon::fromTheme("cursor-arrow"));
    pointer->setToolTip("Pointer tool (arrow cursor), for looking without an active adjustment. Both "
                        "drags keep working.");
    pointer->setCheckable(true);

    m_toolGroup = new QActionGroup(this);
    m_toolGroup->setExclusive(true);
    for (auto* a : {m_windowAction, m_panAction, pointer}) {
        a->setActionGroup(m_toolGroup);
        m_toolGroup->addAction(a);
    }
    connect(m_windowAction, &QAction::triggered, this,
            [this] { setActiveTool(ActiveTool::WindowLevel); });
    connect(m_panAction, &QAction::triggered, this, [this] { setActiveTool(ActiveTool::Pan); });
    connect(pointer, &QAction::triggered, this, [this] { setActiveTool(ActiveTool::Pointer); });

    auto* sidebar = new QAction("&Series sidebar", this);
    sidebar->setToolTip("Show or hide the series browser");
    sidebar->setCheckable(true);
    sidebar->setChecked(true);
    connect(sidebar, &QAction::toggled, this, [this](bool on) {
        m_seriesList->setVisible(on);
        // Give the reclaimed space to the canvas: the image is the point.
        m_splitter->setSizes(on ? QList<int>{320, 900} : QList<int>{0, 1220});
    });

    // Roadmap 1.5: timings are development information, not primary UI. Session-only by design —
    // nothing is persisted, so a fresh session starts clean.
    auto* diagnostics = new QAction("Show &diagnostics", this);
    m_diagnosticsAction = diagnostics;   // kept for save/restore/reset of layout (roadmap 3.1)
    diagnostics->setToolTip("Show render timing details in the image overlay (remembered across sessions)");
    diagnostics->setCheckable(true);
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
    for (auto* a : {m_windowAction, m_panAction, pointer}) toolbar->addAction(a);   // exclusive tool set
    toolbar->addSeparator();
    for (auto* a : {reset}) toolbar->addAction(a);

    setMinimumSize(720, 480);   // narrower windows clip the toolbar and sidebar
    // Empty state (roadmap 1.6): what Clivus does, a prominent way in, and a route to the manual.
    // It is one page of a stack, so the main window never blocks waiting on it.
    m_emptyState = new QWidget(this);
    auto* emptyLayout = new QVBoxLayout(m_emptyState);
    emptyLayout->addStretch(1);
    auto* welcome = new QLabel("Welcome to Clivus", m_emptyState);
    welcome->setAlignment(Qt::AlignCenter);
    QFont heading = welcome->font();
    heading.setPointSizeF(heading.pointSizeF() * 1.8);
    heading.setBold(true);
    welcome->setFont(heading);
    auto* blurb = new QLabel(
        "Clivus views DICOM medical image folders: scan a folder, browse its series, and examine "
        "each slice. Intended for research and educational use.", m_emptyState);
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
    emptyLayout->addWidget(welcome);
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

    restoreLayout();   // roadmap 3.1: geometry, splitter, sidebar and diagnostics from QSettings

    connect(m_seriesList, &QListWidget::currentRowChanged, this, &MainWindow::showSeries);
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
        m_status->setText(
            QString("Scan cancelled — the folder was left unread; you can open it again or pick "
                    "another one. The application is still usable."));
        return;
    }

    m_series = std::move(r.series);
    m_seriesList->clear();
    for (const auto& s : m_series) {
        auto* item = new QListWidgetItem(seriesLabel(s), m_seriesList);
        // Deliberately not the patient name: ordinary hover tooltips stay free of identifying
        // metadata (roadmap 1.3 / 3.5). The description and modality line are enough to pick a series.
        item->setToolTip(seriesLabel(s));
    }

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
        QListWidgetItem* item = m_seriesList->item(i);
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
        pumpSlice();
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
    // Colour frames ignore the window (render() passes their samples through), so the fields stay
    // visible but disabled, with the reason in the options bar as well as the overlay (roadmap 2.2).
    m_windowBar->setWindowingAvailable(
        !m_currentIsColour,
        m_currentIsColour ? QString("Window/level does not apply to colour images")
                          : QString());
    m_presetBar->setAvailable(
        !m_currentIsColour,
        m_currentIsColour ? QString("Presets need a grayscale image") : QString());
    // The strip lands on the slice that actually arrived; if the wheel had moved on, the check above
    // already returned and the next decode will settle it.
    m_sliceBar->setCurrentSlice(r.index);
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
        m_status->setText("Cancelling — the scan stops after the object it is currently reading; you can keep using the application meanwhile.");
        return;
    }
    if (m_series.empty()) {
        m_status->setText("Cancelled — nothing was loaded. You can open a folder again at any time.");
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
        m_sliceBar->setSliceCount(0);
        m_status->setText("This series has no image objects in the index — try another series; the application remains usable.");
        return;
    }

    ++m_epoch;              // a decode still running belongs to the series we are leaving
    m_currentSeries = index;
    m_currentSlice = -1;    // nothing decoded for this series yet
    m_timing.clear();
    m_windowLevel.clear();
    m_canvas->setFrame(clivus::ImageFrame{});
    // The strip's range is the series' flat frame count, so a multi-frame object counts as the
    // slices it actually carries (the loader already flattens them).
    m_sliceBar->setSliceCount(m_series[size_t(index)].totalFrames());
    m_sliceBar->setCurrentSlice(0);
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
    const int total = m_series[size_t(m_currentSeries)].totalFrames();
    if (total <= 0) return;
    const int target = std::clamp(index, 0, total - 1);
    if (target == m_desiredSlice && (m_sliceInFlight || target == m_currentSlice)) return;
    m_desiredSlice = target;
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
    if (!extra.isEmpty()) left << extra;

    QString right;
    if (m_currentIsColour) {
        right = "colour frame — window/level not applied";
    } else if (!m_windowLevel.isEmpty()) {
        right = m_windowLevel;
    }
    const double zoom = m_canvas->zoomFactor();
    if (zoom > 0.0) right += QString(right.isEmpty() ? "" : "   ") + QString("%1%").arg(int(zoom * 100.0));
    if (m_showDiagnostics && !m_timing.isEmpty()) right += "\n" + m_timing;

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
    QMessageBox::about(this, "About Clivus",
        QString("<b>Clivus %1</b><br><br>"
                "A DICOM viewer.<br><br>"
                "This program is free software: you can redistribute it and/or modify it under "
                "the terms of the GNU General Public License as published by the Free Software "
                "Foundation, either version 3 of the License, or (at your option) any later "
                "version. This program is distributed in the hope that it will be useful, but "
                "WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or "
                "FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more "
                "details.<br><br>"
                "Clivus is intended for <b>research and educational use</b>. It is not a "
                "substitute for a clinically validated diagnostic workstation, and its output "
                "should not be relied upon for clinical decisions without appropriate "
                "independent validation.")
            .arg(QString::fromLatin1(CLIVUS_VERSION)));
}