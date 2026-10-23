// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QWidget>

class QLabel;
class QSlider;
class QLineEdit;

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

// Editable window centre/width bar (roadmap 2.2).
//
// The canonical values live in MainWindow (m_center/m_width), which is also where the canvas reports
// every drag through CanvasWidget::windowChanged. This bar is a view of that state plus an input
// route into it: setValues() updates the fields without emitting, and a validated edit emits
// windowLevelEdited() once, so there is exactly one writer of the rendering state.
//
// Plain QLineEdit fields rather than QDoubleSpinBox: a spin box silently swallows text it does not
// like, and roadmap 2.2 asks for a visible, reversible error instead. Text is parsed here, checked
// for empty/non-finite/reversed values, and a rejected entry keeps the last good value in the field.
class WindowLevelBar : public QWidget {
    Q_OBJECT
public:
    explicit WindowLevelBar(QWidget* parent = nullptr);

    // Called with whatever the canvas says is being rendered right now (drag, preset, reset, new
    // frame). Never emits windowLevelEdited().
    void setValues(double center, double width);

    // Colour frames take no VOI transform (render() ignores the window), so the fields are shown but
    // disabled, with the reason stated inline instead of sitting there looking broken.
    void setWindowingAvailable(bool available, const QString& reason = {});

signals:
    void windowLevelEdited(double center, double width);

private:
    void commit();
    void setError(const QString& text);
    void clearError();
    void checkTypedText();

    QLineEdit* m_center = nullptr;
    QLineEdit* m_width = nullptr;
    QLabel* m_error = nullptr;
    QLabel* m_note = nullptr;
    double m_lastCenter = 0.0;
    double m_lastWidth = 1.0;
    bool m_available = true;
    bool m_updating = false;
};
