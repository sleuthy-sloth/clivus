// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QImage>
#include <QString>
#include <QStringList>

#include <functional>

#include "clivus/series_index.h"

// Image export: what you see, written out.
//
// Two routes, both honest about what they contain:
//   - exportDisplayedPng(): the canvas framebuffer, i.e. exactly the pixels the window is showing at
//     the current window/level and zoom, with no overlay text baked in. Requires a live GL context;
//     returns false (and says why) when there is none, rather than writing a black image.
//   - exportSeriesPngs(): every frame of a series rendered at the *current* window/level, named by
//     slice number. Decoding happens on a worker; the caller supplies a cancel flag so a long series
//     can be stopped. Files that fail to decode are reported, never faked with a blank frame.
struct SeriesExportResult {
    int written = 0;
    int failed = 0;
    bool cancelled = false;
    QString error;          // set when the export could not start at all
    QStringList failures;   // per-frame decode failures
};

bool exportDisplayedPng(QImage image, const QString& path, QString* error);

SeriesExportResult exportSeriesPngs(const clivus::SeriesInfo& series, double center, double width,
                                    const QString& folder,
                                    const std::function<bool()>& cancelled);
