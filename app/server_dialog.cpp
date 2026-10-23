// SPDX-License-Identifier: GPL-3.0-or-later
#include "server_dialog.h"

#include "style.h"

#include <QComboBox>
#include <QDir>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
#include <QTableWidget>
#include <QVBoxLayout>

namespace {
constexpr const char* kServerUrlKey = "dicomweb/url";

QString studiesRoot()
{
    const QString cache = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    return (cache.isEmpty() ? QDir::tempPath() : cache) + QStringLiteral("/studies");
}
}  // namespace

ServerDialog::ServerDialog(QWidget* parent) : QDialog(parent)
{
    setWindowTitle(tr("Open from DICOMweb server"));
    resize(860, 560);
    setModal(true);

    m_client = new DicomWebClient(this);

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(12, 12, 12, 12);
    outer->setSpacing(clivus::style::kSpacing);

    auto* form = new QFormLayout();
    form->setSpacing(6);
    m_url = new QLineEdit(this);
    m_url->setPlaceholderText(tr("https://pacs.example.com/dicomweb"));
    m_url->setText(QSettings().value(QLatin1String(kServerUrlKey)).toString());
    m_url->setToolTip(tr("Base URL of the DICOMweb service (QIDO-RS and WADO-RS)"));
    form->addRow(tr("Server"), m_url);

    m_auth = new QLineEdit(this);
    m_auth->setPlaceholderText(tr("optional: Authorization header value (Bearer …)"));
    m_auth->setEchoMode(QLineEdit::Password);
    form->addRow(tr("Auth"), m_auth);

    auto* filters = new QHBoxLayout();
    m_patient = new QLineEdit(this);
    m_patient->setPlaceholderText(tr("Patient name"));
    m_date = new QLineEdit(this);
    m_date->setPlaceholderText(tr("Study date (YYYYMMDD)"));
    m_accession = new QLineEdit(this);
    m_accession->setPlaceholderText(tr("Accession"));
    m_modality = new QComboBox(this);
    m_modality->addItems({tr("Any modality"), QStringLiteral("CT"), QStringLiteral("MR"),
                          QStringLiteral("CR"), QStringLiteral("DX"), QStringLiteral("US"),
                          QStringLiteral("PT"), QStringLiteral("NM"), QStringLiteral("RTDOSE")});
    filters->addWidget(m_patient, 2);
    filters->addWidget(m_date, 1);
    filters->addWidget(m_accession, 1);
    filters->addWidget(m_modality, 1);
    outer->addLayout(form);
    outer->addWidget(new QLabel(tr("Filters (any combination)"), this));
    outer->addLayout(filters);

    auto* buttons = new QHBoxLayout();
    m_search = new QPushButton(tr("Search"), this);
    m_search->setDefault(true);
    m_download = new QPushButton(tr("Download and open"), this);
    m_download->setEnabled(false);
    m_cancel = new QPushButton(tr("Cancel download"), this);
    m_cancel->setEnabled(false);
    buttons->addWidget(m_search);
    buttons->addStretch(1);
    buttons->addWidget(m_cancel);
    buttons->addWidget(m_download);
    outer->addLayout(buttons);

    m_results = new QTableWidget(this);
    m_results->setColumnCount(5);
    m_results->setHorizontalHeaderLabels(
        {tr("Patient"), tr("Study date"), tr("Modality"), tr("Description"), tr("Instances")});
    m_results->horizontalHeader()->setStretchLastSection(false);
    m_results->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    m_results->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_results->setSelectionMode(QAbstractItemView::SingleSelection);
    m_results->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_results->verticalHeader()->setVisible(false);
    outer->addWidget(m_results, 1);

    m_status = new QLabel(this);
    m_status->setStyleSheet(QStringLiteral("color:#8a94a0;"));
    m_status->setWordWrap(true);
    outer->addWidget(m_status);

    connect(m_search, &QPushButton::clicked, this, &ServerDialog::runSearch);
    connect(m_url, &QLineEdit::returnPressed, this, &ServerDialog::runSearch);
    connect(m_patient, &QLineEdit::returnPressed, this, &ServerDialog::runSearch);
    connect(m_results, &QTableWidget::itemSelectionChanged, this,
            [this] { m_download->setEnabled(m_results->currentRow() >= 0 && !m_cancel->isEnabled()); });
    connect(m_download, &QPushButton::clicked, this, &ServerDialog::downloadSelected);
    connect(m_cancel, &QPushButton::clicked, this, [this] {
        m_client->cancel();
        setBusy(false, tr("Cancelling…"));
    });
}

void ServerDialog::setBusy(const bool busy, const QString& message)
{
    m_search->setEnabled(!busy);
    m_download->setEnabled(!busy && m_results->currentRow() >= 0);
    m_cancel->setEnabled(busy);
    if (!message.isEmpty()) m_status->setText(message);
}

void ServerDialog::runSearch()
{
    const QString url = m_url->text().trimmed();
    if (url.isEmpty()) {
        m_status->setText(tr("Enter the server's DICOMweb base URL first."));
        return;
    }
    QSettings().setValue(QLatin1String(kServerUrlKey), url);
    m_client->setBaseUrl(QUrl(url));
    m_client->setAuthorizationHeader(m_auth->text().trimmed());

    const QString modality = m_modality->currentIndex() == 0 ? QString() : m_modality->currentText();
    setBusy(true, tr("Searching…"));
    m_results->setRowCount(0);
    m_studies.clear();
    m_client->searchStudies(m_patient->text(), m_date->text(), m_accession->text(), modality,
                            [this](bool ok, const QVector<DicomWebClient::StudySummary>& studies,
                                   const QString& error) {
        setBusy(false);
        if (!ok) {
            m_status->setText(error);
            return;
        }
        m_studies = studies;
        m_results->setRowCount(studies.size());
        for (int row = 0; row < studies.size(); ++row) {
            const auto& s = studies.at(row);
            m_results->setItem(row, 0, new QTableWidgetItem(s.patientName));
            m_results->setItem(row, 1, new QTableWidgetItem(s.studyDate));
            m_results->setItem(row, 2, new QTableWidgetItem(s.modalities));
            m_results->setItem(row, 3, new QTableWidgetItem(s.description));
            m_results->setItem(row, 4, new QTableWidgetItem(QString::number(s.instances)));
        }
        m_results->resizeColumnsToContents();
        m_results->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
        m_status->setText(tr("%1 studies found. Select one, then Download and open.")
                              .arg(studies.size()));
    });
}

QString ServerDialog::selectedStudyUid() const
{
    const int row = m_results->currentRow();
    if (row < 0 || row >= m_studies.size()) return {};
    return m_studies.at(row).studyUid;
}

void ServerDialog::downloadSelected()
{
    const QString uid = selectedStudyUid();
    if (uid.isEmpty()) return;
    // Each download gets its own folder: two studies in the cache must never merge into one scan.
    const QString dest = studiesRoot() + QLatin1Char('/') + uid;
    QDir().mkpath(dest);
    setBusy(true, tr("Downloading study…"));
    m_client->downloadStudy(
        uid, dest,
        [this](int done, int total) {
            m_status->setText(tr("Downloading… %1 of %2 instances").arg(done).arg(total));
        },
        [this, dest](bool ok, int files, const QString& error) {
            setBusy(false);
            if (!ok) {
                m_status->setText(error);
                return;
            }
            m_status->setText(tr("Downloaded %1 instances.").arg(files));
            emit studyReady(dest);
            accept();
        });
}
