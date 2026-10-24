// SPDX-License-Identifier: GPL-3.0-or-later
// The browser's grouping and labelling, without a window: what the tree looks like for the shapes a
// folder actually produces, and what a filter does to it. Widget-free on purpose — the row text and
// the boundaries are what a user judges the browser by, and both are testable here.
#include "browser_model.h"

#include "clivus/series_index.h"

#include <QString>

#include <cstdio>
#include <string>
#include <vector>

namespace {

int failures = 0;

void expect(bool ok, const char* what)
{
    if (!ok) {
        std::printf("FAIL %s\n", what);
        ++failures;
    }
}

clivus::SeriesInfo makeSeries(const std::string& study, const std::string& series,
                              const std::string& patientId, const std::string& patientName,
                              const std::string& description, const std::string& modality,
                              int images)
{
    clivus::SeriesInfo s;
    s.studyUid = study;
    s.seriesUid = series;
    s.patientId = patientId;
    s.patientName = patientName;
    s.seriesDescription = description;
    s.studyDescription = "CT HEAD";
    s.modality = modality;
    s.width = 512;
    s.height = 512;
    for (int i = 0; i < images; ++i) {
        s.files.push_back("/study/slice" + std::to_string(i) + ".dcm");
        s.frameCounts.push_back(1);
    }
    return s;
}

}  // namespace

