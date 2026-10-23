// SPDX-License-Identifier: GPL-3.0-or-later
#include "view_controls.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QSlider>

#include <algorithm>
#include <cmath>

namespace {

// Field width: enough for a five-digit centre with a sign and one decimal, which covers CT/MR and
// dose grids without the bar jumping around as values change.
constexpr int kFieldWidth = 96;

QString formatValue(double v)
{
    // One decimal, trimmed: -600.0 reads as -600, 40.5 stays 40.5. The field is for humans.
    QString s = QString::number(v, 'f', 1);
    if (s.endsWith(QLatin1String(".0"))) s.chop(2);
    return s;
}

// Parses what the user typed. Rejects empty text, non-numeric text and non-finite numbers: those
// would otherwise reach ImageFrame::render() and produce undefined bytes rather than an error.
bool parseValue(const QString& text, double& out)
{
    bool ok = false;
    const double v = text.trimmed().toDouble(&ok);
    if (!ok || !std::isfinite(v)) return false;
    out = v;
    return true;
}

}  // namespace

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

WindowLevelBar::WindowLevelBar(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(8, 2, 8, 2);
    layout->setSpacing(6);

    auto* centerLabel = new QLabel("Window Center", this);
    auto* widthLabel = new QLabel("Window Width", this);
    m_center = new QLineEdit(this);
    m_width = new QLineEdit(this);
    for (auto* field : {m_center, m_width}) {
        field->setFixedWidth(kFieldWidth);
        field->setAlignment(Qt::AlignRight);
        // No keyboard tracking of partial input: the value is committed on Return or focus-out, so a
        // half-typed "1" cannot briefly re-window the image to a nonsense range.
        field->setToolTip("Grayscale only — colour frames are shown without a window/level.");
    }
    m_center->setToolTip("Window Centre (WC) in modality units. Grayscale only — colour frames ignore "
                         "window/level. Enter a number and press Return.");
    m_width->setToolTip("Window Width (WW) in modality units, minimum 1. Grayscale only — colour "
                        "frames ignore window/level. Enter a number and press Return.");
    centerLabel->setBuddy(m_center);
    widthLabel->setBuddy(m_width);

    m_error = new QLabel(this);
    m_error->setObjectName("wlError");
    m_error->setStyleSheet("color: #d98b8b;");   // restrained error tint, no animation
    m_error->hide();
    m_note = new QLabel(this);
    m_note->setObjectName("wlNote");
    m_note->setStyleSheet("color: #9a9fa6;");
    m_note->hide();

    layout->addWidget(centerLabel);
    layout->addWidget(m_center);
    layout->addWidget(widthLabel);
    layout->addWidget(m_width);
    layout->addWidget(m_error);
    layout->addWidget(m_note);
    // No trailing stretch: the options bar that owns this widget adds one, so the preset combo can sit
    // to the right of the numeric fields on the same line.

    for (auto* field : {m_center, m_width}) {
        connect(field, &QLineEdit::editingFinished, this, &WindowLevelBar::commit);
    }
    connect(m_center, &QLineEdit::textEdited, this, &WindowLevelBar::checkTypedText);
    connect(m_width, &QLineEdit::textEdited, this, &WindowLevelBar::checkTypedText);
    setValues(0.0, 1.0);
}

void WindowLevelBar::setValues(double center, double width)
{
    m_updating = true;
    m_lastCenter = center;
    m_lastWidth = std::max(1.0, width);
    m_center->setText(formatValue(m_lastCenter));
    m_width->setText(formatValue(m_lastWidth));
    m_updating = false;
    // A drag that lands on a legal value clears a stale complaint: the state on screen is valid again.
    clearError();
}

void WindowLevelBar::setWindowingAvailable(bool available, const QString& reason)
{
    m_available = available;
    m_center->setEnabled(available);
    m_width->setEnabled(available);
    if (!available) {
        m_note->setText(reason.isEmpty() ? QString("Window/level does not apply to this image")
                                         : reason);
        m_note->show();
        m_error->hide();
    } else {
        m_note->hide();
    }
}

// Early complaint while typing: it does not reject anything yet (the field belongs to the user until
// Return), it only says the text will not be accepted, so the failure is visible before committing.
void WindowLevelBar::checkTypedText()
{
    if (m_updating || !m_available) return;
    double ignored = 0.0;
    const QLineEdit* sender = qobject_cast<QLineEdit*>(this->sender());
    if (sender && !parseValue(sender->text(), ignored)) {
        setError("Enter a finite number (leave a value as it was to keep it).");
    } else {
        clearError();
    }
}

