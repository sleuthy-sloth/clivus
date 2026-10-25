// SPDX-License-Identifier: GPL-3.0-or-later
#include "vtk_mpr_dialog.h"

#include "vtk_plane_widget.h"
#include "vtk_volume_widget.h"

#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QThreadPool>
#include <QVBoxLayout>
#include <QtConcurrent>

#include <algorithm>
#include <cmath>

namespace {

const char* kPlaneNames[3] = {"Axial", "Coronal", "Sagittal"};

}  // namespace

VtkMprDialog::VtkMprDialog(clivus::SeriesInfo series, QWidget* parent)
    : QDialog(parent), m_series(std::move(series))
{
    setWindowTitle(tr("VTK view (experimental)"));
    setMinimumSize(900, 700);

    auto* root = new QVBoxLayout(this);
    auto* grid = new QGridLayout();
    const VtkPlaneWidget::Orientation orientations[3] = {VtkPlaneWidget::Orientation::Axial,
                                                         VtkPlaneWidget::Orientation::Coronal,
                                                         VtkPlaneWidget::Orientation::Sagittal};
    const int gridPos[3][2] = {{0, 0}, {0, 1}, {1, 0}};
    for (int axis = 0; axis < 3; ++axis) {
        auto* cell = new QVBoxLayout();
        auto* plane = new VtkPlaneWidget(orientations[axis], this);
        auto* row = new QHBoxLayout();
        auto* label = new QLabel(tr(kPlaneNames[axis]), this);
        auto* slider = new QSlider(Qt::Horizontal, this);
        slider->setRange(0, 0);
        auto* count = new QLabel(QStringLiteral("—"), this);
        row->addWidget(label);
        row->addWidget(slider, 1);
        row->addWidget(count);
        cell->addWidget(plane, 1);
        cell->addLayout(row);
        grid->addLayout(cell, gridPos[axis][0], gridPos[axis][1]);
        m_planes[size_t(axis)] = PlaneCell{plane, slider, count};
        connect(plane, &VtkPlaneWidget::worldPointChanged, this,
                &VtkMprDialog::onPlanePointChanged);
        connect(plane, &VtkPlaneWidget::sliceStepRequested, this,
                [this, axis](int delta) { onPlaneSliceStep(axis, delta); });
        connect(slider, &QSlider::valueChanged, this,
                [this, axis](int value) { onSliderMoved(axis, value); });
    }
    m_volumeWidget = new VtkVolumeWidget(this);
    auto* volumeCell = new QVBoxLayout();
    volumeCell->addWidget(m_volumeWidget, 1);
    volumeCell->addWidget(new QLabel(tr("Volume (GPU ray cast)"), this));
    grid->addLayout(volumeCell, 1, 1);
    root->addLayout(grid, 1);

    auto* controls = new QHBoxLayout();
    controls->addWidget(new QLabel(tr("Center"), this));
    m_centerSpin = new QDoubleSpinBox(this);
    m_centerSpin->setRange(-1000000.0, 1000000.0);
    m_centerSpin->setDecimals(1);
    m_centerSpin->setSingleStep(10.0);
    controls->addWidget(m_centerSpin);
    controls->addWidget(new QLabel(tr("Width"), this));
    m_widthSpin = new QDoubleSpinBox(this);
    m_widthSpin->setRange(1.0, 1000000.0);
    m_widthSpin->setDecimals(1);
    m_widthSpin->setSingleStep(10.0);
    controls->addWidget(m_widthSpin);
    controls->addWidget(new QLabel(tr("Slab"), this));
    m_projection = new QComboBox(this);
    m_projection->addItems({tr("Off"), tr("MIP"), tr("MinIP")});
    controls->addWidget(m_projection);
    m_thickness = new QSpinBox(this);
    m_thickness->setRange(1, 100);
    m_thickness->setValue(10);
    m_thickness->setSuffix(tr(" mm"));
    controls->addWidget(m_thickness);
    controls->addWidget(new QLabel(tr("Interpolation"), this));
    m_interpolation = new QComboBox(this);
    m_interpolation->addItems({tr("Nearest"), tr("Linear")});
    m_interpolation->setToolTip(tr("Applies to slab computation only; plain planes are direct VTK slices."));
    controls->addWidget(m_interpolation);
    m_crosshairBox = new QCheckBox(tr("Crosshair"), this);
    m_crosshairBox->setChecked(true);
    controls->addWidget(m_crosshairBox);
    auto* reset = new QPushButton(tr("Reset views"), this);
    controls->addWidget(reset);
    controls->addStretch(1);
    root->addLayout(controls);

    connect(m_centerSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            &VtkMprDialog::onWindowChanged);
    connect(m_widthSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            &VtkMprDialog::onWindowChanged);
    connect(m_projection, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &VtkMprDialog::onSlabOptionsChanged);
    connect(m_thickness, QOverload<int>::of(&QSpinBox::valueChanged), this,
            &VtkMprDialog::onSlabOptionsChanged);
    connect(m_interpolation, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &VtkMprDialog::onSlabOptionsChanged);
    connect(m_crosshairBox, &QCheckBox::toggled, this, &VtkMprDialog::updateCrosshairs);
    connect(reset, &QPushButton::clicked, this, &VtkMprDialog::onResetViews);

    auto* footnote = new QLabel(
        tr("Experimental: planes use VTK's window/level mapping (within a grey level of DICOM for "
           "typical widths); the 2D canvas uses the parity-tested DICOM LUT mapping."),
        this);
    footnote->setWordWrap(true);
    root->addWidget(footnote);

    m_status = new QLabel(tr("Loading volume…"), this);
    root->addWidget(m_status);

    onSlabOptionsChanged();   // interpolation only applies while a slab is on
    startVolumeLoad();
}

