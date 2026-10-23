// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVector>

#include <atomic>
#include <functional>

class QNetworkAccessManager;
class QNetworkReply;

// DICOMweb (QIDO-RS search, WADO-RS retrieve). Qt6::Network only — no third-party HTTP stack and no
// DIMSE: this is the half of network access a viewer actually needs to open a study.
//
// The client is deliberately dumb about policy and honest about failure: every call reports either
// data or an error string (HTTP status included), a download that loses instances is an error rather
// than a partial study, and nothing here writes patient identifiers anywhere.
//
// Threading: all calls are asynchronous on the GUI thread's event loop; the callbacks run there too.
// A single QNetworkAccessManager is owned by the client.
class DicomWebClient : public QObject {
    Q_OBJECT
public:
    struct StudySummary {
        QString studyUid;
        QString patientName;
        QString studyDate;     // as the server sent it (DICOM DA format)
        QString modalities;    // concatenated, e.g. "CT\\MR"
        QString description;
        int instances = 0;
    };

    explicit DicomWebClient(QObject* parent = nullptr);

    // Base URL, e.g. https://pacs.example.com/dicomweb. A trailing slash is tolerated.
    void setBaseUrl(const QUrl& base);
    QUrl baseUrl() const { return m_base; }

    // Optional Authorization header value ("Bearer …", "Basic …"), applied to every request.
    void setAuthorizationHeader(const QString& value);

    // QIDO-RS: GET /studies?<filters>. Empty filters are omitted from the query.
    // access to patientName/studyDate/accessionNumber/modalities (any may be empty).
    void searchStudies(const QString& patientName, const QString& studyDate,
                       const QString& accessionNumber, const QString& modality,
                       std::function<void(bool ok, const QVector<StudySummary>& studies,
                                          const QString& error)> done);

    // QIDO-RS: GET /studies/{uid}/instances → instance UIDs, in server order.
    void listInstances(const QString& studyUid,
                       std::function<void(bool ok, const QStringList& instanceUids,
                                          const QString& error)> done);

    // WADO-RS: retrieve every instance of the study into destDir as <index>-<uid>.dcm.
    // onProgress is called with (downloaded, total) after each file. A failed instance aborts the
    // download and reports an error — a study with missing instances must not look complete.
    void downloadStudy(const QString& studyUid, const QString& destDir,
                       std::function<void(int done, int total)> onProgress,
                       std::function<void(bool ok, int files, const QString& error)> finished);

    // Asks any download in flight to stop; it reports cancellation through its finished callback.
    void cancel() { m_cancelled = true; }

    // Builds the QIDO query URL — exposed for tests so URL/filter construction is pinned.
    QUrl studiesUrl(const QString& patientName, const QString& studyDate,
                    const QString& accessionNumber, const QString& modality) const;
    QUrl instancesUrl(const QString& studyUid) const;
    QUrl instanceUrl(const QString& studyUid, const QString& instanceUid) const;

    // Parses a QIDO-RS response body (application/dicom+json): a JSON array of study results.
    // Returns false with a message when the body is not the expected shape.
    static bool parseStudies(const QByteArray& json, QVector<StudySummary>& out, QString& error);
    // Parses a QIDO-RS instance response: an array of objects each carrying 00080018 (SOP Instance UID).
    static bool parseInstanceUids(const QByteArray& json, QStringList& out, QString& error);

private:
    QUrl m_base;
    QString m_authHeader;
    QNetworkAccessManager* m_net = nullptr;
    std::atomic<bool> m_cancelled{false};
};
