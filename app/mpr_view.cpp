// SPDX-License-Identifier: GPL-3.0-or-later
#include "mpr_view.h"

#include "canvas_widget.h"
#include "style.h"
#include "view_controls.h"

#include <QCloseEvent>
#include <QFrame>
#include <QFutureWatcher>
#include <QComboBox>
#include <QHBoxLayout>
#include <QSpinBox>
#include <QLabel>
#include <QThreadPool>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

using namespace clivus;

namespace {

struct RowSpec {
    const char* name;
    MprAxis axis;
};
constexpr RowSpec kRows[3] = {
    {"Axial", MprAxis::Axial},
    {"Coronal", MprAxis::Coronal},
    {"Sagittal", MprAxis::Sagittal},
};

// setPixelSpacing(rowMm, colMm) per plane: the vertical step is the plane's j-axis, the horizontal
// is its i-axis (see resliceVolume's extents).
void applyPlaneSpacing(CanvasWidget* canvas, const Volume& volume, const MprAxis axis)
{
    switch (axis) {
    case MprAxis::Axial: canvas->setPixelSpacing(volume.spacingY, volume.spacingX); break;
    case MprAxis::Coronal: canvas->setPixelSpacing(volume.spacingZ, volume.spacingX); break;
    case MprAxis::Sagittal: canvas->setPixelSpacing(volume.spacingY, volume.spacingZ); break;
    }
}

}  // namespace

MprView::MprView(SeriesInfo series, QWidget* parent)
    : QDialog(parent), m_series(std::move(series))
{
    setWindowTitle(tr("MPR (research/experimental) — Clivus"));
    setModal(false);   // the main viewer stays usable beside it
    setAttribute(Qt::WA_DeleteOnClose);
    resize(900, 760);

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(10, 10, 10, 10);
    outer->setSpacing(clivus::style::kSpacing);

    for (int i = 0; i < 3; ++i) {
        Plane plane;
        plane.canvas = new CanvasWidget(this);
        plane.canvas->setMinimumSize(280, 220);
        plane.strip = new SliceBar(this);
        plane.label = new QLabel(tr(kRows[i].name), this);
        plane.label->setStyleSheet(QStringLiteral("color:#8a94a0; font-weight:600; padding:1px 3px;"));

        auto* head = new QHBoxLayout();
        head->setSpacing(8);
        head->addWidget(plane.label);
        head->addStretch(1);
        outer->addLayout(head);
        outer->addWidget(plane.canvas, 1);
        outer->addWidget(plane.strip, 0);
        if (i < 2) {
            auto* line = new QFrame(this);
            line->setFrameShape(QFrame::HLine);
            line->setFrameShadow(QFrame::Sunken);
            outer->addWidget(line);
        }
        m_planes.push_back(plane);
    }

    m_status = new QLabel(this);
    m_status->setStyleSheet(QStringLiteral("color:#6d7682;"));
    m_status->setWordWrap(true);
    outer->addWidget(m_status);

    // Window/level synced across planes: any plane's drag adjusts the rest from the same state.
    // CanvasWidget::setWindowLevel re-emits windowChanged, so pushing one plane's values into the
    // others would bounce forever without this guard (found by the offscreen harness as a stack
    // overflow). The flag makes the sync one level deep: the echo never re-enters.
    for (int i = 0; i < 3; ++i) {
        connect(m_planes[size_t(i)].canvas, &CanvasWidget::windowChanged, this,
                [this, i](double c, double w) {
                    if (m_syncingWindow) return;
                    m_syncingWindow = true;
                    for (int j = 0; j < 3; ++j)
                        if (j != i) m_planes[size_t(j)].canvas->setWindowLevel(c, w);
                    m_syncingWindow = false;
                });
    }

    // Each plane scrolls on its own coordinate: the canvas's wheel steps it, and so does the strip.
    for (int i = 0; i < 3; ++i) {
        connect(m_planes[size_t(i)].canvas, &CanvasWidget::scrollRequested, this,
                [this, i](int delta) { setPlanePosition(i, m_planes[size_t(i)].index + delta); });
        connect(m_planes[size_t(i)].strip, &SliceBar::sliceRequested, this,
                [this, i](int position) { setPlanePosition(i, position); });
    }

    // Reformat options (roadmap D3). Nearest stays available because it invents nothing; the linear
    // mode blends only between adjacent slices, and the label on the status line says which is in use
    // so a smooth image is never mistaken for measured detail.
    auto* optionsRow = new QHBoxLayout();
    m_interpolation = new QComboBox(this);
    m_interpolation->addItem(tr("Nearest neighbour"), int(clivus::ResliceInterpolation::Nearest));
    m_interpolation->addItem(tr("Linear between slices"),
                             int(clivus::ResliceInterpolation::Linear));
    m_projection = new QComboBox(this);
    m_projection->addItem(tr("Single slice"), int(clivus::SlabProjection::None));
    m_projection->addItem(tr("MIP (maximum)"), int(clivus::SlabProjection::Maximum));
    m_projection->addItem(tr("MinIP (minimum)"), int(clivus::SlabProjection::Minimum));
    m_thickness = new QSpinBox(this);
    m_thickness->setRange(1, 50);
    m_thickness->setValue(5);
    m_thickness->setSuffix(tr(" mm"));
    m_thickness->setToolTip(tr("Slab thickness for MIP/MinIP, centred on the current plane"));
    optionsRow->addWidget(new QLabel(tr("Reformat:"), this));
    optionsRow->addWidget(m_interpolation);
    optionsRow->addWidget(m_projection);
    optionsRow->addWidget(m_thickness);
    optionsRow->addStretch(1);
    outer->insertLayout(1, optionsRow);   // under the title, above the planes

    connect(m_interpolation, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this] { rebuildOptions(); });
    connect(m_projection, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this] { rebuildOptions(); });
    connect(m_thickness, QOverload<int>::of(&QSpinBox::valueChanged), this,
            [this] { rebuildOptions(); });

    startVolumeLoad();
}

