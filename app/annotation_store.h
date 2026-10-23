// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QString>
#include <QVector>

#include "canvas_widget.h"

// Annotations persisted beside the cache, one JSON file per series.
//
// Why not DICOM SR/GSPS: writing those correctly needs a validated encoder, and inventing one that a
// clinical system might read is worse than a private file. This is a local sidecar: it keeps what the
// user measured, is human-readable, and is deleted by "Clear index cache" along with everything else.
//
// Deliberate content limits: no patient name, no file paths, no study description — only the series
// UID (which the file is named after anyway), slice indices, geometry in image pixels, and the tool
// type. Statistics are NOT stored: they are recomputed from the pixels on load, so a file that has
// been edited by hand cannot make the numbers lie.
//
// Failure policy: a missing file is an empty series, a corrupt file is an empty series with a
// reported reason, and a write failure is reported — never a crash and never a silent claim of
// success.
class AnnotationStore {
public:
    struct SliceRois {
        int slice = 0;
        QVector<CanvasWidget::RoiAnnotation> rois;
    };

    // Absolute path of the sidecar for a series (inside the app data directory).
    static QString pathForSeries(const QString& seriesUid);

    // Loads every slice's ROIs for one series. `warning` receives a human-readable reason when the
    // file existed but could not be used.
    static QVector<SliceRois> load(const QString& seriesUid, QString* warning = nullptr);

    // Replaces the stored ROIs for one slice and writes the file. Empty lists remove the slice.
    // Returns false (with `error`) when the file could not be written.
    static bool setSliceRois(const QString& seriesUid, int slice,
                             const QVector<CanvasWidget::RoiAnnotation>& rois, QString* error = nullptr);

    // Removes the series' annotations entirely.
    static bool clearSeries(const QString& seriesUid, QString* error = nullptr);
};
