// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QFutureWatcher>
#include <QMainWindow>
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

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

    void openFolder(const QString& dir);
    void runBenchmark(int frames, int seriesRow = 0);

    // The tools (and their enum) are public API: layouts and tests address the active tool.
    enum class ActiveTool { WindowLevel, Pan, Pointer, Measure };
    void setActiveTool(ActiveTool tool);
    int prefetchIndex() const { return m_prefetchIndex; }   // test probe: the smooth-scroll slot
    bool mprAvailable() const { return m_mprAction && m_mprAction->isEnabled(); }
    // Documentation capture support: true once a slice is decoded and on the canvas.
    bool canvasReady() const;
    void openMprForTest() { openMprView(); }

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

    QLineEdit* m_seriesSearch = nullptr;   // roadmap 3.2: filters the list by description/modality
    QListWidget* m_seriesList = nullptr;
    QSplitter* m_splitter = nullptr;
    QStackedWidget* m_centralStack = nullptr;
    QWidget* m_emptyState = nullptr;
    HelpWindow* m_help = nullptr;
    CanvasWidget* m_canvas = nullptr;
    SliceBar* m_sliceBar = nullptr;
    WindowLevelBar* m_windowBar = nullptr;
    WindowPresetBar* m_presetBar = nullptr;
    QLabel* m_status = nullptr;
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

    // Loading runs off the UI thread. Two pools on purpose: the folder scan and each slice decode go
    // on m_foreground (one thread), so the first slice of a folder never queues behind that folder's
    // thumbnails; the thumbnails themselves go on the global pool, one task per series.
    QThreadPool m_foreground;
    std::shared_ptr<clivus::ScanControl> m_scanControl;
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
    int m_benchFrames = 0;     // pending --bench request, honoured once the first slice is on screen
    int m_benchRow = 0;
};