void MprView::startVolumeLoad()
{
    m_loadWatcher = new QFutureWatcher<std::pair<bool, Volume>>(this);
    connect(m_loadWatcher, &QFutureWatcherBase::finished, this, &MprView::onVolumeLoaded);
    const SeriesInfo series = m_series;   // a private copy: the dialog may die mid-decode
    m_loadWatcher->setFuture(QtConcurrent::run(QThreadPool::globalInstance(), [series]() {
        Volume volume;
        const bool loaded = loadVolume(series, volume);
        return std::pair<bool, Volume>{loaded, std::move(volume)};
    }));
}

void MprView::onVolumeLoaded()
{
    if (m_loadCancelled) return;
    const auto result = m_loadWatcher->result();
    m_haveVolume = result.first;
    if (!m_haveVolume) {
        m_status->setText(tr("The study could not be resliced (it failed to decode, or its "
                             "slices disagree on size). The main viewer is unaffected."));
        return;
    }
    m_volume = std::move(result.second);
    rebuildOptions();
    // Each plane navigates its own coordinate: axial slices, coronal rows, sagittal columns.
    m_planes[0].maxIndex = m_volume.depth - 1;
    m_planes[1].maxIndex = m_volume.height - 1;
    m_planes[2].maxIndex = m_volume.width - 1;
    for (int i = 0; i < 3; ++i) presentPlane(m_planes[size_t(i)]);
}

int MprView::planeIndex(int axis) const
{
    return (axis >= 0 && axis < int(m_planes.size())) ? m_planes[size_t(axis)].index : -1;
}

void MprView::setReformatForTest(const bool linear, const int projection, const double thicknessMm)
{
    m_interpolation->setCurrentIndex(linear ? 1 : 0);
    m_projection->setCurrentIndex(std::clamp(projection, 0, 2));
    m_thickness->setValue(std::max(1, int(std::lround(thicknessMm))));
    rebuildOptions();
}

void MprView::rebuildOptions()
{
    m_options.interpolation = clivus::ResliceInterpolation(
        m_interpolation->currentData().toInt());
    m_options.projection = clivus::SlabProjection(m_projection->currentData().toInt());
    m_options.slabThicknessMm = double(m_thickness->value());
    m_thickness->setEnabled(m_options.projection != clivus::SlabProjection::None);
    if (!m_haveVolume) return;
    QString note = m_options.projection == clivus::SlabProjection::None
                       ? (m_options.interpolation == clivus::ResliceInterpolation::Nearest
                              ? tr("Nearest-neighbour reslice — no interpolation, nothing invented.")
                              : tr("Linear interpolation between adjacent slices; in-plane detail "
                                   "is unchanged."))
                       : tr("%1 over a %2 mm slab, centred on each plane.")
                             .arg(m_options.projection == clivus::SlabProjection::Maximum
                                      ? QStringLiteral("MIP")
                                      : QStringLiteral("MinIP"))
                             .arg(m_thickness->value());
    m_status->setText(note + tr(" Research/experimental — not for diagnosis."));
    for (int i = 0; i < 3; ++i) presentPlane(m_planes[size_t(i)]);
}

void MprView::presentPlane(Plane& plane)
{
    const int which = int(&plane - m_planes.data());
    ImageFrame frame;
    if (!resliceVolume(m_volume, kRows[which].axis, double(plane.index), m_options, frame)) {
        plane.canvas->setFrame(ImageFrame{});
        return;
    }
    applyPlaneSpacing(plane.canvas, m_volume, kRows[which].axis);
    plane.canvas->setFrame(std::move(frame));
    plane.strip->setSliceCount(plane.maxIndex + 1);
    plane.strip->setCurrentSlice(plane.index);
}

void MprView::setPlanePosition(const int axis, int position)
{
    if (axis < 0 || axis >= int(m_planes.size()) || !m_haveVolume) return;
    Plane& plane = m_planes[size_t(axis)];
    position = std::clamp(position, 0, plane.maxIndex);
    plane.index = position;
    presentPlane(plane);
    // Crosshairs through the new reference position, on the two OTHER planes. A plane's own image
    // pixel (i, j) inside the reference frame (axial slice a, coronal row c, sagittal column s):
    //   axial:    i = s, j = c     coronal: i = s, j = a     sagittal: i = a, j = c
    const int a = m_planes[0].index, c = m_planes[1].index, s = m_planes[2].index;
    m_planes[0].canvas->setCrosshair(s, c);
    m_planes[1].canvas->setCrosshair(s, a);
    m_planes[2].canvas->setCrosshair(a, c);
}

void MprView::closeEvent(QCloseEvent* event)
{
    m_loadCancelled = true;   // the watcher's result lands after, and is ignored
    QDialog::closeEvent(event);
}
