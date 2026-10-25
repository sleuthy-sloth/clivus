// SPDX-License-Identifier: GPL-3.0-or-later
#include "vtk_canvas_widget.h"

#include "canvas_widget.h"   // OverlayWidget (the shared raster overlay)
#include "style.h"
#include "vtk_volume_bridge.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QWheelEvent>
#include <QtMath>

#include <vtkCamera.h>
#include <vtkInteractorStyleUser.h>
#include <vtkRenderWindowInteractor.h>

#include <algorithm>
#include <cmath>
#include <vector>

#include "clivus/roi.h"

namespace {

// VTK displays row 0 at the bottom (world +y is up); the legacy canvas
// draws row 0 at the top. Reverse the display image's rows at setFrame()
// so the picture matches — the stored frame is untouched.
void reverseRows(vtkImageData* image)
{
    if (image == nullptr) return;
    int dims[3] = {0, 0, 0};
    image->GetDimensions(dims);
    if (dims[0] <= 0 || dims[1] <= 1) return;
    const int components = image->GetNumberOfScalarComponents();
    const int scalarSize = image->GetScalarSize();
    const size_t rowBytes = size_t(dims[0]) * size_t(components) * size_t(scalarSize);
    auto* base = static_cast<unsigned char*>(image->GetScalarPointer());
    if (base == nullptr) return;
    std::vector<unsigned char> tmp(rowBytes);
    for (int j = 0; j < dims[1] / 2; ++j) {
        unsigned char* a = base + size_t(j) * rowBytes;
        unsigned char* b = base + size_t(dims[1] - 1 - j) * rowBytes;
        std::memcpy(tmp.data(), a, rowBytes);
        std::memcpy(a, b, rowBytes);
        std::memcpy(b, tmp.data(), rowBytes);
    }
}

// Overlay text follows the widget's own font instead of a fixed 10 pt,
// floor 10 pt, ceiling 18 pt — the same rule as the legacy canvas, so a
// measurement label is never the smallest thing on screen.
QFont vtkOverlayFont(const QFont& base)
{
    QFont font = base;
    const double size = font.pointSizeF() > 0.0 ? font.pointSizeF() : 10.0;
    font.setPointSizeF(std::clamp(size, 10.0, 18.0));
    return font;
}

}  // namespace

VtkCanvasWidget::VtkCanvasWidget(QWidget* parent) : QVTKOpenGLNativeWidget(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    setMinimumSize(320, 240);
    setToolHint(ToolHint::WindowLevel);

    m_renderWindow = vtkSmartPointer<vtkGenericOpenGLRenderWindow>::New();
    setRenderWindow(m_renderWindow);
    m_renderer = vtkSmartPointer<vtkRenderer>::New();
    m_renderer->SetBackground(0.0, 0.0, 0.0);
    m_renderWindow->AddRenderer(m_renderer);

    m_mapToColors = vtkSmartPointer<vtkImageMapToColors>::New();
    m_mapper = vtkSmartPointer<vtkImageSliceMapper>::New();
    m_mapper->SetSliceNumber(0);
    m_slice = vtkSmartPointer<vtkImageSlice>::New();
    m_slice->SetMapper(m_mapper);
    m_renderer->AddActor(m_slice);

    // The interactor must not also interpret gestures: every gesture is
    // handled in Qt below, mirroring the legacy canvas, and a VTK
    // interactor style acting on the same drag would double-apply it.
    // (The Qt handlers also never forward to the base class, so the
    // interactor sees nothing either way; this is belt and suspenders.)
    vtkSmartPointer<vtkInteractorStyleUser> idle = vtkSmartPointer<vtkInteractorStyleUser>::New();
    if (vtkRenderWindowInteractor* iact = interactor()) iact->SetInteractorStyle(idle);

    m_overlay = new OverlayWidget(this, [this](QPainter& p) { paintOverlays(p); },
                                  [this] { return hasValidFrame(); });
    m_overlay->setGeometry(rect());
}

