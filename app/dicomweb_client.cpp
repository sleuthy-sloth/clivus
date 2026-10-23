// SPDX-License-Identifier: GPL-3.0-or-later
#include "dicomweb_client.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrlQuery>

#include <memory>

namespace {

// DICOMweb JSON encodes values as {"vr": "PN", "Value": ["Doe^Jane"]}; some servers omit "Value"
// for an empty attribute. Everything here reads that shape defensively.
QString firstValue(const QJsonObject& item, const char* tag)
{
    const QJsonObject element = item.value(QLatin1String(tag)).toObject();
    const QJsonArray values = element.value(QStringLiteral("Value")).toArray();
    if (values.isEmpty()) return {};
    const QJsonValue first = values.first();
    return first.isString() ? first.toString() : QString::number(first.toDouble());
}

QStringList allValues(const QJsonObject& item, const char* tag)
{
    QStringList out;
    const QJsonArray values =
        item.value(QLatin1String(tag)).toObject().value(QStringLiteral("Value")).toArray();
    for (const QJsonValue& v : values) out << (v.isString() ? v.toString() : QString());
    return out;
}

QString httpStatusError(QNetworkReply* reply)
{
    const int code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (code <= 0) return reply->errorString();
    return QStringLiteral("HTTP %1 %2").arg(code).arg(reply->errorString());
}

}  // namespace

DicomWebClient::DicomWebClient(QObject* parent) : QObject(parent), m_net(new QNetworkAccessManager(this)) {}

void DicomWebClient::setBaseUrl(const QUrl& base)
{
    m_base = base;
    while (m_base.path().endsWith(QLatin1Char('/'))) m_base.setPath(m_base.path().chopped(1));
}

void DicomWebClient::setAuthorizationHeader(const QString& value)
{
    m_authHeader = value.trimmed();
}

QUrl DicomWebClient::studiesUrl(const QString& patientName, const QString& studyDate,
                                const QString& accessionNumber, const QString& modality) const
{
    QUrl url = m_base;
    url.setPath(m_base.path() + QStringLiteral("/studies"));
    QUrlQuery query;
    if (!patientName.trimmed().isEmpty())
        query.addQueryItem(QStringLiteral("PatientName"), patientName.trimmed());
    if (!studyDate.trimmed().isEmpty())
        query.addQueryItem(QStringLiteral("StudyDate"), studyDate.trimmed());
    if (!accessionNumber.trimmed().isEmpty())
        query.addQueryItem(QStringLiteral("AccessionNumber"), accessionNumber.trimmed());
    if (!modality.trimmed().isEmpty())
        query.addQueryItem(QStringLiteral("ModalitiesInStudy"), modality.trimmed());
    url.setQuery(query);
    return url;
}

QUrl DicomWebClient::instancesUrl(const QString& studyUid) const
{
    QUrl url = m_base;
    url.setPath(m_base.path() + QStringLiteral("/studies/") + studyUid + QStringLiteral("/instances"));
    return url;
}

QUrl DicomWebClient::instanceUrl(const QString& studyUid, const QString& instanceUid) const
{
    QUrl url = m_base;
    url.setPath(m_base.path() + QStringLiteral("/instances/") + instanceUid);
    Q_UNUSED(studyUid);   // the instance-level WADO-RS form; study/series path not needed
    return url;
}

bool DicomWebClient::parseStudies(const QByteArray& json, QVector<StudySummary>& out, QString& error)
{
    out.clear();
    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        error = QStringLiteral("The server's reply was not JSON (%1)").arg(parseError.errorString());
        return false;
    }
    if (!doc.isArray()) {
        error = QStringLiteral("The server's reply was not a list of studies");
        return false;
    }
    for (const QJsonValue& value : doc.array()) {
        const QJsonObject item = value.toObject();
        StudySummary summary;
        summary.studyUid = firstValue(item, "0020000D");
        if (summary.studyUid.isEmpty()) continue;   // nothing addressable: not a result we can open
        summary.patientName = firstValue(item, "00100010");
        summary.studyDate = firstValue(item, "00080020");
        summary.modalities = allValues(item, "00080061").join(QLatin1String("\\"));
        summary.description = firstValue(item, "00081030");
        const QString count = firstValue(item, "00201208");
        summary.instances = count.isEmpty() ? 0 : count.toInt();
        out.push_back(summary);
    }
    if (out.isEmpty()) {
        error = QStringLiteral("No studies matched.");
        return false;
    }
    return true;
}

bool DicomWebClient::parseInstanceUids(const QByteArray& json, QStringList& out, QString& error)
{
    out.clear();
    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        error = QStringLiteral("The server's instance list was not JSON (%1)")
                    .arg(parseError.errorString());
        return false;
    }
    if (!doc.isArray()) {
        error = QStringLiteral("The server's instance list was not an array");
        return false;
    }
    for (const QJsonValue& value : doc.array()) {
        const QString uid = firstValue(value.toObject(), "00080018");
        if (!uid.isEmpty()) out << uid;
    }
    if (out.isEmpty()) {
        error = QStringLiteral("The server reported the study but listed no instances.");
        return false;
    }
    return true;
}

