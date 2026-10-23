// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// A throwaway HTTP server implementing just enough DICOMweb for tests: QIDO study search, QIDO
// instance listing, WADO instance retrieval. Shared by the client test and the dialog test so both
// speak to the same fake PACS.

#include <QByteArray>
#include <QHostAddress>
#include <QObject>
#include <QStringList>
#include <QTcpServer>
#include <QTcpSocket>

class MockDicomWeb : public QObject {
public:
    MockDicomWeb(QByteArray studyJson, QByteArray instancesJson, QByteArray instanceBody)
        : m_studyJson(std::move(studyJson)), m_instancesJson(std::move(instancesJson)),
          m_instanceBody(std::move(instanceBody))
    {
        m_server.listen(QHostAddress::LocalHost, 0);
        m_port = m_server.serverPort();
        connect(&m_server, &QTcpServer::newConnection, this, [this] {
            QTcpSocket* socket = m_server.nextPendingConnection();
            connect(socket, &QTcpSocket::readyRead, this, [this, socket] {
                const QByteArray request = socket->readAll();
                const QByteArray line = request.left(request.indexOf("\r\n"));
                const QString path = QString::fromUtf8(line).section(QLatin1Char(' '), 1, 1);
                respond(socket, path);
            });
            connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        });
    }

    quint16 port() const { return m_port; }
    QStringList seenPaths() const { return m_paths; }
    int downloads() const { return m_downloads; }

    bool failInstances = false;

private:
    void respond(QTcpSocket* socket, const QString& path)
    {
        m_paths << path;
        QByteArray body;
        QByteArray status = "200 OK";
        QByteArray contentType = "application/dicom+json";
        if (path.contains(QLatin1String("/instances/")) &&
            !path.endsWith(QLatin1String("/instances"))) {
            contentType = "application/dicom";
            if (failInstances) {
                status = "404 Not Found";
                body = QByteArray("{\"error\":\"no such instance\"}");
            } else {
                body = m_instanceBody;
                ++m_downloads;
            }
        } else if (path.endsWith(QLatin1String("/instances"))) {
            body = m_instancesJson;
        } else {
            body = m_studyJson;
        }
        const QByteArray response = "HTTP/1.1 " + status + "\r\nContent-Type: " + contentType +
                                    "\r\nContent-Length: " + QByteArray::number(body.size()) +
                                    "\r\nConnection: close\r\n\r\n" + body;
        socket->write(response);
        socket->flush();
        socket->disconnectFromHost();
    }

    QTcpServer m_server;
    quint16 m_port = 0;
    QByteArray m_studyJson;
    QByteArray m_instancesJson;
    QByteArray m_instanceBody;
    QStringList m_paths;
    int m_downloads = 0;
};

// Canned QIDO bodies shared by the tests: two studies, one multi-modality, two instances in the
// first study.
inline QByteArray mockStudyJson()
{
    return R"([{
  "0020000D": {"vr": "UI", "Value": ["1.2.826.0.1.3680043.9.1"]},
  "00100010": {"vr": "PN", "Value": ["Doe^Jane"]},
  "00080020": {"vr": "DA", "Value": ["20240115"]},
  "00080061": {"vr": "CS", "Value": ["CT"]},
  "00081030": {"vr": "LO", "Value": ["CT ABDOMEN"]},
  "00201208": {"vr": "IS", "Value": ["2"]}
},{
  "0020000D": {"vr": "UI", "Value": ["1.2.826.0.1.3680043.9.2"]},
  "00080061": {"vr": "CS", "Value": ["MR", "PT"]},
  "00081030": {"vr": "LO", "Value": ["PET MR"]},
  "00201208": {"vr": "IS", "Value": ["1"]}
}])";
}

inline QByteArray mockInstancesJson()
{
    return R"([
  {"00080018": {"vr": "UI", "Value": ["1.2.3.100"]}},
  {"00080018": {"vr": "UI", "Value": ["1.2.3.101"]}}
])";
}
