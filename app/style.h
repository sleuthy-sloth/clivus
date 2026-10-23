// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
class QApplication;

namespace clivus::style {

// One place for the dark workspace look (UI/UX roadmap 6.4): charcoal panels, a black canvas and a
// single restrained accent. QSS/QPalette only — no gradients, shadows or animations.
void apply(QApplication& app);

// Standard spacing and icon sizing, shared by the toolbar, lists and dialogs so widgets agree
// without each hard-coding its own numbers.
constexpr int kIconSize = 20;      // toolbar icon edge, px
constexpr int kSpacing = 6;        // standard widget spacing
constexpr int kThumbSize = 72;     // series browser thumbnail edge

}  // namespace clivus::style
