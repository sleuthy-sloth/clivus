// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QDialog>
#include <QString>

class QCheckBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;

// "Export de-identified copy…": pick a destination, choose what to strip, run it, and read what
// actually happened.
//
// The dialog refuses to present the result as "anonymous". It reports the counts, shows the
// verification outcome, and keeps the two limits in front of the user: pixel data is untouched (so
// burned-in annotations survive), and de-identification is not a legal determination.
class DeidDialog : public QDialog {
    Q_OBJECT
public:
    explicit DeidDialog(const QString& sourceFolder, QWidget* parent = nullptr);

private:
    void chooseDestination();
    void run();
    void showReport(bool ok, const QString& summary, const QStringList& lines);

    QString m_source;
    QLineEdit* m_destination = nullptr;
    QCheckBox* m_remapUids = nullptr;
    QCheckBox* m_shiftDates = nullptr;
    QCheckBox* m_redact = nullptr;
    QCheckBox* m_privateTags = nullptr;
    QCheckBox* m_sex = nullptr;
    QLabel* m_status = nullptr;
    QPlainTextEdit* m_report = nullptr;
    QPushButton* m_run = nullptr;
    bool m_running = false;
};
