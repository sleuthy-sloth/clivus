// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QPixmap>
#include <QString>
#include <QWidget>

// Branding: the Clivus mark and the welcome / about surfaces that give it a home.
//
// The MARK IS AN ASSET SWAP-POINT. Drop a file named clivus-mark.svg (or .png) into any of the
// places brandMark() searches and every branded surface picks it up at the next launch — no code
// change. Until that asset exists the caller draws the plain wordmark instead, so the interface
// never ships a broken image: absence is a state, not a failure.
//
// Asset search order (first hit wins):
//   1. <executable dir>/assets/clivus-mark.svg|png   (packaged next to the binaries)
//   2. <executable dir>/../assets/…                  (build-tree runs)
//   3. <source checkout>/assets/…                    (development runs, via a compile-time define)
//   4. :/assets/…                                    (qrc, if one is ever embedded)
namespace clivus::branding {

// The mark scaled to `px` on its longest edge, or a null pixmap when no asset is present.
QPixmap brandMark(int px);

// True when a mark asset was found (the UI can then skip the wordmark fallback).
bool hasBrandMark();

// A centred label carrying the mark when present, or the styled CLIVUS wordmark when not. Never
// returns null; the caller owns the widget.
QWidget* makeBrandHeader(int markPx, QWidget* parent);

}  // namespace clivus::branding