void DicomWebClient::searchStudies(
    const QString& patientName, const QString& studyDate, const QString& accessionNumber,
    const QString& modality,
    std::function<void(bool, const QVector<StudySummary>&, const QString&)> done)
{
    if (!m_base.isValid() || m_base.isEmpty()) {
        done(false, {}, QStringLiteral("Set a DICOMweb base URL first (e.g. https://host/dicomweb)."));
        return;
    }
    QNetworkRequest request(studiesUrl(patientName, studyDate, accessionNumber, modality));
    request.setRawHeader("Accept", "application/dicom+json");
    if (!m_authHeader.isEmpty()) request.setRawHeader("Authorization", m_authHeader.toUtf8());
    QNetworkReply* reply = m_net->get(request);
    connect(reply, &QNetworkReply::finished, this, [reply, done = std::move(done)] {
        reply->deleteLater();
        const QByteArray body = reply->readAll();
        if (reply->error() != QNetworkReply::NoError) {
            done(false, {}, QStringLiteral("Search failed: %1").arg(httpStatusError(reply)));
            return;
        }
        QVector<StudySummary> studies;
        QString error;
        if (!DicomWebClient::parseStudies(body, studies, error)) {
            done(false, {}, error);
            return;
        }
        done(true, studies, QString());
    });
}

void DicomWebClient::listInstances(
    const QString& studyUid,
    std::function<void(bool, const QStringList&, const QString&)> done)
{
    if (studyUid.isEmpty()) {
        done(false, {}, QStringLiteral("No study selected."));
        return;
    }
    QNetworkRequest request(instancesUrl(studyUid));
    request.setRawHeader("Accept", "application/dicom+json");
    if (!m_authHeader.isEmpty()) request.setRawHeader("Authorization", m_authHeader.toUtf8());
    QNetworkReply* reply = m_net->get(request);
    connect(reply, &QNetworkReply::finished, this, [reply, done = std::move(done)] {
        reply->deleteLater();
        const QByteArray body = reply->readAll();
        if (reply->error() != QNetworkReply::NoError) {
            done(false, {}, QStringLiteral("Listing instances failed: %1").arg(httpStatusError(reply)));
            return;
        }
        QStringList uids;
        QString error;
        if (!DicomWebClient::parseInstanceUids(body, uids, error)) {
            done(false, {}, error);
            return;
        }
        done(true, uids, QString());
    });
}

void DicomWebClient::downloadStudy(const QString& studyUid, const QString& destDir,
                                   std::function<void(int, int)> onProgress,
                                   std::function<void(bool, int, const QString&)> finished)
{
    m_cancelled = false;
    listInstances(studyUid, [this, studyUid, destDir, onProgress = std::move(onProgress),
                             finished = std::move(finished)](bool ok, const QStringList& uids,
                                                             const QString& error) {
        if (!ok) {
            finished(false, 0, error);
            return;
        }
        // Sequential downloads: a study can be thousands of instances, and a queue of parallel
        // replies is how servers get hammered and client memory balloons.
        auto index = std::make_shared<int>(0);
        auto files = std::make_shared<int>(0);
        auto step = std::make_shared<std::function<void()>>();
        const int total = uids.size();
        *step = [this, uids, destDir, index, files, step, onProgress, finished, total] {
            if (m_cancelled) {
                finished(false, *files, QStringLiteral("Download cancelled."));
                return;
            }
            if (*index >= total) {
                finished(true, *files, QString());
                return;
            }
            const QString uid = uids.at(*index);
            QNetworkRequest request(instanceUrl(QString(), uid));
            request.setRawHeader("Accept", "application/dicom");
            if (!m_authHeader.isEmpty()) request.setRawHeader("Authorization", m_authHeader.toUtf8());
            QNetworkReply* reply = m_net->get(request);
            connect(reply, &QNetworkReply::finished, this,
                    [this, reply, uid, index, files, step, destDir, onProgress, finished, uids] {
                reply->deleteLater();
                const QByteArray body = reply->readAll();
                if (reply->error() != QNetworkReply::NoError) {
                    finished(false, *files,
                             QStringLiteral("Downloading instance %1 failed: %2")
                                 .arg(uid, httpStatusError(reply)));
                    return;
                }
                // WADO-RS may answer multipart/related; a single-part reply is the common case and
                // the only one handled here (documented limitation).
                if (body.isEmpty()) {
                    finished(false, *files, QStringLiteral("Instance %1 came back empty.").arg(uid));
                    return;
                }
                const QString path =
                    QStringLiteral("%1/%2-%3.dcm").arg(destDir).arg(*index, 6, 10, QLatin1Char('0')).arg(uid);
                QFile file(path);
                if (!file.open(QIODevice::WriteOnly) || file.write(body) != body.size()) {
                    finished(false, *files,
                             QStringLiteral("Could not write %1 (disk full or read-only?).").arg(path));
                    return;
                }
                file.close();
                ++*files;
                ++*index;
                if (onProgress) onProgress(*files, uids.size());
                (*step)();
            });
        };
        (*step)();
    });
}
