// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QDialog>
#include <QFutureWatcher>

#include "clivus/image_frame.h"
#include "clivus/volume.h"

class QComboBox;
class QSpinBox;

class CanvasWidget;
class QLabel;
class SliceBar;

// First-round MPR (research/experimental): an opt-in three-up window with axial / coronal /
// sagittal planes resliced from the series' own geometry. Deliberately opt-in — the single-series
// workflow stays the default view; MPR adds a window, never replaces it.
//
// Scroll any plane (wheel on the image, drag or click its strip) and the other two show hairline
// crosshairs through that position; scroll positions convert through patient coordinates (the
// stored orientation cosines and spacings), not approximations. Window/level is synced across the
// three planes by default: a drag on any plane adjusts all of them from one state.
//
// Volume building runs on the foreground pool with progress and cancel — a large study needs
// seconds, not a frozen UI. A series the query says has no usable geometry never opens this view
// (the menu entry disables before you can click it).
class MprView : public QDialog {
    Q_OBJECT
public:
    explicit MprView(clivus::SeriesInfo series, QWidget* parent = nullptr);

    int planesShown() const { return int(m_planes.size()); }
    int planeIndex(int axis) const;   // test probe: the plane's current slice position
    // Test probe: drive a plane's position through the same path the strip/wheel use.
    void setPositionForTest(int axis, int position) { setPlanePosition(axis, position); }
    // Test probe: choose reformat options through the same path the combo boxes use.
    void setReformatForTest(bool linear, int projection, double thicknessMm);
    bool haveVolumeForTest() const { return m_haveVolume; }

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    struct Plane {
        CanvasWidget* canvas = nullptr;
        SliceBar* strip = nullptr;
        QLabel* label = nullptr;
        int index = 0;   // current slice position within the plane's own coordinate
        int maxIndex = 0;
    };

    void startVolumeLoad();
    void onVolumeLoaded();
    void presentPlane(Plane& plane);
    void setPlanePosition(int axis, int position);
    void rebuildOptions();   // interpolation / slab controls changed: restate and redraw

    clivus::SeriesInfo m_series;
    clivus::Volume m_volume;
    bool m_haveVolume = false;
    bool m_syncingWindow = false;   // re-entrancy guard: window/level echo across planes
    bool m_loadCancelled = false;
    QLabel* m_status = nullptr;
    QComboBox* m_interpolation = nullptr;
    QComboBox* m_projection = nullptr;
    QSpinBox* m_thickness = nullptr;
    clivus::ResliceOptions m_options;
    QFutureWatcher<std::pair<bool, clivus::Volume>>* m_loadWatcher = nullptr;

    // Order: axial, coronal, sagittal (MprAxis order).
    std::vector<Plane> m_planes;
};
