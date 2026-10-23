// SPDX-License-Identifier: GPL-3.0-or-later
// The "Open from server" dialog against a mock DICOMweb server: typing a URL and searching fills the
// result table, selecting a row enables Download, and a successful download emits a folder that the
// window can open. This is the UI path the client test cannot cover.
//
// Run: QT_QPA_PLATFORM=offscreen ./app/clivus_test_server_dialog <corpus-folder>

#include "server_dialog.h"

#include "test_mock_dicomweb.h"

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QSettings>
#include <QTemporaryDir>
#include <QThread>
#include <cstdio>
#include <functional>

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
    QCoreApplication::setApplicationName(QStringLiteral("clivus-server-dialog-test"));
    QApplication app(argc, argv);
    QSettings().clear();
    QTemporaryDir cache;
    CHECK(cache.isValid());
    qputenv("CLIVUS_CACHE_DIR", cache.path().toUtf8());
    CHECK(argc > 1);

    QFile corpus(QString::fromUtf8(argv[1]) + QStringLiteral("/CT_small.dcm"));
    CHECK(corpus.open(QIODevice::ReadOnly));
    const QByteArray dicom = corpus.readAll();

    MockDicomWeb server(mockStudyJson(), mockInstancesJson(), dicom);

    ServerDialog dialog;
    dialog.show();

    const auto edits = dialog.findChildren<QLineEdit*>();
    CHECK(edits.size() >= 2);
    QLineEdit* url = edits.first();   // the first field is the server URL by construction
    url->setText(QStringLiteral("http://127.0.0.1:%1/dicomweb").arg(server.port()));

    QPushButton* search = nullptr;
    QPushButton* download = nullptr;
    for (QPushButton* button : dialog.findChildren<QPushButton*>()) {
        if (button->text().contains(QStringLiteral("Search"))) search = button;
        if (button->text().contains(QStringLiteral("Download"))) download = button;
    }
    CHECK(search != nullptr);
    CHECK(download != nullptr);
    CHECK(!download->isEnabled());   // nothing selected yet

    search->click();
    QTableWidget* table = dialog.findChild<QTableWidget*>();
    CHECK(table != nullptr);
    CHECK(waitFor([&] { return table->rowCount() == 2; }, 15000));
    CHECK(table->item(0, 0)->text() == QStringLiteral("Doe^Jane"));
    CHECK(table->item(1, 2)->text() == QStringLiteral("MR\\PT"));

    table->selectRow(0);
    QCoreApplication::processEvents();
    CHECK(download->isEnabled());

    QString readyFolder;
    QObject::connect(&dialog, &ServerDialog::studyReady,
                     [&](const QString& folder) { readyFolder = folder; });
    download->click();
    CHECK(waitFor([&] { return !readyFolder.isEmpty(); }, 20000));
    CHECK(QDir(readyFolder).entryList(QDir::Files).size() == 2);

    // A study whose instances fail reports a message and does not emit a ready folder.
    readyFolder.clear();
    table->selectRow(1);
    QCoreApplication::processEvents();
    server.failInstances = true;
    download->click();
    CHECK(waitFor([&] { return !download->isEnabled() || !readyFolder.isEmpty(); }, 20000));
    QCoreApplication::processEvents();
    CHECK(readyFolder.isEmpty());

    QSettings().clear();
    std::printf("server dialog: all checks passed\n");
    return 0;
}