void WindowLevelBar::commit()
{
    if (m_updating || !m_available) return;

    double center = m_lastCenter;
    double width = m_lastWidth;
    const bool centerOk = parseValue(m_center->text(), center);
    const bool widthOk = parseValue(m_width->text(), width);

    // Reversible rejection: put the last accepted values back in the fields (so the bar always shows
    // what is actually being rendered) and leave the reason up until the next valid edit.
    QString rejection;
    if (!centerOk || !widthOk)
        rejection = "Window values must be finite numbers — reverted to the last accepted values.";
    else if (width < 1.0)
        rejection = "Window Width must be at least 1 — reverted to the last accepted value.";
    if (!rejection.isEmpty()) {
        setValues(m_lastCenter, m_lastWidth);   // clears the message first
        setError(rejection);
        return;
    }
    if (std::abs(center - m_lastCenter) < 1e-9 && std::abs(width - m_lastWidth) < 1e-9) {
        clearError();
        return;   // focus-out with nothing changed: not an edit worth re-rendering for
    }

    clearError();
    m_lastCenter = center;
    m_lastWidth = width;
    // Normalized text so "0040" and "40" end up looking the same as the drag path's formatting.
    m_updating = true;
    m_center->setText(formatValue(center));
    m_width->setText(formatValue(width));
    m_updating = false;
    emit windowLevelEdited(center, width);
}

void WindowLevelBar::setError(const QString& text)
{
    m_error->setText(text);
    m_error->show();
    m_center->setProperty("invalid", true);
    m_width->setProperty("invalid", true);
}

void WindowLevelBar::clearError()
{
    m_error->hide();
    m_error->clear();
    m_center->setProperty("invalid", false);
    m_width->setProperty("invalid", false);
}

namespace {

// Viewing conventions, not clinical recommendations (roadmap 2.3). These are the numbers radiology
// reading software has used for decades to *look at* CT and MR: they are starting points for
// inspection, not validated diagnostic settings, and none of them is appropriate for every modality.
// The numeric values and their intent are spelled out here and shown in the combo's tooltip so the
// interface does not present a name without its numbers.
struct Preset {
    const char* name;
    double center;
    double width;
    const char* note;
};

const Preset kPresets[] = {
    {"Soft Tissue", 40.0, 400.0, "abdominal/soft-tissue CT viewing convention"},
    {"Lung", -600.0, 1500.0, "lung parenchyma CT viewing convention"},
    {"Bone", 300.0, 1500.0, "bone CT viewing convention"},
    {"Brain", 40.0, 80.0, "brain CT viewing convention"},
    {"Liver", 60.0, 160.0, "liver CT viewing convention"},
    // MR has no modality-defined units at all: the values are whatever the scanner wrote, so this is
    // a generic mid-range starting point, not an MRI standard.
    {"MRI Default", 450.0, 900.0, "generic MR starting point (MR intensities are not standardized)"},
};
constexpr int kPresetCount = int(sizeof(kPresets) / sizeof(kPresets[0]));

bool sameValue(double a, double b) { return std::abs(a - b) < 0.05; }

}  // namespace

WindowPresetBar::WindowPresetBar(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);

    auto* label = new QLabel("Preset", this);
    m_combo = new QComboBox(this);
    m_combo->setToolTip("Window/level viewing conventions, not diagnostic settings. "
                        "Picking one sets Window Center and Window Width; changing them afterwards "
                        "shows '(custom)'.");
    m_combo->addItem("(select preset)", QVariant());
    for (int i = 0; i < kPresetCount; ++i) {
        // Each entry carries its numbers, so the tooltip can be specific rather than promising
        // something the value does not deliver.
        m_combo->addItem(QString::fromLatin1(kPresets[i].name), i);
        m_combo->setItemData(
            i + 1,
            QString("%1 — WC %2, WW %3. Viewing convention, not a diagnostic setting.")
                .arg(QString::fromLatin1(kPresets[i].note))
                .arg(kPresets[i].center)
                .arg(kPresets[i].width),
            Qt::ToolTipRole);
    }
    m_combo->addItem("(custom)", -1);
    label->setBuddy(m_combo);

    m_note = new QLabel(this);
    m_note->setStyleSheet("color: #9a9fa6;");
    m_note->hide();

    layout->addWidget(label);
    layout->addWidget(m_combo);
    layout->addWidget(m_note);

    connect(m_combo, QOverload<int>::of(&QComboBox::activated), this, &WindowPresetBar::choose);
    m_combo->setCurrentIndex(0);
}

void WindowPresetBar::choose(int index)
{
    if (m_updating) return;
    const QVariant data = m_combo->itemData(index);
    if (!data.isValid() || data.toInt() < 0) return;   // placeholder or "(custom)": nothing to apply
    const Preset& p = kPresets[data.toInt()];
    emit presetChosen(p.center, p.width);
}

void WindowPresetBar::syncTo(double center, double width)
{
    if (!m_available) return;
    m_updating = true;
    int match = -1;
    for (int i = 0; i < kPresetCount; ++i) {
        if (sameValue(kPresets[i].center, center) && sameValue(kPresets[i].width, width)) {
            match = i;
            break;
        }
    }
    // A value that is not a named preset is shown as custom, never as the closest name: the combo
    // must not claim the image is being viewed with settings it is not.
    m_combo->setCurrentIndex(match >= 0 ? match + 1 : kPresetCount + 1);
    m_updating = false;
}

void WindowPresetBar::setAvailable(bool available, const QString& reason)
{
    m_available = available;
    m_combo->setEnabled(available);
    if (!available) {
        m_note->setText(reason);
        m_note->setVisible(!reason.isEmpty());
        m_updating = true;
        m_combo->setCurrentIndex(0);
        m_updating = false;
    } else {
        m_note->hide();
    }
}
