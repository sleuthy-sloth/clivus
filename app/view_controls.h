// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QWidget>

class QLabel;
class QSlider;

// Slice navigation strip under the canvas (roadmap 2.5).
//
// The strip never owns the slice selection: it echoes whatever MainWindow says is selected and sends
// an absolute index back when the user drags or clicks. MainWindow coalesces those requests through
// the existing single-in-flight decode pump, so a long drag produces one decode at a time chasing the
// slider, not one decode per slider step.
class SliceBar : public QWidget {
    Q_OBJECT
public:
    explicit SliceBar(QWidget* parent = nullptr);

    // A series of one frame is not navigable: the slider is disabled and the label says so, rather
    // than offering a control that cannot move.
    void setSliceCount(int total);
    // 0-based index of the slice the user is on; -1 while nothing has been decoded yet.
    void setCurrentSlice(int index);

signals:
    void sliceRequested(int index);   // absolute, 0-based

private:
    void updateLabel(int index);

    QLabel* m_label = nullptr;
    QSlider* m_slider = nullptr;
    int m_total = 0;
    int m_current = -1;
    bool m_updating = false;   // programmatic setValue() must not look like a user drag
};
