// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "clivus/series_index.h"

#include <QString>

// How a series presents itself in the browser (roadmap 1.3): a title that prefers the series
// description, a metadata line of modality · count · dimensions, and the combined two-line label.
// Pure formatting from SeriesInfo — no widgets, no I/O — which is why it lives outside the window
// and is covered by its own test: the row text is the first thing a user judges the app by, and it
// has already shipped one encoding bug (a QLatin1String separator painting "Â·" everywhere).
namespace clivus::ui {

QString seriesTitle(const clivus::SeriesInfo& series);
QString seriesDetails(const clivus::SeriesInfo& series);
QString seriesLabel(const clivus::SeriesInfo& series);

}  // namespace clivus::ui
