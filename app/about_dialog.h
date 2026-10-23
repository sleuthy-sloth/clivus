// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QDialog>

// The About surface: a proper identity home rather than a modal text blob.
//
// Full mark + name + version, one-line description, the repository link, licence terms,
// acknowledgments (the libraries that make the viewer possible and the data that proves it), and
// the research-use disclaimer stated plainly. Built once per open; no state to persist.
class AboutDialog : public QDialog {
    Q_OBJECT
public:
    explicit AboutDialog(QWidget* parent = nullptr);
};