int main()
{
    const std::vector<clivus::SeriesInfo> scan = {
        makeSeries("1.2.S1", "1.2.S1.1", "PID-1", "Doe^Jane", "AXIAL 5mm", "CT", 3),
        makeSeries("1.2.S1", "1.2.S1.2", "PID-1", "Doe^Jane", "CORONAL 5mm", "CT", 2),
        makeSeries("1.2.S2", "1.2.S2.1", "PID-1", "Doe^Jane", "AXIAL 1mm", "CT", 4),
        // A different patient with an identically described series: the case the flat list could not
        // distinguish at all.
        makeSeries("1.2.S3", "1.2.S3.1", "PID-2", "Doe^Jane", "AXIAL 5mm", "CT", 2),
    };

    {   // Shape: two patients, first with two studies, each study holding its own series.
        const auto tree = clivus::ui::buildBrowserTree(scan);
        expect(tree.patients.size() == 2, "two patient IDs make two patient nodes");
        expect(tree.seriesCount() == 4, "every series appears exactly once");
        if (tree.patients.size() == 2) {
            expect(tree.patients[0].label == "Patient A", "the first patient is labelled A");
            expect(tree.patients[1].label == "Patient B", "the second patient is labelled B");
            expect(tree.patients[0].studies.size() == 2, "the first patient has both studies");
            expect(tree.patients[0].studies[0].series.size() == 2,
                   "the first study holds both of its series");
            expect(tree.patients[0].studies[1].series.size() == 1, "the second study holds one");
            expect(tree.patients[1].studies.size() == 1, "the second patient has one study");
        }
    }

    {   // The two identical series descriptions live under different patients and stay distinguishable
        // because the patient nodes differ, which is the whole point of the hierarchy.
        const auto tree = clivus::ui::buildBrowserTree(scan);
        expect(tree.patients.size() == 2 &&
                   tree.patients[0].studies[0].series[0].title ==
                       tree.patients[1].studies[0].series[0].title,
               "the same series title appears under both patients");
    }

    {   // Indices point back into the scan vector, so a selection can drive the existing decode path.
        const auto tree = clivus::ui::buildBrowserTree(scan);
        int seen = 0;
        std::vector<bool> visited(scan.size(), false);
        for (const auto& patient : tree.patients)
            for (const auto& study : patient.studies)
                for (const auto& entry : study.series) {
                    expect(entry.seriesIndex >= 0 && entry.seriesIndex < int(scan.size()),
                           "a series index is inside the scan vector");
                    if (entry.seriesIndex >= 0 && entry.seriesIndex < int(scan.size()))
                        visited[size_t(entry.seriesIndex)] = true;
                    ++seen;
                }
        expect(seen == int(scan.size()), "the tree holds one entry per scanned series");
        bool allVisited = true;
        for (bool v : visited) allVisited = allVisited && v;
        expect(allVisited, "every scanned series is reachable from the tree");
    }

    {   // Names are off by default and the ordinal is always there when they are on.
        const auto anonymous = clivus::ui::buildBrowserTree(scan);
        expect(!anonymous.patients.empty() && anonymous.patients[0].label == "Patient A",
               "by default a patient node shows only its ordinal");

        clivus::ui::BrowserOptions named;
        named.showPatientNames = true;
        const auto withNames = clivus::ui::buildBrowserTree(scan, named);
        expect(!withNames.patients.empty() && withNames.patients[0].label.contains("Doe^Jane"),
               "with names enabled the name appears");
        expect(!withNames.patients.empty() && withNames.patients[0].label.contains("A"),
               "with names enabled the ordinal stays, so equal names remain distinct");
        expect(withNames.patients.size() == 2 &&
                   withNames.patients[0].label != withNames.patients[1].label,
               "two patients with the same name still get different labels");
    }

    {   // A patient with no ID falls back to the name; a series with neither shares the unknown node.
        std::vector<clivus::SeriesInfo> partial;
        partial.push_back(makeSeries("1.2.S1", "1.2.S1.1", "", "Roe^Rick", "AXIAL", "CT", 1));
        auto tree = clivus::ui::buildBrowserTree(partial);
        expect(tree.patients.size() == 1, "a name-only patient still makes one node");

        partial.push_back(makeSeries("1.2.S1", "1.2.S1.2", "", "", "AXIAL", "CT", 1));
        tree = clivus::ui::buildBrowserTree(partial);
        expect(tree.patients.size() == 2, "an unidentified series does not join a named patient");
        expect(tree.patients[1].label == "Patient B", "the unidentified patient still gets an ordinal");
    }

    {   // Quarantined objects get their own node, at the end, labelled for what they are.
        std::vector<clivus::SeriesInfo> withQuarantine;
        withQuarantine.push_back(makeSeries("1.2.S1", "1.2.S1.1", "PID-1", "Doe^Jane", "AXIAL", "CT", 1));
        clivus::SeriesInfo orphan = makeSeries("1.2.S1", "quarantine", "PID-1", "Doe^Jane", "", "", 2);
        orphan.quarantined = true;
        withQuarantine.push_back(orphan);

        const auto tree = clivus::ui::buildBrowserTree(withQuarantine);
        expect(tree.patients.size() == 2, "the orphan does not join the identified patient");
        if (tree.patients.size() == 2) {
            expect(tree.patients[1].label == "Objects without a series identity",
                   "the quarantine node says what it is");
            expect(tree.patients[1].studies.size() == 1 &&
                       tree.patients[1].studies[0].label == "No study identity",
                   "the quarantine study says what it is");
            expect(tree.patients[1].studies[0].series[0].title ==
                       clivus::ui::quarantinedSeriesLabel(),
                   "the quarantined series carries its own label");
        }
    }

    {   // Filtering keeps the boundaries: a matching series keeps its patient and study visible.
        const auto tree = clivus::ui::buildBrowserTree(scan);
        const auto axial = clivus::ui::filterBrowserTree(tree, "axial");
        expect(axial.seriesCount() == 3, "the filter matches the three axial series");
        expect(axial.patients.size() == 2, "both patients survive because both have a match");
        expect(axial.patients[0].studies.size() == 2,
               "a study whose series did not match is dropped, one that did is kept");
        expect(axial.patients[0].studies[0].series.size() == 1, "only the matching series is kept");

        const auto none = clivus::ui::filterBrowserTree(tree, "nothing matches this");
        expect(none.empty(), "a query with no match empties the tree");

        const auto identity = clivus::ui::filterBrowserTree(tree, "   ");
        expect(identity.seriesCount() == 4, "a blank query returns everything");

        // The patient label is searchable: "Patient B" finds the second patient's series.
        const auto second = clivus::ui::filterBrowserTree(tree, "patient b");
        expect(second.patients.size() == 1 && second.seriesCount() == 1,
               "a patient label is searchable and keeps only that patient");
    }

    if (failures == 0) std::printf("browser_model: all checks passed\n");
    return failures == 0 ? 0 : 1;
}
