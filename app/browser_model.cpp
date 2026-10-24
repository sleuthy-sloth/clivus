// SPDX-License-Identifier: GPL-3.0-or-later
#include "browser_model.h"

#include "series_summary.h"

#include <QStringList>

#include <map>

namespace clivus::ui {
namespace {

// "Patient A", "Patient B", ... then "Patient AA" — an ordinal that stays short for the handful of
// patients a folder normally holds and never runs out of letters.
QString ordinalLabel(size_t index)
{
    QString letters;
    size_t n = index;
    do {
        letters.prepend(QChar('A' + int(n % 26)));
        n = n / 26;
    } while (n > 0);
    return QStringLiteral("Patient ") + letters;
}

// The identity a series is grouped under. Patient ID first: it is what the scan proved, and a name
// can be corrected or transliterated between two exports of the same study.
std::string patientKeyOf(const clivus::SeriesInfo& s)
{
    if (!s.patientId.empty()) return "id:" + s.patientId;
    if (!s.patientName.empty()) return "name:" + s.patientName;
    return std::string();   // unknown: every unknown series shares one node rather than inventing one
}

QString studyLabelOf(const clivus::SeriesInfo& s)
{
    if (!s.studyDescription.empty()) return QString::fromStdString(s.studyDescription);
    return QStringLiteral("(no study description)");
}

}  // namespace

int BrowserTree::seriesCount() const
{
    int count = 0;
    for (const auto& patient : patients)
        for (const auto& study : patient.studies)
            count += int(study.series.size());
    return count;
}

BrowserTree buildBrowserTree(const std::vector<clivus::SeriesInfo>& series, const BrowserOptions& options)
{
    BrowserTree tree;

    // Insertion order decides the labels, so it has to be stable: patients are numbered by their first
    // appearance in the scan's own order, not by a hash or a pointer.
    std::map<std::string, size_t> patientPositions;
    std::map<std::pair<std::string, std::string>, size_t> studyPositions;

    for (size_t i = 0; i < series.size(); ++i) {
        const clivus::SeriesInfo& s = series[i];

        // Objects with no series identity get their own node at the end: they are not a patient, and
        // folding them into one would be a lie about what the folder contains.
        const std::string patientKey = s.quarantined ? std::string("quarantine") : patientKeyOf(s);

        auto patientIt = patientPositions.find(patientKey);
        if (patientIt == patientPositions.end()) {
            BrowserPatient patient;
            patient.patientKey = patientKey;
            const size_t ordinal = tree.patients.size();
            if (s.quarantined) {
                patient.label = QStringLiteral("Objects without a series identity");
            } else if (options.showPatientNames && !s.patientName.empty()) {
                // The ordinal stays even with names on: two patients can share a name, and the point
                // of the ordinal is that they remain distinguishable.
                patient.label = QStringLiteral("%1 (%2)")
                                    .arg(QString::fromStdString(s.patientName),
                                         ordinalLabel(ordinal).mid(QStringLiteral("Patient ").size()));
            } else {
                patient.label = ordinalLabel(ordinal);
            }
            patientPositions.emplace(patientKey, tree.patients.size());
            tree.patients.push_back(std::move(patient));
            patientIt = patientPositions.find(patientKey);
        }
        BrowserPatient& patient = tree.patients[patientIt->second];

        const auto studyKey = std::make_pair(patientKey, s.studyUid);
        auto studyIt = studyPositions.find(studyKey);
        if (studyIt == studyPositions.end()) {
            BrowserStudy study;
            study.studyUid = s.studyUid;
            study.label = s.quarantined ? QStringLiteral("No study identity") : studyLabelOf(s);
            // The study node carries its own count so a collapsed tree still says how much is inside.
            studyPositions.emplace(studyKey, patient.studies.size());
            patient.studies.push_back(std::move(study));
            studyIt = studyPositions.find(studyKey);
        }
        BrowserStudy& study = patient.studies[studyIt->second];

        BrowserSeries entry;
        entry.seriesIndex = int(i);
        entry.title = s.quarantined ? quarantinedSeriesLabel() : seriesTitle(s);
        entry.details = seriesDetails(s);
        study.series.push_back(std::move(entry));
    }

    return tree;
}

BrowserTree filterBrowserTree(const BrowserTree& tree, const QString& query)
{
    if (query.trimmed().isEmpty()) return tree;

    const QString needle = query.trimmed();
    const auto matches = [&needle](const QString& haystack) {
        return haystack.contains(needle, Qt::CaseInsensitive);
    };

    BrowserTree out;
    for (const auto& patient : tree.patients) {
        BrowserPatient keptPatient;
        keptPatient.patientKey = patient.patientKey;
        keptPatient.label = patient.label;
        for (const auto& study : patient.studies) {
            BrowserStudy keptStudy;
            keptStudy.studyUid = study.studyUid;
            keptStudy.label = study.label;
            for (const auto& entry : study.series) {
                if (matches(entry.title) || matches(entry.details) || matches(patient.label) ||
                    matches(study.label))
                    keptStudy.series.push_back(entry);
            }
            if (!keptStudy.series.empty()) keptPatient.studies.push_back(std::move(keptStudy));
        }
        if (!keptPatient.studies.empty()) out.patients.push_back(std::move(keptPatient));
    }
    return out;
}

QString quarantinedSeriesLabel()
{
    return QStringLiteral("Images with no series identity");
}

}  // namespace clivus::ui
