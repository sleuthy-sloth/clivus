// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QDialog>
#include <QVector>

#include "dicomweb_client.h"

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;

// "Open from server": a DICOMweb (QIDO-RS/WADO-RS) front door. Search returns studies; picking one
// and confirming downloads its instances into a fresh folder under the app cache and hands that
// folder to the normal scan path, so nothing about decoding or viewing changes.
//
// The URL is remembered between sessions; no patient data, study UIDs or credentials are persisted
// beyond that URL (and an Authorization header, if the user types one, lives only in this dialog's
// lifetime unless stored as a setting by an explicit choice — currently not stored).
class ServerDialog : public QDialog {
    Q_OBJECT
public:
    explicit ServerDialog(QWidget* parent = nullptr);

signals:
    // Emitted with a folder ready to open (already downloaded and written).
    void studyReady(const QString& folder);

private:
    void runSearch();
    void downloadSelected();
    void setBusy(bool busy, const QString& message = {});
    QString selectedStudyUid() const;

    DicomWebClient* m_client = nullptr;
    QLineEdit* m_url = nullptr;
    QLineEdit* m_auth = nullptr;
    QLineEdit* m_patient = nullptr;
    QLineEdit* m_date = nullptr;
    QLineEdit* m_accession = nullptr;
    QComboBox* m_modality = nullptr;
    QPushButton* m_search = nullptr;
    QPushButton* m_download = nullptr;
    QPushButton* m_cancel = nullptr;
    QLabel* m_status = nullptr;
    QTableWidget* m_results = nullptr;
    QVector<DicomWebClient::StudySummary> m_studies;
};