void VtkMprDialog::closeEvent(QCloseEvent* event)
{
    m_loadCancelled = true;
    QDialog::closeEvent(event);
}

void VtkMprDialog::startVolumeLoad()
{
    m_loadWatcher = new QFutureWatcher<std::pair<bool, clivus::Volume>>(this);
    connect(m_loadWatcher, &QFutureWatcherBase::finished, this, &VtkMprDialog::onVolumeLoaded);
    const clivus::SeriesInfo series = m_series;   // a private copy: the dialog may die mid-decode
    m_loadWatcher->setFuture(QtConcurrent::run(QThreadPool::globalInstance(), [series]() {
        clivus::Volume volume;
        const bool loaded = clivus::loadVolume(series, volume);
        return std::pair<bool, clivus::Volume>{loaded, std::move(volume)};
    }));
}

void VtkMprDialog::onVolumeLoaded()
{
    if (m_loadCancelled) return;
    const auto result = m_loadWatcher->result();
    m_haveVolume = result.first;
    if (!m_haveVolume) {
        m_status->setText(tr("The study could not be loaded for the VTK view (it failed to decode, "
                             "or its slices disagree on size). The main viewer is unaffected."));
        return;
    }
    m_volume = std::move(result.second);
    for (int axis = 0; axis < 3; ++axis) m_planes[size_t(axis)].widget->setVolume(m_volume);
    m_volumeWidget->setVolume(m_volume);

    // Each plane navigates its own coordinate: axial slices, coronal rows,
    // sagittal columns — the same convention as the legacy MPR view.
    m_planes[0].slider->setRange(0, m_volume.depth - 1);
    m_planes[1].slider->setRange(0, m_volume.height - 1);
    m_planes[2].slider->setRange(0, m_volume.width - 1);
    m_index[0] = m_volume.width / 2;
    m_index[1] = m_volume.height / 2;
    m_index[2] = m_volume.depth / 2;
    indicesToWorld(m_index, m_point);

    const double center = (double(m_volume.minValue) + double(m_volume.maxValue)) / 2.0;
    const double width = std::max(1.0, double(m_volume.maxValue) - double(m_volume.minValue));
    m_centerSpin->setValue(center);
    m_widthSpin->setValue(width);
    onWindowChanged();

    for (int axis = 0; axis < 3; ++axis) {
        m_planes[size_t(axis)].slider->setValue(
            axis == 0 ? m_index[2] : axis == 1 ? m_index[1] : m_index[0]);
        m_planes[size_t(axis)].widget->setSlice(
            axis == 0 ? m_index[2] : axis == 1 ? m_index[1] : m_index[0]);
    }
    updateCrosshairs();
    updateCountLabels();
    m_status->setText(tr("VTK view ready."));
}

