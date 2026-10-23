// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QSettings>
#include <QString>

// Every persisted setting key lives here (roadmap 3.1), so a renamed key cannot drift between the
// writer and the reader. Organization/application are fixed too: QSettings picks the on-disk store
// from them, and a mismatch would silently read an empty file.
//
// Privacy decision (recorded where the keys are defined): only window layout is persisted. Recent
// folders, paths, and anything derived from patient data (the series list, thumbnails, the last
// opened study) are deliberately NOT stored — a viewer of medical images must not leave a trail of
// what was opened on shared or clinical machines.

namespace clivus::settings {

inline QSettings clivusSettings()
{
    return QSettings(QStringLiteral("sleuthy-sloth"), QStringLiteral("clivus"));
}

// Keys, grouped by what they describe.
inline const QString kGeometry = QStringLiteral("window/geometry");
inline const QString kSplitter = QStringLiteral("layout/splitter");
inline const QString kSidebarVisible = QStringLiteral("layout/sidebar_visible");
inline const QString kDiagnostics = QStringLiteral("view/show_diagnostics");

// Built-in defaults, shared by construction, Reset Layout and the corrupt-value fallback.
inline const QSize kDefaultWindowSize = QSize(1280, 860);
inline const QList<int> kDefaultSplitterSizes = {320, 900};

}  // namespace clivus::settings