void VtkCanvasWidget::setToolHint(ToolHint hint)
{
    m_toolHint = hint;
    switch (hint) {
    case ToolHint::Pan:
        setCursor(Qt::OpenHandCursor);
        break;
    case ToolHint::Pointer:
        setCursor(Qt::ArrowCursor);
        break;
    case ToolHint::Measure:
    case ToolHint::WindowLevel:
    default:
        setCursor(Qt::CrossCursor);   // crosshair says measuring mode, same as window/level
        break;
    }
}

void VtkCanvasWidget::setFrame(clivus::ImageFrame frame)
{
    // Same state rules as the legacy canvas: window/level is user state,
    // zoom/pan are viewport state. Both survive stepping through a series
    // and reset only when the image itself changes shape or goes away.
    const bool sameSlice = frame.valid() && m_lastFrameIndex == frame.frameIndex;
    m_lastFrameIndex = frame.frameIndex;
    const bool keepView =
        frame.valid() && m_frame.valid() && frame.width == m_frame.width && frame.height == m_frame.height;
    const int previousWidth = m_frame.width;
    const int previousHeight = m_frame.height;
    const int previousChannels = m_frame.channels;
    setPixelSpacing(frame.pixelSpacingRow, frame.pixelSpacingCol);
    if (!sameSlice) clearMeasurements();

    m_frame = std::move(frame);
    const bool newImage = !m_frame.valid() || m_frame.width != previousWidth ||
                          m_frame.height != previousHeight || m_frame.channels != previousChannels;
    if (newImage) m_frame.fullWindow(m_center, m_width);
    if (!keepView) {
        m_zoom = 1.0f;
        m_pan = QPointF(0, 0);
    }
    rebuildDisplay();
    syncCamera();
    emit windowChanged(m_center, m_width);
    updateOverlay();
}

void VtkCanvasWidget::setPixelSpacing(double rowSpacingMm, double colSpacingMm)
{
    m_spacingRow = rowSpacingMm;
    m_spacingCol = colSpacingMm;
    updateOverlay();
}

void VtkCanvasWidget::setWindowLevel(double center, double width)
{
    m_center = center;
    m_width = std::max(1.0, width);
    rebuildDisplay();
    m_renderWindow->Render();
    emit windowChanged(m_center, m_width);
    updateOverlay();
}

void VtkCanvasWidget::rebuildDisplay()
{
    if (!m_frame.valid()) {
        m_mapper->SetInputData(nullptr);
        return;
    }
    vtkSmartPointer<vtkImageData> image = clivus::vtkImageDataFromFrame(m_frame);
    if (image == nullptr) {
        m_mapper->SetInputData(nullptr);
        return;
    }
    // Display in pixel units, like the legacy canvas: the frame's pixel
    // spacing stays in the measurement math, not in the camera.
    image->SetSpacing(1.0, 1.0, 1.0);
    reverseRows(image);
    if (m_frame.isColour()) {
        // No VOI transform applies to a colour image (PS3.3 C.11.2): the
        // bytes go straight to the mapper, window arguments ignored.
        m_mapper->SetInputData(image);
    } else {
        m_mapToColors->SetInputData(image);
        m_mapToColors->SetLookupTable(clivus::dicomLookupTable(m_center, m_width, m_frame.invert));
        m_mapper->SetInputConnection(m_mapToColors->GetOutputPort());
    }
}

void VtkCanvasWidget::updateOverlay()
{
    if (m_overlay) m_overlay->update();
}

void VtkCanvasWidget::syncCamera()
{
    vtkCamera* camera = m_renderer->GetActiveCamera();
    if (m_frame.valid() && m_frame.width > 0 && m_frame.height > 0 && width() > 0 && height() > 0) {
        // Legacy framing verbatim: fit by pixel counts, then zoom, then pan
        // in widget pixels. The focal signs fall out of the camera geometry
        // (world +x maps to screen right, world +y maps to screen up, widget
        // y grows downward): a rightward pan moves the focal point left in
        // x, a downward pan moves it up in world y. Both are checked against
        // the legacy formulas: the image centre must land at
        // (W/2 + pan.x, H/2 + pan.y) in widget coordinates.
        const double W = width(), H = height();
        const double fit = std::min(W / m_frame.width, H / m_frame.height) * m_zoom;
        const double focalX = m_frame.width / 2.0 - m_pan.x() / fit;
        const double focalY = m_frame.height / 2.0 + m_pan.y() / fit;
        camera->ParallelProjectionOn();
        camera->SetParallelScale(H / (2.0 * fit));
        camera->SetPosition(focalX, focalY, 1.0);
        camera->SetFocalPoint(focalX, focalY, 0.0);
        camera->SetViewUp(0.0, 1.0, 0.0);
        camera->SetClippingRange(0.01, 10.0);
    }
    m_renderWindow->Render();
}