clivus::MprAxis VtkMprDialog::axisForPlane(int axis) const
{
    return axis == 0 ? clivus::MprAxis::Axial
                     : axis == 1 ? clivus::MprAxis::Coronal
                                 : clivus::MprAxis::Sagittal;
}

void VtkMprDialog::worldToIndices(const double world[3], int indices[3]) const
{
    // Direction orthonormal, so the inverse is the transpose: index along
    // an axis is the dot product with that axis's direction over its spacing.
    const double dx = world[0] - m_volume.originX;
    const double dy = world[1] - m_volume.originY;
    const double dz = world[2] - m_volume.originZ;
    const double fi = (dx * m_volume.orientRowX + dy * m_volume.orientRowY +
                       dz * m_volume.orientRowZ) /
                      m_volume.spacingX;
    const double fj = (dx * m_volume.orientColX + dy * m_volume.orientColY +
                       dz * m_volume.orientColZ) /
                      m_volume.spacingY;
    const double fk = (dx * m_volume.normalX + dy * m_volume.normalY + dz * m_volume.normalZ) /
                      m_volume.spacingZ;
    indices[0] = std::clamp(int(std::lround(fi)), 0, m_volume.width - 1);
    indices[1] = std::clamp(int(std::lround(fj)), 0, m_volume.height - 1);
    indices[2] = std::clamp(int(std::lround(fk)), 0, m_volume.depth - 1);
}

void VtkMprDialog::indicesToWorld(const int indices[3], double world[3]) const
{
    world[0] = m_volume.originX + m_volume.orientRowX * indices[0] * m_volume.spacingX +
               m_volume.orientColX * indices[1] * m_volume.spacingY +
               m_volume.normalX * indices[2] * m_volume.spacingZ;
    world[1] = m_volume.originY + m_volume.orientRowY * indices[0] * m_volume.spacingX +
               m_volume.orientColY * indices[1] * m_volume.spacingY +
               m_volume.normalY * indices[2] * m_volume.spacingZ;
    world[2] = m_volume.originZ + m_volume.orientRowZ * indices[0] * m_volume.spacingX +
               m_volume.orientColZ * indices[1] * m_volume.spacingY +
               m_volume.normalZ * indices[2] * m_volume.spacingZ;
}

void VtkMprDialog::setSharedIndices(int i, int j, int k)
{
    if (!m_haveVolume) return;
    m_index[0] = std::clamp(i, 0, m_volume.width - 1);
    m_index[1] = std::clamp(j, 0, m_volume.height - 1);
    m_index[2] = std::clamp(k, 0, m_volume.depth - 1);
    indicesToWorld(m_index, m_point);
    const int perPlane[3] = {m_index[2], m_index[1], m_index[0]};
    for (int axis = 0; axis < 3; ++axis) {
        m_planes[size_t(axis)].widget->setSlice(perPlane[axis]);
        m_planes[size_t(axis)].slider->setValue(perPlane[axis]);
    }
    updateCrosshairs();
    updateCountLabels();
    applyPlanes();
}

void VtkMprDialog::setPlaneIndex(int axis, int index)
{
    if (!m_haveVolume) return;
    const int perPlaneMax[3] = {m_volume.depth - 1, m_volume.height - 1, m_volume.width - 1};
    index = std::clamp(index, 0, perPlaneMax[axis]);
    const int mapped = axis == 0 ? 2 : axis == 1 ? 1 : 0;   // plane axis -> volume index
    if (index == m_index[mapped]) return;
    m_index[mapped] = index;
    indicesToWorld(m_index, m_point);
    m_planes[size_t(axis)].widget->setSlice(index);
    updateCrosshairs();
    updateCountLabels();
    if (m_projection->currentIndex() != 0) rebuildSlab(axis);
}

