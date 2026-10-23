// SPDX-License-Identifier: GPL-3.0-or-later
#include "view_controls.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QSlider>

#include <algorithm>

SliceBar::SliceBar(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(8, 2, 8, 4);
    layout->setSpacing(8);

    m_slider = new QSlider(Qt::Horizontal, this);
    m_slider->setSingleStep(1);
    m_slider->setPageStep(5);
    m_slider->setTracking(true);   // follow the drag continuously: direct navigation, not on release
    m_slider->setToolTip("Drag to go straight to a slice; the mouse wheel on the image steps one slice "
                         "at a time");
    m_label = new QLabel(this);
    m_label->setMinimumWidth(120);
    m_label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    layout->addWidget(m_slider, 1);
    layout->addWidget(m_label, 0);

    connect(m_slider, &QSlider::valueChanged, this, [this](int value) {
        if (m_updating) return;   // the strip is echoing MainWindow, not asking it to move
        updateLabel(value);
        emit sliceRequested(value);
    });

    setSliceCount(0);
}

void SliceBar::setSliceCount(int total)
{
    m_total = std::max(0, total);
    m_updating = true;
    m_slider->setRange(0, std::max(0, m_total - 1));
    m_updating = false;

    // A single-image series has nothing to navigate: disable the slider and say why, instead of
    // showing a thumb that cannot move (roadmap 2.5).
    const bool navigable = m_total > 1;
    m_slider->setEnabled(navigable);
    m_label->setEnabled(navigable);
    m_label->setToolTip(navigable ? QString()
                                  : QString("This series has a single image, so there is no slice "
                                            "navigation"));
    if (!navigable) {
        m_updating = true;
        m_slider->setValue(0);
        m_updating = false;
        m_label->setText(m_total == 1 ? "Single image" : "No image");
    } else {
        updateLabel(m_current < 0 ? 0 : m_current);
    }
}

void SliceBar::setCurrentSlice(int index)
{
    if (m_total <= 1) return;
    const int clamped = std::clamp(index, 0, m_total - 1);
    m_current = clamped;
    m_updating = true;
    m_slider->setValue(clamped);   // wheel/keyboard moved the selection: the slider follows it
    m_updating = false;
    updateLabel(clamped);
}

void SliceBar::updateLabel(int index)
{
    if (m_total <= 1) return;
    m_label->setText(QString("Slice %1 / %2").arg(index + 1).arg(m_total));
}
