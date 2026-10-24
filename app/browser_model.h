// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "clivus/series_index.h"

#include <QString>
#include <Qt>

#include <string>
#include <vector>

// The browser's shape: Patient -> Study -> Series.
//
// The scan hands back a flat vector, because that is what a folder of DICOM objects is. A person
// picking a study needs the boundaries between studies, and those boundaries are exactly what the
// flat list could not show: two patients whose series happened to share a description were
// indistinguishable, and there was no indication where one study ended and the next began.
//
// Grouping uses the identifiers the scan has already validated — study UID for the study level,
// patient ID for the patient level, falling back to the name only when the ID is absent — and never a
// display name when a real identifier exists.
//
// Privacy: patient nodes are labelled by an ordinal ("Patient A", "Patient B") by default. The real
// name is available but off, because this browser sits on a screen in a room; a setting turns it on,
// and even then the ordinal stays in the label so two patients with the same name remain distinct.
//
// This file is deliberately widgets-free: the tree is built and filtered as plain data, which is what
// makes it testable without a window or a GL context.
namespace clivus::ui {

// Item-data roles the browser rows use. The series index lives in Qt::UserRole, so the marks that
// distinguish "group header" from "placeholder" sit beside it.
inline constexpr int kHeaderRole = Qt::UserRole + 2;

enum class HeaderKind { None = 0, Patient, Study };

struct BrowserSeries {
    int seriesIndex = -1;   // index into the vector passed to buildBrowserTree()
    QString title;          // series description, else study description, else an explicit fallback
    QString details;        // modality · count · dimensions
};

struct BrowserStudy {
    std::string studyUid;
    QString label;          // study description, else an explicit fallback
    std::vector<BrowserSeries> series;
};

struct BrowserPatient {
    std::string patientKey;   // the identity the grouping used; never shown
    QString label;            // "Patient A", or the name plus the ordinal when names are enabled
    std::vector<BrowserStudy> studies;
};

struct BrowserTree {
    std::vector<BrowserPatient> patients;

    int seriesCount() const;
    bool empty() const { return patients.empty(); }
};

struct BrowserOptions {
    // Off by default: the ordinal label alone distinguishes records, and a name on screen is a
    // privacy decision the user makes, not one the viewer makes for them.
    bool showPatientNames = false;
};

// Groups one scan into the tree. Deterministic: the input order is the scan's order, and patients are
// numbered in the order their first series appears, so a rescan produces the same labels.
BrowserTree buildBrowserTree(const std::vector<clivus::SeriesInfo>& series,
                             const BrowserOptions& options = {});

// Keeps only what a query matches, in the same shape. A patient or study survives when it has a
// matching descendant, so filtering never hides the boundaries it exists to show. An empty query
// returns the input unchanged. Matching is case-insensitive and covers the visible labels plus the
// series title and details, which is what the old flat filter did.
BrowserTree filterBrowserTree(const BrowserTree& tree, const QString& query);

// The label a quarantined group carries, so the UI does not have to invent its own wording.
QString quarantinedSeriesLabel();

}  // namespace clivus::ui
