// SPDX-License-Identifier: GPL-3.0-or-later
#include "about_dialog.h"

#include "branding.h"
#include "style.h"

#include "clivus/version.h"

#include <QApplication>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

AboutDialog::AboutDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("About Clivus"));
    setModal(true);
    setMinimumWidth(460);

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(24, 20, 24, 18);
    outer->setSpacing(clivus::style::kSpacing);

    outer->addWidget(clivus::branding::makeBrandHeader(96, this));

    auto* version = new QLabel(tr("Version %1").arg(QString::fromLatin1(CLIVUS_VERSION)), this);
    version->setAlignment(Qt::AlignCenter);
    version->setStyleSheet(QStringLiteral("color:#8a94a0;"));
    outer->addWidget(version);

    auto* blurb = new QLabel(
        tr("A DICOM viewer for research and education: scan a folder, browse its series, examine "
           "each slice — with measurements, multiplanar reconstruction in progress, and an honest "
           "account of what it does not do."),
        this);
    blurb->setAlignment(Qt::AlignCenter);
    blurb->setWordWrap(true);
    outer->addWidget(blurb);

    auto* repo = new QLabel(
        tr("<a href=\"https://github.com/sleuthy-sloth/clivus\">github.com/sleuthy-sloth/clivus</a>"),
        this);
    repo->setAlignment(Qt::AlignCenter);
    repo->setOpenExternalLinks(true);
    repo->setTextInteractionFlags(Qt::TextBrowserInteraction);
    outer->addWidget(repo);

    auto* rule = new QFrame(this);
    rule->setFrameShape(QFrame::HLine);
    rule->setFrameShadow(QFrame::Sunken);
    outer->addWidget(rule);

    auto* license = new QLabel(
        tr("<b>Licence</b><br>GNU General Public License v3.0 or later. This program is free "
           "software and comes with ABSOLUTELY NO WARRANTY; you may redistribute it under the "
           "terms of the GPL. See the LICENSE file for the full text, and THIRD-PARTY.md for the "
           "components below."),
        this);
    license->setWordWrap(true);
    outer->addWidget(license);

    auto* credits = new QLabel(
        tr("<b>Acknowledgments</b><br>"
           "Qt 6 — application framework.<br>"
           "DCMTK — DICOM parsing, transfer syntaxes and codecs.<br>"
           "Synthetic test objects derived from pydicom's public sample data (NEMA/MIT).<br>"
           "Interface conventions follow Horos/OsiriX and the DICOM standard's display rules."),
        this);
    credits->setWordWrap(true);
    outer->addWidget(credits);

    auto* disclaimer = new QLabel(
        tr("<b>Research use only</b> — Clivus is not a medical device. It has not been cleared or "
           "approved by any regulator, and nothing it displays should inform clinical decisions "
           "without independent validation at the point of care."),
        this);
    disclaimer->setWordWrap(true);
    disclaimer->setStyleSheet(QStringLiteral("color:#d8b46a;"));
    outer->addWidget(disclaimer);

    auto* close = new QPushButton(tr("Close"), this);
    close->setDefault(true);
    connect(close, &QPushButton::clicked, this, &QDialog::accept);
    auto* buttons = new QHBoxLayout();
    buttons->addStretch(1);
    buttons->addWidget(close);
    outer->addLayout(buttons);
}
