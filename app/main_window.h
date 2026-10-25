// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QFutureWatcher>
#include <QMainWindow>
#include <QSet>

class QTimer;
#include <QTimer>
#include <QThreadPool>
#include <memory>
#include <vector>

#include "clivus/image_frame.h"
#include "clivus/index_store.h"
#include "mpr_view.h"

#include "clivus/series_index.h"
#include "settings_names.h"

class CanvasWidget;
class HelpWindow;
class SliceBar;
class WindowLevelBar;
class WindowPresetBar;
class QAction;
class QActionGroup;
class QCloseEvent;
class QLabel;
class QListWidget;
class QLineEdit;
class QProgressBar;
class QPushButton;
class QSplitter;
class QStackedWidget;
class QTimer;
#include <QTimer>

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

    void openFolder(const QString& dir);
    void runBenchmark(int frames, int seriesRow = 0);

    // The tools (and their enum) are public API: layouts and tests address the active tool.
    enum class ActiveTool { WindowLevel, Pan, Pointer, Measure, RoiRect, RoiEllipse };
    void setActiveTool(ActiveTool tool);

    // Annotations (roadmap D2): ROIs belong to one slice of one series. The window converts between
    // the canvas's in-memory list and the per-series sidecar, so the canvas never touches files.
    QString currentSeriesUid() const;
    // Test/introspection only: the Series Instance UID the annotation sidecar is keyed by.
    std::string seriesUidForTest() const { return currentSeriesUid().toStdString(); }
    // The row a series occupies in the browser. Not the series index: group headers and the
    // "no matching series" placeholder can sit above the series rows.
    int rowForSeriesIndex(int seriesIndex) const;
    // The folder epoch a batch has to carry to be accepted, and the batch entry point itself. Exposed
    // so the staleness rule can be tested without waiting on a real multi-second scan.
    quint64 folderEpochForTest() const { return m_folderEpoch; }
    void applyScanBatchForTest(quint64 epoch, std::vector<clivus::SeriesInfo> batch)
    {
        applyScanBatch(epoch, std::move(batch));
    }
    int currentSliceForTest() const { return m_currentSlice; }
    int frameCountForTest() const
    {
        return (m_currentSeries >= 0 && m_currentSeries < int(m_series.size()))
                   ? m_series[size_t(m_currentSeries)].totalFrames()
                   : 0;
    }
    void showSliceForTest(int delta) { showSlice(delta); }   // user-like manual step
    // Documentation capture (main.cpp --capture): place one rectangle ROI over the middle of the
    // displayed frame, using the same path a drag takes, and report how many pixels it covered.
    int addRoiForCapture();
    // Documentation capture: composite the MPR dialog for a saved review, after its volume loads.
    bool captureMprImage(const QString& path);
    bool cineRunningForTest() const { return m_cineTimer != nullptr && m_cineTimer->isActive(); }
    void loadAnnotationsForSlice(int slice);
    void saveAnnotationsForSlice(int slice);
    void exportMeasurements();
    void exportCurrentImage();
    void exportSeriesImages();
    void exportDeidentifiedCopy();

    // Cine (roadmap D3): play the current series at a fixed rate through the ordinary decode pump, so
    // playback never decodes faster than the one-at-a-time path allows and never shows a stale slice
    // out of order. Any manual slice action stops it, as does changing series or folder.
    void toggleCine();
    void stopCine(const QString& reason = QString());
    void cineTick();
    int prefetchIndex() const { return m_prefetchIndex; }   // test probe: the smooth-scroll slot
    bool mprAvailable() const { return m_mprAction && m_mprAction->isEnabled(); }
    // Documentation capture support: true once a slice is decoded and on the canvas, and a way to
    // ask for a chosen series so a capture shows anatomy rather than whichever row sorts first.
    bool canvasReady() const;
    bool selectSeriesForCapture(const QString& modality);
    // Documentation capture: QWidget::grab() does not composite a QOpenGLWidget's framebuffer, so
    // the capture path asks for the canvas separately and pastes it in. Both pieces are real
    // renders from this process.
    QImage captureCanvasImage();
    QPoint captureCanvasPosition() const;
    bool hasSeriesForCapture() const { return m_canvas != nullptr && !m_series.empty(); }
    void openMprForTest() { openMprView(); }

    // Browser groups: collapse and expand through the same entry points the mouse and the keyboard
    // use, so a test drives the real path instead of a copy of the rule. A row that is not a group
    // header is not a toggle target and these refuse it.
    bool toggleGroupAtRowForTest(int row) { return toggleGroupCollapse(row); }
    bool rowIsGroupHeaderForTest(int row) const;
    QString groupKeyAtRowForTest(int row) const;

    // Scroll scheduling, made observable (v0.4.1, radiology report: "scrolling through the stack feels
    // jerky"). A jerk is not a number, so these are: how many targets were asked for, how many frames
    // reached the canvas, how many of those were not the slice the wheel was pointing at any more, and
    // how often a slice was decoded twice because the prefetch and the request path both asked for it.
    struct ScrollStats {
        int requests = 0;
        int presents = 0;
        int stalePresents = 0;      // decoded a slice the wheel had already moved past
        int prefetchStarts = 0;
        int prefetchServed = 0;     // presented straight from the speculative slot
        std::vector<double> latencyMs;   // request -> present, for the targets that were shown
    };
    const ScrollStats& scrollStatsForTest() const { return m_scrollStats; }
    const std::vector<int>& presentedSlicesForTest() const { return m_presentedSlices; }
    void resetScrollStatsForTest();