void VtkMprDialog::updateCrosshairs()
{
    const bool visible = m_haveVolume && m_crosshairBox->isChecked();
    for (int axis = 0; axis < 3; ++axis)
        m_planes[size_t(axis)].widget->setCrosshair(m_point, visible);
}

void VtkMprDialog::updateCountLabels()
{
    if (!m_haveVolume) return;
    const int perPlane[3] = {m_index[2], m_index[1], m_index[0]};
    const int counts[3] = {m_volume.depth, m_volume.height, m_volume.width};
    for (int axis = 0; axis < 3; ++axis)
        m_planes[size_t(axis)].count->setText(
            QStringLiteral("%1 / %2").arg(perPlane[axis] + 1).arg(counts[axis]));
}

void VtkMprDialog::applyPlanes()
{
    if (!m_haveVolume) return;
    if (m_projection->currentIndex() == 0) {
        for (int axis = 0; axis < 3; ++axis) m_planes[size_t(axis)].widget->restoreVolume();
        return;
    }
    for (int axis = 0; axis < 3; ++axis) rebuildSlab(axis);
}

void VtkMprDialog::rebuildSlab(int axis)
{
    if (!m_haveVolume || m_projection->currentIndex() == 0) return;
    clivus::ResliceOptions options;
    options.interpolation = m_interpolation->currentIndex() == 1
                                ? clivus::ResliceInterpolation::Linear
                                : clivus::ResliceInterpolation::Nearest;
    options.projection = m_projection->currentIndex() == 1 ? clivus::SlabProjection::Maximum
                                                            : clivus::SlabProjection::Minimum;
    options.slabThicknessMm = double(m_thickness->value());
    const int position = axis == 0 ? m_index[2] : axis == 1 ? m_index[1] : m_index[0];
    clivus::ImageFrame frame;
    if (clivus::resliceVolume(m_volume, axisForPlane(axis), position, options, frame))
        m_planes[size_t(axis)].widget->setSlabFrame(frame);
    // A failed reslice leaves the plane where it was rather than blanking
    // it: the slab is a view option, not the image itself.
}

void VtkMprDialog::onPlanePointChanged(double x, double y, double z)
{
    if (!m_haveVolume) return;
    const double world[3] = {x, y, z};
    int indices[3];
    worldToIndices(world, indices);
    setSharedIndices(indices[0], indices[1], indices[2]);
}

void VtkMprDialog::onPlaneSliceStep(int axis, int delta)
{
    if (!m_haveVolume) return;
    m_planes[size_t(axis)].slider->setValue(m_planes[size_t(axis)].slider->value() + delta);
}

void VtkMprDialog::onSliderMoved(int axis, int value)
{
    setPlaneIndex(axis, value);
}

void VtkMprDialog::onWindowChanged()
{
    if (!m_haveVolume) return;
    const double center = m_centerSpin->value();
    const double width = m_widthSpin->value();
    for (int axis = 0; axis < 3; ++axis)
        m_planes[size_t(axis)].widget->setWindowLevel(center, width);
    m_volumeWidget->setWindowLevel(center, width);
}

void VtkMprDialog::onSlabOptionsChanged()
{
    const bool slabOn = m_projection->currentIndex() != 0;
    m_interpolation->setEnabled(slabOn);
    applyPlanes();
}

void VtkMprDialog::onResetViews()
{
    if (!m_haveVolume) return;
    setSharedIndices(m_volume.width / 2, m_volume.height / 2, m_volume.depth / 2);
    for (int axis = 0; axis < 3; ++axis) m_planes[size_t(axis)].widget->resetCamera();
    m_volumeWidget->resetCamera();
}
