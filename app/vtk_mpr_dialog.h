// SPDX-License-Identifier: GPL-3.0-or-later
// Phase 3 + 4, delivered as a dialog: three VTK planes (axial, coronal,
// sagittal) with a linked crosshair, plus GPU volume rendering, for one
// series. It mirrors MprView's shape on purpose (SeriesInfo in, volume
// decoded on a worker thread, sliders per plane) so the two views can be
// compared side by side while the VTK path earns trust.
//
// Linked crosshair semantics, stated so they can be checked: the dialog
// owns one shared point (indices i, j, k + its world position). Clicking
// or dragging on any plane sets all three indices from the picked world
// point, so all planes jump. Moving one plane's slider changes only that
// plane's index; the other planes keep their slices and their crosshair
// lines move to the shared point's new projection. Slabs are CPU
// resliceVolume() frames (the same code the legacy MPR uses) shown in the
// plane widgets: the slab is centred on the plane's current index and
// rebuilt when the slider, projection, thickness or interpolation changes.
// The interpolation control applies to slab computation only; plain planes
// are direct VTK slices and VTK does its own resampling there.
#pragma once

#include <QDialog>

#include <QFutureWatcher>

#include <array>
#include <utility>

#include "clivus/series_index.h"
#include "clivus/volume.h"

class QCheckBox;
class QCloseEvent;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QSlider;
class QSpinBox;
class VtkPlaneWidget;
class VtkVolumeWidget;

class VtkMprDialog : public QDialog {
    Q_OBJECT
public:
    explicit VtkMprDialog(clivus::SeriesInfo series, QWidget* parent = nullptr);

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    struct PlaneCell {
        VtkPlaneWidget* widget = nullptr;
        QSlider* slider = nullptr;
        QLabel* count = nullptr;
    };

    void startVolumeLoad();
    void onVolumeLoaded();
    void applyPlanes();              // volume mode or slab mode, per current options
    void rebuildSlab(int axis);      // one plane's slab frame, if slabs are on
    void setSharedIndices(int i, int j, int k);   // from a click: all planes jump
    void setPlaneIndex(int axis, int index);      // from a slider: one plane moves
    void updateCrosshairs();
    void updateCountLabels();
    void worldToIndices(const double world[3], int indices[3]) const;
    void indicesToWorld(const int indices[3], double world[3]) const;
    clivus::MprAxis axisForPlane(int axis) const;

    void onPlanePointChanged(double x, double y, double z);
    void onPlaneSliceStep(int axis, int delta);
    void onSliderMoved(int axis, int value);
    void onWindowChanged();
    void onSlabOptionsChanged();
    void onResetViews();

    clivus::SeriesInfo m_series;
    clivus::Volume m_volume;
    bool m_haveVolume = false;
    bool m_loadCancelled = false;
    QFutureWatcher<std::pair<bool, clivus::Volume>>* m_loadWatcher = nullptr;

    std::array<PlaneCell, 3> m_planes;
    VtkVolumeWidget* m_volumeWidget = nullptr;
    QLabel* m_status = nullptr;
    QDoubleSpinBox* m_centerSpin = nullptr;
    QDoubleSpinBox* m_widthSpin = nullptr;
    QComboBox* m_projection = nullptr;
    QSpinBox* m_thickness = nullptr;
    QComboBox* m_interpolation = nullptr;
    QCheckBox* m_crosshairBox = nullptr;

    int m_index[3] = {0, 0, 0};   // i (columns), j (rows), k (slices)
    double m_point[3] = {0.0, 0.0, 0.0};   // shared world point
};