protected:
    void closeEvent(QCloseEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;   // drop a folder or DICOM file anywhere
    void dropEvent(QDropEvent* event) override;

    bool eventFilter(QObject* watched, QEvent* event) override;   // roadmap 3.4: Escape in the search box

private:

    // Whatever a worker hands back carries the request it answers, so a result that arrives after
    // the user moved on is dropped instead of shown.
    struct ScanResult {
        std::vector<clivus::SeriesInfo> series;
        bool cancelled = false;
    };
    struct SliceResult {
        clivus::ImageFrame frame;
        int index = -1;
        quint64 epoch = 0;
        bool ok = false;
    };

    void showSeries(int index);
    void showSlice(int delta);
    void goToSlice(int index);   // absolute: the slice slider's direct navigation
    void setWindowLevel(double center, double width);   // single writer of the rendering state
    void updateInfo(const QString& extra = {});

    void startScan();
    void updateScanProgress();
    void scanFinished();
    // A snapshot of the series found so far, delivered while the scan is still running. Ignored when
    // it belongs to a folder the user has already left.
    void applyScanBatch(quint64 folderEpoch, std::vector<clivus::SeriesInfo> batch);
    // Rebuilds the rows from m_series, keeping the row whose series UID is keepUid selected.
    void rebuildSeriesRows(const QString& keepUid);
    void startThumbnails();
    void fillReadyThumbnails();
    void pumpSlice();
    void sliceFinished();
    void presentSlice(int index, clivus::ImageFrame frame);
    void onPrefetchLanded();
    void cancelLoading();
    void startBenchmark();
    void setIdleProgress();
    void showManual();
    void showAbout();
private:   // one source of truth: toolbar, menu, cursor, hint
    void openMprView();
    void saveLayout();     // roadmap 3.1: geometry/splitter/sidebar/diagnostics on close
    void restoreLayout();  // roadmap 3.1: guarded restore with built-in fallbacks
    void resetLayout();    // roadmap 3.1: View ▸ Reset Layout back to the built-in default
    void applySeriesFilter();   // roadmap 3.2: hide/show rows from the search text
    // Collapse state of the browser's groups. Keyed by the identifiers the model already validated
    // (clivus::ui::kGroupKeyRole), never by row number or label: every scan batch rebuilds the rows,
    // and two patients can share a name.
    bool toggleGroupCollapse(int row);
    void applyCollapseState(bool filtering);

    QLineEdit* m_seriesSearch = nullptr;   // roadmap 3.2: filters the list by description/modality
    QListWidget* m_seriesList = nullptr;
    // v0.4.1: a folder of studies is a tree on screen now. A collapsed patient hides its studies and
    // series; a collapsed study hides its series. Expanded is the default, so nothing a 0.4.0 user
    // learned changes until they ask for it.
    QSet<QString> m_collapsedGroups;
    // Moving the caret onto a header is how the mouse collapses a group; it must not also re-open that
    // group's first series, so the selection handler stays quiet for exactly that one move.
    bool m_suppressRowActivation = false;
    QSplitter* m_splitter = nullptr;
    QStackedWidget* m_centralStack = nullptr;
    QWidget* m_emptyState = nullptr;
    HelpWindow* m_help = nullptr;
    CanvasWidget* m_canvas = nullptr;
    SliceBar* m_sliceBar = nullptr;
    WindowLevelBar* m_windowBar = nullptr;
    WindowPresetBar* m_presetBar = nullptr;
    QLabel* m_status = nullptr;
    // The pixel readout sits in its own permanent status-bar slot: it changes on every pointer move and
    // must not fight the slice/status text, which is rebuilt on every slice change.
    QLabel* m_pixelReadout = nullptr;
    QProgressBar* m_progress = nullptr;
    QPushButton* m_cancel = nullptr;
    QTimer* m_scanTicker = nullptr;
    // Roadmap 2.1/2.6: which tool the toolbar and menu are highlighting. Both drags always work;
    // this only decides the cursor and the status hint, so the UI cannot put the canvas into a mode
    // the user cannot get out of.
    ActiveTool m_activeTool = ActiveTool::WindowLevel;
    QActionGroup* m_toolGroup = nullptr;
    QAction* m_windowAction = nullptr;
    QAction* m_panAction = nullptr;
    QAction* m_pointerAction = nullptr;
    QAction* m_measureAction = nullptr;
    QAction* m_roiRectAction = nullptr;
    QAction* m_roiEllipseAction = nullptr;
    QAction* m_cineAction = nullptr;
    QTimer* m_cineTimer = nullptr;
    bool m_cineAdvancing = false;   // true only while cine itself is stepping slices
    int m_cineFps = 12;
    QAction* m_mprAction = nullptr;
    QAction* m_diagnosticsAction = nullptr;   // kept for layout save/restore (roadmap 3.1)
    bool m_showDiagnostics = false;   // persisted (roadmap 3.1): the toggle survives a restart
    std::vector<clivus::SeriesInfo> m_series;
    QString m_folder;
    int m_currentSeries = -1;
    int m_currentSlice = -1;   // -1: nothing decoded for the current series yet
    bool m_currentIsColour = false;   // window/level means nothing for a colour frame
    QString m_timing;
    QString m_windowLevel;
    // Canonical window/level for the frame on screen. Every control reads these and every change goes
    // through setWindowLevel(), which is what keeps one source of truth (roadmap 2.2/6.3).
    double m_center = 0.0;
    double m_width = 1.0;

    // Loading runs off the UI thread. Three pools on purpose. The folder scan and each slice decode go
    // on m_foreground (one thread), so the first slice of a folder never queues behind that folder's
    // thumbnails; the thumbnails go on the global pool, one task per series. The speculative neighbour
    // decode gets its own single-thread pool: QtConcurrent cannot cancel a running or queued task, so
    // while the prefetch shared m_foreground a direction reversal made the frame the user actually
    // asked for queue behind a decode of the frame they had just scrolled away from.
    QThreadPool m_foreground;
    QThreadPool m_prefetch;
    std::shared_ptr<clivus::ScanControl> m_scanControl;
    quint64 m_folderEpoch = 0;   // bumped on every open: a batch from an older folder is stale
    QFutureWatcher<ScanResult>* m_scanWatcher = nullptr;
    QFutureWatcher<QImage>* m_thumbWatcher = nullptr;
    QFutureWatcher<SliceResult>* m_sliceWatcher = nullptr;
    quint64 m_epoch = 0;       // bumped when the on-screen series changes; stale decodes are dropped
    int m_desiredSlice = -1;   // where the wheel wants to be; decodes catch up to it one at a time
    int m_failedSlice = -1;    // last slice a decode failed on: pumpSlice() refuses to auto-retry it
    bool m_lastStep = true;    // the wheel's travel direction (true = forward); drives the prefetch
    bool m_hasStep = false;    // before the first step, the lookahead stays quiet
    QFutureWatcher<clivus::ImageFrame>* m_prefetchWatcher = nullptr;
    clivus::ImageFrame m_prefetchFrame;   // the parked, still-unpresented neighbour frame
    int m_prefetchIndex = -1;             // which slice the parked frame (or the in-flight one) is
    bool m_sliceInFlight = false;
    // Scroll scheduling measurement, and the monotonic clock the latency is measured against. Both are
    // read by the scroll test; nothing in the product depends on them.
    ScrollStats m_scrollStats;
    std::vector<int> m_presentedSlices;
    QElapsedTimer m_requestClock;
    int m_benchFrames = 0;     // pending --bench request, honoured once the first slice is on screen
    int m_benchRow = 0;
};