void VtkCanvasWidget::setOverlay(const QString& topLeft, const QString& topRight)
{
    m_overlayLeft = topLeft;
    m_overlayRight = topRight;
    updateOverlay();
}

void VtkCanvasWidget::resetView()
{
    m_frame.fullWindow(m_center, m_width);
    m_zoom = 1.0f;
    m_pan = QPointF(0, 0);
    rebuildDisplay();
    syncCamera();
    emit windowChanged(m_center, m_width);
    emit viewChanged();
    updateOverlay();
}

void VtkCanvasWidget::fitView()
{
    m_zoom = 1.0f;
    m_pan = QPointF(0, 0);
    syncCamera();
    emit viewChanged();
    updateOverlay();
}

VtkCanvasWidget::PixelInfo VtkCanvasWidget::pixelInfoAt(QPointF widget) const
{
    PixelInfo info;
    if (!m_frame.valid()) return info;

    const QPointF p = mapToFrame(widget);
    const int col = int(std::floor(p.x()));
    const int row = int(std::floor(p.y()));
    if (col < 0 || row < 0 || col >= m_frame.width || row >= m_frame.height) return info;

    info.valid = true;
    info.row = row + 1;   // DICOM counts pixels from 1
    info.col = col + 1;

    const size_t index = size_t(row) * size_t(m_frame.width) + size_t(col);
    if (m_frame.isColour()) {
        if (index * 3 + 2 < m_frame.pixels.size()) {
            info.modality = double(m_frame.pixels[index * 3]);
            info.unit = QStringLiteral("RGB");
        }
        return info;
    }
    if (index >= m_frame.pixels.size()) return info;

    info.modality = double(m_frame.pixels[index]);
    if (m_frame.rescaleKnown && m_frame.rescaleSlope != 0.0f) {
        info.storedKnown = true;
        info.stored = (info.modality - double(m_frame.rescaleIntercept)) / double(m_frame.rescaleSlope);
    }
    if (!m_frame.unit.empty()) info.unit = QString::fromStdString(m_frame.unit);
    return info;
}

QPointF VtkCanvasWidget::frameToWidget(QPointF imagePixel) const
{
    if (!m_frame.valid()) return QPointF();
    const double W = width(), H = height();
    const double fit = std::min(W / m_frame.width, H / m_frame.height) * m_zoom;
    const double w = m_frame.width * fit, h = m_frame.height * fit;
    const double x0 = (W - w) / 2.0 + m_pan.x();
    const double y0 = (H - h) / 2.0 + m_pan.y();
    return QPointF(imagePixel.x() * fit + x0, imagePixel.y() * fit + y0);
}

QPointF VtkCanvasWidget::mapToFrame(QPointF widget) const
{
    if (!m_frame.valid()) return QPointF();
    const double W = width(), H = height();
    const double fit = std::min(W / m_frame.width, H / m_frame.height) * m_zoom;
    const double w = m_frame.width * fit, h = m_frame.height * fit;
    const double x0 = (W - w) / 2.0 + m_pan.x();
    const double y0 = (H - h) / 2.0 + m_pan.y();
    return QPointF(double(widget.x() - x0) / fit, double(widget.y() - y0) / fit);
}

