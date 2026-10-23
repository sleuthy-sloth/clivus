// SPDX-License-Identifier: GPL-3.0-or-later
// Series browser row text. The row is what a user reads first, and it has already shipped one
// encoding bug: a QLatin1String separator repainted as "Â·" on every platform. These checks pin both
// the wording and the character.
#include "series_summary.h"

#include <QCoreApplication>
#include <cstdio>

#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
            std::exit(1);                                                  \
        }                                                                  \
    } while (false)

namespace {

clivus::SeriesInfo series(const std::string& seriesDesc, const std::string& studyDesc,
                          const std::string& modality, int files, int frames, int w, int h)
{
    clivus::SeriesInfo s;
    s.seriesDescription = seriesDesc;
    s.studyDescription = studyDesc;
    s.modality = modality;
    s.width = w;
    s.height = h;
    for (int i = 0; i < files; ++i) s.files.push_back("f" + std::to_string(i) + ".dcm");
    for (int i = 0; i < files; ++i) s.frameCounts.push_back(i == 0 ? frames : 1);
    return s;
}

}  // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);

    // Title prefers the series description, falls back to the study description, then says so.
    CHECK(clivus::ui::seriesTitle(series("AX 3.0", "CHEST", "CT", 1, 1, 512, 512)) == "AX 3.0");
    CHECK(clivus::ui::seriesTitle(series("", "CHEST", "CT", 1, 1, 512, 512)) == "CHEST");
    CHECK(clivus::ui::seriesTitle(series("", "", "CT", 1, 1, 512, 512)) == "(no description)");

    // Details: modality · count · dimensions, with the middle dot the source actually carries.
    {
        const QString details =
            clivus::ui::seriesDetails(series("AX", "CHEST", "CT", 120, 1, 512, 512));
        CHECK(details == QString::fromUtf8("CT · 120 images · 512 × 512"));
        CHECK(details.contains(QChar(0x00B7)));          // U+00B7 MIDDLE DOT, not "Â·"
        CHECK(!details.contains(QString::fromUtf8("Â"))); // the mojibake that shipped once
        CHECK(details.contains(QChar(0x00D7)));          // U+00D7 MULTIPLICATION SIGN for 512 × 512
    }
    // Singular wording.
    CHECK(clivus::ui::seriesDetails(series("AX", "CHEST", "CT", 1, 1, 128, 128))
          == QString::fromUtf8("CT · 1 image · 128 × 128"));
    // A multi-frame object says both counts: one object, fifteen slices.
    {
        const QString details =
            clivus::ui::seriesDetails(series("DOSE", "", "RTDOSE", 1, 15, 10, 10));
        CHECK(details.contains(QStringLiteral("1 object")));
        CHECK(details.contains(QStringLiteral("15 frames")));
    }
    // Missing dimensions and modality are omitted rather than guessed.
    {
        const QString details = clivus::ui::seriesDetails(series("", "", "", 2, 1, 0, 0));
        CHECK(details == QStringLiteral("2 images"));
    }

    // The combined label is exactly two lines: title then details.
    {
        const QString label = clivus::ui::seriesLabel(series("AX", "CHEST", "CT", 3, 1, 64, 64));
        const QStringList lines = label.split(QLatin1Char('\n'));
        CHECK(lines.size() == 2);
        CHECK(lines[0] == "AX");
        CHECK(lines[1] == QString::fromUtf8("CT · 3 images · 64 × 64"));
    }

    std::printf("series_summary: all checks passed\n");
    return 0;
}
