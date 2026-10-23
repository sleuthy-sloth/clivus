// SPDX-License-Identifier: GPL-3.0-or-later
// DICOMweb client against a local mock server: URL construction, QIDO parsing, WADO download,
// cancellation and failure reporting. Nothing here talks to a real PACS — the point is the protocol
// handling and the error paths, which is what breaks in practice.
//
// Run: QT_QPA_PLATFORM=offscreen ./app/clivus_test_dicomweb <corpus-folder>

#include "dicomweb_client.h"

#include "test_mock_dicomweb.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QHostAddress>
#include <QTemporaryDir>
#include <QThread>
#include <cstdio>
#include <functional>
#include <memory>

#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
            std::exit(1);                                                  \
        }                                                                  \
    } while (false)

namespace {

bool waitFor(const std::function<bool()>& condition, int timeoutMs)
{
    QElapsedTimer clock;
    clock.start();
    while (!condition()) {
        if (clock.elapsed() > timeoutMs) return false;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        QThread::msleep(10);
    }
    return true;
}

}  // namespace

int main(int argc, char** argv)
{
    QCoreApplication::setOrganizationName(QStringLiteral("sleuthy-sloth"));
    QCoreApplication::setApplicationName(QStringLiteral("clivus-dicomweb-test"));
    QCoreApplication app(argc, argv);
    qputenv("CLIVUS_CACHE_DIR", (QDir::tempPath() + QStringLiteral("/clivus-dicomweb-cache")).toUtf8());
    CHECK(argc > 1);

    // A real DICOM file is the instance body: the client must write bytes verbatim.
    QFile corpusFile(QString::fromUtf8(argv[1]) + QStringLiteral("/CT_small.dcm"));
    CHECK(corpusFile.open(QIODevice::ReadOnly));
    const QByteArray dicom = corpusFile.readAll();

    MockDicomWeb server(mockStudyJson(), mockInstancesJson(), dicom);
    DicomWebClient client;
    client.setBaseUrl(QUrl(QStringLiteral("http://127.0.0.1:%1/dicomweb/").arg(server.port())));

    // (a) URL construction: filters appear, empty ones do not, trailing slash tolerated.
    {
        const QUrl url = client.studiesUrl(QStringLiteral("Doe^Jane"), QStringLiteral("20240115"),
                                           QString(), QStringLiteral("CT"));
        CHECK(url.path() == QStringLiteral("/dicomweb/studies"));
        const QString query = url.query();
        CHECK(query.contains(QStringLiteral("PatientName=Doe%5EJane")) ||
              query.contains(QStringLiteral("PatientName=Doe^Jane")));
        CHECK(query.contains(QStringLiteral("StudyDate=20240115")));
        CHECK(query.contains(QStringLiteral("ModalitiesInStudy=CT")));
        CHECK(!query.contains(QStringLiteral("AccessionNumber")));
    }

    // (b) search parses both studies, including the multi-valued modality list.
    {
        bool done = false, ok = false;
        QVector<DicomWebClient::StudySummary> studies;
        QString error;
        client.searchStudies(QString(), QString(), QString(), QString(),
                             [&](bool success, const QVector<DicomWebClient::StudySummary>& result,
                                 const QString& err) {
                                 done = true;
                                 ok = success;
                                 studies = result;
                                 error = err;
                             });
        CHECK(waitFor([&] { return done; }, 10000));
        CHECK(ok);
        CHECK(studies.size() == 2);
        CHECK(studies[0].studyUid == QStringLiteral("1.2.826.0.1.3680043.9.1"));
        CHECK(studies[0].patientName == QStringLiteral("Doe^Jane"));
        CHECK(studies[0].instances == 2);
        CHECK(studies[1].studyUid == QStringLiteral("1.2.826.0.1.3680043.9.2"));
        CHECK(studies[1].modalities == QStringLiteral("MR\\PT"));
    }

    // (c) download writes one file per instance, byte-identical, reporting progress.
    QTemporaryDir dest;
    CHECK(dest.isValid());
    {
        bool done = false, ok = false;
        int files = 0, progressCalls = 0, lastTotal = 0;
        QString error;
        client.downloadStudy(
            QStringLiteral("1.2.826.0.1.3680043.9.1"), dest.path(),
            [&](int, int total) { ++progressCalls; lastTotal = total; },
            [&](bool success, int count, const QString& err) {
                done = true;
                ok = success;
                files = count;
                error = err;
            });
        CHECK(waitFor([&] { return done; }, 15000));
        CHECK(ok);
        CHECK(files == 2);
        CHECK(progressCalls == 2);
        CHECK(lastTotal == 2);
        const QStringList written = QDir(dest.path()).entryList(QDir::Files);
        CHECK(written.size() == 2);
        QFile first(dest.path() + QLatin1Char('/') + written.first());
        CHECK(first.open(QIODevice::ReadOnly));
        CHECK(first.readAll() == dicom);
    }

    // (d) a failing instance is an error, not a partial study that looks complete.
    {
        server.failInstances = true;
        bool done = false, ok = true;
        QString error;
        client.downloadStudy(QStringLiteral("1.2.3"), dest.path(), nullptr,
                             [&](bool success, int, const QString& err) {
                                 done = true;
                                 ok = success;
                                 error = err;
                             });
        CHECK(waitFor([&] { return done; }, 15000));
        CHECK(!ok);
        CHECK(error.contains(QStringLiteral("404")));
    }
    server.failInstances = false;

    // (e) cancellation stops the download and says so.
    {
        bool done = false, ok = true;
        QString error;
        client.downloadStudy(QStringLiteral("1.2.3"), dest.path(), nullptr,
                             [&](bool success, int, const QString& err) {
                                 done = true;
                                 ok = success;
                                 error = err;
                             });
        client.cancel();
        CHECK(waitFor([&] { return done; }, 15000));
        CHECK(!ok);
        CHECK(error.contains(QStringLiteral("cancelled"), Qt::CaseInsensitive));
    }

    // (f) bad JSON is reported, not silently empty.
    {
        QVector<DicomWebClient::StudySummary> studies;
        QString error;
        CHECK(!DicomWebClient::parseStudies(QByteArray("not json"), studies, error));
        CHECK(error.contains(QStringLiteral("JSON")));
        CHECK(!DicomWebClient::parseStudies(QByteArray("[]"), studies, error));
        CHECK(error.contains(QStringLiteral("No studies")));
    }

    std::printf("dicomweb: all checks passed\n");
    return 0;
}