double VtkCanvasWidget::distanceMm(QPointF p1, QPointF p2, bool& spacingKnown) const
{
    spacingKnown = m_spacingRow > 0.0 && m_spacingCol > 0.0;
    if (!spacingKnown) return 0.0;
    const QPointF f1 = mapToFrame(p1), f2 = mapToFrame(p2);
    const double dx = (f1.x() - f2.x()) * m_spacingCol;   // columns run horizontally
    const double dy = (f1.y() - f2.y()) * m_spacingRow;   // rows run vertically
    return std::sqrt(dx * dx + dy * dy);
}

double VtkCanvasWidget::angleDeg(QPointF p1, QPointF p2, QPointF p3) const
{
    const QPointF v1 = mapToFrame(p1) - mapToFrame(p2);
    const QPointF v2 = mapToFrame(p3) - mapToFrame(p2);
    const double d1 = std::sqrt(QPointF::dotProduct(v1, v1));
    const double d2 = std::sqrt(QPointF::dotProduct(v2, v2));
    if (d1 == 0.0 || d2 == 0.0) return 0.0;   // degenerate segment: no angle to speak of
    constexpr double kFullCircle = 360.0;
    const double deg =
        std::acos(std::clamp(QPointF::dotProduct(v1, v2) / (d1 * d2), -1.0, 1.0)) * 180.0 / M_PI;
    return deg > 180.0 ? kFullCircle - deg : deg;
}

VtkCanvasWidget::RoiAnnotation VtkCanvasWidget::buildRoi(bool ellipse, int row0, int col0,
                                                         int row1, int col1) const
{
    RoiAnnotation roi;
    roi.ellipse = ellipse;
    if (row0 > row1) std::swap(row0, row1);
    if (col0 > col1) std::swap(col0, col1);
    roi.row0 = std::max(0, std::min(row0, m_frame.height - 1));
    roi.row1 = std::max(0, std::min(row1, m_frame.height - 1));
    roi.col0 = std::max(0, std::min(col0, m_frame.width - 1));
    roi.col1 = std::max(0, std::min(col1, m_frame.width - 1));
    if (!m_frame.valid() || m_frame.isColour()) return roi;   // colour frames carry no HU to average
    const clivus::RoiStats stats = clivus::roiStats(
        m_frame.pixels.data(), m_frame.height, m_frame.width,
        ellipse ? clivus::RoiShape::Ellipse : clivus::RoiShape::Rectangle, roi.row0, roi.col0,
        roi.row1, roi.col1, m_frame.pixelSpacingRow, m_frame.pixelSpacingCol);
    roi.pixels = stats.pixels;
    roi.mean = stats.mean;
    roi.stddev = stats.stddev;
    roi.min = stats.min;
    roi.max = stats.max;
    roi.areaMm2 = stats.areaMm2;
    roi.areaKnown = stats.areaMm2 > 0.0;
    return roi;
}

void VtkCanvasWidget::setRois(std::vector<RoiAnnotation> rois)
{
    m_rois = std::move(rois);
    updateOverlay();
}

void VtkCanvasWidget::clearRois()
{
    if (m_rois.empty()) return;
    m_rois.clear();
    emit roisChanged();
    updateOverlay();
}

bool VtkCanvasWidget::removeLastRoi()
{
    if (m_rois.empty()) return false;
    m_rois.pop_back();
    emit roisChanged();
    updateOverlay();
    return true;
}

void VtkCanvasWidget::addRoiForCapture(int row0, int col0, int row1, int col1)
{
    m_rois.push_back(buildRoi(false, row0, col0, row1, col1));
    emit roisChanged();
    updateOverlay();
}

void VtkCanvasWidget::refreshRoiStats()
{
    if (m_rois.empty() || !m_frame.valid()) return;
    for (RoiAnnotation& roi : m_rois)
        roi = buildRoi(roi.ellipse, roi.row0, roi.col0, roi.row1, roi.col1);
    updateOverlay();
}

void VtkCanvasWidget::clearMeasurements()
{
    m_distances.clear();
    m_angles.clear();
    m_measureDragging = false;
    m_anglePending.clear();
    updateOverlay();
}

QFont VtkCanvasWidget::overlayTextFont() const
{
    return vtkOverlayFont(font());
}
