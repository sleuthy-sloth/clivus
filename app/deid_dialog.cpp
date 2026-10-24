// SPDX-License-Identifier: GPL-3.0-or-later
#include "deid_dialog.h"

#include "clivus/anonymize.h"

#include <QCheckBox>
#include <QDir>
#include <QFileDialog>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStandardPaths>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

DeidDialog::DeidDialog(const QString& sourceFolder, QWidget* parent)
    : QDialog(parent), m_source(sourceFolder)
{
    setWindowTitle(tr("Export a de-identified copy"));
    resize(720, 560);
    setModal(true);

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(14, 14, 14, 14);
    outer->setSpacing(10);

    auto* explain = new QLabel(
        tr("<b>This writes a copy with patient identity removed from the metadata.</b> Every DICOM "
           "object in the folder is rewritten: name and ID replaced, identifiers and institution "
           "fields removed, private (vendor) elements removed by default, and — if you leave the "
           "options on — UIDs remapped and dates shifted so the copy cannot be joined back to the "
           "original study.</p>"
           "<p><b>It cannot see inside the picture.</b> Pixel data is copied unchanged, so any text "
           "burned into the image (annotations, ultrasound overlays, screenshots) is still there. "
           "Clivus performs no OCR. Review the images themselves before sharing a copy.</p>"
           "<p>De-identification is not the same as anonymity, and this is not a legal "
           "determination. That call belongs to whoever is accountable for the data.</p>"),
        this);
    explain->setWordWrap(true);
    outer->addWidget(explain);

    auto* destinationRow = new QHBoxLayout();
    m_destination = new QLineEdit(this);
    m_destination->setPlaceholderText(tr("Choose an empty folder for the copy"));
    auto* browse = new QPushButton(tr("Browse…"), this);
    destinationRow->addWidget(new QLabel(tr("Destination"), this));
    destinationRow->addWidget(m_destination, 1);
    destinationRow->addWidget(browse);
    outer->addLayout(destinationRow);

    m_remapUids = new QCheckBox(tr("Remap Study, Series and SOP Instance UIDs (and references)"), this);
    m_remapUids->setChecked(true);
    m_shiftDates = new QCheckBox(tr("Shift study, series and acquisition dates by one random offset"), this);
    m_shiftDates->setChecked(true);
    m_redact = new QCheckBox(tr("Clear series/study descriptions and protocol names (free text)"), this);
    m_redact->setChecked(true);
    m_privateTags = new QCheckBox(tr("Remove private tags (vendor elements — a common hiding place)"), this);
    m_privateTags->setChecked(true);
    m_sex = new QCheckBox(tr("Remove patient sex (a quasi-identifier)"), this);
    m_sex->setChecked(true);
    outer->addWidget(m_remapUids);
    outer->addWidget(m_shiftDates);
    outer->addWidget(m_redact);
    outer->addWidget(m_privateTags);
    outer->addWidget(m_sex);

    m_status = new QLabel(tr("Nothing written yet."), this);
    m_status->setWordWrap(true);
    m_status->setStyleSheet(QStringLiteral("color:#8a94a0;"));
    outer->addWidget(m_status);

    m_report = new QPlainTextEdit(this);
    m_report->setReadOnly(true);
    m_report->setPlaceholderText(tr("The report appears here: what was removed, what was verified, "
                                    "and anything that could not be done."));
    outer->addWidget(m_report, 1);

    auto* buttons = new QHBoxLayout();
    m_run = new QPushButton(tr("Export de-identified copy"), this);
    m_run->setDefault(true);
    auto* close = new QPushButton(tr("Close"), this);
    buttons->addStretch(1);
    buttons->addWidget(m_run);
    buttons->addWidget(close);
    outer->addLayout(buttons);

    connect(browse, &QPushButton::clicked, this, &DeidDialog::chooseDestination);
    connect(m_run, &QPushButton::clicked, this, &DeidDialog::run);
    connect(close, &QPushButton::clicked, this, &QDialog::close);
}

void DeidDialog::chooseDestination()
{
    const QString dir = QFileDialog::getExistingDirectory(
        this, tr("Destination folder for the de-identified copy"),
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation));
    if (!dir.isEmpty()) m_destination->setText(dir);
}

void DeidDialog::run()
{
    if (m_running) return;
    const QString destination = m_destination->text().trimmed();
    if (destination.isEmpty()) {
        m_status->setText(tr("Choose a destination folder first."));
        return;
    }
    if (QDir(destination).isEmpty() == false) {
        // An export is verified as a whole: files already in the folder were not written by this run.
        m_status->setText(tr("The destination folder is not empty. Choose an empty folder so the "
                             "verification covers exactly what this export wrote."));
        return;
    }

    clivus::DeidOptions options;
    options.remapUids = m_remapUids->isChecked();
    options.shiftDates = m_shiftDates->isChecked();
    options.redactDescriptions = m_redact->isChecked();
    options.removePrivateTags = m_privateTags->isChecked();
    options.removePatientSex = m_sex->isChecked();

    m_running = true;
    m_run->setEnabled(false);
    m_report->clear();
    m_status->setText(tr("Working… writing and verifying the copy."));

    const QString source = m_source;
    auto* watcher = new QFutureWatcher<std::pair<bool, clivus::DeidReport>>(this);
    connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher] {
        const auto result = watcher->result();
        watcher->deleteLater();
        m_running = false;
        m_run->setEnabled(true);
        const clivus::DeidReport& report = result.second;
        QStringList lines;
        lines << tr("Files written: %1 (skipped as non-DICOM: %2)").arg(report.files).arg(report.skipped);
        if (!report.newPatientId.empty())
            lines << tr("Patient ID in the copy: %1").arg(QString::fromStdString(report.newPatientId));
        lines << tr("Attributes removed: %1, replaced: %2").arg(report.attributesRemoved)
                                                             .arg(report.attributesReplaced);
        lines << tr("Private tags removed: %1").arg(report.privateTagsRemoved);
        lines << tr("UIDs remapped: %1").arg(report.uidsRemapped);
        lines << tr("Dates shifted by %1 days").arg(report.appliedShiftDays);
        lines << QString();
        lines << (report.verified
                      ? tr("Verification: PASSED — the original identifying values were searched for "
                           "in every written file and none were found.")
                      : tr("Verification: FAILED — an original value survived. Do not share this copy."));
        for (const auto& residual : report.residual)
            lines << tr("  residual: %1").arg(QString::fromStdString(residual));
        for (const auto& warning : report.warnings)
            lines << tr("Note: %1").arg(QString::fromStdString(warning));
        for (const auto& failure : report.failures)
            lines << tr("Problem: %1").arg(QString::fromStdString(failure));

        m_report->setPlainText(lines.join(QLatin1Char('\n')));
        showReport(result.first, report.verified ? tr("De-identified copy written and verified.")
                                                 : tr("The copy was not verified — do not share it."),
                   lines);
    });
    watcher->setFuture(QtConcurrent::run([source, destination, options] {
        clivus::DeidReport report;
        const bool ok = clivus::deidentifyStudy(source.toStdString(), destination.toStdString(),
                                                options, report);
        return std::pair<bool, clivus::DeidReport>{ok, report};
    }));
}

void DeidDialog::showReport(const bool ok, const QString& summary, const QStringList& lines)
{
    Q_UNUSED(lines);
    m_status->setText(summary);
    m_status->setStyleSheet(ok ? QStringLiteral("color:#7fb069;") : QStringLiteral("color:#d08770;"));
}
